// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

#include "cas2pbm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static int read_infile( char *, Casio_FX * );
static int parse_header( Casio_FX * );
static void show_help( char * );

/*******************************************
*                M A I N
*******************************************/
int main( int argc, char **argv )
{
  Casio_FX casio;
  memset( &casio, 0, sizeof( casio ) );

  casio.unchecked = false;
  casio.large	  = false;

  char *fname[ 2 ] = { NULL, NULL };
  int fname_i = 0;

  // Parse command line options
  struct option longopts[] = {
    { "help",	 no_argument,       0, 'h' },
    { "large",	 no_argument,       0, 'l' },
    { "version", no_argument,       0, 'V' },
    { 0, 0, 0, 0 }
  };

  int opt;
  while( optind < argc )
  {
    if( ( opt = getopt_long( argc, argv, "+hluV", longopts, NULL ) ) != -1 )
    {
      switch( opt )
      {
	 case 'h':
	   show_help( argv[ 0 ] );
	   return 0;
	 case 'l':
	   casio.large= true;
	   break;
	 case 'u':
	   casio.unchecked = true;
	   break;
	 case 'V':
	   fprintf( stderr, "%s version: %s\n", argv[ 0 ], CASIO_FX_VERSION );
	   return 0;
	 default:
	   show_help( argv[ 0 ] );
	   return 1;
      }
    }
    else
    {
      if( fname_i < 2 )
        fname[ fname_i++ ] = argv[ optind++ ];
    }
  }

  if( fname_i < 2 )
  {
    fprintf( stderr, "%s: missing filenames\n", argv[ 0 ] );
    show_help( argv[ 0 ] );
    return 1;
  }

  if( read_infile( fname[ 0 ], &casio ) < 0 )
    return -1;

  // Reorder the bytes from CAS format to PBM format
  int dst = ( casio.width + 7 ) / 8 - 1;
  for( int src = 1; src < casio.size - 1; src++ )
  {
    casio.pbm_data[ dst ] = casio.data[ src ];
    dst += ( casio.width + 7 ) / 8;
    if( dst > casio.size - 2 )
      dst -= casio.size - 1;
  }

  if( casio.large )
  {
    memset( casio.pbm_data_l, 0, ( 4 * casio.width + 7 ) / 8 * ( 4 * casio.height ) );

    int dst = 0;
    int dst_row_start = dst;
    int col = 0;
    uint32_t bitbuf = 0;

    for( int src = 0; src < casio.size - 2; src++ )
    {
      for( size_t i = 1; i <= 0x80; i <<= 1 )
      {
	bitbuf >>= 4;
	if( casio.pbm_data[ src ] & i )
	  bitbuf |= 0xf0000000;
      }

      for( int j = 0; j < 4; j++ )
	casio.pbm_data_l[ dst + j ] = (uint8_t) ( bitbuf >> ( 3 - j ) * 8 );
      dst += 4;

      if( ++col == ( casio.width + 7 ) / 8 )
      {
	col = 0;
	dst = dst_row_start + ( 4 * casio.width + 7 ) / 8;
	memcpy( casio.pbm_data_l + dst, casio.pbm_data_l + dst_row_start, ( 4 * casio.width + 7 ) / 8 );
	dst += ( 4 * casio.width + 7 ) / 8;
	memcpy( casio.pbm_data_l + dst, casio.pbm_data_l + dst_row_start, ( 4 * casio.width + 7 ) / 8 * 2 );
	dst += ( 4 * casio.width + 7 ) / 8 * 2;
        dst_row_start = dst;
      }
    }
  }

  // Write PBM
  FILE *outfile;

  if( ( outfile = fopen( fname[ 1 ], "wb" ) ) == NULL )
			{ perror( fname[ 1 ] ); return -1; }

  // PBM binary signature and picture dimensions
  if( casio.large )
  {
    fprintf( outfile, "P4\n%d %d\n", 4 * casio.width, 4 * casio.height );
    fwrite( casio.pbm_data_l, 1, ( 4 * casio.width + 7 ) / 8 * ( 4 * casio.height ), outfile );
  }
  else
  {
    fprintf( outfile, "P4\n%d %d\n", casio.width, casio.height );
    fwrite( casio.pbm_data, 1, casio.size - 2, outfile );
  }
  fclose( outfile );

  return 0;
}

static int read_infile( char *fname, Casio_FX *c )
{
  FILE *infile;
  size_t i, n;

  if( ( infile = fopen( fname, "rb" ) ) == NULL ) { perror( fname ); return -1; }

  // Read header from infile
  n = fread( c->hdr, 1, sizeof( c->hdr ), infile );
  if( n != sizeof( c->hdr ) || c->hdr[ 0 ] != ':' ||
         c->hdr[ 1 ] != 'D' || c->hdr[ 2 ] != 'D' )
  {
    fclose( infile );
    fprintf( stderr, "File \"%s\": Not a Casio calculator screendump\n", fname );
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
	       "you could try -u option.\n",
               fname );
      return -1;
    }

  parse_header( c );

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

  fclose( infile );

  return 0;
}

static int parse_header( Casio_FX *c )
{
  for( int i = 0; i < 2; i++ )
    c->magic[ i ] = (char) c->hdr[ i+1 ];
  if( c->magic[0] != 'D' )
    return -1;

  // Get the screendump dimensions
  c->height = c->hdr[ 2+1 ];
  c->width  = c->hdr[ 3+1 ];
  c->size = (uint16_t)( c->height * ( ( c->width + 7 ) / 8 ) + 2 );

  if( c->magic[ 1 ] != 'D' || c->size <= 2 )
      return -1;

  c->data_i = 0;
  c->data_sum = 0;

  return 0;
}

static void show_help( char *name )
{
  fprintf( stderr,
	"\nUsage: %s [OPTIONS] INPUT_FILENAME OUTPUT_FILENAME\n"
	"Convert Casio screenshot CAS file to PBM P4.\n\n"
	"Filenames are required arguments.\n"
	"\t-h, --help\tShow this help text and terminate\n"
	"\t-l, --large\tCreate 4x magnified image\n"
	"\t-u\t\t'Unchecked'. Do not check the file checksums, recalculate\n",
	name );
}
