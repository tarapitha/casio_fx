# casio_fx
Suite of Linux command-line utilities for communication between the calculator and the PC via the calculator serial port.


## INTRODUCTION

Some Casio calculators, including fx-7700GB, have serial interface 
with 2.5mm plug for exchanging data with other calculators and PCs.

 - The communication line carries RS-232 TTL signal, the voltage is 5V for older models and 3.3V for newer models;
 - Data rate can be configured as 1200, 2400, 4800 and 9600 baud
 - Parity can be set to Even, Odd or None

Casio provided optional cables for connecting two calculators, part numbers
**SB-60** or **SB-62**, and also optional battery powered level-shifter kits
for connecting the calculator to PC via native serial interface, part numbers
**FA-100** or **FA-121** and maybe also others.


## REQUIREMENTS

This suite of utilities was written to enable the communication between the Linux PC
and the calculator. It should work with any suitable serial device that has compatible
voltage levels. However, testing has been done only using the FTDI TTL-232R-5V-AJ cable
with the fitted 3.5mm to 2.5mm plug converter.
The converter has to have tip and ring connection crossed.

Hardware used for testing:

| Calculator | FTDI TTL-232R-5V-AJ |
|------------|---------------------|
| Tip        | Ring                |
| Ring       | Tip                 |
| Sleeve     | Sleeve              |


## FEATURES

The utilities were tested and will work with **fx-7700GB** and **fx-7700GE** models for
communicating only programs. Only monochrome screendump was tested to work with **CFX-9850G**.
To be clear, no other data communication functions are currently implemented, as I wrote
the utility primarily for Casio fx-7700GB, this calculator is able to transmit and receive
only programs.


## COMPILING

The utility suite is written for Linux. For creating binaries, run '**make**' in the
project root directory. Binaries will be built into 'build' subdirectory.
Run '**make clean**' to delete the files createf by building process.


## BINARIES

Three binaries will be built:

casio_rx -  utility to receive data from the calculator.
            Running the utility will set the PC into state of waiting for the calculator to start the transmission.
            
casio_tx -  utility to transmit data to the calculator.
            Running the utility will start transmission to the calculator. Calculator has to be already in receiving state
            
cas2pbm  -  utility to convert CAS formatted screenshot from the caclulator into PBM P4 image.


All three utilities will output details about how to use them when run without the command line arguments or when
started with -h or --help command line option.


