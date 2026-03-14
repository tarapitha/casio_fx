// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_sx_fsm.c
*
* Finite state machine implementation for Casio Graphing calculator
* program link receiver protocol.
*/

#include "casio_rx.h"

extern const uint64_t SHORTWAIT;
extern const uint64_t LONGWAIT;

static int parse_header( Casio_FX * );

static Action none( void ) { Action a = { ACT_NONE,  { 0, 0 }, NULL, 0 }; return a; }
static Action send1( uint8_t b )
			   { Action a = { ACT_SEND1, { b, 0 }, NULL, 0 }; return a; }
static Action send1_print_msg_exit( uint8_t b, char *str )
{
  Action a = { ACT_SEND1_PRINT_MSG_EXIT, { b }, str, 0 };
  return a;
}

Action casio_fx_on_byte( Casio_FX *c, uint8_t b )
{
  switch( c->st )
  {
    case ST_WAIT_SYN:
      switch( b )
      {
	case SYN:
	  c->st = ST_WAIT_HDR;
	  c->limit_s = SHORTWAIT;
	  return send1( DC3 );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_HDR:
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_HDR;
	  c->hdr[ 0 ] = b;
	  c->hdr_i = 1;
	  c->hdr_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_HDR:
      c->hdr_sum += b;
      c->hdr[ c->hdr_i++ ] = b;
      if( c->hdr_i < sizeof( c->hdr ) )
	return none();

      if( c->hdr_sum != 0 )
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

      if( c->dst_file_exists )
      {
	c->st = ST_WAIT_OVRWRT_DECISION;
	c->limit_s = LONGWAIT;
	return send1( AREA_USED );
      }

      switch( c->magic )
      {
	case P1:	// One program
	  c->st = ST_WAIT_PRG_DATA;
	  return send1( ACK );
	case PZ:	// All programs
	  c->st = ST_WAIT_ALLPRG_DIRECTORY;
	  return send1( ACK );
	case DD:
	  c->st = ST_WAIT_SCRNDMP;
	  return send1( ACK );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: The calculator sent malformatted header!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_OVRWRT_DECISION:
      switch( b )
      {
	case ACK:
	  c->limit_s = SHORTWAIT;
	  switch( c->magic )
	  {
	    case P1:	// One program
	      c->st = ST_WAIT_PRG_DATA;
	      return send1( ACK );
	    case PZ:	// All programs
	      c->st = ST_WAIT_ALLPRG_DIRECTORY;
	      return send1( ACK );
	    default:
	      c->st = ST_DONE;
	      return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: The calculator sent malformatted header!\n"
			"Checking the communication port parity setting might help.\n" );
	  }
	case NAK:
	  c->st = ST_DONE;
	  return none();
	default:
	      c->st = ST_DONE;
	      return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_PRG_DATA:
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_PRG_DATA;
	  c->data[ 0 ] = b;
	  c->data_i = 1;
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
      c->data[ c->data_i++ ] = b;
      if( c->data_i < c->size )
	return none();

      if( c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect program checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      return send1( ACK );

    case ST_WAIT_ALLPRG_DIRECTORY:
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLPRG_DIRECTORY;
	  c->directory[ 0 ] = b;
	  c->directory_i = 1;
	  c->directory_sum = 0;
	  return none();
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_READ_ALLPRG_DIRECTORY:
      c->directory_sum += b;
      c->directory[ c->directory_i++ ] = b;
      if( c->directory_i < sizeof( c->directory ) )
	return none();

      if( c->directory_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect all programs directory checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_WAIT_ALLPRG_DATA;
      return send1( ACK );

    case ST_WAIT_ALLPRG_DATA:
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_ALLPRG_DATA;
	  c->data[ 0 ] = b;
	  c->data_i = 1;
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
      c->data[ c->data_i++ ] = b;
      if( c->data_i < c->size )
	return none();

      if( c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect all programs collection checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      return send1( ACK );

    case ST_WAIT_SCRNDMP:
      switch( b )
      {
	case INIT_SEND: // ':'
	  c->st = ST_READ_SCRNDMP;
	  c->data[ 0 ] = b;
	  c->data_i = 1;
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
      c->data[ c->data_i++ ] = b;
      if( c->data_i < c->size )
	return none();

      if( c->data_sum != 0 )
      {
	c->st = ST_DONE;
	return send1_print_msg_exit( CHKSUM_ERROR,
			"\n** ERROR: Incorrect program checksum!\n"
			"Checking the communication port parity setting might help.\n" );
      }

      c->st = ST_DONE;
      return send1( ACK );

    case ST_DONE:
    default:
      return none();
  }
}

Action casio_on_timeout( Casio_FX *c )
{
  switch( c->st )
  {
    case ST_WAIT_HDR:
    case ST_READ_HDR:
    case ST_WAIT_PRG_DATA:
    case ST_READ_PRG_DATA:
    case ST_WAIT_ALLPRG_DIRECTORY:
    case ST_READ_ALLPRG_DIRECTORY:
    case ST_WAIT_ALLPRG_DATA:
    case ST_READ_ALLPRG_DATA:
      c->st = ST_DONE;
      return send1_print_msg_exit( GENERIC_ERROR,
			  "\n** ERROR: Timeout while waiting calculator to respond!\n" );
    default:
      return none();
  }
}

static int parse_header( Casio_FX *c )
{
  uint8_t size_msb, size_lsb;
  uint8_t img_height, img_width;

  const Magic_tr magic_values[] =
      { { "P1", P1 },
	{ "PZ", PZ },
	{ "DD", DD } };

  c->magic = UNKNOWN;
  for( int i = 0; i < 3; i++ )
  {
    if( ! strncmp( (char *) ( c->hdr + 1 ), magic_values[ i ].str, 2 ) )
    {
      c->magic = magic_values[ i ].magic;
      break;
    }
  }
  if( c->magic == UNKNOWN )
    return -1;

  switch( c->magic )
  {
    case P1:
    case PZ:

      // Get the payload size
      size_msb = c->hdr[ 3+1 ];
      size_lsb = c->hdr[ 4+1 ];
      c->size = (uint16_t)( ( size_msb << 8 ) | size_lsb );

      if( c->size < 2 )
        return -1;

      c->mode = c->hdr[ 5+1 ];
      break;

    case DD:
      img_height = c->hdr[ 2+1 ];
      img_width = c->hdr[ 3+1 ];
      c->size = (uint16_t) img_height * (uint16_t) img_width / 8 + 2;
      break;

    default:
      return -1;
  }

  c->data_i = 0;
  c->data_sum = 0;
  c->directory_sum = 0;

  return 0;
}
