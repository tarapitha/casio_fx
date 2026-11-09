// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_rx_fsm.c
*
* Finite state machine implementation for Casio Graphing calculator
* program link receiver protocol.
*/

#include <assert.h>
#include <string.h>
#include "casio_rx.h"

// ----------------- Helper methods for state machine -------------------
// Sentinel for avoiding buffer overflow/overread
#define CHECK_BOUNDARY( c, s ) /* c - state structure, s - section size */ \
  do \
  {  \
    if( (c)->data + (s) >= (c)->data_end ) \
      (c)->save_stream = false; \
  } while( 0 )
// ----------------------------------------------------------------------

extern const uint64_t SHORTWAIT;
extern const uint64_t LONGWAIT;

static int parse_header( Casio_FX * );
static Action magic_decision( Casio_FX * );

static Action none( void )
	{ Action a = { ACT_NONE,  { 0, 0 }, NULL, 0 }; return a; }

static Action send1( uint8_t b )
	{ Action a = { ACT_SEND1, { b, 0 }, NULL, 0 }; return a; }

static Action send1_print_msg_exit( uint8_t b, char *str )
	{ Action a = { ACT_SEND1_PRINT_MSG_EXIT, { b }, str, 0 }; return a; }

/**************************************
*   Main Finite State Machine routine
**************************************/
Action casio_fx_on_byte( Casio_FX *c, uint8_t b )
{
  switch( c->st )
  {
    case ST_WAIT_SYN:
      switch( b )
      {
	case SYN:
	  c->st		 = ST_WAIT_HDR;
	  c->limit_s	 = SHORTWAIT;
	  c->data	 = c->buf;
	  c->data_i	 = 0;
	  c->do_overwrite = false;
	  c->all_data    = false;
	  c->save_stream = true;
	  return send1( DC3 );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"The calculator communication port settings might be off.\n" );
      }

    case ST_WAIT_HDR:
      CHECK_BOUNDARY( c, HDR_SIZE );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_HDR;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_HDR:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < HDR_SIZE )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect header checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( parse_header( c ) != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: The calculator sent malformatted header!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      // If the file with the same name already exists,
      // let calculator ask user wheter to replace it
      if( c->dst_file_exists && ! c->do_overwrite && ( ! c->all_data || c->magic == AL ) )
      {
	switch( c->magic )
	{
	  // These do not play nicely with overwrite dialog
	  // *** Have to implement this functionality on PC side ***
	  // Until then we just tell calculator that the memory is full ;^)
	  case AL:	// All data - has no clue of AREA_USED or MEM_FULL
	  case BU:	// Memdump  - has no clue of AREA_USED or MEM_FULL

	  case AD:	// All variables
	  case AM:	// Alpha variables
	  case DM:	// Defined variables
	  case GF:	// Graphic zoom factor
	  case GR:	// Graphic range
	    c->st = ST_DONE;
	    return send1_print_msg_exit( MEM_FULL,
			"\n** Refusing to rewrite an already existing file!\n" );
	  case ETB1:	// End markers cannot overwrite anything
	  case ETB2:
	    return magic_decision( c );
	  default:
	    break;
	}
	c->st = ST_WAIT_OVRWRT_DECISION;
	c->limit_s = LONGWAIT;
	return send1( AREA_USED );
      }

      return magic_decision( c );

    case ST_WAIT_OVRWRT_DECISION:
      switch( b )
      {
	case ACK:
	  c->limit_s = SHORTWAIT;
	  c->do_overwrite = true;
	  return magic_decision( c );
	case NAK:
	  c->st = ST_DONE;
	  return none();
	default:
	      c->st = ST_DONE;
	      return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_BACKUP_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_BACKUP_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_BACKUP_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect backup checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_VARIABLES_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_VARIABLES_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_VARIABLES_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect variables data checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( ++c->variables_i < c->variables_n )
	c->st = ST_WAIT_VARIABLES_DATA;
      else
	c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_FACTOR_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_FACTOR_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_FACTOR_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect zoom range data checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_RANGE_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_RANGE_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_RANGE_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect graphics range data checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_GRAPHFUNC_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_GRAPHFUNC_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_GRAPHFUNC_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect graph function memory checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_ALLGRAPHFUNC_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLGRAPHFUNC_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLGRAPHFUNC_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect graph function memory checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_WAIT_HDR;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_FUNCMEM_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_FUNCMEM_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_FUNCMEM_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect function memory checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_ALLFUNCMEM_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLFUNCMEM_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLFUNCMEM_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect all function memory collection checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_MATRIX_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_MATRIX_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_MATRIX_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect matrix checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( ++c->matrix_i < c->matrix_n )
	c->st = ST_WAIT_MATRIX_DATA;
      else
	c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_ALLMATRIX_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLMATRIX_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLMATRIX_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect matrices checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( ++c->matrix_i < c->matrix_n )
      {
	c->st = ST_WAIT_ALLMATRIX_DATA;
	c->data += c->data_i;
      }
      else
      {
	c->st = ST_WAIT_HDR;
	c->data += c->data_i;
      }
      return send1( ACK );

    case ST_WAIT_PRG_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_PRG_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_PRG_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect program checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_ALLPRG_DIRECTORY:
      CHECK_BOUNDARY( c, PZ_DIR_SIZE );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLPRG_DIRECTORY;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLPRG_DIRECTORY:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < PZ_DIR_SIZE )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect all programs directory checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_WAIT_ALLPRG_DATA;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_ALLPRG_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLPRG_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLPRG_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect all programs collection checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_REGRESSION_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_REGRESSION_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_REGRESSION_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect regression data checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( ++c->regression_i < c->regression_n )
	c->st = ST_WAIT_REGRESSION_DATA;
      else
	c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_STAT_DATA:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_STAT_DATA;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_STAT_DATA:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect statistical data checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      if( ++c->sd_i < c->sd_n )
	c->st = ST_WAIT_STAT_DATA;
      else
	c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_WAIT_SCRNDMP:
      CHECK_BOUNDARY( c, c->size );
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_SCRNDMP;
	  c->data_i = 0;
	  if( c->save_stream )
	    c->data[ c->data_i ] = b;
	  c->data_i++;
	  c->data_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_SCRNDMP:
      c->data_sum += b;
      if( c->save_stream )
	c->data[ c->data_i ] = b;
      if( ++c->data_i < c->size )
	return none();

      if( ! c->save_stream )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( GENERIC_ERROR, "\n** ERROR: casio_rx internal buffer overflow!\n" );
      }

      if( ! c->unchecked && c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect screendump checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      c->data += c->data_i;
      return send1( ACK );

    case ST_DONE:
    default:
      return none();
  }
}

static Action magic_decision( Casio_FX *c )
{
  switch( c->magic )
  {
    case DD:	// Screendump
      c->st = ST_WAIT_SCRNDMP;
      break;

    case AL:	// All calculator objects
      c->st = ST_WAIT_HDR;
      break;

    case BU:
      c->st = ST_WAIT_BACKUP_DATA;
      break;

    case AD:	// All variables
    case AM:	// Alphabetic variables
    case DM:	// Defined memory variables
      c->st = ST_WAIT_VARIABLES_DATA;
      c->variables_i = 0;
      break;

    case G1:	// One graph function memory
      c->st = ST_WAIT_GRAPHFUNC_DATA;
      break;

    case GA:	// All graph function memory
      c->st = ST_WAIT_ALLGRAPHFUNC_DATA;
      break;

    case GF:	// Graphic zoom factor definition
      c->st = ST_WAIT_FACTOR_DATA;
      break;

    case PD:	// Polynomial equation
      if( c->polynomial_degree > 0 )
	c->st = ST_WAIT_RANGE_DATA;
      else
	c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      break;

    case GR:	// Graphic range definition
      c->st = ST_WAIT_RANGE_DATA;
      break;

    case F1:	// One function memory
      c->st = ST_WAIT_FUNCMEM_DATA;
      break;

    case F6:	// All function memories
      c->st = ST_WAIT_ALLFUNCMEM_DATA;
      break;

    case SD:	// Linear multivariable
    case M1:	// One matrix
      c->st = ST_WAIT_MATRIX_DATA;
      c->matrix_i = 0;
      break;

    case MA:	// All matrices
      if( c->matrix_n > 0 )
      {
        c->st = ST_WAIT_ALLMATRIX_DATA;
        c->matrix_i = 0;
      }
      else
        c->st = ST_WAIT_HDR;
      break;

    case P1:	// One program
      c->st = ST_WAIT_PRG_DATA;
      break;

    case PZ:	// All programs
      c->st = ST_WAIT_ALLPRG_DIRECTORY;
      break;

    case SR:	// Regression analysis
      c->st = ST_WAIT_REGRESSION_DATA;
      c->regression_i = 0;
      break;

    case SS:	// Single-variable statistics
      c->st = ST_WAIT_STAT_DATA;
      c->sd_i = 0;
      break;

    case ETB1:	// End of Transmission Block for some set of data types
      c->st = c->all_data ? ST_WAIT_HDR : ST_DONE;
      break;

    case ETB2:	// End of Transmission Block for all data
      c->st = ST_DONE;
      break;

    default:
      c->st = ST_DONE;
      return send1_print_msg_exit( GENERIC_ERROR,
    		"\n** ERROR: The calculator sent malformatted header!\n"
    		"Checking the communication port parity setting might help.\n" );
  }
  c->data += c->data_i;
  return send1( ACK );
}

Action casio_on_timeout( Casio_FX *c )
{
  if( c->st < ST_DONE )
  {
    c->st = ST_DONE;
    return send1_print_msg_exit( GENERIC_ERROR,
			  "\n** ERROR: Timeout while waiting calculator to respond!\n" );
  }

  return none();
}

static int parse_header( Casio_FX *c )
{
  uint8_t size_msb, size_lsb;

  const char *magic_values[] = {
    #define X(name) [name] = ( name == ETB1 ? "\x17\xFF" : \
			     ( name == ETB2 ? "\x17\x17" : #name ) ),
    MAGIC_VALUES
    #undef X
  };

  // Check that Magic_t has correct structure
  static_assert(
    ( sizeof( magic_values ) / sizeof( magic_values[ 0 ] ) ) == MAGIC_COUNT,
    "ERROR: Magic_t and magic_values array are out of sync!"
  );

  c->magic = UNKNOWN;

  for( int magic = 0; magic < MAGIC_COUNT; magic++ )
  {
    if( magic_values[ magic ] && !strncmp( (char *)( c->data + 1 ), magic_values[ magic ], 2 ) ) 
    {
      c->magic = (Magic_t) magic;
      break;
    }
  }

  if( c->magic == UNKNOWN )
    return -1;

  switch( c->magic )
  {
    case AL:	// All objects
      c->all_data = true;
      break;

    case BU:	// Backup
      c->size = BU_SIZE;	// Almost all the 8kB RAM content is transmitted
      break;

    case AD:	// All variables memory
    case AM:	// Alpha variables memory
    case DM:	// Defined variables memory
      c->size = VARIABLES_SIZE;
      size_msb = c->data[ 4+1 ];
      size_lsb = c->data[ 5+1 ];
      c->variables_n = ( size_msb << 8 ) | size_lsb;
      break;

    case G1:	// One graph function
      size_msb = c->data[ 3+1 ];
      size_lsb = c->data[ 4+1 ];
      c->size = ( ( size_msb << 8 ) | size_lsb );
      break;

    case GA:	// All graph functions
      size_msb = c->data[ 3+1 ];
      size_lsb = c->data[ 4+1 ];
      c->size = ( ( size_msb << 8 ) | size_lsb );
      break;

    case GF:	// Graphics zoom factor
    case GR:	// Graphics range
      c->size = c->data[ 5+1 ] * 8 + 4;
      break;

    case DD:	// Screendump
      c->height = c->data[ 2+1 ];
      c->width  = c->data[ 3+1 ];
      c->size = (uint16_t) c->height * (uint16_t) ( ( c->width + 7 ) / 8 ) + 2;
      break;

    case PD:	// Polynomial equation
      c->polynomial_degree = c->data[ 5+1 ];
      c->size = ( c->polynomial_degree + 1 ) * 8 + 4;
      break;

    case SD:	// Linear multivariable equation
    case M1:	// One matrix
      c->width  = c->data[ 4+1 ];	// Max value == 9
      c->height = c->data[ 5+1 ];	// Max value == 9
      c->matrix_n = c->width * c->height + 1;	// Max value == 81 + 1
      c->size = MATRIX_CELL_SIZE;
      break;

    case MA:	// All matrices
      c->width  = c->data[ 4+1 ];	// Max value == 9
      c->height = c->data[ 5+1 ];	// Max value == 9
      c->matrix_n = c->width * c->height;	// Max value == 81
      c->size = MATRIX_CELL_SIZE;
      break;

    case F1:	// One function memory
    case F6:	// All function memories
    case P1:	// One program
    case PZ:	// All programs
      size_msb = c->data[ 3+1 ];
      size_lsb = c->data[ 4+1 ];
      c->size = (uint16_t)( ( size_msb << 8 ) | size_lsb );
      c->mode = c->data[ 5+1 ];
      break;

    case SR:
      c->size = REGRESSION_ROW_SIZE;
      size_msb = c->data[ 4+1 ];
      size_lsb = c->data[ 5+1 ];
      c->regression_n = ( size_msb << 8 ) | size_lsb;
      break;

    case SS:
      c->size = SD_ROW_SIZE;
      size_msb = c->data[ 4+1 ];
      size_lsb = c->data[ 5+1 ];
      c->sd_n = ( size_msb << 8 ) | size_lsb;
      break;

    case ETB1:	// End of Transmission Block: last section for set of some data types
    case ETB2:	// End of Transmission Block: last header block for All data
      break;

    default:
      return -1;
  }

  return 0;
}
