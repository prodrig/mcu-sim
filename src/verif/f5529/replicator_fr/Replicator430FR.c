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
| Replicator430FR.c                                                          |
|                                                                            |
| JTAG Replicator for FRAM-based MSP430 devices                              |
|                                                                            |
| Key features:                                                              |
| · Supports JTAG communication to all FRxx family devices                   |
| · Max. code size of target program: 57kB                                   |
| · Programming speed: ~60kB/10sec (~6kB/sec)                                |
| · Fast Verification and Erase Check: ~60kB/350ms                           |
| · Supports Device Access with user set JTAG password                       |
| · Supports deactivation of the Memory Protection Unit                      |
| · Supports Programming of JTAG Access Protection Fuse                      |
|                                                                            |
|----------------------------------------------------------------------------|
| Project:              MSP430 FRAM Replicator                               |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/14 RL          Initial version.                                     |
| 1.1 06/15 RL          Commented out optional RAM operations                |
|----------------------------------------------------------------------------|
| Designed 2014 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file Replicator430FR.c
//! \brief JTAG Replicator for FRAM-based MSP430 devices.
/****************************************************************************/
/* Main section of Replicator program: User can modify/insert code as needed*/
/****************************************************************************/
/* Main function:
   Upon execution completion, LED blinks for device target socket
   boards when used as the target system (P1.0 drives LED).

   Note: All High Level JTAG Functions are applied here.

   A number of lines have been commented out which are not strictly required
   to successfully program a flash or fram-based MSP430 device using the 
   Replicator concept. Please note that these lines have been commented out in 
   order to provide reference examples for calling such functions.
   Uncommented lines below represent the minimum requirements to successfully
   program an MSP430 device using the Replicator.
   
   The basic routine consists of the following steps:
   
   1.  | Initialize the host MSP430 on the Replicator board
   2.  | Connect to the target device (provide JTAG password if needed)
   3.  | Control the target peripherals directly (optional)
   4.  | Perform a write + read + verify in the target RAM (optional)
   5.  | Operations in the device's main memory
   6.  | Program the JTAG lock key (optional)
   7.  | Release the device from JTAG control and wait for the user to press button "S1"
*/

/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "JTAGfunc430FR.h"       // JTAG functions
#include "Config430FR.h"         // High level user configuration
#include "LowLevelFunc430Xv2.h"  // Low level functions

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
/*  2. | Connect to the target device (provide JTAG password if needed)                                 */
/*------------------------------------------------------------------------------------------------------*/

    // Uncomment the following section if the JTAG password is set and should be removed - FR5xx/FR6xx
#ifdef MCUSIM_CLAVE   /* mcu-sim: el mismo bloque de TI, activado para la prueba de la contraseña (testfr5 L6) */
    {
        unsigned short Password[] = { 0x1111, 0x2222 };
        unsigned long  PasswordLength = 0x02; // password length in words
        if(UnlockDevice_430Xv2(Password, PasswordLength) != STATUS_OK)
        {
            ShowStatus(STATUS_ERROR, 1);  // stop here password was wrong
        }
    }
#endif
    /*{   
        // enter password & length here:
        unsigned short Password[] = { 0x1111, 0x2222 };
        unsigned long  PasswordLength = 0x02; // password length in words  
        
        //Unlock device with user password
        if(UnlockDevice_430Xv2(Password, PasswordLength) != STATUS_OK)   
        {
            ShowStatus(STATUS_ERROR, 1);  // stop here password was wrong
        } 
    } */
    
    // Uncomment the following section if the JTAG password is set and should be removed - FR4xx/FR2xx    
    /*if(GetDevice_430Xv2() == STATUS_FUSEBLOWN)
    {
        if (!EraseFRAMViaBootCode_430Xv2(MAIL_BOX_32BIT, STOP_DEVICE, USER_CODE_ERASE))   
        { 
            ShowStatus(STATUS_ERROR, 2);
        }      
        // Check if main memory is completely erased.
        if (!EraseCheck_430Xv2(mainStartAdress, mainLength/2))   
        { 
            ShowStatus(STATUS_ERROR, 2);
        }
    }*/
    
   
    if (GetDevice_430Xv2() != STATUS_OK)         // Set DeviceId
    {
        ShowStatus(STATUS_ERROR, 1);      // Stop here if invalid JTAG ID or
    }  
    // time-out. (error: red LED is ON)
    

    
/*------------------------------------------------------------------------------------------------------*/
/*  3. | Perform a erase + write + read + verify in the target RAM (optional)                           */
/*------------------------------------------------------------------------------------------------------*/
    
    // The following section is not required and included only as a reference on
    // how to access the target's RAM and alter its content
    // DisableMpu_430Xv2() must be called prior to this, if the device's MPU is enabled 
    
    /*
    word i,j;
    // write dummy data to target RAM    
    for(i = 0, j = 0; j < WordBufferSize; j++, i+=2)
    {
        ReadData[j]  = 0;
        WriteData[j] = j;
        WriteMem_430Xv2(F_WORD, MAIN_START_ADDRESS + i, j);
    }      
    // read data from target RAM and verify    
    for(i = 0, j = 0; j < WordBufferSize; j++, i+=2)
    {
        ReadData[j] = ReadMem_430Xv2(F_WORD, MAIN_START_ADDRESS + i);
        MsDelay(1);
        if(ReadData[j] != WriteData[j])
        {
            ShowStatus(STATUS_ERROR, 2);
        }
    }
    // verify content with PSA
    if(VerifyMem_430Xv2(MAIN_START_ADDRESS, WordBufferSize, WriteData) != STATUS_OK)
    {
        ShowStatus(STATUS_ERROR, 3);
    }
    */
    
/*------------------------------------------------------------------------------------------------------*/
/*  4. | Operations in the device's main memory (disable MPU if necessary)                              */
/*------------------------------------------------------------------------------------------------------*/       

    // The Memory Protection Unit (MPU) allows the user to set up individual access rights for up to 
    // three user defined memory segments. For detailed information see the Memory Protection Unit
    // description in the device family user's guide.
    
    // Disable Memory Protection Unit
    if (DisableMpu_430Xv2() != STATUS_OK)
    {
        ShowStatus(STATUS_ERROR, 1);
    }
    
    // Not possible on FR5739 - use EraseFRAM_430Xv2 instead
    // Erase main FRAM memory using the JTAG mailbox and Bootcode
    if (!EraseFRAMViaBootCode_430Xv2(MAIL_BOX_32BIT, STOP_DEVICE, USER_CODE_ERASE))
    { 
        ShowStatus(STATUS_ERROR, 2);
    }      
    // Check if main memory is completely erased.
    if (!EraseCheck_430Xv2(mainStartAdress, mainLength/2))   
    { 
        ShowStatus(STATUS_ERROR, 2);
    }
    // Disable FRAM write protection - only FR5994 and FR6047 device family. Disabled by default
    /*if (DisableFramWprod_430Xv2() != STATUS_OK)
    {
        ShowStatus(STATUS_ERROR, 2);
    }*/
    // Since EraseCheck executes a BOR - The MPU gets enabled again - Disable it before code download
    if (DisableMpu_430Xv2() != STATUS_OK)
    {
        ShowStatus(STATUS_ERROR, 2);
    }         
    //Program blinking LED target code    
    if(DownloadMsp430Code() != STATUS_OK) 
    {
        ShowStatus(STATUS_ERROR, 2);
    }    
    
/*------------------------------------------------------------------------------------------------------*/
/*  5. | Program the JTAG lock key (optional)                                                           */
/*------------------------------------------------------------------------------------------------------*/
    
    // Remove following comments to enable Lock Key programming routine.
    // This makes the MSP430 device permanently inaccessible via JTAG
    
    /*if (DisableMpu_430Xv2() != STATUS_OK)
    {
        ShowStatus(STATUS_ERROR, 2);
    }    
    if (!ProgramLockKey())        // ***Action is permanent***
    {
        ShowStatus(STATUS_ERROR, 15);
    }*/

/*------------------------------------------------------------------------------------------------------*/
/*  6. | Release the target device from JTAG control and wait for the user to press button "S1"         */
/*------------------------------------------------------------------------------------------------------*/

    ReleaseDevice_430Xv2(V_RESET);  // Perform Reset, release CPU from JTAG control
                                    // Target board LED should start blinking
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
