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
/*=============================================================================\
|                                                                              |
| JSBW.c                                                                       |
|                                                                              |
| This file contains the necessary high level routines to wake up an MSP430    |
| device from LPM5 in 4-wire mode and putting it in Reset State. Since the     |
| 4-wire pins are locked by the JTAG lock, the JTAG access routine must be     |
| emulated using Spy-Bi-Wire.                                                  |
|------------------------------------------------------------------------------|
| Project:              MSP430 Replicator Xv2                                  |
| Developed using:      IAR Embedded Workbench 6.20                            |
|------------------------------------------------------------------------------|
| Version history:                                                             |
| 1.0 04/11 FB          Initial Version                                        |
|------------------------------------------------------------------------------|
| Designed 2011 by Texas Instruments Germany                                   |
\=============================================================================*/
//! \file JSBW.c
//! \brief 4-wire JTAG emulation using Spy-Bi-Wire for LPM5 wakeup of an MSP430 device
/****************************************************************************/
/* Includes                                                                 */
/****************************************************************************/

#include "JTAGfunc430Xv2.h"         // JTAG functions
#include "LowLevelFunc430Xv2.h"     // low level user functions

// Spy-Bi-Wire addons --------------------------------------------------------
extern byte tdo_bit;
extern byte TCLK_saved;      // holds the last value of TCLK before entering a JTAG sequence

//----------------------------------------------------------------------------
//! \brief Releases the JSBW logic.
void jRelease(void)
{
    // drive target RST/SBWTDIO pin high
    (JTAGOUT &= ~SBWDATO);      // TDI drives target RST high // (JTAGOUT |= SBWDATO); 
    
    MsDelay(1);
    
    // drive target TEST/SBWTCK pin low
    (JTAGOUT &= ~SBWCLK);       // TCK drives target TEST low - release Spy-Bi-Wire logic
}

//----------------------------------------------------------------------------
//! \brief Start JTAG communication in JSBW mode.
//! \param byte states (reset state)
void StartJtagJSbw(byte states)
{
    // reset TEST logic  
    ClrTST();
    MsDelay(20);             // delay 20ms
    
    if(states == RSTLOW_SBW || states == RSTLOW_JTAG)
    {   
        // Set Reset pin 0
        (JTAGOUT) &= (~RST);
    }
    else
    {   
        // Set Reset pin = 1 
        SetRST();
    }  

    SetTST();
    // activate TEST logic
    MsDelay(25); 

    // phase 1
    if(states == RSTLOW_JTAG || states == RSTHIGH_JTAG)
    {
        //Set Reset pin =0 
        ClrRST();
    }
    else
    {
        SetRST();
    }  
    usDelay(40); 
    
    // phase 2 -> TEST pin to 0, no change on RST pin
    if(states == RSTLOW_SBW || states == RSTHIGH_SBW)
    { 
        // for Spy-Bi-Wire
      //  JTAGOUT &= ~SBWCLK;
    }
    else
    { 
        // for 4-wire JTAG
        ClrTST();
    }
    
    // phase 3
    if(states == RSTLOW_JTAG)
    {
        SetRST();
    }
    usDelay(1); 
    
    // phase 4 -> TEST pin to 1, no change on RST pin
    if(states == RSTLOW_SBW || states == RSTHIGH_SBW)
    { 
        // for Spy-Bi-Wire
     //   JTAGOUT |=   SBWCLK;
    }
    else
    { 
        // for 4-wire JTAG
        SetTST();
    }
    usDelay(40); 
    
    // phase 5
    if(states == RSTHIGH_JTAG)
    {
        SetRST();
    }
    MsDelay(5);    
}

//----------------------------------------------------------------------------
//! \brief Reset target JTAG interface and perform fuse-HW check.
void jResetJtagTap(void)
{
    word i;  
    // Check Fuse, Reset JTAG FSM
    for (i = 6; i > 0; i--)      // 6 is nominal
    {
        TMSH_TDIH();
    }
    // JTAG FSM is now in Test-Logic-Reset
    TMSL_TDIH();                 // now in Run/Test Idle
}   

//----------------------------------------------------------------------------
//! \brief Shift a value into TDI (MSB first) and simultaneously shift out a 
//! value from TDO (MSB first).
//! \param word Format (number of bits shifted, 8 (F_BYTE), 16 (F_WORD), 
//! 20 (F_ADDR) or 32 (F_LONG))
//! \param long Data (data to be shifted into TDI)
//! \return unsigned long (scanned TDO value)
long jsbw_Shift(word Format, long Data)
{
    word tclk = StoreTCLK();  // Store TCLK state;
    unsigned long TDOword = 0x00000000;
    unsigned long MSB = 0x00000000;
    word i;

    switch(Format)
    {
    case F_BYTE: MSB = 0x00000080;
      break;
    case F_WORD: MSB = 0x00008000;
      break;
    case F_ADDR: MSB = 0x00080000;
      break;
    case F_LONG: MSB = 0x80000000;
      break;
    default: // this is an unsupported format, function will just return 0
      return TDOword;
    }    
    for (i = Format; i > 0; i--)
    {
      ((Data & MSB) == 0) ? ClrTDI() : SetTDI();
      Data <<= 1;
      if (i == 1)
      {                       // Last bit requires TMS=1
         SetTMS();
      }
      ClrTCK();
      SetTCK();
      TDOword <<= 1;          // TDO could be any port pin
      if (ScanTDO() != 0)
      {
          TDOword++;    
      }
    }
    // common exit
    RestoreTCLK(tclk);                  // restore TCLK state

    // JTAG FSM = Exit-DR
    ClrTCK();
    SetTCK();
    // JTAG FSM = Update-DR
    ClrTMS();
    ClrTCK();
    SetTCK();
    // JTAG FSM = Run-Test/Idle
    return(TDOword);  
}

//----------------------------------------------------------------------------
//! \brief Function for shifting a new instruction into the JTAG instruction
//! register through JSBW (MSB first, but with interchanged MSB - LSB, to
//! simply use the same shifting function Shift()).
//! \param instruction (8 bit JTAG instruction)
//! \return word (TDOword - value shifted out from TDO: JTAG identification)
long jsbw_IR_Shift(byte instruction)
{
    // JTAG FSM state = Select DR-Scan
    TMSH_TDIH();

    // JTAG FSM state = Select IR-Scan
    TMSL_TDIH();
    // JTAG FSM state = Capture-IR
    TMSL_TDIH();
    // JTAG FSM state = Shift-IR, Shift in TDI (8-bit)
    return(jsbw_Shift(F_BYTE, instruction));
    // JTAG FSM state = Run-Test/Idle
}

//----------------------------------------------------------------------------
//! \brief Function for shifting data into the JTAG data register through JSBW 
//! (MSB first, but with interchanged MSB - LSB, to simply use the same shifting 
//! function Shift()).
//! \param data
//! \return word (TDOword - value shifted out from TDO: JTAG identification)
long jsbw_DR_Shift(long data)
{
    // JTAG FSM state = Run-Test/Idle
    if (TCLK_saved & SBWDATO)
    {
        TMSH_TDIH();
    }
    else
    {
        TMSH_TDIL();
    }
    // JTAG FSM state = Select DR-Scan
    TMSL_TDIH();
    // JTAG FSM state = Capture-DR
    TMSL_TDIH();        
    // JTAG FSM state = Shift-DR, Shift in TDI (16-bit)
    return(jsbw_Shift(F_WORD, data));
    // JTAG FSM state = Run-Test/Idle
}

//----------------------------------------------------------------------------
//! \brief Function for applying the magic pattern via JSBW.
void JsbwMagicPattern(void)
{
    jsbw_IR_Shift(IR_JMB_EXCHANGE);
    MsDelay(10);       
    jsbw_DR_Shift(0x0001);
    MsDelay(10);    
    jsbw_DR_Shift(0xA55A);    
    MsDelay(15);        
}

//----------------------------------------------------------------------------
//! \brief Function for resetting the JTAG lock via JSBW.
void jsbwJtagUnlock(void)
{    
    jsbw_IR_Shift(IR_TEST_3V_REG);
    MsDelay(10);  
    jsbw_DR_Shift(0x4020);
    MsDelay(10);  
}

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
