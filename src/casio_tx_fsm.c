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

static Action none( void ) { Action a = { ACT_NONE }; return a; }
static Action send1_add_nxt_st( uint8_t b )
{
  Action a = { ACT_SEND1, { b }, NULL, NULL, 0, b == ACK ? ST_WAIT_HDR_RESPONSE : ST_DONE };
  return a;
}
static Action send1_print_msg_exit( uint8_t b, char *buf )
{
  Action a = { ACT_SEND1_PRINT_MSG_EXIT, { b }, NULL, buf, 0, ST_NONE };
  return a;
}
static Action send_buf( uint8_t *buf, int len )
		{ Action a = { ACT_SEND_BUF, { 0 },  buf, NULL, len, ST_NONE }; return a; }
static Action print_msg( char *str )
		{ Action a = { ACT_PRINT_MSG, { 0 }, NULL, str, 0, ST_NONE }; return a; }
static Action decide_y_n( void )
		{ Action a = { ACT_NEED_DECISION, { 0 }, NULL, NULL, 0, ST_NONE }; return a; }

/*******************************************
*     Finite State Machine Main Body 
*******************************************/
Action casio_fx_on_byte( Casio_FX *c, uint8_t b )
{
  switch( c->st )
  {
    // Initial state
    case ST_WAIT_RDY_TO_RECEIVE:
      switch( b )
      {
	case DC3:  // Ready to receive
	  c->st = ST_WAIT_HDR_RESPONSE;
	  c->limit_s = SHORTWAIT;
	  return send_buf( c->hdr, sizeof( c->hdr ) );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
			"\n** ERROR: Got erroneous response from the calculator!\n"
			"Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_HDR_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->limit_s = SHORTWAIT;
	  switch( c->magic )
	  {
	    case P1:	// One program
	      c->st = ST_WAIT_PRG_DATA_RESPONSE;
	      return send_buf( c->data, c->size );
	    case PZ:	// All programs
	      c->st = ST_WAIT_ALLPRG_DIRECTORY_RESPONSE;
	      return send_buf( c->directory, sizeof( c->directory ) );
	    default:	// This should never happen, because file is sanitized during reading
	      c->st = ST_DONE;
	      return send1_print_msg_exit( GENERIC_ERROR,
    			"\n** Internal error: an impossible event occurred.\n"
			"If you are seeing this, reality has diverged "
			"from the design document.\n" );
	  }

	// How to proceed if calculator reports that this program area is already used:
	case AREA_USED:
	  c->limit_s = LONGWAIT;
	  return decide_y_n();

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid header checksum,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	case GENERIC_ERROR: // Invalid header
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid header,\n"
			    "Checking the communication port parity setting could help.\n" );
	case MODE_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported mode error,\n"
			    "Checking the communication port parity setting could help.\n" );
	case MEM_FULL:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator memory full, "
			    "free some space and try again!\n" );

	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		  "\n** ERROR: Got erroneous response from the calculator!\n"
			  "Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_PRG_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_DONE;
	  return send_buf( c->data, c->size );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid program checksum,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted program,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got erroneous response from the calculator!\n"
			       "Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_ALLPRG_DIRECTORY_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_WAIT_ALLPRG_DATA_RESPONSE;
	  return send_buf( c->data, c->size );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported invalid directory checksum,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted programs directory,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		       "\n** ERROR: Got erroneous response from the calculator!\n"
			       "Checking the communication port parity setting might help.\n" );
      }

    case ST_WAIT_ALLPRG_DATA_RESPONSE:
      switch( b )
      {
	case ACK:
	  c->st = ST_DONE;
	  return send_buf( c->data, c->size );

	case CHKSUM_ERROR:
	  c->st = ST_DONE;
	  return print_msg(
		"\n** ERROR: Calculator reported invalid programs collection checksum,\n"
		"Checking the communication port parity setting could help.\n"
		"If you feel adventurous, then -u option might help.\n" );
	case GENERIC_ERROR:
	  c->st = ST_DONE;
	  return print_msg( "\n** ERROR: Calculator reported corrupted programs collection,\n"
			    "Checking the communication port parity setting could help.\n"
			    "If you feel adventurous, then -u option might help.\n" );
	default:
	  c->st = ST_DONE;
	  return send1_print_msg_exit( GENERIC_ERROR,
	  		 "\n** ERROR: Got erroneous response from the calculator!\n"
			 "Checking the communication port parity setting might help.\n" );
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
  switch( c->st )
  {
    case ST_WAIT_RDY_TO_RECEIVE:
    case ST_WAIT_HDR_RESPONSE:
    case ST_WAIT_PRG_DATA_RESPONSE:
    case ST_WAIT_ALLPRG_DIRECTORY_RESPONSE:
    case ST_WAIT_ALLPRG_DATA_RESPONSE:
      c->st = ST_DONE;
      return send1_print_msg_exit( GENERIC_ERROR,
			  "\n** ERROR: Timeout while waiting calculator to respond!\n" );
    default:
      return none();
  }
}
