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
| JTAGfunc430FR.h                                                            |
|                                                                            |
| JTAG Function Prototypes and Definitions                                   |
|----------------------------------------------------------------------------|
| Project:              MSP430 FRAM Replicator                               |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/14 RL        Initial version.                                       |
|----------------------------------------------------------------------------|
| Designed 2014 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file JTAGfunc430FR.h
//! \brief JTAG Function Prototypes and Definitions
/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "Config430FR.h"        // High-level user input

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
//! \brief Constant for an erase of the FRAM Main memory on FR5xx/FR6xx
#define MAIN_ERASE  0x1A1A
//! \brief Constant for an erase of the FRAM Main, INFO  & IP 
//! protected memory on FR5xx/FR6xx
#define TOTAL_ERASE  0x1B1B
//! \brief Constant for an erase of the FRAM Main & INFO memory including JTAG 
//! lock signature on FR4xx
#define USER_CODE_ERASE  0x1A1A
#define STOP_DEVICE 0xA55A

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
//! \brief JTAG identification value for 430Xv2 architecture FR4XX/FR2xx devices
#define JTAG_ID98                  0x98
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
//! \brief JTAG mailbox mode 32 bit - 
#define MAIL_BOX_32BIT 0x10
//! \brief JTAG mailbox moede 16 bit - 
#define MAIL_BOX_16BIT 0x00

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
#define VPP_ON_TEST                 1

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/

//! \brief Holds the device's JTAGID
static word JtagId = 0;
//! \brief Holds the device's CoreID
static word CoreId = 0;
//! \brief Holds the memory adress used to read the device ID
static unsigned long DeviceIdPointer = 0;
//! \brief Holds the device ID
static word DeviceId = 0;

//! \brief Holds the Flash InfoA Lock/Unlock Key, default = locked
static unsigned short SegmentInfoAKey5xx = 0xA548;

//! Structure to hold text, data sections and meta data to download
//! the program to th target
typedef struct struct_program 
{
    //! Array executable code and data sections
    const unsigned short* textData;
    //! Array of start addresses of the sections
    const unsigned long* startAddress;
    //! Array of length information of the sections
    const unsigned long* sectionLength;
    //! Number of sections
    unsigned long noSections;
    //! Execution start address
    unsigned long start;
} struct_Program;

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
word VerifyPSA_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray);
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
void ReleaseDevice_430Xv2(unsigned long Addr);
void WriteMem_430Xv2(word Format, unsigned long Addr, word Data);
void WriteMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray);
word ReadMem_430Xv2(word Format, unsigned long Addr);
void ReadMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray);
word EraseCheck_430Xv2(unsigned long StartAddr, unsigned long Length);
word VerifyMem_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray);
word UnlockDevice_430Xv2(unsigned short* password, unsigned long passwordLength);
void EraseFRAM_430Xv2(unsigned long StartAddr, unsigned long Length);
word DisableMpu_430Xv2(void);
word DisableFramWprod_430Xv2(void);
word ProgramLockKey(void);
word IsLockKeyProgrammed(void);
void UnlockBsl_430Xv2FRAM(void);

// File download functions 
short DownloadProgram(struct_Program* program);
short DownloadMsp430Code();
word EraseFRAMViaBootCode_430Xv2(word mailBoxMode, word data1, word data2);

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
