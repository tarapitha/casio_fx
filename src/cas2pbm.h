// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Robert Tiismus

/*
* cas2pbm.h
*
* Header file for Casio Graphing Calculators
* screenshot CAS file to PBM converter utility.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define kB (1024)

typedef struct
{
  uint8_t	hdr[ 38 + 2 ];	// ':' + header + ctrlsum
  uint8_t	hdr_sum;
  size_t	hdr_i;

  uint8_t	data[ 8 * kB ]; // 2^13 = 8kB, bit over max possible size for screendump
  uint16_t	size;
  uint8_t	data_sum;
  size_t	data_i;
  uint8_t	pbm_data[ 8 * kB ];	// PBM image binary data
  uint8_t	pbm_data_l[ 128 * kB ];	// PBM image 4x magnified image

  char		magic[ 2 ];	// "DD"

  uint8_t	height;
  uint8_t	width;

  bool		unchecked;
  bool		large;

} Casio_FX;
