## Format of Casio CAS file.

CAS files store data exactly in the same form as it is received from the calculator.
It consists of one or more sections that start with the ':' (0x3A) byte and
end with a checksum byte. Checksum is mod 256 cumulative,
starting from the second byte to the penultimate one. The checksum byte
is equal to mod 256 negative of the calculation. That way, adding the checksum
byte to the calculated sum mod 256 is equal to 0x00.

The first section is always the 40 byte header.

| Byte           | Value or description                              |
|----------------|---------------------------------------------------|
|   0            |  ':' (0x3A)                                       |
| 1, 2           |  Magic string, identifying the type of the header |
| 3 to [6 .. 26] | depending on the Magic value, header data         |
| until 38       | usually filled with 0xFF                          |
| 39             | -Sum( bytes 1 to 38 ) % 256 (Modulo 256)          |


## Magic strings and header format

| : | P | 1 |0x00|MSB|LSB|MODE|0x00|0xFF| ..... |CHKSUM|
|---|---|---|----|---|---|----|----|----|-------|------|

**P1** - One program

|Supported by|
|------------|
| fx-7700GB  |
| fx-7700GE  |

| offset | value                   |
|--------|-------------------------|
|  3     | 0x00                    |
|  4     | MSB of the payload SIZE |
|  5     | LSB of the payload SIZE |
|  6     | MODE bits               |
|  7     | 0x00                    |

| Mode bit | Meaning               |
|----------|-----------------------|
|    0     | Unknown               |
|    1     | Statistics data       |
|    2     | Matrix mode           |
|    3     | Unknown               |
|    4     | Standard deviation    |
|    5     | Linear regression     |
|    6     | Base-N                |
|    7     | Draw Statistics Graph |

  Header is followed by variable size section containing the program.
  Program is terminated by one 0xFF byte, SIZE value includes the starting ':' and
  checksum byte.

| : | P | Z |0x00|MSB|LSB|MODE|0x00|0xFF| ..... |CHKSUM|
|---|---|---|----|---|---|----|----|----|-------|------|
 
**PZ** - All 38 programs

|Supported by|
|------------|
| fx-7700GB  |
| fx-7700GE  |

| offset | value                   |
|--------|-------------------------|
|  3     | 0x00                    |
|  4     | MSB of the payload SIZE |
|  5     | LSB of the payload SIZE |
|  6     | MODE bits               |
|  7     | 0x00                    |

| Mode bit | Meaning               |
|----------|-----------------------|
|	0      | Unknown               |
|   1      | Statistics data       |
|   2      | Matrix mode           |
|   3      | Unknown               |
|   4      | Standard deviation    |
|   5      | Linear regression     |
|   6      | Base-N                |
|   7      | Draw Statistics Graph |

  Header is followed by the 192-byte programs directory section that contains 38 5-byte fields,
  one field for each program, from A-Z, r and theta.

| offset | value                   |
|--------|-------------------------|
|  0     | 0x00                    |
|  1     | MSB of the payload SIZE |
|  2     | LSB of the payload SIZE |
|  3     | MODE bits               |
|  4     | 0x00                    |

  The last section is variable size and contains the actual programs data.
  All programs are terminated with one 0xFF byte, SIZE value includes the starting ':'
  and checksum byte.

| : | F | 1 |0x00|MSB|LSB|MODE|0x00|0xFF| ..... |CHKSUM|
|---|---|---|----|---|---|----|----|----|-------|------|

 **F1** - One function memory

|Supported by|
|------------|
| fx-7700GE  |

| offset | value                   |
|--------|-------------------------|
|  3     | 0x00                    |
|  4     | MSB of the payload SIZE |
|  5     | LSB of the payload SIZE |
|  6     | MODE bits               |
|  7     | 0x00                    |

  Header is followed by variable size section with the content of the function memory.
  Content is terminated by one 0xFF byte, SIZE value includes the starting ':' and
  checksum byte.

| : | F | 6 |0x00|MSB|LSB|MODE|0x00|MS1|LS1|MS2|LS2| ..... |CHKSUM|
|---|---|---|----|---|---|----|----|---|---|---|---|-------|------|

**F6** - All 6 function memories

|Supported by|
|------------|
| fx-7700GE  |

| offset | value                   |
|--------|-------------------------|
|  3     | 0x00                    |
|  4     | MSB of the payload SIZE |
|  5     | LSB of the payload SIZE |
|  6     | MODE bits               |
|  7     | 0x00                    |

  bytes 8..19 of the header contain 6 2-byte fields, indicating the size of each function.
  Every pair has MSB first and LSB second.

  Header is followed by variable size section that contains the actual function memory data.
  All functions are terminated with one 0xFF byte, SIZE value includes the starting ':'
  and checksum byte of the section.

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | M | 1 | R | 8 |COL|ROW| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 M1	One matrix
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - number of columns (C)
  6 - number of rows (R)

  Header is followed by CxR+1 14-byte sections. The last section is just a filler, marking
  the ending of the matrix data.
  Every significant section has the following
  bytes
  1 - 0x00
  2 - 0x00
  3 - colum number
  4 - row number
  5..12 - cell value

  The last terminating section contains
  1 - 0x17 (End of Transmission Block)
  2..12 - filled with 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | M | A | R | 8 |COL|ROW| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 MA	Set of matrices
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - number of columns (C)
  6 - number of rows (R)

  Header section is followed by CxR 14-byte sections.
  Every section has the following
  bytes
  1 - 0x00
  2 - 0x00
  3 - colum number
  4 - row number
  5..12 - cell value

  This matrix description is followed by zero or more similar matrix descriptions with the
  magic value 'MA'.
  The matrix descriptions are terminated by the "End of Transmission Block" or ETB header
  section that has byte
  1 - 0x17 (End of Transmission Block)
  2...38 - 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | S | S | R | 8 |MSR|LSR| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 SS	Statistics for Single variable
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of ROW_COUNT
  6 - Least significant byte of ROW_COUNT

  Header is followed by ROW_COUNT+1 20-byte sections. The last section is just a filler,
  marking the ending of the data.
  Every significant section has the following bytes
  bytes
   1 - 0x00
   2 - 0x00
   3..10 - data field value
  11..18 - repetition field value

  The last terminating section contains
  1 - 0x17 (End of Transmission Block)
  2..18 - filled with 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | S | R | R | 8 |MSR|LSR| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 SR	Statistics with Regression for multivariable
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of ROW_COUNT
  6 - Least significant byte of ROW_COUNT

  Header is followed by ROW_COUNT+1 28-byte sections. The last section is just a filler,
  marking the ending of the data.
  Every significant section has the following bytes
  bytes
   1 - 0x00
   2 - 0x00
   3..10 - data X field value
  11..18 - data Y field value
  19..26 - repetition field value

  The last terminating section contains
  1 - 0x17 (End of Transmission Block)
  2..26 - filled with 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | A | D | R | 8 |MSB|LSB| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+
 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | A | M | R | 8 |MSB|LSB| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+
 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | D | M | R | 8 |MSB|LSB| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 AD	All Data variables
 AM	Alpha Memory variables
 DM	Defined Memory variables
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of VAR_COUNT
  6 - Least significant byte of VAR_COUNT

  Header is followed by VAR_COUNT 12-byte sections.
  The last section is just a filler marking the end of data.
  Every significant section has the following bytes
  bytes
   1 - 0x00
   2 - 0x00
   3..10 - data field value

  The last terminating section contains
  1 - 0x17 (End of Transmission Block)
  2..10 - filled with 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | G | R | R | 8 |MSV|LSV| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 GR	Graphics range
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of VAL_COUNT that is always = 0x00
  6 - Least significant byte of VAL_COUNT that is always = 0x09

  Header is followed by VAL_COUNT*8+4 byte section.
  It has the following bytes
  bytes
   1 - 0x00
   2 - 0x00
   3..10 - Xmin
  11..18 -  max
  19..26 -  scale
  27..34 - Ymin
  35..42 -  max
  43..50 -  scale
  51..58 - T/Theta min
  59..66 - T/Theta max
  67..74 - T/Theta pitch

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | G | F | R | 8 |MSV|LSV| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 GF	Graphics zoom factor
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of VAL_COUNT that is always = 0x00
  6 - Least significant byte of VAL_COUNT that is always = 0x02

  Header is followed by VAL_COUNT*8+4 byte section.
  It has the following bytes
  bytes
   1 - 0x00
   2 - 0x00
   3..10 - X factor
  11..18 - Y factor

 +---+---+---+---+---+---+---+---+---+---+---+-------+---+
 | : | G | 1 | 00|MSB|LSB|MDE| 00|SEL|TYP| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+---+---+-------+---+

 G1	One graphics function
	Supported by fx-7700GE
  Bytes
  3 - 0x00 
  4 - MSB of the payload SIZE
  5 - LSB of the payload SIZE
  6 - MODE bits ?
  7 - 0x00
  8 - 0x00 not selected
      0x01 selected
  9 - graph type
	0x00 - Rectangular
	0x01 - unknown?
	0x02 - Polar
	0x03 - Parametrical
	0x04 - Inequality >
	0x05 - Inequality <
	0x06 - Inequality >=
	0x07 - Inequality <=

  Header is followed by variable size section containing the function.
  Function is terminated by one 0xFF byte, SIZE value includes the starting ':' and
  checksum byte.

 +---+---+---+---+---+---+---+---+---+---+---+-------+---+
 | : | G | A | 00|MSB|LSB|MDE| 00|SEL|TYP| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+---+---+-------+---+

 GA	Set of graphics functions
	Supported by fx-7700GE
  Bytes
  3 - 0x00 
  4 - MSB of the payload SIZE
  5 - LSB of the payload SIZE
  6 - MODE bits ?
  7 - 0x00
  8 - selected flag
  9 - graph type

  Header is followed by variable size section containing the function.
  Function is terminated by one 0xFF byte, SIZE value includes the starting ':' and
  checksum byte.

  This graphic function description is followed by zero or more similar descriptions with the
  magic value 'GA'.
  The descriptions are terminated by the "End of Transmission Block" or ETB header section
  that has the form
  1 - 0x17 (End of Transmission Block)
  2...38 - 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | S | D | R | 8 |COL|ROW| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 SD	One Linear multivariable equation, the same format as one matrix
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - number of columns (C)
  6 - number of rows (R)

  Header is followed by CxR+1 14-byte sections. The last section is just a filler, marking
  the ending of the matrix data.
  Every significant section has the following bytes
  bytes
  1 - 0x00
  2 - 0x00
  3 - colum number
  4 - row number
  5..12 - cell value

  The last terminating section contains
  1 - 0x17 (End of Transmission Block)
  2..12 - filled with 0xFF

 +---+---+---+---+---+---+---+---+---+-------+---+
 | : | P | D | R | 8 |MSD|LSD| FF| FF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+-------+---+

 PD	Polynomial equation
	Supported by	fx-7700GE

  bytes 3,4 are characters "R8"
  5 - Most significant byte of DEGREE_COUNT
  6 - Least significant byte of DEGREE_COUNT

  There is no followup sections if DEGREE_COUNT equals 0.
  Header is followed by (DEGREE_COUNT+1)*8+4 byte section if
  DEGREE_COUNT > 0
  The section bytes
    1 - 0x00
    2 - 0x00
    3..10 - coefficient for the largest power
   11..18 - next coefficient
   19..26 - ...

 fx-7700GE
 +---+---+---+---+---+---+---+---+---+---+-------+---+
 | : | D | D |HGT|WDT|x00| R | W | F |xFF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+---+-------+---+
 CFX-9850G monochrome
 +---+---+---+---+---+---+---+---+---+---+-------+---+
 | : | D | D |HGT|WDT|x10| D | W | F |xFF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+---+-------+---+

 DD	Screendump
	Supported by	fx-7700GE
			CFX-9850G

  bytes
  3 - Image HEIGHT in pixels
  4 - image WIDTH in pixels
  5 - Unknown?
  6,7,8 - fx-7700GE "RWF"
	  CFX-9850G "DWF"

  Header is followed by HEIGHT*((WIDTH+7)/8)+2 byte section.
  Bytes leftmost pixel contains MSB and rightmost pixel LSB.

  fx-7700GE bytes are ordered in rows from left to right
	    rows are ordered from top to bottom.
  CFX-9850G bytes are ordered in columns from top to bottom
	    columns are ordered from right to left. 

 +---+---+---+---+---+-------+---+
 | : | A | L |xFF|xFF| ..... |SUM|
 +---+---+---+---+---+-------+---+

 AL	All data. All the current objects of the calculator
	Supported by	fx-7700GE

  Header section is idirectly followed by objects in order:
  PZ	All 38 programs
  F6	All Function memories
  MA	All Matrices
  SS	Statistics Single variable
  SR	Statistics Regression
  AD	All variable memory Data
  GR	Graphics Range
  GF	Graphic zoom Factor
  GA	All Graphics functions
  SD	Solve linear multivariable equation
  PD	Polynomial equation
  ETB2	End of Transmission Block header

 +---+---+---+---+---+---+---+---+---+---+---+-------+---+
 | : | B | U | T | Y | P | E | A | 0 | 1 |xFF| ..... |SUM|
 +---+---+---+---+---+---+---+---+---+---+---+-------+---+

 BU	Backup. The RAM dump of the calculator
	Supported by	fx-7700GE

  bytes 3..9 contain string "TYPEA01"

  Header section is followed by 8130 byte section of the RAM dump.
  The fx-7700GE has total 8192 byte RAM.

 Value memory number formats
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~

 Decimal fractions are stored with 8 bytes using Binary Coded Decimal notation.
 +---+---+---+---+---+---+---+---+
 | MS| M | M | M | M | M | M | E |
 +---+---+---+---+---+---+---+---+

 Uninitialized value memory could possibly store 0xFFFFFFFFFFFFFFFF
 Zero value is all zeros 0x0000000000000000

 The MSB four most significant bits represents the mantissa first digit
 The MSB four least significant bits represents the mantissa sign and
 exponent sign
  0x0 - Mantissa > 0; Exponent < 0
  0x1 - Mantissa > 0; Exponent >= 0
  0x5 - Mantissa < 0; Exponent < 0
  0x6 - Mantissa < 0; Exponent >= 0

 The negative exponent is calculated by adding the value to 0x9A, so if
 exponent = -1, then the value 0x9A - 0x01 = 0x99 is stored.
 Mantissa will have space for 13 significant digits and exponent for 2.
 Ex:
 0.03 ->	0x3 0 000000000000 98

 Proper and mixed fractions are indicated with the value 0xAn in the exponent field.
 The number 'n' indicates the length in bytes, used by the fraction, decremented by 2.
 Fields are separated by 0xB.
 MSB lower four bits are used for sign, as described previously.
 Ex:
 1/2 ->		0x1 1 B 2 0000000000 A2
 -2/9 ->	0x2 6 B 9 0000000000 A2
 -1/12345600 ->	0x1 6 B 12345600 000 A9
 5 3/11 ->	0x5 1 B 3 B 11 0000000 A5
 12345 1/20 ->	0x1 1 2345 B 1 B 20 000A9

