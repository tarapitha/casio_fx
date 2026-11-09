// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_tx_parse.c
*
* Finite state machine implementation for Casio Graphing calculator
* CAS file parsing.
*/

#include "casio_tx.h"

static Parse_action parse_none( void )
	{ Parse_action a = { ACT_NONE,		0,		NULL };	return a; }

static Parse_action parse_read_header()
	{ Parse_action a = { ACT_READ_HEADER,	HDR_SIZE,	NULL };	return a; }

static Parse_action parse_read_section( size_t size )
	{ Parse_action a = { ACT_READ_SECTION,	size,		NULL };	return a; }

static Parse_action parse_print_msg_exit( char *s )
	{ Parse_action a = { ACT_PRINT_MSG_EXIT, 0,		s };	return a; }

/**************************************
*  FSM for parsing input file
**************************************/
Parse_action casio_fx_parse_frame( Casio_FX *c )
{
  switch( c->st )
  {
    case ST_HDR:
      c->st = ST_DECIDE_TYPE;
      return parse_read_header();

    case ST_DECIDE_TYPE:
      switch( c->magic )
      {
	case AL:
	  c->st = ST_HDR;
	  return parse_none();

	case BU:	// Backup. The memory dump
	case F1:	// One Function memory
	case G1:	// One graphics function
	case P1:	// One program
	  c->st = ST_READ_SECTION;
	  return parse_none();

	case F6:	// All function memories
	case GF:	// Graphics zoom factor
	case GR:	// Graphics range
	  c->st = ST_READ_SECTION_SET;
	  return parse_none();

	case PD:	// Polynomial equation
	  if( c->polynomial_degree > 0 )
	    c->st = ST_READ_SECTION_SET;
	  else
	    c->st = c->all_data ? ST_HDR : ST_DONE;
	  return parse_none();

	case AD:	// All variables
	case AM:	// Alphabetic variables
	case DM:	// Defined memory variables
	  c->st = ST_READ_VARS;
	  return parse_none();

	case GA:	// Set of graphing functions
	  c->st = ST_READ_ALLGRAPHFUNC_DATA;
	  return parse_none();

	case M1:	// One matrix
	  c->st = ST_READ_MATRIX;
	  return parse_none();

	case MA:	// Set of matrices
	  c->st = ST_READ_ALL_MATRICES;
	  return parse_none();

	case PZ:	// All 38 programs
	  c->st = ST_READ_PRG_DIR;
	  return parse_none();

	case SD:	// Linear multivariable
	  c->st = ST_READ_LINEQ_DATA;
	  return parse_none();

	case SR:	// Statistics regression
	  c->st = ST_READ_REGRESSION_DATA;
	  return parse_none();

	case SS:	// Statistics single variable
	  c->st = ST_READ_STAT_DATA;
	  return parse_none();

	case ETB1:	// End of Tansmission Block: last section for set of some data types
	  c->st = c->all_data ? ST_HDR : ST_DONE;
	  return parse_none();

	case ETB2:	// End of Tansmission Block: last header block for all data
	  c->st = ST_DONE;
	  return parse_none();

	default:
	  c->st = ST_DONE;
	  return parse_print_msg_exit( "\n** ERROR: Unrecognized magic value in file!\n" );
      }

    case ST_READ_SECTION:
      c->st = ST_DONE;
      return parse_read_section( c->size );

    case ST_READ_SECTION_SET:
      c->st = c->all_data ? ST_HDR : ST_DONE;
      return parse_read_section( c->size );

    case ST_READ_VARS:
      if( c->variables_i++ < c->variables_n )
	return parse_read_section( c->size );
      c->st = c->all_data ? ST_HDR : ST_DONE;
      return parse_none();

    case ST_READ_ALLGRAPHFUNC_DATA:
      c->st = ST_HDR;
      return parse_read_section( c->size );

    case ST_READ_MATRIX:
      if( c->matrix_i++ < c->matrix_n )
	return parse_read_section( c->size );
      c->st = ST_DONE;
      return parse_none();

    case ST_READ_ALL_MATRICES:
      if( c->matrix_i++ < c->matrix_n )
	return parse_read_section( c->size );
      c->st = ST_HDR;
      return parse_none();

    case ST_READ_LINEQ_DATA:
      if( c->matrix_i++ < c->matrix_n )
	return parse_read_section( c->size );
      c->st = c->all_data ? ST_HDR : ST_DONE;
      return parse_none();

    case ST_READ_REGRESSION_DATA:
      if( c->regression_i++ < c->regression_n )
	return parse_read_section( c->size );
      c->st = c->all_data ? ST_HDR : ST_DONE;
      return parse_none();

    case ST_READ_STAT_DATA:
      if( c->sd_i++ < c->sd_n )
	return parse_read_section( c->size );
      c->st = c->all_data ? ST_HDR : ST_DONE;
      return parse_none();

    case ST_READ_PRG_DIR:
      c->st = ST_READ_SECTION_SET;
      return parse_read_section( PZ_DIR_SIZE );

    case ST_DONE:
    default:
      return parse_none();
  }
}
