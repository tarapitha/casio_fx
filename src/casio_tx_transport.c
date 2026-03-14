// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_tx_transport.c
*
* Transport logic framework implementation for Casio Graphing calculator
* program link transmit protocol.
*/

#include "casio_tx.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <poll.h>
#include <time.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h>

const time_t SHORTWAIT	= 2;		// about 1-2 seconds
const time_t LONGWAIT	= 6 * 60;	// 6 minutes

extern Action casio_fx_on_byte( Casio_FX *, uint8_t );
extern Action casio_fx_apply_decision( Casio_FX * );
extern Action casio_on_timeout( Casio_FX * );

static int read_infile( char *, Casio_FX * );
static int parse_header( Casio_FX * );
static int set_serial( int, Casio_FX * );
static size_t write_all_nonblocking( int, const uint8_t *, size_t );
static void decide_overwrite( Casio_FX *, unsigned );
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

  casio.interactive = false;
  casio.unchecked   = false;
  casio.overwrite   = NAK;
  casio.parity      = NONE;
  casio.bps	    = B9600;

  const Rate_tr rates[] =
      { { "1200", B1200 },
	{ "2400", B2400 },
	{ "4800", B4800 },
	{ "9600", B9600 } };

  char *commdev = "/dev/ttyUSB0";
  char *fname = NULL;

  // Parse command line options
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
    if( ( opt = getopt_long( argc, argv, "+hd:eob:iynuV", longopts, NULL ) ) != -1 )
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
	 case 'i':
	   // This option is effective only if running from interactive shell
	   if( isatty( STDIN_FILENO ) )
	     casio.interactive = true;
	   break;
	 case 'u':
	   casio.unchecked = true;
	   break;
	 case 'y':
	   casio.overwrite = ACK;
	   break;
	 case 'n':
	   casio.overwrite = NAK;
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

  // Read the file
  int status = read_infile( fname, &casio );
  if( status < 0 ) exit( 1 );

  // Initialize communication channel
  int fd = open( commdev, O_RDWR | O_NOCTTY | O_NONBLOCK );
  if( fd < 0 ) { perror( commdev ); exit( -1 ); }
  if( set_serial( fd, &casio ) != 0 ) { perror( "serial setup" ); exit( -1 ); }

  // Initialize FSM state
  casio.st = ST_WAIT_RDY_TO_RECEIVE;
  casio.last_rx_s = get_systime_s();
  casio.limit_s = SHORTWAIT;

  fputs( "\nSending a file to calculator\n", stderr );

  // Initiate transmission
  uint8_t bt = SYN;
  int n = write_all_nonblocking( fd, &bt, 1 );
  if( n != 1 ) { perror( "Write" ); exit( -1 ); }

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

	if( a.kind == ACT_NEED_DECISION )
	{
	  decide_overwrite( &casio, LONGWAIT );
	  switch( casio.overwrite )
	  {
	    case ACK:
	      fputs( "\nOverwriting the calculator program area\n", stderr );
	      break;
	    case NAK:
	      fputs( "\nNot overwriting the calculator program area.\n"
		     "\nIf overwriting is desirable, "
		     "consider using command line options '-y' or '-i'.\n", stderr );
	      break;
	  }
	  Action a2 = casio_fx_apply_decision( &casio );

	  if( a2.kind == ACT_SEND1 )
	    if( do_action( fd, &a2 ) < 0 )
	      exit( -1 );

	  casio.st = a2.next_state;
	}
	else if( a.kind != ACT_NONE )
	  if( do_action( fd, &a ) < 0 )
	    exit( -1 );
      }
    }
  }

  return 0;
}

static int read_infile( char *fname, Casio_FX *c )
{
  FILE *infile;
  size_t i, n;

  if( ( infile = fopen( fname, "rb" ) ) == NULL ) { perror( fname ); return -1; }

  // Read header from infile
  n = fread( c->hdr, 1, sizeof( c->hdr ), infile );
  if( n != sizeof( c->hdr ) || c->hdr[ 0 ] != ':' || c->hdr[ 1 ] != 'P' )
  {
    fclose( infile );
    fprintf( stderr, "File \"%s\": malformatted header\n", fname );
    return -1;
  }
  for( i = 1, c->hdr_sum = 0; i < sizeof( c->hdr ) - 1; i++ )
    c->hdr_sum += c->hdr[ i ];

  if( c->unchecked )
    c->hdr[ sizeof( c->hdr ) - 1 ] = -c->hdr_sum;
  else
    if( (uint8_t) ( c->hdr[ sizeof( c->hdr ) - 1 ] + c->hdr_sum ) != 0 )
    {
      fclose( infile );
      fprintf( stderr,
	       "\nFile \"%s\": incorrect header checksum,\n"
	       "you could try -u option if you are brave!\n",
               fname );
      return -1;
    }
  parse_header( c );

  switch( c->magic )
  {
    // Read All Programs directory from infile
    case PZ:	// All programs
      n = fread( c->directory, 1, sizeof( c->directory ), infile );
      if( n != sizeof( c->directory ) || c->directory[ 0 ] != ':' )
      {
	fclose( infile );
	fprintf( stderr, "File \"%s\": malformatted directory\n", fname );
	return -1;
      }
      for( i = 1, c->directory_sum = 0; i < sizeof( c->directory ) - 1; i++ )
	c->directory_sum += c->directory[ i ];
      if( c->unchecked )
	c->directory[ sizeof( c->directory ) - 1 ] = -c->directory_sum;
      else
	if( (uint8_t) ( c->directory[ sizeof( c->directory ) - 1 ] + c->directory_sum ) != 0 )
	{
	  fclose( infile );
	  fprintf( stderr,
		   "\nFile \"%s\": incorrect directory checksum,\n"
		   "you could try -u option if you are brave!\n",
		   fname );
	  return -1;
	}
	/*-fallthrough*/

    // Read payload from infile
    case P1:	// One program
      n = fread( c->data, 1, c->size, infile );
      if( n != c->size || c->data[ 0 ] != ':' )
      {
        fclose( infile );
        fprintf( stderr, "File \"%s\": malformatted data payload\n", fname );
        return -1;
      }
      for( i = 1, c->data_sum = 0; i < (size_t) ( c->size - 1 ); i++ )
        c->data_sum += c->data[ i ];
      if( c->unchecked )
	c->data[ c->size - 1 ] = -c->data_sum;
      else
	if( (uint8_t) ( c->data[ c->size - 1 ] + c->data_sum ) != 0 )
	{
	  fclose( infile );
	  fprintf( stderr,
		   "\nFile \"%s\": incorrect data payload checksum,\n"
		   "you could try -u option if you are brave!\n",
		   fname );
	  return -1;
	}
      break;

    default:
      fclose( infile );
      fprintf( stderr, "File \"%s\": malformatted header\n", fname );
      return -1;
  }

  fclose( infile );
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

static size_t write_all_nonblocking( int fd, const uint8_t *buf, size_t len )
{
  size_t written = 0;

  while( written < len )
  {
    ssize_t n = write(fd, buf + written, len - written);
    if( n > 0 )
    {
      written += (size_t) n;
      continue;
    }
    if( n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ) )
    {
      struct pollfd pfd = { .fd = fd, .events = POLLOUT };
      int r = poll(&pfd, 1, 500); /* timeout ms */
      if (r <= 0) return -1;      /* timeout or error */
      continue;
    }
    return -1;
  }
  return written;
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
    case ACT_SEND_BUF:
      n = write_all_nonblocking( fd, a->buf, a->len );
      if( n != a->len ) { perror( "Write" ); return -1; }
      return 0;
    case ACT_PRINT_MSG:
      fputs( a->string, stderr );
      return 0;
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

static int parse_header( Casio_FX *c )
{
  const Magic_tr magic_values[] =
      { { "P1", P1 },
	{ "PZ", PZ } };

  c->magic = UNKNOWN;
  for( int i = 0; i < 2; i++ )
  {
    if( ! strncmp( (char *) ( c->hdr + 1 ), magic_values[ i ].str, 2 ) )
    {
      c->magic = magic_values[ i ].magic;
      break;
    }
  }
  if( c->magic == UNKNOWN )
    return -1;

  // Get the payload size
  uint8_t size_msb = c->hdr[ 3+1 ];
  uint8_t size_lsb = c->hdr[ 4+1 ];
  c->size = (uint16_t)( ( size_msb << 8 ) | size_lsb );

  if( c->size < 2 )
    return -1;

  c->mode = c->hdr[ 5+1 ];

  c->data_i = 0;
  c->data_sum = 0;
  c->directory_sum = 0;

  return 0;
}

static void decide_overwrite( Casio_FX *c, unsigned timeout_seconds )
{
    if ( ! c->interactive )
        return;

    fprintf( stderr,
	"\nSending to calculator\n"
        "This program area is already used, do you want to overwrite?\n"
        "Press 'y' or 'n' then Enter (timeout %u minutes): ",
        timeout_seconds / 60 );
    fflush(stderr);

    fd_set rfds;
    FD_ZERO( &rfds );
    FD_SET( STDIN_FILENO, &rfds );

    struct timeval tv = { .tv_sec = timeout_seconds, .tv_usec = 0 };

    int r = select( STDIN_FILENO + 1, &rfds, NULL, NULL, &tv );
    if( r <= 0 )  /* timeout or error */
    {
      fputc( '\n', stderr );
      c->overwrite = NAK; // In case of timeout, do not overwrite
      return;
    }

    char line[ 16 ];
    if( !fgets( line, sizeof( line ), stdin ) )
    {
      fputc( '\n', stderr );
      c->overwrite = NAK; // In case of error, do not overwrite
      return;
    }

    switch( toupper( line[ 0 ] ) )
    {
      case 'Y':
	c->overwrite = ACK;
	return;
      case 'N':
      default:
	c->overwrite = NAK;
	return;  // If not 'Y', do not overwrite
    }
}

static void show_help( char *name )
{
  fprintf( stderr,
	"\nUsage: %s [OPTIONS] FILENAME\n"
	"Send a file to Casio calculator.\n\n"
	"Filename is a required argument.\n"
	"\t-h, --help\tShow this help text and terminate\n"
	"\t-d, --device\tProvide serial device name, defaults to /dev/ttyUSB0\n"
	"\t-e, --even\tSerial port protocol parity 'even'\n"
	"\t-o, --odd\tSerial port protocol parity 'odd'\n"
	"\t-b, --bps\tProvide bps rate, 1200, 2400, 4800 or 9600 (default)\n"
	"\t-u\t\t'Unchecked'. Do not check the file checksums, recalculate\n"
	"\t-i\t\tIf in interactive shell, then prompt before overwriting\n"
	"\t-y\t\t'Yes'. Overwrite unconditionally, option -i takes precedence\n"
	"\t-n\t\t'No'. Do not overwrite. This is the default behaviour\n"
	"\t-V, --version\tPrint version and exit\n",
	name );
}
