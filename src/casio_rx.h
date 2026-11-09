// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* casio_rx.h
*
* Header file for Casio Graphing calculator
* program link receiver protocol utility.
*/

#define _DEFAULT_SOURCE
#define kB (1024)

#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <termios.h>

enum signals
{
  ACK		= 0x06, // Reply: Acknowledged / Yes!
  DC3		= 0x13, // Reply: Ready to receive!
  NAK		= 0x15,	// Reply: Not Acknowledged / No!
  SYN		= 0x16,	// Request: Are you ready to receive?
  AREA_USED	= 0x21,	// ! Reply: The area already contains data
  GENERIC_ERROR	= 0x22,	// " Reply: Unspecified error condition
  MEM_FULL	= 0x24,	// $ Reply: Not enough memory to store this transmission
  CHKSUM_ERROR	= 0x2B,	// + Reply: The sent data section has incorrect control sum
  INIT_SEND	= 0x3A,	// : Marker: data payload follows
  MODE_ERROR	= 0x3F	// ? Reply: Header mode byte has unrecognized value
};

// Magic values for different CAS object formats
#define MAGIC_VALUES \
  X(AD)	/* All variables memory 	 */ \
  X(AL)	/* All data			 */ \
  X(AM)	/* Alpha variables memory   	 */ \
  X(BU)	/* Backup			 */ \
  X(DD)	/* Screendump			 */ \
  X(DM)	/* Defined variables memory 	 */ \
  X(F1)	/* One function memory		 */ \
  X(F6)	/* All function memories	 */ \
  X(G1)	/* One graph function		 */ \
  X(GA)	/* All graph functions		 */ \
  X(GF)	/* Graphic zoom factor		 */ \
  X(GR)	/* Graphics range		 */ \
  X(M1)	/* One Matrix			 */ \
  X(MA)	/* All Matrices			 */ \
  X(P1)	/* One program			 */ \
  X(PD)	/* Polynomial equation		 */ \
  X(PZ)	/* All programs			 */ \
  X(SD)	/* Linear multivariable equation */ \
  X(SR)	/* Regression			 */ \
  X(SS)	/* Single-variable statistics	 */ \
  X(ETB1) /* End of Transmission Block - "0x17,0xFF" */ \
  X(ETB2) /* End of Transmission Block - "0x17,0x17" */

// Enum of the Magic values
typedef enum
{
  #define X(name) name,
  MAGIC_VALUES
  #undef X
  MAGIC_COUNT,	// Number of magic values defined, do not change order!
  UNKNOWN	// Placeholder for Undefined value
} Magic_t;

// FSM states
typedef enum
{
  ST_WAIT_SYN,
  ST_WAIT_HDR,
  ST_READ_HDR,
  ST_WAIT_OVRWRT_DECISION,
  ST_WAIT_BACKUP_DATA,
  ST_READ_BACKUP_DATA,
  ST_WAIT_VARIABLES_DATA,
  ST_READ_VARIABLES_DATA,
  ST_WAIT_FACTOR_DATA,
  ST_READ_FACTOR_DATA,
  ST_WAIT_RANGE_DATA,
  ST_READ_RANGE_DATA,
  ST_WAIT_GRAPHFUNC_DATA,
  ST_READ_GRAPHFUNC_DATA,
  ST_WAIT_ALLGRAPHFUNC_DATA,
  ST_READ_ALLGRAPHFUNC_DATA,
  ST_WAIT_FUNCMEM_DATA,
  ST_READ_FUNCMEM_DATA,
  ST_WAIT_ALLFUNCMEM_DATA,
  ST_READ_ALLFUNCMEM_DATA,
  ST_WAIT_MATRIX_DATA,
  ST_READ_MATRIX_DATA,
  ST_WAIT_ALLMATRIX_DATA,
  ST_READ_ALLMATRIX_DATA,
  ST_WAIT_PRG_DATA,
  ST_READ_PRG_DATA,
  ST_WAIT_ALLPRG_DIRECTORY,
  ST_READ_ALLPRG_DIRECTORY,
  ST_WAIT_ALLPRG_DATA,
  ST_READ_ALLPRG_DATA,
  ST_WAIT_REGRESSION_DATA,
  ST_READ_REGRESSION_DATA,
  ST_WAIT_STAT_DATA,
  ST_READ_STAT_DATA,
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

typedef enum { NONE, EVEN, ODD, PARITY_UNSPECIFIED } Parity_t;

typedef struct
{
  Action_type	kind;
  uint8_t	bytes[ 4 ];
  char		*string;
  size_t	len;
} Action;

enum bufsize
{
  BUF_N		= 64*kB,	// 2^16 = 64kB, it is max possible ".size" value
  HDR_SIZE	= 1+38+1,	// fx-7700 standard header size
  BU_SIZE	= 8130,		// Almost all of the 8kB RAM
  PZ_DIR_SIZE	= 1+38*5+1,	// ':', 38xSizeMode[5], Checksum
  SD_ROW_SIZE	= 1+2+2*8+1,	// ':', 0x00, 0x00, Number[8], Number[8], Checksum
  REGRESSION_ROW_SIZE	= 28,	// ':', 0x00, 0x00, Number[8], Number[8], Number[8], Checksum
  MATRIX_CELL_SIZE	= 14,	// ':', 0x00, 0x00, Col, Row, Number[8], Checksum
  VARIABLES_SIZE	= 1+2+8+1  // ':', 0x00, 0x00, Number[8], Checksum
};

typedef struct
{
  State		st;
  uint8_t	buf[ BUF_N ];
  uint8_t	* const data_end;

  uint8_t	*data;	// Section pointer
  uint16_t	size;
  uint8_t	data_sum;
  size_t	data_i;

  uint8_t	mode;
  Magic_t	magic;

  uint8_t	height;
  uint8_t	width;
  uint8_t	matrix_n;
  uint8_t	matrix_i;
  uint16_t	sd_n;
  uint16_t	sd_i;
  uint16_t	regression_n;
  uint16_t	regression_i;
  uint16_t	variables_n;
  uint16_t	variables_i;
  uint8_t	polynomial_degree;

  bool		all_data;
  bool		save_stream;

  time_t	last_rx_s;
  time_t	limit_s;
  time_t	deadline_s;

  Parity_t	parity;
  speed_t	bps;

  uint8_t	overwrite;	// ACK or NAK
  bool		unchecked;
  bool		dst_file_exists;
  bool		do_overwrite;
} Casio_FX;
