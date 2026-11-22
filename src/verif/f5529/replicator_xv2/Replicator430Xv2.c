/*
 * 
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
| Replicator430Xv2.c                                                         |
|                                                                            |
| JTAG Replicator for the MSP430Xv2 Flash-based family (5xx/6xx)             |
| devices.                                                                   |
|                                                                            |
| Key features:                                                              |
| � Supports JTAG communication to all 5xx/6xx MSP430 Flash devices          |
| � Max. code size of target program: 57kB                                   |
| � Programming speed: ~60kB/10sec (~6kB/sec)                                |
| � Fast Verification and Erase Check: ~60kB/350ms                           |
| � Supports Device Access with user set JTAG password (FRAM only)           |
| � Supports deactivation of the Memory Protection Unit (FRAM only)          |
| � Supports Programming of JTAG Access Protection Fuse                      |
|                                                                            |
|----------------------------------------------------------------------------|
| Project:              MSP430 Replicator                                    |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Essentials 6.0                         |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 08/08 WLUT        Initial version.                                     |
| 1.1 06/09 FB          add Loop for 'Go'Button P1.6                         |
| 1.2 06/09 FB          updated Constants for flash erasing modes &          |
|                       added section with "erase the all Flash memory banks |
|                       in main memory and verify them.                      |
| 1.3 10/10 FB          update code examples for F6638 family, F5509 family  |
|                       and F5132 Family                                     |
| 1.4 04/12 RL          updated commentaries                                 |
| 1.5 11/12 RL          Fixed erase main memory flow                         |
| 1.6 03/13 RL/MD       Added InfoA/BSL unlock                               |
| 1.7 05/14 RL          Removed FRAM functionality (see Replicator430FR)     |
| 1.8 06/15 RL          Commented out optional RAM operations                |
|----------------------------------------------------------------------------|
| Designed 2008 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file Replicator430Xv2.c
//! \brief JTAG Replicator for the MSP430Xv2 Flash-based family (5xx/6xx) devices.
/****************************************************************************/
/* Main section of Replicator program: User can modify/insert code as needed*/
/****************************************************************************/
/* Main function:
   Upon execution completion, LED blinks for F5xx/F6xx device target socket
   boards when used as the target system (P1.0 drives LED).

   Note: All High Level JTAG Functions are applied here.

   A number of lines have been commented out which are not strictly required
   to successfully program a flash-based MSP430 device using the 
   Replicator concept. Please note that these lines have been commented out in 
   order to provide reference examples for calling such functions.
   Uncommented lines below represent the minimum requirements to successfully
   program an MSP430 device using the Replicator.
   
   The basic routine consists of the following steps:
   
   1.  | Initialize the host MSP430 on the Replicator board
   2.  | Connect to the target device
   3.  | Control the target peripherals directly (optional)
   4.  | Perform a write + read + verify in the target RAM (optional)
   5.  | Operations in the device's main memory
   6.  | Program the JTAG lock key (optional)
   7.  | Release the device from JTAG control and wait for the user to press button "S1"
*/

/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "JTAGfunc430Xv2.h"      // JTAG functions
#include "Config430Xv2.h"        // High level user configuration
#include "LowLevelFunc430Xv2.h"  // Low level functions
#include "Target_Code.h"         // holds declarations and/or data of target program code

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/

//! \brief This variable holds the start address of the main memory       
unsigned long mainStartAdress = MAIN_START_ADDRESS;
//! \brief This variable holds the length of the main memory (in words)
unsigned long mainLength = MAIN_LENGTH;

/****************************************************************************/
/* FUNCTIONS                                                                */
/****************************************************************************/

//! Main function
void main(void)
{
    runProgramm();
}

//! \brief The basic Replicator routine
//! \details This function is executed once at startup and can be restarted by pressing button S1 on the REP430F board.
void runProgramm(void)
{

//! \brief Array to store data for a memory write
word WriteData[WordBufferSize];
//! \brief Array to store data for a memory read
word ReadData[WordBufferSize];

/*------------------------------------------------------------------------------------------------------*/
/*  1. | Initialize host MSP430 (on Replicator board) & target board                                    */
/*------------------------------------------------------------------------------------------------------*/

    InitController();                     // Initialize the host MSP430F5437
    
    ShowStatus(STATUS_ACTIVE, 0);         // Switch both LEDs on to indicate operation.
    
    InitTarget();                         // Initialize target board    
    
/*------------------------------------------------------------------------------------------------------*/
/*  2. | Connect to the target device                                                                   */
/*------------------------------------------------------------------------------------------------------*/
    
#ifdef MCUSIM_FIRMA   /* mcu-sim: antes, el patron magico (F8): para la CPU del objetivo en el arranque */
    if (magicPatternMcuSim() != 0x91)
    {
        ShowStatus(STATUS_ERROR, 20);
    }
#endif
    if (GetDevice() != STATUS_OK)         // Set DeviceId
    {
        ShowStatus(STATUS_ERROR, 1);      // Stop here if invalid JTAG ID or
    }                                     // time-out. (error: red LED is ON)

/*------------------------------------------------------------------------------------------------------*/
/*  3. | Control the target peripherals directly (optional)                                             */
/*------------------------------------------------------------------------------------------------------*/
    
    // Remove the following comments to toggle Pin 1.0 (i.e flash the LED for MSP430 target socket boards)
    /*{ 
        word k;
        
        WriteMem(F_BYTE, 0x204, 0x01);         // P1.0 for F1xx,2xx devices
        
        for(k = 0; k < 3; k++)
        {
            WriteMem(F_BYTE, 0x202, 0x01);
            MsDelay(500);                      // LED on for 0.5s
            WriteMem(F_BYTE, 0x202, 0x00);
            MsDelay(500);                      // LED off for 0.5s
        }
        
        WriteMem(F_BYTE, 0x202, 0x01);
    }*/
    
/*------------------------------------------------------------------------------------------------------*/
/*  4. | Perform a write + read + verify in the target RAM (optional)                                   */
/*------------------------------------------------------------------------------------------------------*/   
    
	// The following section is not required and included only as a reference on
    // how to access the target's RAM and alter its content
	/*
    {
        word i,j;
  
        // write dummy data to target RAM    
        for (i = 0, j = 0; j < WordBufferSize; j++, i+=2)
        {
            ReadData[j]  = 0;
            WriteData[j] = j;
            WriteMem(F_WORD, RAM_START_ADDRESS + i, j);
        }      
        // read data from target RAM and verify    
        for (i = 0, j = 0; j < WordBufferSize; j++, i+=2)
        {
            ReadData[j] = ReadMem(F_WORD, RAM_START_ADDRESS + i);
            if(ReadData[j] != WriteData[j])
            {
                ShowStatus(STATUS_ERROR, 2);
            }
        }
        // verify content with PSA
        if(VerifyMem(RAM_START_ADDRESS, WordBufferSize, WriteData) != STATUS_OK)
        {
            ShowStatus(STATUS_ERROR, 3);
        }
    }
    */

/*------------------------------------------------------------------------------------------------------*/
/*  5. | Operations in the device's main memory                                                         */
/*------------------------------------------------------------------------------------------------------*/
    
    // Check Flash memory access
    {
        // Program Flash in main memory
        EraseFLASH(ERASE_SEGMENT, mainStartAdress);
        WriteFLASH(mainStartAdress, WordBufferSize, WriteData);
        if(VerifyMem(mainStartAdress, WordBufferSize, WriteData) != STATUS_OK)
        {
            ShowStatus(STATUS_ERROR, 4);
        }
    }

    // This will erase all Flash memory banks in main memory.
    // Additionally an erase check is performed
    {
        // Erase all Flash memory banks in main memory.
        EraseFLASH(ERASE_MAIN, mainStartAdress);  
        // Check, if main memory is erased completely
        if (!EraseCheck(mainStartAdress, mainLength))   
        {
            ShowStatus(STATUS_ERROR, 2);
        }       
    }

    // This will only erase one flash memory bank (64KB) in main memory.
    // I.e. the second memory bank of an MSP430F5438 can be erased by calling:
    {   
        //EraseFLASH(ERASE_BANK, 0x10100);
    }  

    // The following section shows how to erase Info-Segments on 5xx Devices selectively
    {
        // Comment-in the following code to unlock Info memory segment A
      
        /*{
            UnlockInfoA_430Xv2();
      
            // Info A memory erase
            EraseFLASH(ERASE_SEGMENT, 0x1980);     
        
            // Check Info A memory erasure
            if (!EraseCheck(0x1980, 0x0040))       
            {
                ShowStatus(STATUS_ERROR, 2);
            }
        }*/   
        
        // Info B memory erase
        EraseFLASH(ERASE_SEGMENT, 0x1900); 
        
        // Check Info B memory erasure     
        if (!EraseCheck(0x1900, 0x0040))      
        {
            ShowStatus(STATUS_ERROR, 2);
        }
        // Info C erase
        EraseFLASH(ERASE_SEGMENT, 0x1880);
        // Check Info C memory erasure
        if (!EraseCheck(0x1880, 0x0040))      
        {
            ShowStatus(STATUS_ERROR, 2);
        }
        // Info D erase
        EraseFLASH(ERASE_SEGMENT, 0x1800);    
        // Check Info D memory erasure 
        if (!EraseCheck(0x1800, 0x0040))      
        {
            ShowStatus(STATUS_ERROR, 2);
        }    
    }

    // The following code unlocks the BSL memory on 5xx/6xx devices with flash memory 
	// for reading/writing/erasure.
	// WARNING: The code in the BSL memory area should only be erased if another BSL is 
	//          programmed immediately afterwards. Otherwise, the device might not start 
	//          up properly anymore.  
    /*{
        UnlockBsl_430Xv2Flash();
    }*/
       
    // Program target code    
    if (!WriteFLASHallSections(&eprom[0], &eprom_address[0], &eprom_length_of_sections[0], eprom_sections))
    {
        ShowStatus(STATUS_ERROR, 10);
    }

/*------------------------------------------------------------------------------------------------------*/
/*  6. | Program the JTAG lock key (optional)                                                           */
/*------------------------------------------------------------------------------------------------------*/
    
    // Remove following comments to enable Lock Key programming routine.
    // This makes the MSP430 device permanently inaccessible via JTAG
    
    /*if (!ProgramLockKey())        // ***Action is permanent***
    {
        ShowStatus(STATUS_ERROR, 15);
    }*/
#ifdef MCUSIM_FIRMA   /* mcu-sim: el mismo bloque, activado para la prueba de la firma (F8) */
    if (!ProgramLockKey())
    {
        ShowStatus(STATUS_ERROR, 15);
    }
#endif

/*------------------------------------------------------------------------------------------------------*/
/*  7. | Release the target device from JTAG control and wait for the user to press button "S1"         */
/*------------------------------------------------------------------------------------------------------*/

    ReleaseDevice(V_RESET);         // Perform Reset, release CPU from JTAG control
    //MsDelay(3000);                // Target board LED should start blinking
    ShowStatus(STATUS_OK, 0);       // OK: green LED is ON
    
    _EINT();                        // Enable Interrupts
    P1IE |= 0x040;                  // P1.6 interrupt enabled
    
    while(1);                       // Idle, wait for keypress (handled in ISR)   
}

/*------------------------------------------------------------------------------------------------------*/

//! This interrupt service routine calls runProgramm() if button "S1" is pressed
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void)
{   
    ReleaseTarget();                // Release Target Board from power
    ShowStatus(STATUS_ACTIVE, 0);   // Switch both LEDs on to indicate operation.
    P1IE &= ~0x040;                 // P1.6 interrupt disabled
    MsDelay(1000);                  // Wait 1s before restarting the Controller and Target
    P1IFG = 0;
    runProgramm();                  // Restart Controller and Target
}

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
