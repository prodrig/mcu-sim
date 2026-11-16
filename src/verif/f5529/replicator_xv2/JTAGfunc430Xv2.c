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
| JTAGfunc430Xv2.c                                                           |
|                                                                            |
| JTAG Control Sequences for Erasing / Programming / Fuse Programming        |
|----------------------------------------------------------------------------|
| Project:              JTAG Functions                                       |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 08/08 WLUT        Initial version.                                     |
| 1.1 10/08 WLUT        Fixed VerifyPSA_430Xv2(). The previous version was   |
|                       driving StartAddr-2 on the devices' address bus.     |
|                       This can cause security violations in the 5xx system.|
|                       The fixed version of the algorithm is now operating  |
|                       only within the specified memory range which is to   |
|                       be verified.                                         |
|                       Removed 'fuse check' in ResetTap() for SPYBIWIREMODE.|
|                       Fuse check is not required for 5xx.                  |
|                       Added correct Spy-Bi-Wire entry sequence to StartJtag|
| 1.2 07/09 FB          Fix JTAG 17 BUG, Add support for new Replecator      |
|                       Fix Spy Bi wire entry sequence                       |
| 1.3 08/09 FB          Changed VerifyPSA_430Xv2 to clock through PSA during |
|                       TCLK low phase                                       |
| 1.3 10/10 FB          changed function StartJtag(), that the device could  |
|                       be held in reset to shift in the "magic pattern"     |
|                       to stop user code excecution                         |
|                       added new function ConnectJTAG() to enstablish the   |
|                       physical JTAG connection                             |
|                       added new function MagicPattern() to stop user code  |
|                       execution and set the target device into LPM4        |
| 1.4 GC (Elprotronic)  Bug in GetCoreID() --> DeviceIdPointer has been fixed|
| 1.5 08/11 FB          Added FRAM support functions:                        |
|                           ProgramLockKey() - Set JTAG Lock Key             |
|                           UnlockDevice() - Open a device with JTAG password|
|                           WriteFram_430Xv2 - Write FRAM memory             |
|                           EraseFRAM_430Xv2 - Erase FRAM memory             |
| 1.6 10/11 FB/RL       Fixed JTAG mailbox ready check                       |
|                       Added function descriptions                          |
|                       Added FRAM write through JTAG mailbox                |
| 1.7 06/12 RL          Updated commentaries                                 |
| 1.8 10/12 RL          Removed hard-coded RAM addresses - RAM start address |
|                       has to be specified in the config header file        |
| 1.9 03/13 RL/MD       Added unlock functions for Info A & BSL              |
| 1.10 2/14 RL          Added FRAM robustness flow in ExecutePOR_430Xv2 and  |
|                       WriteMem_430Xv2 for MSP430FR59xx devices             |
| 1.11 5/14 RL          Removed FRAM functionality (see Replicator430FR)     |
|----------------------------------------------------------------------------|
| Designed 2008 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file JTAGfunc430Xv2.c
//! \brief JTAG Control Sequences for Erasing / Programming / Fuse Programming
/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "LowLevelFunc430Xv2.h"
#include "JTAGfunc430Xv2.h"
#include "FlashErase.c"
#include "FlashWrite.c"

// Spy-Bi-Wire addons --------------------------------------------------------
extern byte tdo_bit;
extern byte TCLK_saved;      // holds the last value of TCLK before entering a JTAG sequence

/****************************************************************************/
/* Low level routines for accessing the target device via JTAG:             */
/****************************************************************************/

//----------------------------------------------------------------------------
//! \brief Function for shifting a given 16-bit word into the JTAG data
//! register through TDI.
//! \param[in] word data (16-bit data, MSB first)
//! \return word (value is shifted out via TDO simultaneously)
static word DR_Shift16(word data)
{
#ifdef SPYBIWIRE_MODE
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
#else
    // JTAG FSM state = Run-Test/Idle
    SetTMS();
    ClrTCK();
    SetTCK();

    // JTAG FSM state = Select DR-Scan
    ClrTMS();
    ClrTCK();
    SetTCK();
    // JTAG FSM state = Capture-DR
    ClrTCK();
    SetTCK();
#endif
    // JTAG FSM state = Shift-DR, Shift in TDI (16-bit)
    return(AllShifts(F_WORD, data));
    // JTAG FSM state = Run-Test/Idle
}

//----------------------------------------------------------------------------
//! \brief Function for shifting a given 20-bit address word into the
//! JTAG address register through TDI.
//! \param[in] unsigned long address (20-bit address word, MSB first)
//! \return unsigned long TDOvalue (is shifted out via TDO simultaneously)
static unsigned long DR_Shift20(unsigned long address)
{
#ifdef SPYBIWIRE_MODE
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
#else
    // JTAG FSM state = Run-Test/Idle
    SetTMS();
    ClrTCK();
    SetTCK();

    // JTAG FSM state = Select DR-Scan
    ClrTMS();
    ClrTCK();
    SetTCK();
    // JTAG FSM state = Capture-DR
    ClrTCK();
    SetTCK();
#endif
    // JTAG FSM state = Shift-DR, Shift in TDI (16-bit)
    return(AllShifts(F_ADDR, address));
    // JTAG FSM state = Run-Test/Idle
}

//----------------------------------------------------------------------------
//! \brief Function for shifting a new instruction into the JTAG instruction
//! register through TDI (MSB first, but with interchanged MSB - LSB, to
//! simply use the same shifting function, Shift(), as used in DR_Shift16).
//! \param[in] byte Instruction (8bit JTAG instruction, MSB first)
//! \return word TDOword (value shifted out from TDO = JTAG ID)
static word IR_Shift(byte instruction)
{
#ifdef SPYBIWIRE_MODE
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
    TMSH_TDIH();

    // JTAG FSM state = Select IR-Scan
    TMSL_TDIH();
    // JTAG FSM state = Capture-IR
    TMSL_TDIH();
#else
    // JTAG FSM state = Run-Test/Idle
    SetTMS();
    ClrTCK();
    SetTCK();
    // JTAG FSM state = Select DR-Scan
    ClrTCK();
    SetTCK();

    // JTAG FSM state = Select IR-Scan
    ClrTMS();
    ClrTCK();
    SetTCK();
    // JTAG FSM state = Capture-IR
    ClrTCK();
    SetTCK();
#endif
    // JTAG FSM state = Shift-IR, Shift in TDI (8-bit)
    return(AllShifts(F_BYTE, instruction));
    // JTAG FSM state = Run-Test/Idle
}

//----------------------------------------------------------------------------
//! \brief Reset target JTAG interface and perform fuse-HW check.
static void ResetTAP(void)
{
    word i;

#ifdef SPYBIWIRE_MODE
    // Reset JTAG FSM
    for (i = 6; i > 0; i--)
    {
        TMSH_TDIH();
    }
    // JTAG FSM is now in Test-Logic-Reset
    TMSL_TDIH();                 // now in Run/Test Idle
#else
    // process TDI first to settle fuse current
    SetTDI();
    SetTMS();
    SetTCK();

    // Reset JTAG FSM
    for (i = 6; i > 0; i--)
    {
        ClrTCK();
        SetTCK();
    }
    // JTAG FSM is now in Test-Logic-Reset
    ClrTCK();
    ClrTMS();
    SetTCK();
    ClrTCK();    
    SetTCK();
    ClrTCK();    
    // JTAG FSM is now in Run-Test/IDLE
#endif
}

//----------------------------------------------------------------------------
//! \brief Function to execute a Power-On Reset (POR) using JTAG CNTRL SIG 
//! register
//! \return word (STATUS_OK if target is in Full-Emulation-State afterwards,
//! STATUS_ERROR otherwise)
static word ExecutePOR_430Xv2(void)
{
    word id = 0;

    id = IR_Shift(IR_CNTRL_SIG_CAPTURE);
    
    // provide one clock cycle to empty the pipe
    ClrTCLK();
    SetTCLK();

    // prepare access to the JTAG CNTRL SIG register  
    IR_Shift(IR_CNTRL_SIG_16BIT);
    // release CPUSUSP signal and apply POR signal
    DR_Shift16(0x0C01);
    // release POR signal again
    DR_Shift16(0x0401);
  
    ClrTCLK();
    SetTCLK();
    ClrTCLK();
    SetTCLK();
    ClrTCLK();
    SetTCLK();

    // two more to release CPU internal POR delay signals
    ClrTCLK();
    SetTCLK();
    ClrTCLK();
    SetTCLK();
    
    // now set CPUSUSP signal again
    IR_Shift(IR_CNTRL_SIG_16BIT);
    DR_Shift16(0x0501);
    // and provide one more clock
    ClrTCLK();
    SetTCLK();
    // the CPU is now in 'Full-Emulation-State'
  
    // disable Watchdog Timer on target device now by setting the HOLD signal
    // in the WDT_CNTRL register
    WriteMem_430Xv2(F_WORD, 0x015C, 0x5A80);

    // Check if device is in Full-Emulation-State again and return status
    IR_Shift(IR_CNTRL_SIG_CAPTURE);
    if(DR_Shift16(0) & 0x0301)
    {
      return(STATUS_OK);
    }
  
    return(STATUS_ERROR);
}

//----------------------------------------------------------------------------
//! \brief Load a given address into the target CPU's program counter (PC).
//! \param[in] unsigned long Addr (destination address)
static void SetPC_430Xv2(unsigned long Addr)
{
    unsigned short Mova;
    unsigned short Pc_l;
  
    Mova  = 0x0080;
    Mova += (unsigned short)((Addr>>8) & 0x00000F00);
    Pc_l  = (unsigned short)((Addr & 0xFFFF));
  
    // Check Full-Emulation-State at the beginning
    IR_Shift(IR_CNTRL_SIG_CAPTURE);
    if(DR_Shift16(0) & 0x0301)
    {
        // MOVA #imm20, PC
        ClrTCLK();
        // take over bus control during clock LOW phase
        IR_Shift(IR_DATA_16BIT);
        SetTCLK();
        DR_Shift16(Mova);
        ClrTCLK();
        // above is just for delay
        IR_Shift(IR_CNTRL_SIG_16BIT);
        DR_Shift16(0x1400);
        IR_Shift(IR_DATA_16BIT);
        ClrTCLK();
        SetTCLK();
        DR_Shift16(Pc_l);
        ClrTCLK();
        SetTCLK();
        DR_Shift16(0x4303);    
        ClrTCLK();
        IR_Shift(IR_ADDR_CAPTURE);    
        DR_Shift20(0x00000);
    }
}

//----------------------------------------------------------------------------
//! \brief Read a 32bit value from the JTAG mailbox.
//! \return unsigned long (32bit value from JTAG mailbox)
static unsigned long i_ReadJmbOut(void)
{
    unsigned short sJMBINCTL;
    unsigned long  lJMBOUT = 0;
    unsigned short sJMBOUT0, sJMBOUT1;
  
    sJMBINCTL = 0;
  
    IR_Shift(IR_JMB_EXCHANGE);// start exchange
    lJMBOUT = DR_Shift16(sJMBINCTL);
  
    if(lJMBOUT & OUT1RDY)// check if new data available 
    {
        sJMBINCTL |= JMB32B + OUTREQ;
        lJMBOUT  = DR_Shift16(sJMBINCTL);
        sJMBOUT0 = (unsigned short)DR_Shift16(0);
        sJMBOUT1 = (unsigned short)DR_Shift16(0);
    
        lJMBOUT = ((unsigned long)sJMBOUT1<<16) + sJMBOUT0;
    }
  
    return lJMBOUT;
}

//----------------------------------------------------------------------------
//! \brief Write a 16bit value into the JTAG mailbox system.
//! The function timeouts if the mailbox is not empty after a certain number
//! of retries.
//! \param[in] word dataX (data to be shifted into mailbox)
static short i_WriteJmbIn16(word dataX)
{
    unsigned short sJMBINCTL;
    unsigned short sJMBIN0;
    unsigned long Timeout = 0;
    sJMBIN0 = (unsigned short)(dataX & 0x0000FFFF);
    sJMBINCTL = INREQ;

    IR_Shift(IR_JMB_EXCHANGE); 
    do
    {
        Timeout++;
        if(Timeout >= 3000)
        {
            return STATUS_ERROR;
        }
    }
    while(!(DR_Shift16(0x0000) & IN0RDY) && Timeout < 3000);
    if(Timeout < 3000)
    {
        DR_Shift16(sJMBINCTL);
        DR_Shift16(sJMBIN0);
    }
    return STATUS_OK;
}

//! \brief Write a 32bit value into the JTAG mailbox system.
//! The function timeouts if the mailbox is not empty after a certain number
//! of retries.
//! \param[in] word dataX (data to be shifted into mailbox)
//! \param[in] word dataY (data to be shifted into mailbox)
static short i_WriteJmbIn32(unsigned short dataX,unsigned short dataY)
{
    unsigned short sJMBINCTL;
    unsigned short sJMBIN0,sJMBIN1;
    unsigned long Timeout = 0;

    sJMBIN0 = (unsigned short)(dataX & 0x0000FFFF);
    sJMBIN1 = (unsigned short)(dataY & 0x0000FFFF);
    sJMBINCTL =  JMB32B | INREQ;

    IR_Shift(IR_JMB_EXCHANGE); 
    do
    {
        Timeout++;
        if(Timeout >= 3000)
        {
            return STATUS_ERROR;
        }
    }
    while(!(DR_Shift16(0x0000) & IN0RDY) && Timeout < 3000);

    if(Timeout < 3000)
    {
        sJMBINCTL = 0x11;
        DR_Shift16(sJMBINCTL) ;
        DR_Shift16(sJMBIN0);
        DR_Shift16(sJMBIN1);
    }
    return STATUS_OK;
}

//----------------------------------------------------------------------------
//! \brief This function compares the computed PSA (Pseudo Signature Analysis)
//! value to the PSA value shifted out from the target device.
//! It is used for very fast data block write or erasure verification.
//! \param[in] unsigned long StartAddr (Start address of data block to be checked)
//! \param[in] unsigned long Length (Number of words within data block)
//! \param[in] word *DataArray (Pointer to array with the data, 0 for Erase Check)
//! \return word (STATUS_OK if comparison was successful, STATUS_ERROR otherwise)
word VerifyPSA_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray)
{
    word TDOword;
    unsigned long i;
    const word POLY = 0x0805;             // Polynom value for PSA calculation
    word PSA_CRC = (word)(StartAddr-2);   // Start value for PSA calculation

    ExecutePOR_430Xv2();
    
    SetPC_430Xv2(StartAddr);
    
    SetTCLK();
    
    IR_Shift(IR_CNTRL_SIG_16BIT);
    DR_Shift16(0x0501);

    IR_Shift(IR_DATA_16BIT);
    DR_Shift16(PSA_CRC);

    IR_Shift(IR_DATA_PSA);   
    
    for (i = 0; i < Length; i++)
    {      
        // Calculate the PSA (Pseudo Signature Analysis) value
        if ((PSA_CRC & 0x8000) == 0x8000)
        {
            PSA_CRC ^= POLY;
            PSA_CRC <<= 1;
            PSA_CRC |= 0x0001;
        }
        else
        {
            PSA_CRC <<= 1;
        }
        // if pointer is 0 then use erase check mask, otherwise data
        &DataArray[0] == 0 ? (PSA_CRC ^= 0xFFFF) : (PSA_CRC ^= DataArray[i]);      
        
        ClrTCLK();
        
#ifdef SPYBIWIRE_MODE

        TMSH_TDIH();
        TMSL_TDIH();
        TMSL_TDIH();
        TMSH_TDIH();
        TMSH_TDIH();
        TMSL_TDIH();
#else
        // Clock through the PSA          
        ClrTCK();
     
        SetTMS();
        SetTCK();            // Select DR scan
        ClrTCK();
        ClrTMS();
        
        SetTCK();            // Capture DR
        ClrTCK();
       
        SetTCK();            // Shift DR
        ClrTCK();
         
        SetTMS();        
        SetTCK();          // Exit DR  
        ClrTCK();
        
         // Set JTAG FSM back into Run-Test/Idle
        SetTCK();
        ClrTMS();
        ClrTCK();
        SetTCK();          
        
#endif        
        SetTCLK();    
      
    }
    
    IR_Shift(IR_SHIFT_OUT_PSA);
    TDOword = DR_Shift16(0x0000);     // Read out the PSA value
     
    ExecutePOR_430Xv2();

    return((TDOword == PSA_CRC) ? STATUS_OK : STATUS_ERROR);
}

/****************************************************************************/
/* High level routines for accessing the target device via JTAG:            */
/*                                                                          */
/* For the following section, the user is relieved from coding anything.    */
/* To provide better understanding and clearness, some functionality is     */
/* coded generously. (Code and speed optimization enhancements may          */
/* be desired)                                                              */
/****************************************************************************/

//----------------------------------------------------------------------------
//! \brief Function to set up the JTAG pins
static void ConnectJTAG()
{
    // drive JTAG/TEST signals
    DrvSignals();
    MsDelay(15);             // delay 15ms
}
//----------------------------------------------------------------------------
//! \brief Function to stop the JTAG communication by releasing the JTAG signals
static void StopJtag (void)
{
    // release JTAG/TEST signals
    RlsSignals();
    MsDelay(15);             // delay 15ms
}

//! \brief Function to start the SBW communication - RST line high - device starts
//! code execution   
static void EntrySequences_RstHigh_SBW()
{
    ClrTST();    //1
    MsDelay(4); // reset TEST logic

    SetRST();    //2

    SetTST();    //3
    MsDelay(20); // activate TEST logic

    // phase 1
    SetRST();    //4
    usDelay(60);

    // phase 2 -> TEST pin to 0, no change on RST pin
    // for Spy-Bi-Wire
    _DINT();
    ClrTST();  

    // phase 3
    usDelay(1);
    // phase 4 -> TEST pin to 1, no change on RST pin
    // for Spy-Bi-Wire
    SetTST();  
    _EINT();
    //_EINT_FET();
    usDelay(60);

    // phase 5
    MsDelay(5);
}

//! \brief Function to start the SBW communication - RST line low - device do not 
//! start code execution   
static void EntrySequences_RstLow_SBW()
{
    ClrTST();                //1
    MsDelay(1);       // reset TEST logic

    ClrRST();                //2
    MsDelay(50);
      
    SetTST();                //3
    MsDelay(100);     // activate TEST logic

    // phase 1
    SetRST();                //4
    usDelay(40);

    // phase 2 -> TEST pin to 0, no change on RST pin
    // for Spy-Bi-Wire
     _DINT();                       
    ClrTST();                  //5
       
    usDelay(1);

    // phase 4 -> TEST pin to 1, no change on RST pin
    // for Spy-Bi-Wire
    SetTST();      //7
    _EINT();
    usDelay(40);
    MsDelay(5);
}

//! \brief Function to start the JTAG communication - RST line high - device starts
//! code execution   
static void EntrySequences_RstHigh_JTAG()
{
    ClrTST();    //1
    MsDelay(4); // reset TEST logic

    SetRST();    //2
    
    SetTST();    //3
    MsDelay(20); // activate TEST logic

    // phase 1
    ClrRST();    //4
    usDelay(60);

    // phase 2 -> TEST pin to 0, no change on RST pin
    // for 4-wire JTAG clear Test pin
    ClrTST();  //5

    // phase 3
    usDelay(1);

    // phase 4 -> TEST pin to 1, no change on RST pin
    // for 4-wire JTAG
    SetTST();//7
    usDelay(60);

    // phase 5
    SetRST();
    MsDelay(5);
}

//! \brief Function to start the JTAG communication - RST line low - device do not 
//! start code execution   
static void EntrySequences_RstLow_JTAG()
{
    _DINT();
    ClrTST();                    //1    
    MsDelay(4);                  //reset TEST logic
    
    ClrRST();                    //2
    MsDelay(50);
    
    SetTST();                   //3
    MsDelay(50);                 //activate TEST logic
               
    ClrRST();                   //4
    usDelay(40);

     // for 4-wire JTAG clear Test pin Test(0)   
    ClrTST();                   //5
    usDelay(2);

    // for 4-wire JTAG -drive  Reset(0)
    ClrRST();                          
    usDelay(2);

    // 4-wire JTAG - Test (1)
    SetTST();
    MsDelay(5);
    _EINT();
    SetRST();
}

//----------------------------------------------------------------------------
extern void configure_IO_JTAG( void );
//----------------------------------------------------------------------------
//! \brief Function to enable JTAG communication with a target. Use JSBW mode
//!  if device is in LPM5 mode.
//! \return word (JTAG_ID91(0x91) if connection was established successfully, 
//! invalid JTAG ID (0x1) otherwise)
static word magicPattern(void)
{
    word deviceJtagID = 0;
    
    // Enable the JTAG interface to the device.
    ConnectJTAG();     
    // Apply again 4wire/SBW entry Sequence.
    // set ResetPin = 0
    if(INTERFACE == SPYBIWIRE_IF)
    {
        EntrySequences_RstLow_SBW();
    }
    else
    {
        EntrySequences_RstLow_JTAG();
    }          
    // reset TAP state machine -> Run-Test/Idle
    ResetTAP();  
    // feed JTAG mailbox with magic pattern
    if(i_WriteJmbIn16(STOP_DEVICE) == STATUS_OK)
    {          
        // Apply again 4wire/SBW entry Sequence.                        
        if(INTERFACE == SPYBIWIRE_IF)
        {
            EntrySequences_RstHigh_SBW();
        }
        else
        {
            EntrySequences_RstHigh_JTAG();
        } 
        ResetTAP();  // reset TAP state machine -> Run-Test/Idle
        
        deviceJtagID = (word)IR_Shift(IR_CNTRL_SIG_CAPTURE);
        
        if(deviceJtagID == JTAG_ID91)
        {
            // if Device is in LPM.x5 -> reset IO lock of JTAG pins and Configure it for debug
            IR_Shift(IR_TEST_3V_REG);  
            DR_Shift16(0x4020);
        }
        return deviceJtagID;
    }        
    
    // if Device is in LPM.x5 -> reset IO lock of JTAG pins and Configure it for debug
#ifdef LPM5_ACTIVATED
    {
#ifdef SPYBIWIREJTAG_IF // run 2 wire mode of 4 wire selection because 4wire pins are locked by JTAG lock
      
        ConnectJTAG();
        StartJtagJSbw(RSTLOW_SBW);        
        jResetJtagTap();
        JsbwMagicPattern();
        jRelease();          
        MsDelay(60); 
        
        // reset JTAG lock
        configure_IO_JTAG();                    
        StartJtagJSbw(RSTHIGH_SBW);   
        jResetJtagTap();
        jsbwJtagUnlock();
        MsDelay(60); 
        
        configure_IO_JTAG();
        EntrySequences_RstHigh_JTAG();      
        ResetTAP();  // reset TAP state machine -> Run-Test/Idle    
        MsDelay(60); 
        deviceJtagID = (word)IR_Shift(IR_CNTRL_SIG_CAPTURE);
        if(deviceJtagID == JTAG_ID91)
        {
            return deviceJtagID;
        }
#endif
    } 
#endif
    return 1;  // return 1 as an invalid JTAG ID
}

//----------------------------------------------------------------------------
//! \brief Function to determine & compare core identification info 
//! \return word (STATUS_OK if correct JTAG ID was returned, STATUS_ERROR 
//! otherwise)
#ifdef MCUSIM_FIRMA
/* mcu-sim: magicPattern() es static; esta es la puerta para la prueba F8 */
word magicPatternMcuSim(void) { return magicPattern(); }
#endif

static word GetCoreID (void)
{
    word i;
    word JtagId = 0;  //initialize JtagId with an invalid value
    for (i = 0; i < MAX_ENTRY_TRY; i++)
    {
        // release JTAG/TEST signals to safely reset the test logic
        StopJtag();        
        // establish the physical connection to the JTAG interface
        ConnectJTAG();               
        // Apply again 4wire/SBW entry Sequence. 
        // set ResetPin =1   
        
        if(INTERFACE == SPYBIWIRE_IF)
        {
            EntrySequences_RstHigh_SBW();
        }
        else
        {
            EntrySequences_RstHigh_JTAG();
        }         
        // reset TAP state machine -> Run-Test/Idle
        ResetTAP();  
        // shift out JTAG ID
        JtagId = (word)IR_Shift(IR_CNTRL_SIG_CAPTURE);  
         
        // break if a valid JTAG ID is being returned
        if((JtagId == JTAG_ID91) || (JtagId == JTAG_ID99))                     //****************************
        {
            break;
        }
    }
    if(i >= MAX_ENTRY_TRY)
    {
      
    // if connected device is MSP4305438 JTAG Mailbox is not usable
#ifdef ACTIVATE_MAGIC_PATTERN
        for(i = 0; i < MAX_ENTRY_TRY; i++)
        {
            // if no JTAG ID is returns -> apply magic pattern to stop user code execution 
            JtagId = magicPattern();
          
            if((JtagId == 1) || (i >= MAX_ENTRY_TRY))
            {
                // if magic pattern failed and 4 tries passed -> return status error
                return(STATUS_ERROR);
            }
            else
            {
                break; 
            }
        }
        // For MSP430F5438 family mailbox is not functional in reset state.
        // Because of this issue the magicPattern is not usable on MSP430F5438 family devices
#else
    return(STATUS_ERROR);
#endif   
    }
    if((JtagId == JTAG_ID91) || (JtagId == JTAG_ID99))                         //****************************
    {
        return(STATUS_OK);
    }
    else
    {
        return(STATUS_ERROR);
    } 
}

//----------------------------------------------------------------------------
//! \brief Function to determine & compare core identification info (Xv2)
//! \return word (STATUS_OK if correct JTAG ID was returned, STATUS_ERROR 
//! otherwise)
word GetCoreipIdXv2()
{
    IR_Shift(IR_COREIP_ID);
    CoreId = DR_Shift16(0);
    if(CoreId == 0)
    {
        return(STATUS_ERROR);
    }
    IR_Shift(IR_DEVICE_ID);
    DeviceIdPointer = DR_Shift20(0);
    // The ID pointer is an un-scrambled 20bit value    
    return(STATUS_OK);
}

//----------------------------------------------------------------------------
//! \brief Function to resync the JTAG connection and execute a Power-On-Reset
//! \return word (STATUS_OK if operation was successful, STATUS_ERROR 
//! otherwise)
static word SyncJtag_AssertPor (void)
{
    word i = 0;
   
    IR_Shift(IR_CNTRL_SIG_16BIT);
    DR_Shift16(0x1501);                  // Set device into JTAG mode + read
    
    if ((IR_Shift(IR_CNTRL_SIG_CAPTURE) != JTAG_ID91) && (IR_Shift(IR_CNTRL_SIG_CAPTURE) != JTAG_ID99)) //****************
    {
      return(STATUS_ERROR);
    }
    // wait for sync
    while(!(DR_Shift16(0) & 0x0200) && i < 50)
    {
        i++;
    };
    // continues if sync was successful
    if(i >= 50)
    {
        return(STATUS_ERROR);
    }

    // execute a Power-On-Reset
    if(ExecutePOR_430Xv2() != STATUS_OK)
    {
        return(STATUS_ERROR);
    }
  
    return(STATUS_OK);
}

//----------------------------------------------------------------------------
//! \brief Function to take target device under JTAG control. Disables the 
//! target watchdog. Sets the global DEVICE variable as read from the target 
//! device.
//! \return word (STATUS_ERROR if fuse is blown, incorrect JTAG ID or
//! synchronizing time-out; STATUS_OK otherwise)
word GetDevice_430Xv2(void)
{  
    if(GetCoreID () != STATUS_OK)
    { 
        return(STATUS_ERROR);
    }
    if (IsLockKeyProgrammed())                 // Stop here if fuse is already blown
    {
        return(STATUS_FUSEBLOWN);
    }    
    if (GetCoreipIdXv2()!= STATUS_OK)
    { 
        return(STATUS_ERROR);
    }
    if(SyncJtag_AssertPor() != STATUS_OK)
    {
        return(STATUS_ERROR);
    }
    // CPU is now in Full-Emulation-State  
    // read DeviceId from memory
    ReadMemQuick_430Xv2(DeviceIdPointer + 4, 1, (word*)&DeviceId);
    
    return(STATUS_OK);
}

//----------------------------------------------------------------------------
//! \brief Function to release the target device from JTAG control
//! \param[in] word Addr (0xFFFE: Perform Reset, means Load Reset Vector into 
//! PC, otherwise: Load Addr into PC)
void ReleaseDevice_430Xv2(unsigned long Addr)
{
    switch(Addr)
    {
        case V_BOR:
        
        // perform a BOR via JTAG - we loose control of the device then...
        IR_Shift(IR_TEST_REG);
        DR_Shift16(0x0200);
        MsDelay(5);     // wait some time before doing any other action
        // JTAG control is lost now - GetDevice() needs to be called again to gain control.
        break;
        
        case V_RESET:
        
        IR_Shift(IR_CNTRL_SIG_16BIT);
        DR_Shift16(0x0C01);                 // Perform a reset
        DR_Shift16(0x0401);
        IR_Shift(IR_CNTRL_SIG_RELEASE);
        break;
        
        default:
        
        SetPC_430Xv2(Addr);                 // Set target CPU's PC
        // prepare release & release
        SetTCLK();
        IR_Shift(IR_CNTRL_SIG_16BIT);
        DR_Shift16(0x0401);
        IR_Shift(IR_ADDR_CAPTURE);
        IR_Shift(IR_CNTRL_SIG_RELEASE);
    }
}

//----------------------------------------------------------------------------
//! \brief This function writes one byte/word at a given address ( <0xA00)
//! \param[in] word Format (F_BYTE or F_WORD)
//! \param[in] word Addr (Address of data to be written)
//! \param[in] word Data (shifted data)
void WriteMem_430Xv2(word Format, unsigned long Addr, word Data)
{
    // Check Init State at the beginning
    IR_Shift(IR_CNTRL_SIG_CAPTURE);
    if(DR_Shift16(0) & 0x0301)
    {
        ClrTCLK();
        IR_Shift(IR_CNTRL_SIG_16BIT);
        if  (Format == F_WORD)
        {
            DR_Shift16(0x0500);
        }
        else
        {
            DR_Shift16(0x0510);
        }
        IR_Shift(IR_ADDR_16BIT);
        DR_Shift20(Addr);
        
        SetTCLK();
        // New style: Only apply data during clock high phase
        IR_Shift(IR_DATA_TO_ADDR);
        DR_Shift16(Data);           // Shift in 16 bits
        ClrTCLK();
        IR_Shift(IR_CNTRL_SIG_16BIT);
        DR_Shift16(0x0501);
        SetTCLK();
        // one or more cycle, so CPU is driving correct MAB
        ClrTCLK();
        SetTCLK();
        // Processor is now again in Init State
    }
}

//----------------------------------------------------------------------------
//! \brief This function writes an array of words into the target memory.
//! \param[in] word StartAddr (Start address of target memory)
//! \param[in] word Length (Number of words to be programmed)
//! \param[in] word *DataArray (Pointer to array with the data)
void WriteMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray)
{
    unsigned long i;
  
    for (i = 0; i < Length; i++)
    {
        WriteMem_430Xv2(F_WORD, StartAddr, DataArray[i]);
        StartAddr += 2;
    }
}

//----------------------------------------------------------------------------
//! \brief This function programs/verifies an array of words into the FLASH
//! memory by using the FLASH controller.
//! \param[in] word StartAddr (Start address of FLASH memory)
//! \param[in] word Length (Number of words to be programmed)
//! \param[in] word *DataArray (Pointer to array with the data)
void WriteFLASH_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray)
{
    word loadAddr  = RAM_START_ADDRESS;            // RAM start address specified in config header file
    word startAddr = loadAddr + FlashWrite_o[0];   // start address of the program in traget RAM

    FlashWrite_o[2] = (unsigned short)(StartAddr);     // set write start address
    FlashWrite_o[3] = (unsigned short)(StartAddr>>16);
    FlashWrite_o[4] = (unsigned short)(Length);        // set number of words to write
    FlashWrite_o[5] = (unsigned short)(Length>>16);
    FlashWrite_o[6] = SegmentInfoAKey5xx;               // FCTL3: lock/unlock INFO Segment A
                                                        // default = locked

    WriteMemQuick_430Xv2(loadAddr, FlashWrite_o_length/2, (word*)FlashWrite_o);
    ReleaseDevice_430Xv2(startAddr);

    {
        unsigned long Jmb = 0;
        unsigned long Timeout = 0;
    
        do
        {
            Jmb = i_ReadJmbOut();
            Timeout++;
        }
        while(Jmb != 0xABADBABE && Timeout < 3000);
        
        if(Timeout < 3000)
        {
            unsigned long i;
          
            for(i = 0; i < Length; i++)
            {
                i_WriteJmbIn16(DataArray[i]);
                //usDelay(100);            // delay 100us  - added by GC       
            }
        }
    }
    {
        unsigned long Jmb = 0;
        unsigned long Timeout = 0;
        
        do
        {
            Jmb = i_ReadJmbOut();
            Timeout++;
        }
        while(Jmb != 0xCAFEBABE && Timeout < 3000);
    }

    SyncJtag_AssertPor();

    // clear RAM here - init with JMP $
    {
        word i;

        for (i = 0; i < FlashWrite_o_length/2; i++)
        {
            WriteMem_430Xv2(F_WORD, loadAddr, 0x3fff);
            loadAddr += 2;
        }
    }
}

//----------------------------------------------------------------------------
//! \brief This function programs/verifies a set of data arrays of words into a FLASH
//! memory by using the "WriteFLASH()" function. It conforms with the
//! "CodeArray" structure convention of file "Target_Code_5xx_(IDE).s43" or "Target_Code.h".
//! \param[in] const unsigned int  *DataArray (Pointer to array with the data)
//! \param[in] const unsigned long *address (Pointer to array with the startaddresses)
//! \param[in] const unsigned long *length_of_sections (Pointer to array with the number of words counting from startaddress)
//! \param[in] const unsigned long sections (Number of sections in code file)
//! \return word (STATUS_OK if verification was successful,
//! STATUS_ERROR otherwise)
word WriteFLASHallSections_430Xv2(const unsigned int *data, const unsigned long *address, const unsigned long *length_of_sections, const unsigned long sections)
{
    int i, init = 1;
  
    for(i = 0; i < sections; i++)
    {
        // Write/Verify(PSA) one FLASH section 
        WriteFLASH(address[i], length_of_sections[i], (word*)&data[init-1]);        
        if (!VerifyMem(address[i], length_of_sections[i], (word*)&data[init-1]))
        {
            return(STATUS_ERROR);
        }
        init += length_of_sections[i];      
    }    
    return(STATUS_OK);
}

//----------------------------------------------------------------------------
//! \brief This function reads one byte/word from a given address in memory
//! \param[in] word Format (F_BYTE or F_WORD)
//! \param[in] word Addr (address of memory)
//! \return word (content of the addressed memory location)
word ReadMem_430Xv2(word Format, unsigned long Addr)
{
    word TDOword = 0;
  
    // Check Init State at the beginning
    IR_Shift(IR_CNTRL_SIG_CAPTURE);
    if(DR_Shift16(0) & 0x0301)
    {
        // Read Memory
        ClrTCLK();
        IR_Shift(IR_CNTRL_SIG_16BIT);
        if  (Format == F_WORD)
        {
            DR_Shift16(0x0501);             // Set word read
        }
        else
        {
            DR_Shift16(0x0511);             // Set byte read
        }
        IR_Shift(IR_ADDR_16BIT);
        DR_Shift20(Addr);                   // Set address
        IR_Shift(IR_DATA_TO_ADDR);
        SetTCLK();
        ClrTCLK();
        TDOword = DR_Shift16(0x0000);       // Shift out 16 bits
        
        SetTCLK();
        // one or more cycle, so CPU is driving correct MAB
        ClrTCLK();
        SetTCLK();
        // Processor is now again in Init State
    }
    
    return TDOword;
}

//----------------------------------------------------------------------------
//! \brief This function reads an array of words from the memory.
//! \param[in] word StartAddr (Start address of memory to be read)
//! \param[in] word Length (Number of words to be read)
//! \param[out] word *DataArray (Pointer to array for the data)
void ReadMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray)
{
    unsigned long i, lPc = 0;

    // Set PC to 'safe' address
    if (IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID99)
    {
        lPc = 0x00000004;
    }
    
    SetPC_430Xv2(StartAddr);
    SetTCLK();
    IR_Shift(IR_CNTRL_SIG_16BIT);
    DR_Shift16(0x0501);
    IR_Shift(IR_ADDR_CAPTURE);
  
    IR_Shift(IR_DATA_QUICK);
  
    for (i = 0; i < Length; i++)
    {
        SetTCLK();
        ClrTCLK(); 
        *DataArray++   = DR_Shift16(0);  // Read data from memory.         
    }
    
    if(lPc)
    {
        SetPC_430Xv2(lPc);
    }    
    SetTCLK();
    
}

//----------------------------------------------------------------------------
//! \brief This function performs a mass erase (with and w/o info memory) or a
//! segment erase of a FLASH module specified by the given mode and address.
//! Large memory devices get additional mass erase operations to meet the spec.
//! (Could be extended with erase check via PSA)
//! \param[in] word Mode (could be ERASE_MASS or ERASE_MAIN or ERASE_SGMT)
//! \param[in] word Addr (any address within the selected segment)
void EraseFLASH_430Xv2(word EraseMode, unsigned long EraseAddr)
{
    word loadAddr  = RAM_START_ADDRESS;            // RAM start address specified in config header file
    word startAddr = loadAddr + FlashErase_o[0];   // start address of the program in target RAM

    FlashErase_o[2] = (unsigned short)(EraseAddr);     // set dummy write address
    FlashErase_o[3] = (unsigned short)(EraseAddr>>16);
    FlashErase_o[4] = EraseMode;                       // set erase mode
    FlashErase_o[5] = SegmentInfoAKey5xx;               // FCTL3: lock/unlock INFO Segment A
                                                        // default = locked

    WriteMemQuick_430Xv2(loadAddr, FlashErase_o_length/2, (word*)FlashErase_o);
    ReleaseDevice_430Xv2(startAddr);

    {
        unsigned long Jmb = 0;
        unsigned long Timeout = 0;
        
        do
        {
            Jmb = i_ReadJmbOut();
            Timeout++;
        }
        while(Jmb != 0xCAFEBABE && Timeout < 3000);
    }

    SyncJtag_AssertPor();

    // clear RAM here - init with JMP $
    {
        word i;

        for (i = 0; i < FlashErase_o_length/2; i++)
        {
            WriteMem_430Xv2(F_WORD, loadAddr, 0x3fff);
            loadAddr += 2;
        }
    }
}

//----------------------------------------------------------------------------
//! \brief This function performs a mass erase (with and w/o info memory) or a
//! segment erase of a FLASH module specified by the given mode and address
//! without releasing the device from JTAG control.
//! Large memory devices get additional mass erase operations to meet the spec.
//! (Could be extended with erase check via PSA)
//! \param[in] word Mode (could be ERASE_MASS or ERASE_MAIN or ERASE_SGMT)
//! \param[in] word Addr (any address within the selected segment)
void EraseFLASH_430Xv2_wo_release(word EraseMode, unsigned long EraseAddr)
{
    word loadAddr  = RAM_START_ADDRESS;            // RAM start address specified in config header file
    word startAddr = loadAddr + FlashErase_o[0];   // start address of the program in target RAM

    FlashErase_o[2] = (unsigned short)(EraseAddr);     // set dummy write address
    FlashErase_o[3] = (unsigned short)(EraseAddr>>16);
    FlashErase_o[4] = EraseMode;                       // set erase mode
    FlashErase_o[5] = SegmentInfoAKey5xx;               // FCTL3: lock/unlock INFO Segment A
                                                        // default = locked

    WriteMemQuick_430Xv2(loadAddr, FlashErase_o_length/2, (word*)FlashErase_o);
  
    {
        word i;
        SetPC_430Xv2(startAddr);
        for (i=110; i--;)
        {
            ClrTCLK();
            SetTCLK();
        }
    }

    //max mass/segment erase time for F543x is 32ms
    //do not check mailbox, just wait..
    MsDelay(35);

    //let Erase function finish
    {
        word i;
        for (i=110; i--;) //110 to let the erase routine finish the restauration
        {
            ClrTCLK();
            SetTCLK();
        }
    }

    SyncJtag_AssertPor();

    // clear RAM here - init with JMP $
    {
        word i;

        for (i = 0; i < FlashErase_o_length/2; i++)
        {
            WriteMem_430Xv2(F_WORD, loadAddr, 0x3fff);
            loadAddr += 2;
        }
    }
}

//----------------------------------------------------------------------------
//! \brief This function performs an Erase Check over the given memory range
//! \param[in] word StartAddr (Start address of memory to be checked)
//! \param[in] word Length (Number of words to be checked)
//! \return word (STATUS_OK if erase check was successful, STATUS_ERROR 
//! otherwise)
word EraseCheck_430Xv2(unsigned long StartAddr, unsigned long Length)
{
    return (VerifyPSA_430Xv2(StartAddr, Length, 0));
}

//----------------------------------------------------------------------------
//! \brief This function performs a Verification over the given memory range
//! \param[in] word StartAddr (Start address of memory to be verified)
//! \param[in] word Length (Number of words to be verified)
//! \param[in] word *DataArray (Pointer to array with the data)
//! \return word (STATUS_OK if verification was successful, STATUS_ERROR
//! otherwise)
word VerifyMem_430Xv2(unsigned long StartAddr, unsigned long Length, word *DataArray)
{
    return (VerifyPSA_430Xv2(StartAddr, Length, DataArray));
}
//------------------------------------------------------------------------
//! \brief This function disables JTAG access to the target device.
//! \return word (TRUE if fuse blow was successful, FALSE otherwise)
word ProgramLockKey(void)
{
        word LockKey[2] = { 0xDEAD, 0xBABE };

        // unprotect BSL memory by writing to the SYSBSLC register
        // to reset the BSL protection bit (SYSBSLPE)
        WriteMem(F_WORD, 0x0182, 0x0003);
    
        // write JTAG lock key to flash memory
        WriteFLASH(0x17FC, 2, LockKey);
      
        // now perform a BOR via JTAG - we loose control of the device then...
        IR_Shift(IR_TEST_REG);
        DR_Shift16(0x0200);
        MsDelay(5);     // wait some time until Bootcode is executed
        
        // -> get it under JTAG control again
        // and return result of "is fuse blown?"
        return(GetDevice() == STATUS_FUSEBLOWN);
}

//------------------------------------------------------------------------
//! \brief This function checks if the JTAG lock key is programmed.
//! \return word (STATUS_OK if fuse is blown, STATUS_ERROR otherwise)
word IsLockKeyProgrammed(void)
{
    word i;

    for (i = 3; i > 0; i--)     //  First trial could be wrong
    {
        IR_Shift(IR_CNTRL_SIG_CAPTURE);
        if (DR_Shift16(0xAAAA) == 0x5555)
        {
            return(STATUS_OK);  // Fuse is blown
        }
    }
    return(STATUS_ERROR);       // Fuse is not blown
}

//------------------------------------------------------------------------
//! \brief This function unlocks the BSL memory protection.
void UnlockBsl_430Xv2Flash(void)
{
    unsigned short BslCur;

    // Read SYSBSLC register
    BslCur = ReadMem_430Xv2(F_WORD, 0x0182);
    // Disable BSL area protection
	BslCur &= ~SYSBSLPE;
	// Write back SYSBSLC register
    WriteMem_430Xv2(F_WORD, 0x0182, BslCur);
}

//------------------------------------------------------------------------
//! \brief This function unlocks segment A of the InfoMemory (Flash)
void UnlockInfoA_430Xv2(void)
{
    SegmentInfoAKey5xx = 0xA508;
}

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
