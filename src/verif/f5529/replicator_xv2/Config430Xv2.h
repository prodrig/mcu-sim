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
| Config430Xv2.h                                                             |
|                                                                            |
| Replicator configuration file for MSP430Xv2 Flash-based family (5xx/6xx)   |
| and FRAM-based devices.                                                    |
|                                                                            |
|----------------------------------------------------------------------------|
| Project:              MSP430 Replicator Xv2                                |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/12 RL          Initial version.                                     |
| 1.1 10/12 RL          Added #define for start address of target RAM        |
| 1.2 05/14 RL          Removed FRAM related functionality                   |
|----------------------------------------------------------------------------|
| Designed 2012 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file Config430Xv2.h
//! \brief Configurations for the MSP430 Replicator Xv2.
/****************************************************************************/
/* QUICK START OPTIONS                                                      */
/****************************************************************************/

//! \brief Select the interface to be used to communicate with the device
//! \details Options: 
//! <UL> 
//! <li> JTAG_IF          - MCU has 4-wire JTAG ONLY (F1xx, old F4xx)
//! <li> SPYBIWIRE_IF     - 2-wire Spy-Bi-Wire (F2xx, F4xx with SBW, F5xx, F6xx)
//! <li> SPYBIWIREJTAG_IF - 4-wire JTAG in MCU with Spy-Bi-Wire (F2xx, F4xx 
//! with SBW, F5xx, F6xx)
//! </UL>
//! Select ONLY ONE interface, comment-out the remaining option
//#define INTERFACE  JTAG_IF
#ifndef INTERFACE   /* mcu-sim: -DINTERFACE=... (Makefile.f5529) */
#define INTERFACE  SPYBIWIRE_IF
#endif
//#define INTERFACE  SPYBIWIREJTAG_IF

//! \brief Set the target's Vcc level supplied by REP430F 
//! \details data = 10*Vcc - range 2.1V to 3.6V or 0 (Vcc-OFF)  
#define VCC_LEVEL  30

//! \brief Set start address of the main memory
#define MAIN_START_ADDRESS 0x4400   /* mcu-sim: el objetivo es un F5529 (tabla de abajo) */       

//! \brief Set length of main memory
//! \details Note that the memory length is counted in 16-bit WORDS!
#define MAIN_LENGTH 0x10000        /* mcu-sim */

//! \brief Set the start address of the device RAM
#define RAM_START_ADDRESS 0x2400    /* mcu-sim: F52xx/F55xx (tabla de abajo) */

// This table lists the possible start adresses for the device's RAM
/*--------------------------------------------------------------------------*/
/*                                                                          */
/*                 DEFAULT  ->  RAM_START_ADDRESS  0x1C00                   */
/*                   F52xx  ->  RAM_START_ADDRESS  0x2400                   */
/*--------------------------------------------------------------------------*/

// The following table lists the main start addresses and memory length for 
// most 5xx/6xx devices. If your device is not listed, please look up these 
// values in the device data sheet.
/*--------------------------------------------------------------------------*/
/*    Examples:                                                             */
/*                                                                          */
/* F5529 F5528 F5519  ->  MAIN_START_ADDRESS  0x4400  MAIN_LENGTH  0x10000  */
/* F5527 F5526 F5517  ->  MAIN_START_ADDRESS  0x4400  MAIN_LENGTH  0xC000   */
/* F5525 F5524 F5515  ->  MAIN_START_ADDRESS  0x4400  MAIN_LENGTH  0x8000   */
/* F5522 F5521 F5513  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x4000   */
/*                                                                          */  
/* F6137 F6127 F5137  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x4000   */   
/*                                                                          */   
/* F5438A F5438       ->  MAIN_START_ADDRESS  0x5C00  MAIN_LENGTH  0x20000  */
/* F5436A F5436       ->  MAIN_START_ADDRESS  0x5C00  MAIN_LENGTH  0x18000  */ 
/* F5419A F5419       ->  MAIN_START_ADDRESS  0x5C00  MAIN_LENGTH  0x10000  */ 
/*                                                                          */
/* F5510              ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x4000   */ 
/*                                                                          */
/* F5132              ->  MAIN_START_ADDRESS  0xE000  MAIN_LENGTH  0x1000   */                
/* F5152              ->  MAIN_START_ADDRESS  0xC000  MAIN_LENGTH  0x2000   */ 
/* F5172              ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x4000   */ 
/*                                                                          */
/* F5636 F5633 F5630  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x10000  */                
/* F5637 F5634 F5631  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x18000  */ 
/* F5638 F5635 F5632  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x20000  */   
/*                                                                          */
/* F6636 F6633 F6630  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x10000  */                
/* F6637 F6634 F6631  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x18000  */ 
/* F6638 F6635 F6632  ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x20000  */
/*                                                                          */
/* F67x9(1) F67x8(1)  ->  MAIN_START_ADDRESS  0xC000  MAIN_LENGTH  0x40000  */ 
/* F67x7(1) F67x6(1)  ->  MAIN_START_ADDRESS  0xC000  MAIN_LENGTH  0x20000  */
/* F67x5(1)           ->  MAIN_START_ADDRESS  0xC000  MAIN_LENGTH  0x10000  */
/*                                                                          */
/* CC430F6147         ->  MAIN_START_ADDRESS  0x8000  MAIN_LENGTH  0x4000   */
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
//! \brief For devices with JTAG bug 17 - see device errata sheet (slaz046) 
#define DEVICE_HAS_JTAG17  1  
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
