// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_sx_transport.c
*
* Transport logic framework implementation for Casio Graphing calculator
* program link receiver protocol.
*/

#include "casio_rx.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <getopt.h>

const uint64_t SHORTWAIT	= 2;		// about 1-2 seconds
const uint64_t LONGWAIT		= 6 * 60;	// 6 minutes

extern Action casio_fx_on_byte( Casio_FX *, uint8_t ); // FSM engine
extern Action casio_on_timeout( Casio_FX * );

static int set_serial( int, Casio_FX * );	// Configure serial interface
static int write_outfile( char *, Casio_FX * ); // Write the received data to file
static int do_action( int, Action * );
static time_t get_systime_s( void );
static bool timeout_expired( Casio_FX * );
static void show_help( char * );

/*******************************************
*                M A I N
*******************************************/
int main( int argc, char **argv )
{
  Casio_FX casio;
  memset( &casio, 0, sizeof( casio ) );

  // Parse command line options
  casio.dst_file_exists = false;
  casio.parity      = NONE;	// No parity by default
  casio.bps	    = B9600;	// Default data rate 9600 bps

  char *commdev = "/dev/ttyUSB0";
  char *fname   = NULL;

  const Rate_tr rates[] =
      { { "1200", B1200 },
	{ "2400", B2400 },
	{ "4800", B4800 },
	{ "9600", B9600 } };

  struct option longopts[] = {
    { "help",	 no_argument,       0, 'h' },
    { "device",	 required_argument, 0, 'd' },
    { "even",	 no_argument,	    0, 'e' },
    { "odd",	 no_argument,	    0, 'o' },
    { "bps",	 required_argument, 0, 'b' },
    { "version", no_argument,	    0, 'V' },
    { 0, 0, 0, 0 }
  };

  int opt;
  while( optind < argc )
  {
    if( ( opt = getopt_long( argc, argv, "+hd:eob:V", longopts, NULL ) ) != -1 )
    {
      switch( opt )
      {
	 case 'd':
	   commdev = optarg;
	   break;
	 case 'e':
	   casio.parity = EVEN;
	   break;
	 case 'o':
	   casio.parity = ODD;
	   break;
	 case 'b':
	   casio.bps = B0;
	   for( int i = 0; i < 4; i++ )
	   {
	     if( ! strcmp( optarg, rates[ i ].str ) )
	     {
	       casio.bps = rates[ i ].speed;
	       break;
	     }
	   }
	   if( casio.bps == B0 )
	   {
	     fprintf( stderr, "%s: invalid bps: %s\n", argv[ 0 ], optarg );
	     show_help( argv[ 0 ] );
	     exit( 1 );
	   }
	   break;
	 case 'V':
	   fprintf( stderr, "%s version: %s\n", argv[ 0 ], CASIO_FX_VERSION );
	   exit( 0 );
	 case 'h':
	   show_help( argv[ 0 ] );
	   exit( 0 );
	 default:
	   show_help( argv[ 0 ] );
	   exit( 1 );
      }
    }
    else
    {
      fname = argv[ optind++ ];
    }
  }

  if( fname == NULL )
  {
    fprintf( stderr, "%s: missing filename\n", argv[ 0 ] );
    show_help( argv[ 0 ] );
    exit( 1 );
  }

  // Check if destination file exists
  struct stat st;
  if( stat( fname, &st ) == 0 )
    casio.dst_file_exists = true;

  // Initialize communication channel
  int fd = open( commdev, O_RDWR | O_NOCTTY );
  if( fd < 0 ) { perror( commdev ); exit( -1 ); }
  if( set_serial( fd, &casio ) != 0 ) { perror( "serial setup" ); exit( -1 ); }

  // Initialize FSM state
  casio.st = ST_WAIT_SYN;
  casio.last_rx_s = get_systime_s();
  casio.limit_s = LONGWAIT;

  fputs( "\nReceiving a file from calculator\n", stderr );

  /***********************
  *   FSM Loop
  ***********************/
  while( casio.st != ST_DONE )
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

    int r = select(fd + 1, &rfds, NULL, NULL, &tv);
    if( r < 0 ) { perror( "select" ); exit( 1 ); }
  
    if( timeout_expired( &casio ) )
    {
      Action a = casio_on_timeout( &casio );
      if( a.kind != ACT_NONE )
	if( do_action( fd, &a ) < 0 )
	  exit( -1 );
    }

    if( r == 0 ) continue;

    uint8_t buf[255];
    int n = read( fd, buf, sizeof buf );
    if( n > 0 )
    {
      casio.last_rx_s = get_systime_s();

      for( int i = 0; i < n; i++ )
      {
        Action a = casio_fx_on_byte( &casio, buf[ i ] );
	if( a.kind != ACT_NONE )
	if( do_action( fd, &a ) < 0 )
	  exit( -1 );
      }
    }
  }

  if( write_outfile( fname, &casio ) < 0 )
    exit( -1 );

  return 0;
}

static int write_outfile( char *fname, Casio_FX *c )
{
  FILE *outfile;

  if( ( outfile = fopen( fname, "wb" ) ) == NULL ) { perror( fname ); return -1; }

  switch( c->magic )
  {
    case P1: // One program
      fwrite( c->hdr, 1, sizeof( c->hdr ), outfile );
      fwrite( c->data, 1, c->size, outfile );
      break;

    case PZ: // All programs
      fwrite( c->hdr, 1, sizeof( c->hdr ), outfile );
      fwrite( c->directory, 1, sizeof( c->directory ), outfile );
      fwrite( c->data, 1, c->size, outfile );
      break;

    case DD: // Screendump
          fwrite( c->hdr, 1, sizeof( c->hdr ), outfile );
          fwrite( c->data, 1, c->size, outfile );
          break;
    default: // This should never happen, incoming data is sanitized
      fclose( outfile );
      fputs( "\n** ERROR: Malformatted header!\n", stderr );
      return -1;
  }

  fclose( outfile );

  return 0;
}

/*****************************************************************
* https://tldp.org/HOWTO/Serial-Programming-HOWTO/x115.html
*****************************************************************/
static int set_serial( int fd, Casio_FX *c )
{
  struct termios tio;
  if( tcgetattr( fd, &tio ) != 0 )
    return -1;

  cfmakeraw( &tio );
  cfsetispeed( &tio, c->bps );
  cfsetospeed( &tio, c->bps );

  // 8[N|E|O]1
  switch( c->parity )
  {
    case EVEN:
      tio.c_cflag |= PARENB;
      tio.c_cflag &= ~PARODD;
      break;

    case ODD:
      tio.c_cflag |= ( PARENB | PARODD );
      break;
  
    default:
      tio.c_cflag &= ~PARENB; // No parity
  }
  tio.c_cflag &= ~( CSTOPB | CSIZE );
  tio.c_cflag |= ( CS8 | CLOCAL | CREAD );

  // No flow control
  tio.c_cflag &= ~CRTSCTS;
  tio.c_iflag &= ~( IXON | IXOFF | IXANY );

  // Read timeouts: return quickly so we can select()
  tio.c_cc[VMIN]  = 0;
  tio.c_cc[VTIME] = 1; // 0.1s

  tcflush( fd, TCIOFLUSH );
  if( tcsetattr( fd, TCSANOW, &tio ) != 0 )
    return -1;

  return 0;
}

static int do_action( int fd, Action *a )
{
  size_t n;

  switch( a->kind )
  {
    case ACT_SEND1:
      n = write( fd, a->bytes, 1 );
      if( n != 1 ) { perror( "Write" ); return -1; }
      return 0;
    case ACT_SEND1_PRINT_MSG_EXIT:
      n = write( fd, a->bytes, 1 );
      if( n != 1 ) { perror( "Write" ); return -1; }
      fputs( a->string, stderr );
      return -1;
    case ACT_NONE:
    default:
      return 0;
  }
}

static time_t get_systime_s( void )
{
  struct timespec t;
  clock_gettime( CLOCK_MONOTONIC_RAW, &t );
  return t.tv_sec;
}

static bool timeout_expired( Casio_FX *c )
{
  time_t now = get_systime_s();
  return ( now - c->last_rx_s ) > c->limit_s;
}

static void show_help( char *name )
{
  fprintf( stderr,
	"\nUsage: %s [OPTIONS] FILENAME\n"
	"Receive a file from Casio calculator.\n\n"
	"Filename is a required argument.\n"
	"\t-h, --help\tShow this help text and terminate\n"
	"\t-d, --device\tProvide serial device name, defaults to /dev/ttyUSB0\n"
	"\t-e, --even\tSerial port protocol parity 'even'\n"
	"\t-o, --odd\tSerial port protocol parity 'odd'\n"
	"\t-b, --bps\tProvide bps rate, 1200, 2400, 4800 or 9600 (default)\n"
	"\t-V, --version\tPrint version and exit\n",
	name );
}
