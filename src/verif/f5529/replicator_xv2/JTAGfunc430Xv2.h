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
| JTAGfunc430Xv2.h                                                           |
|                                                                            |
| JTAG Function Prototypes and Definitions                                   |
|----------------------------------------------------------------------------|
| Project:              JTAG Functions                                       |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/02 FRGR        Initial version.                                     |
| 1.1 06/02 ALB2        Formatting changes, added comments.                  |
| 1.2 08/02 ALB2        Initial code release with Lit# SLAA149.              |
| 1.3 01/06 STO         Minor cosmetic changes                               |
| 1.4 04/07 WLUT        WriteFLASHallSections changed due to function spec   |
| 1.5 03/08 WLUT        Added instruction for JTAG_ID91 architectures        |
|                       Constants for JTAG mailbox data exchange             |
| 1.6 08/11 FB          Added JSBW function prototypes                       |
|                       Added support for JTAG17 bugfix                      |
|                       Added support for Fram functions (Write/Erase/Memory |
|                       protection (MPU)/JTAG password)                      |
| 1.7 03/13 RL/MD       Added unlock functions for Info A & BSL              |
|----------------------------------------------------------------------------|
| Designed 2002 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file JTAGfunc430Xv2.h
//! \brief JTAG Function Prototypes and Definitions
/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "Config430Xv2.h"        // High-level user input

/****************************************************************************/
/* Global types                                                             */
/****************************************************************************/

#ifndef __BYTEWORD__
#define __BYTEWORD__
typedef unsigned short word;
typedef unsigned char byte;
#endif

/****************************************************************************/
/* Define section for constants                                             */
/****************************************************************************/

// Constants for flash erasing modes
//! \brief Constant for an erase of the entire flash main memory
#define ERASE_MAIN          0xA506
//! \brief Constants for an erase of a single flash memory bank
#define ERASE_BANK          0xA504
//! \brief Constant for an erase of the selected flash memory segment
#define ERASE_SEGMENT       0xA502

//----------------------------------------------------------------------------
// Constants for the JTAG instruction register (IR) require LSB first.
// The MSB has been interchanged with LSB due to use of the same shifting
// function as used for the JTAG data register (DR) which requires MSB 
// first.
//----------------------------------------------------------------------------

// Instructions for the JTAG control signal register
//! \brief Set the JTAG control signal register
#define IR_CNTRL_SIG_16BIT         0xC8   // original value: 0x13
//! \brief Read out the JTAG control signal register
#define IR_CNTRL_SIG_CAPTURE       0x28   // original value: 0x14
//! \brief Release the CPU from JTAG control
#define IR_CNTRL_SIG_RELEASE       0xA8   // original value: 0x15

// Instructions for the JTAG fuse
//! \brief Prepare for JTAG fuse blow
#define IR_PREPARE_BLOW            0x44   // original value: 0x22
//! \brief Perform JTAG fuse blow
#define IR_EX_BLOW                 0x24   // original value: 0x24

// Instructions for the JTAG data register
//! \brief Set the MSP430 MDB to a specific 16-bit value with the next 
//! 16-bit data access 
#define IR_DATA_16BIT              0x82   // original value: 0x41
//! \brief Set the MSP430 MDB to a specific 16-bit value (RAM only)
#define IR_DATA_QUICK              0xC2   // original value: 0x43

// Instructions for the JTAG PSA mode
//! \brief Switch JTAG data register to PSA mode
#define IR_DATA_PSA                0x22   // original value: 0x44
//! \brief Shift out the PSA pattern generated by IR_DATA_PSA
#define IR_SHIFT_OUT_PSA           0x62   // original value: 0x46

// Instructions for the JTAG address register
//! \brief Set the MSP430 MAB to a specific 16-bit value
//! \details Use the 20-bit macro for 430X and 430Xv2 architectures
#define IR_ADDR_16BIT              0xC1   // original value: 0x83
//! \brief Read out the MAB data on the next 16/20-bit data access
#define IR_ADDR_CAPTURE            0x21   // original value: 0x84
//! \brief Set the MSP430 MDB with a specific 16-bit value and write
//! it to the memory address which is currently on the MAB
#define IR_DATA_TO_ADDR            0xA1   // original value: 0x85
//! \brief Bypass instruction - TDI input is shifted to TDO as an output
#define IR_BYPASS                  0xFF   // original value: 0xFF
#define IR_DATA_CAPTURE            0x42

// JTAG identification values for all existing Flash-based MSP430 devices
//! \brief JTAG identification value for 430X architecture devices
#define JTAG_ID                    0x89
//! \brief JTAG identification value for 430Xv2 architecture devices
#define JTAG_ID91                  0x91
//! \brief JTAG identification value for 430Xv2 architecture FR59XX devices
#define JTAG_ID99                  0x99
// Additional instructions for JTAG_ID91 architectures
//! \brief Instruction to determine device's CoreIP
#define IR_COREIP_ID               0xE8   // original value: 0x17
//! \brief Instruction to determine device's DeviceID
#define IR_DEVICE_ID               0xE1   // original value: 0x87

// Instructions for the JTAG mailbox
//! \brief Request a JTAG mailbox exchange
#define IR_JMB_EXCHANGE            0x86   // original value: 0x61
//! \brief Instruction for test register in 5xx
#define IR_TEST_REG                0x54   // original value: 0x2A
//! \brief Instruction for 3 volt test register in 5xx
#define IR_TEST_3V_REG             0xF4   // original value: 0x2F

// Constants for JTAG mailbox data exchange
//! \brief JTAG mailbox constant - 
#define OUT1RDY 0x0008
//! \brief JTAG mailbox constant - 
#define IN0RDY  0x0001
//! \brief JTAG mailbox constant - 
#define JMB32B  0x0010
//! \brief JTAG mailbox constant - 
#define OUTREQ  0x0004
//! \brief JTAG mailbox constant - 
#define INREQ   0x0001

// Constants for data formats
#ifndef __DATAFORMATS__
#define __DATAFORMATS__
#define F_BYTE                     8
#define F_WORD                     16
#define F_ADDR                     20
#define F_LONG                     32
#endif

// dedicated addresses
//! \brief Triggers a regular reset on device release from JTAG control
#define V_RESET                    0xFFFE
//! \brief Triggers a "brown-out" reset on device release from JTAG control
#define V_BOR                      0x1B08

// Constants for VPP connection at Blow-Fuse
//! \brief Fuse blow voltage is supplied via the TDI pin
#define VPP_ON_TDI                 0
//! \brief Fuse blow voltage is supplied via the TEST pin
#define VPP_ON_TEST                1

#define STOP_DEVICE 0xA55A

/****************************************************************************/
/* FUNCTION PROTOTYPES                                                      */
/****************************************************************************/

// Low level JTAG functions
static word DR_Shift16(word Data);
static unsigned long DR_Shift20(unsigned long address);
static word IR_Shift(byte Instruction);
static void ResetTAP(void);
static word ExecutePOR_430Xv2(void);
static void SetPC_430Xv2(unsigned long Addr);
word VerifyPSA_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
word GetCoreipIdXv2();
void jResetJtagTap(void);
void StartJtagJSbw(byte states);
void jRelease(void);
long jsbw_Shift(word Format, long Data);
long jsbw_IR_Shift(byte instruction);
long jsbw_DR_Shift(long data);
void JsbwMagicPattern(void);
void jsbwJtagUnlock(void);

// High level JTAG functions
word GetDevice_430Xv2(void);
#define GetDevice GetDevice_430Xv2
#ifdef MCUSIM_FIRMA
word magicPatternMcuSim(void);   /* mcu-sim */
#endif
void ReleaseDevice_430Xv2(unsigned long Addr);
#define ReleaseDevice ReleaseDevice_430Xv2
void WriteMem_430Xv2(word Format, unsigned long Addr, word Data);
#define WriteMem WriteMem_430Xv2
void WriteMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
#define WriteMemQuick WriteMemQuick_430Xv2
void WriteFLASH_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
#define WriteFLASH WriteFLASH_430Xv2
word WriteFLASHallSections_430Xv2(const unsigned int *data, const unsigned long *address, const unsigned long *length_of_sections, const unsigned long sections);
#define WriteFLASHallSections WriteFLASHallSections_430Xv2
word ReadMem_430Xv2(word Format, unsigned long Addr);
#define ReadMem ReadMem_430Xv2
void ReadMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
#define ReadMemQuick ReadMemQuick_430Xv2
void EraseFLASH_430Xv2(word EraseMode, unsigned long EraseAddr);
#define EraseFLASH EraseFLASH_430Xv2
word EraseCheck_430Xv2(unsigned long StartAddr, unsigned long Length);
#define EraseCheck EraseCheck_430Xv2
word VerifyMem_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
#define VerifyMem VerifyMem_430Xv2
word ProgramLockKey(void);
word IsLockKeyProgrammed(void);
void UnlockInfoA_430Xv2(void);
void UnlockBsl_430Xv2Flash(void);

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/
//! \brief Holds the device's CoreID
static word CoreId = 0;
//! \brief Holds the memory adress used to read the device ID
static unsigned long DeviceIdPointer = 0;
//! \brief Holds the device ID
static word DeviceId = 0;

//! \brief Holds the Flash InfoA Lock/Unlock Key, default = locked
static unsigned short SegmentInfoAKey5xx = 0xA548;

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
