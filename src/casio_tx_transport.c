// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_tx_transport.c
*
* Transport logic framework implementation for Casio Graphing calculator
* program link transmit protocol.
*/

#include "casio_tx.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h>

const time_t SHORTWAIT	= 2;		// about 1-2 seconds
const time_t LONGWAIT	= 6 * 60;	// 6 minutes

extern Action casio_fx_on_byte( Casio_FX *, uint8_t );
extern Action casio_fx_apply_decision( Casio_FX * );
extern Action casio_on_timeout( Casio_FX * );
extern Parse_action casio_fx_parse_frame( Casio_FX * );

int parse_header( Casio_FX * );

static int read_infile( char *, Casio_FX * );
static int set_serial( int, Casio_FX * );
static ssize_t safe_write( int, const uint8_t *, size_t );
static void decide_overwrite( Casio_FX *, unsigned );
static int do_action( int, Casio_FX *, Action * );
static time_t get_systime_s( void );
static bool timeout_expired( Casio_FX * );
static void show_help( char * );

/*******************************************
*                M A I N
*******************************************/
int main( int argc, char **argv )
{
  Casio_FX casio = { .data_end = casio.buf + BUF_N };

  casio.interactive = false;
  casio.unchecked   = false;
  casio.overwrite   = NAK;
  casio.parity      = NONE;
  casio.bps	    = B9600;

  char *commdev	= "/dev/ttyUSB0";
  char *fname	= NULL;

  const Rate_tr rates[] = {
	{ "1200", B1200 },
	{ "2400", B2400 },
	{ "4800", B4800 },
	{ "9600", B9600 }
  };

  const char *parities[] = {
	[ NONE ] = "none",
	[ EVEN ] = "even",
	[ ODD ]  = "odd"
  };

  // Parse command line options
  struct option longopts[] = {
    { "help",	 no_argument,       0, 'h' },
    { "device",	 required_argument, 0, 'd' },
    { "bps",	 required_argument, 0, 'b' },
    { "parity",	 required_argument, 0, 'p' },
    { "force",	 no_argument,       0, 'f' },
    { "version", no_argument,	    0, 'V' },
    { 0, 0, 0, 0 }
  };

  int opt;
  while( optind < argc )
  {
    if( ( opt = getopt_long( argc, argv, "+hd:b:p:ifuV", longopts, NULL ) ) != -1 )
    {
      switch( opt )
      {
	 case 'd':
	   commdev = optarg;
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
	 case 'p':
	   casio.parity = PARITY_UNSPECIFIED;
	   for( int parity = 0; parity < 3; parity++ )
	   {
	     if( parities[ parity ] && ! strcasecmp( optarg, parities[ parity ] ) )
	     {
	       casio.parity = (Parity_t) parity;
	       break;
	     }
	   }
	   if( casio.parity == PARITY_UNSPECIFIED )
	   {
	     fprintf( stderr, "%s: invalid parity: %s\n", argv[ 0 ], optarg );
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
	 case 'f':
	   casio.overwrite = ACK;
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
  if( read_infile( fname, &casio ) < 0 ) exit( 1 );

  // Initialize communication channel
  int fd = open( commdev, O_RDWR | O_NOCTTY );
  if( fd < 0 ) { perror( commdev ); exit( -1 ); }
  if( set_serial( fd, &casio ) != 0 ) { perror( "serial setup" ); exit( -1 ); }

  fputs( "\nSending a file to calculator\n", stderr );

  // Initialize FSM
  casio.st = ST_INIT;
  casio.last_rx_s = get_systime_s();
  casio.limit_s = SHORTWAIT;
  Action a_init = casio_fx_on_byte( &casio, 0 );
  if( a_init.kind != ACT_NONE && a_init.kind != ACT_NEED_DECISION )
    if( do_action( fd, &casio, &a_init ) < 0 )
      exit( -1 );

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
	if( do_action( fd, &casio, &a ) < 0 )
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
	      fputs( "\nOverwriting the calculator memory area\n", stderr );
	      break;
	    case NAK:
	      fputs( "\nNot overwriting the calculator memory area.\n"
		     "\nIf overwriting is desirable, "
		     "consider using command line options '-y' or '-i'.\n", stderr );
	      break;
	  }
	  Action a2 = casio_fx_apply_decision( &casio );

	  if( a2.kind == ACT_SEND1 )
	    if( do_action( fd, &casio, &a2 ) < 0 )
	      exit( -1 );

	  casio.st = a2.next_state;
	}
	else if( a.kind != ACT_NONE )
	  if( do_action( fd, &casio, &a ) < 0 )
	    exit( -1 );
      }
    }
  }

  return 0;
}

static int parse_do_action( FILE *infile, Casio_FX *c, Parse_action *a )
{
  size_t i, n;

  switch( a->kind )
  {
    case ACT_READ_HEADER:
      if( c->data + a->size >= c->data_end )
	return -4;
      n = fread( c->data, 1 , a->size, infile );
      if( n != a->size || c->data[ 0 ] != ':' )
	return -1;
      for( i = 1, c->data_sum = 0; i < a->size - 1; i++ )
	c->data_sum += c->data[ i ];
      if( c->unchecked )
	c->data[ a->size - 1 ] = -c->data_sum;
      else
	if( (uint8_t) ( c->data[ a->size - 1 ] + c->data_sum ) != 0 )
	  return -2;
      if( parse_header( c ) != 0 )
	return -3;
      c->data += n;
      return 0;

    case ACT_READ_SECTION:
      if( c->data + a->size >= c->data_end )
	return -4;
      n = fread( c->data, 1 , a->size, infile );
      if( n != a->size || c->data[ 0 ] != ':' )
	return -1;
      for( i = 1, c->data_sum = 0; i < a->size - 1; i++ )
	c->data_sum += c->data[ i ];
      if( c->unchecked )
	c->data[ a->size - 1 ] = -c->data_sum;
      else
	if( (uint8_t) ( c->data[ a->size - 1 ] + c->data_sum ) != 0 )
	  return -2;
      c->data += n;
      return 0;

    case ACT_PRINT_MSG_EXIT:
      fputs( a->string, stderr );
      return -1;

    case ACT_NONE:
    default:
      return 0;
  }
}

static int read_infile( char *fname, Casio_FX *c )
{
  FILE *infile;

  if( ( infile = fopen( fname, "rb" ) ) == NULL ) { perror( fname ); return -1; }

  c->st = ST_HDR;
  c->data = c->buf;

  /******************************
  * Input file parsing FSM loop
  ******************************/
  while( c->st != ST_DONE )
  {
    Parse_action a = casio_fx_parse_frame( c );
    if( a.kind != ACT_NONE )
    {
      int r = parse_do_action( infile, c, &a ); 
      if( r < 0 ) 
      {
	fclose( infile );
	switch( r )
	{
	  case -1:
	    fputs( "\n** ERROR: Unrecognized input file format.\n", stderr );
	    break;
	  case -2:
	    fputs( "\n** ERROR: File has section with incorrect checksum.\n", stderr );
	    break;
	  case -3:
	    fputs( "\n** ERROR: File has corrupted header.\n", stderr );
	    break;
	  case -4:
	    fputs( "\n** ERROR: Input buffer overflow while reading file!\n"
		   "The file is probably corrupted.\n", stderr );
	    break;
	  default:
	    break;
	}
	return -1;
      }
    }
  }

  fclose( infile );
  c->data = c->buf;
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

static ssize_t safe_write( int fd, const uint8_t *buf, size_t len )
{
  size_t written = 0;

  while( written < len )
  {
    ssize_t n = write( fd, buf + written, len - written );
    if( n <= 0 )
    {
      if( n < 0 && errno == EINTR )
	continue;
      return -1;
    }
    written += n;
  }
  tcdrain( fd );

  return written;
}

static int do_action( int fd, Casio_FX *c, Action *a )
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
      // Check that we read inside the limits of the buffer
      if( c->data + a->len >= c->data_end )
      {
	fputs( "\n** Internal error: an impossible event ocurred,\n"
	       "trying to read from outside of the memory buffer.\n"
	       "If you are seeing this, reality has diverged from the design document.\n",
		stderr );
	// Calculator expects ':'
	// We'll send something that is not expected to generate an error
	n = write( fd, (uint8_t[]){ MEM_FULL }, 1 );
        if( n != 1 ) { perror( "Write" ); return -1; }
	tcdrain( fd );
	return -1;
      }
      n = safe_write( fd, c->data, a->len );
      if( n != a->len ) { perror( "Write" ); return -1; }
      c->data += a->len;
      c->last_rx_s = get_systime_s();
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

int parse_header( Casio_FX *c )
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
      c->variables_i = 0;
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
      c->matrix_i = 0;
      c->matrix_n = c->width * c->height + 1;	// Max value == 81 + 1
      c->size = MATRIX_CELL_SIZE;
      break;

    case MA:	// All matrices
      c->width  = c->data[ 4+1 ];	// Max value == 9
      c->height = c->data[ 5+1 ];	// Max value == 9
      c->matrix_i = 0;
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
      c->regression_i = 0;
      c->regression_n = ( size_msb << 8 ) | size_lsb;
      break;

    case SS:
      c->size = SD_ROW_SIZE;
      size_msb = c->data[ 4+1 ];
      size_lsb = c->data[ 5+1 ];
      c->sd_i = 0;
      c->sd_n = ( size_msb << 8 ) | size_lsb;
      break;

    case ETB1:	// End of Transmission Block: last header block for set of some data types
    case ETB2:	// End of Transmission Block: last header block for All data
      break;

    default:
      return -1;
  }
  return 0;
}

static void decide_overwrite( Casio_FX *c, unsigned timeout_seconds )
{
    if ( ! c->interactive )
        return;

    fprintf( stderr,
	"\nSending to calculator\n"
        "This memory area is already used, do you want to overwrite?\n"
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
	"\t-b, --bps\tProvide bps rate, 1200, 2400, 4800 or 9600 (default)\n"
	"\t-p, --parity\tProvide parity, even, odd or none (default)\n"
	"\t-u\t\tUnchecked. Do not check the file checksums, recalculate\n"
	"\t-i\t\tIf in interactive shell, then prompt before overwriting\n"
	"\t-f, --force\tOverwrite unconditionally, option -i takes precedence\n"
	"\t-V, --version\tPrint version and exit\n",
	name );
}
