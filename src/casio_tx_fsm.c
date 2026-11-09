// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_tx_fsm.c
*
* Finite state machine implementation for Casio Graphing calculator
* program link transmit protocol.
*/

#include "casio_tx.h"

extern const time_t SHORTWAIT;
extern const time_t LONGWAIT;
extern int parse_header( Casio_FX * );

static Action none( void )
	{ Action a = { ACT_NONE, { 0 }, NULL, 0, ST_NONE }; return a; }

static Action send1( uint8_t b )
	{ Action a = { ACT_SEND1, { b }, NULL, 0, ST_NONE }; return a; }

static Action send1_add_nxt_st( uint8_t b )
	{ Action a = { ACT_SEND1, { b }, NULL, 0, b == ACK ? ST_WAIT_HDR_RESPONSE : ST_DONE }; return a; }

static Action send1_print_msg_exit( uint8_t b, char *buf )
	{ Action a = { ACT_SEND1_PRINT_MSG_EXIT, { b }, buf, 0, ST_NONE }; return a; }

static Action send_buf( int len )
	{ Action a = { ACT_SEND_BUF, { 0 },  NULL, len, ST_NONE }; return a; }

static Action print_msg( char *str )
	{ Action a = { ACT_PRINT_MSG, { 0 }, str, 0, ST_NONE }; return a; }

static Action decide_y_n( void )
	{ Action a = { ACT_NEED_DECISION, { 0 }, NULL, 0, ST_NONE }; return a; }

/*******************************************
*     Finite State Machine Main Body 
*******************************************/
Action casio_fx_on_byte( Casio_FX *c, uint8_t b )
{
  switch( c->st )
  {
    // Initial state
    case ST_INIT:
      c->st = ST_WAIT_RDY_TO_RECEIVE;
      return send1( SYN );

    case ST_WAIT_RDY_TO_RECEIVE:
      switch( b )
      {
	case DC3:  // Ready to receive
	  c->st = ST_WAIT_HDR_RESPONSE;
	  c->limit_s = SHORTWAIT;
	  parse_header( c );	// Headers are already sanitized while parsing/reading the file
	  return send_buf( HDR_SIZE );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_HDR_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->limit_s = SHORTWAIT;
	  switch( c->magic )
	  {
	    case AL:
	      parse_header( c );
	      return send_buf( HDR_SIZE );

	    case BU:	// Backup data
	    case F1:	// One function memory
	    case G1:	// One graphics function
	    case P1:	// One program
	      c->st = ST_WAIT_SECTION_DATA_RESPONSE;
	      return send_buf( c->size );

	    case F6:	// All function memories
	    case GF:	// Graphics zoom factor
	    case GR:	// Graphics range
	      c->st = ST_WAIT_SECTION_SET_DATA_RESPONSE;
	      return send_buf( c->size );

	    case PZ:	// All programs
	      c->st = ST_WAIT_ALLPRG_DIRECTORY_RESPONSE;
	      return send_buf( PZ_DIR_SIZE );

	    case PD:	// Polynomial equation
	      if( c->polynomial_degree > 0 )
	      {
		c->st = ST_WAIT_SECTION_SET_DATA_RESPONSE;
		return send_buf( c->size );
	      }
	      if( c->all_data )
	      {
		c->st = ST_WAIT_HDR_RESPONSE;
		parse_header( c );
		return send_buf( HDR_SIZE );
	      }
	      c->st = ST_DONE;
	      return none();

	    case AD:	// All variables
	    case AM:	// Alphabetic variables
	    case DM:	// Defined memory variables
	      c->st = ST_WAIT_VARIABLES_DATA_RESPONSE;
	      c->variables_i = 0;
	      return send_buf( c->size );

	    case GA:	// Set of graphing functions
	      c->st = ST_WAIT_ALLGRAPHFUNC_DATA_RESPONSE;
	      return send_buf( c->size );

	    case SD:	// Linear multivariable equation
	      c->st = ST_WAIT_LINEQ_DATA_RESPONSE;
	      c->matrix_i = 0;
	      return send_buf( c->size );

	    case M1:	// Matrix
	      c->st = ST_WAIT_MATRIX_DATA_RESPONSE;
	      c->matrix_i = 0;
	      return send_buf( c->size );

	    case MA:	// Set of matrices
	      if( c->matrix_n > 0 )
	      {
		c->st = ST_WAIT_ALL_MATRICES_DATA_RESPONSE;
		c->matrix_i = 0;
		return send_buf( c->size );
	      }
	      c->st = ST_WAIT_HDR_RESPONSE;
	      parse_header( c );	// Headers are already sanitized while parsing/reading the file
	      return send_buf( HDR_SIZE );

	    case SS:	// Single variable statistics
	      c->st = ST_WAIT_STAT_DATA_RESPONSE;
	      c->sd_i = 0;
	      return send_buf( c->size );

	    case SR:	// Regression
	      c->st = ST_WAIT_REGRESSION_DATA_RESPONSE;
	      c->regression_i = 0;
	      return send_buf( c->size );

	    case ETB1:	// End of Transmission Block 1
	      if( c->all_data )
	      {
		c->st = ST_WAIT_HDR_RESPONSE;
		parse_header( c );
		return send_buf( HDR_SIZE );
	      }
	      c->st = ST_DONE;
	      return none();

	    case ETB2:	// End of Transmission Block 2
	      c->st = ST_DONE;
	      return none();

	    case DD:
	      c->st = ST_DONE;
	      return print_msg( "\nScreendump cannot be sent to a calculator.\n" );

	    default:	// This should never happen, because file is sanitized during reading
	      c->st = ST_DONE;
	      return send1_print_msg_exit( GENERIC_ERROR,
    			"\n** Internal error: an impossible event occurred.\n"
			"If you are seeing this, reality has diverged "
			"from the design document.\n" );
	  }

	// How to proceed if calculator reports that this memory area is already used:
	case AREA_USED:
	  c->limit_s = LONGWAIT;
	  return decide_y_n();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid header checksum,\n"
			    "Check the communication port settings.\n" );
	case GENERIC_ERROR: // Invalid header
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid header,\n"
			    "Check the communication port settings.\n" );
	case MODE_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported mode error,\n"
			    "Check the communication port settings.\n" );
	case MEM_FULL:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator memory full, "
			    "free some space and try again!\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		  "\n** ERROR: Got unexpected response from the calculator!\n"
			  "Checking the communication port settings might help.\n" );
      }

    case ST_WAIT_SECTION_SET_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( c->all_data )
	  {
	    c->st = ST_WAIT_HDR_RESPONSE;
	    parse_header( c );
	    return send_buf( HDR_SIZE );
	  }
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid section checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted section data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_SECTION_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_DONE;
	  c->limit_s = SHORTWAIT;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid section checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted section data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_ALLPRG_DIRECTORY_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_WAIT_SECTION_SET_DATA_RESPONSE;
	  return send_buf( c->size );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid program directory checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted programs directory,\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_VARIABLES_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->variables_i < c->variables_n )
	    return send_buf( c->size );
	  if( c->all_data )
	  {
	    c->st = ST_WAIT_HDR_RESPONSE;
	    parse_header( c );
	    return send_buf( HDR_SIZE );
	  }
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid variables data checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted variables data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_ALLGRAPHFUNC_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_WAIT_HDR_RESPONSE;
	  parse_header( c );	// Headers are already sanitized while parsing/reading the file
	  return send_buf( HDR_SIZE );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid graphing function checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted graphing function.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_MATRIX_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->matrix_i < c->matrix_n )
	    return send_buf( c->size );
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid matrix checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted matrix data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_ALL_MATRICES_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->matrix_i < c->matrix_n )
	    return send_buf( c->size );
	  c->st = ST_WAIT_HDR_RESPONSE;
	  parse_header( c );	// Headers are already sanitized while parsing/reading the file
	  return send_buf( HDR_SIZE );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid matrix checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted matrix data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_STAT_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->sd_i < c->sd_n )
	    return send_buf( c->size );
	  if( c->all_data )
	  {
	    c->st = ST_WAIT_HDR_RESPONSE;
	    parse_header( c );
	    return send_buf( HDR_SIZE );
	  }
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid statistics checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted statistics data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_REGRESSION_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->regression_i < c->regression_n )
	    return send_buf( c->size );
	  if( c->all_data )
	  {
	    c->st = ST_WAIT_HDR_RESPONSE;
	    parse_header( c );
	    return send_buf( HDR_SIZE );
	  }
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid regression checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted regression data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_WAIT_LINEQ_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  if( ++c->matrix_i < c->matrix_n )
	    return send_buf( c->size );
	  if( c->all_data )
	  {
	    c->st = ST_WAIT_HDR_RESPONSE;
	    parse_header( c );
	    return send_buf( HDR_SIZE );
	  }
	  c->st = ST_DONE;
	  return none();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid matrix checksum,\n"
			    "Use -u option if you have edited the file.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted matrix data.\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got unexpected response from the calculator!\n" );
      }

    case ST_DONE:
    default:
      return none();
  }
}

Action casio_fx_apply_decision( Casio_FX *c )
{
  if( c->st == ST_WAIT_HDR_RESPONSE )
    return send1_add_nxt_st( c->overwrite );

  return none();
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
