/*
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/
/*==========================================================================*\
|                                                                            |
| Config430FR.h                                                              |
|                                                                            |
| Replicator configuration file for MSP430 FRAM-based families (FRxx)        |
|                                                                            |
|----------------------------------------------------------------------------|
| Project:              MSP430 FRAM Replicator                               |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/14 RL          Initial version.                                     |
|----------------------------------------------------------------------------|
| Designed 2014 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file Config430FR.h
//! \brief Configurations for the MSP430 FRAM Replicator.
/****************************************************************************/
/* QUICK START OPTIONS                                                      */
/****************************************************************************/

//! \brief Select the interface to be used to communicate with the device
//! \details Options: 
//! <UL> 
//! <li> JTAG_IF          - MCU has 4-wire JTAG ONLY (F1xx, old F4xx)
//! <li> SPYBIWIRE_IF     - 2-wire Spy-Bi-Wire (F2xx, F4xx with SBW, F5xx, F6xx, FRxx)
//! <li> SPYBIWIREJTAG_IF - 4-wire JTAG in MCU with Spy-Bi-Wire (F2xx, F4xx 
//! with SBW, F5xx, F6xx, FRxx)
//! </UL>
//! Select ONLY ONE interface, comment-out the remaining option
#define INTERFACE  SPYBIWIRE_IF
//#define INTERFACE  SPYBIWIREJTAG_IF

//! \brief Set the target's Vcc level supplied by REP430F 
//! \details data = 10*Vcc - range 2.1V to 3.6V or 0 (Vcc-OFF)  
#define VCC_LEVEL  30

//! \brief Enable FR4xx/FR2xx special handling
//! \details Comment in this line if the target is an FR4xx/FR2xx device
//#define FR4xxFR2xx

//! \brief Set start address of the main memory
#define MAIN_START_ADDRESS 0xC200

//! \brief Set length of main memory
//! \details Note that the memory length is counted in 16-bit WORDS!
#define MAIN_LENGTH 0x3E00

//! \brief Set the start address of the device RAM
#define RAM_START_ADDRESS 0x1C00

// This table lists possible start adresses for the device's RAM
/*--------------------------------------------------------------------------*/
/*                                                                          */
/*                 DEFAULT  ->  RAM_START_ADDRESS  0x1C00                   */
/*             FR4xx/FR2xx  ->  RAM_START_ADDRESS  0x2000                   */
/*--------------------------------------------------------------------------*/

// The following table lists the main start addresses and memory length for 
// some FRxx devices. If your device is not listed, please look up these 
// values in the device data sheet.
/*--------------------------------------------------------------------------*/
/*    Examples:                                                             */
/*                                                                          */
/* FR5739             ->  MAIN_START_ADDRESS  0xC200  MAIN_LENGTH  0x3E00   */
/* FR5969             ->  MAIN_START_ADDRESS  0x4440  MAIN_LENGTH  0xBC00   */
/* FR6989             ->  MAIN_START_ADDRESS  0x4440  MAIN_LENGTH  0xBC00   */
/* FR4133             ->  MAIN_START_ADDRESS  0xC400  MAIN_LENGTH  0x3C00   */
/* FR2533             ->  MAIN_START_ADDRESS  0xC400  MAIN_LENGTH  0x3C00   */
/*--------------------------------------------------------------------------*/

/****************************************************************************/
/* OTHER DEFINES                                                            */
/****************************************************************************/

//! \brief Select the main clock frequency
//! \details Comment it out for MCLK=12MHz, if the Voltage supplied to the 
//! REP430F is low (below 2.5V).
//! That can apply when the REP430F is supplied from the target device, not 
//! from the external power supply.
#define MCLK_18MHZ
//! \brief Use Jtag Mailbox to write FRAM
//#define MailBoxWriteFram
//! \brief Activate support for Low Power Mode x.5
#define LPM5_ACTIVATED  1
//! \brief Allow use of the "magic pattern"
#define ACTIVATE_MAGIC_PATTERN  1
//! \brief Buffer size for read and write operations in words
#define WordBufferSize  50
//! \brief Maximum number of tries for the determination of the core
//! identification info
#define MAX_ENTRY_TRY  4

/****************************************************************************/
/* TYPEDEFS                                                                 */
/****************************************************************************/

#ifndef __BYTEWORD__
#define __BYTEWORD__
typedef unsigned short word;
typedef unsigned char byte;
#endif

/****************************************************************************/
/* FUNCTION PROTOTYPES                                                      */
/****************************************************************************/

void runProgramm(void);
void main(void);

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
