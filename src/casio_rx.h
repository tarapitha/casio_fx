// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_sx.h
*
* Header file for Casio Graphing calculator
* program link receiver protocol utility.
*/

#define _DEFAULT_SOURCE
#define kB (1024)

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <termios.h>

enum signals
{
  ACK		= 0x06, // Reply: Acknowledged / Yes!
  DC3		= 0x13, // Reply: I'm ready to receive!
  NAK		= 0x15,	// Reply: No!
  SYN		= 0x16,	// Request: Are you ready to receive?
  AREA_USED	= 0x21,	// ! Reply: The area already contains data
  GENERIC_ERROR	= 0x22,	// "
  MEM_FULL	= 0x24,	// $ Reply: No space to store this transmission
  CHKSUM_ERROR	= 0x2B,	// + Reply: The sent data packet has incorrect control sum
  MGC_PRG_ONE	= 0x31,	// 1
  INIT_SEND	= 0x3A,	// : Marker: Data packet will follow immediately
  MODE_ERROR	= 0x3F,	// ? Reply: Header mode byte has unrecognized value
  MAGIC_PRG	= 0x50,	// P
  MGC_PRG_ALL	= 0x5A,	// Z
  END_MARKER	= 0xFF  // Marker: Every program contains this as the last byte
};

typedef enum
{
  P1,		// One program
  PZ,		// All programs
  DD,		// Screendump
  UNKNOWN	// Unknown
} Magic_t;

typedef enum { NONE, EVEN, ODD } Parity_t;

typedef enum
{
  ST_WAIT_SYN,
  ST_WAIT_HDR,
  ST_READ_HDR,
  ST_WAIT_OVRWRT_DECISION,
  ST_WAIT_PRG_DATA,
  ST_READ_PRG_DATA,
  ST_WAIT_ALLPRG_DIRECTORY,
  ST_READ_ALLPRG_DIRECTORY,
  ST_WAIT_ALLPRG_DATA,
  ST_READ_ALLPRG_DATA,
  ST_WAIT_SCRNDMP,
  ST_READ_SCRNDMP,
  ST_DONE
} State;

typedef enum
{
  ACT_NONE,
  ACT_SEND1,
  ACT_SEND1_PRINT_MSG_EXIT
} Action_type;

typedef struct
{
  char	  *str;
  speed_t speed;
} Rate_tr;

typedef struct
{
  char		*str;
  Magic_t	magic;
} Magic_tr;

typedef struct
{
  Action_type	kind;
  uint8_t	bytes[ 4 ];
  char		*string;
  size_t	len;
} Action;

typedef struct
{
  State		st;
  uint8_t	hdr[ 38 + 2 ]; // ':' + header + ctrlsum
  uint8_t	hdr_sum;
  size_t	hdr_i;

  uint8_t	directory[ 38 * 5 + 2 ]; // ':' + 38 program headers + ctrlsum
  uint8_t	directory_sum;
  size_t	directory_i;

  uint8_t	data[ 64 * kB ]; // 2^16 = 64kB, max possible ".size" value
  uint16_t	size;
  uint8_t	data_sum;
  size_t	data_i;

  uint8_t	mode;

  Magic_t	magic;

  uint8_t	height;
  uint8_t	width;

  time_t	last_rx_s;
  time_t	limit_s;
  time_t	deadline_s;

  Parity_t	parity;
  speed_t	bps;

  bool		dst_file_exists;
} Casio_FX;
