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
| JTAGfunc430FR.c                                                            |
|                                                                            |
| JTAG Control Sequences for Erasing / Programming / Fuse Programming        |
|----------------------------------------------------------------------------|
| Project:             MSP430 FRAM Replicator                                |
| Developed using:     IAR Embedded Workbench 6.20                           |
|             and:     Code Composer Studio 6.0                              |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/14 RL         Initial version.                                      |
| 1.1 02/16 RL         Added workaround for inconsistency between PC and MAB |
|----------------------------------------------------------------------------|
| Designed 2014 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file JTAGfunc430FR.c
//! \brief JTAG Control Sequences for Erasing / Programming / Fuse Programming
/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "LowLevelFunc430Xv2.h"
#include "JTAGfunc430FR.h"

#ifdef FR4xxFR2xx
    #include "FramEraseFR4xx.c"
    #include "FramWriteFR4xx.c"
    #include "msp430CodeFR4xx.h"
#else
    #include "FramErase.c"
    #include "FramWrite.c"
    #include "msp430CodeFR57_6xx.h"
#endif



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
#define SAFE_FRAM_PC 0x0004

//----------------------------------------------------------------------------
//! \brief Function to execute a Power-On Reset (POR) using JTAG CNTRL SIG 
//! register
//! \return word (STATUS_OK if target is in Full-Emulation-State afterwards,
//! STATUS_ERROR otherwise)
static word ExecutePOR_430Xv2(void)
{   
    // provide one clock cycle to empty the pipe
    ClrTCLK();
    SetTCLK();

    // prepare access to the JTAG CNTRL SIG register  
    IR_Shift(IR_CNTRL_SIG_16BIT);
    // release CPUSUSP signal and apply POR signal
    DR_Shift16(0x0C01);
    // release POR signal again
    DR_Shift16(0x0401);
  

    // Set PC to 'safe' memory location 
    IR_Shift(IR_DATA_16BIT);
    ClrTCLK();
    SetTCLK();
    ClrTCLK();
    SetTCLK();
    DR_Shift16(SAFE_FRAM_PC);
	// PC is set to 0x4 - MAB value can be 0x6 or 0x8
	
    // drive safe address into PC        
    ClrTCLK();
    SetTCLK();
    
    IR_Shift(IR_DATA_CAPTURE);

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
    unsigned short id =  IR_Shift(IR_CNTRL_SIG_CAPTURE);
    if(id == JTAG_ID98) 
    {
        WriteMem_430Xv2(F_WORD, 0x01CC, 0x5A80);
    }
    else
    {
        WriteMem_430Xv2(F_WORD, 0x015C, 0x5A80);
    }

	// Initialize Test Memory with default values to ensure consistency 
	// between PC value and MAB (MAB is +2 after sync)
	if(id == JTAG_ID91 || id == JTAG_ID99)
	{
		WriteMem_430Xv2(F_WORD, 0x06, 0x3FFF);
		WriteMem_430Xv2(F_WORD, 0x08, 0x3FFF);
	}
	
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
        // insert on 24.03.2010 Florian
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
short i_WriteJmbIn32(unsigned short dataX,unsigned short dataY)
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
word VerifyPSA_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray)
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
//! \brief Function to start the JTAG communication - RST line high - device starts
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

    // for 4-wire JTAG -dry  Reset(0)
    ClrRST();                          
    usDelay(2);

    // 4-wire JTAG - Test (1)
    SetTST();
    MsDelay(5);
    _EINT();
    
    SetRST();
}

//----------------------------------------------------------------------------
//! \brief Function to stop the JTAG communication by releasing the JTAG signals
static void StopJtag (void)
{
    // release JTAG/TEST signals
    RlsSignals();
    MsDelay(15);             // delay 15ms
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
        else if(deviceJtagID == JTAG_ID99)
        {
            IR_Shift(IR_TEST_3V_REG); 
            DR_Shift16(0x40A0);
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
        if(deviceJtagID == JTAG_ID91 || deviceJtagID == JTAG_ID99)
        {
            return deviceJtagID;
        }
#endif
    } 
#endif
    return 1;  // return 1 as an invalid JTAG ID
}

//----------------------------------------------------------------------------
//! \brief This function unlocks the Fram memory when a JTAG password is set.
//! \param[in] unsigned short* password (Pointer to array containing the JTAG
//! Password)
//! \param[in] unsigned long passwordLength (length of the password in words)
//! \return word (STATUS_OK if memory unlock was successful, STATUS_ERROR
//! otherwise)
word UnlockDevice_430Xv2(unsigned short* password, unsigned long passwordLength)
{    
  unsigned short i = 0;
    /*----------------------------------------------------------------------- */ 
    /*            phase 1 of device entry using a user password               */
    /*------------------------------------------------------------------------*/ 
  
    // Enable the JTAG interface to the device.
    ConnectJTAG();     
    // Apply again 4wire/SBW entry Sequence.
    // set ResetPin =0
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
    // shift in JTAG mailbox exchange request
    if(i_WriteJmbIn32(STOP_DEVICE, 0x1E1E) == STATUS_ERROR)
    {
        return STATUS_ERROR;
    }
    StopJtag();
    /*----------------------------------------------------------------------- */ 
    /*            phase 2 of device entry using a user password               */
    /*------------------------------------------------------------------------*/
    // Enable the JTAG interface to the device.
    ConnectJTAG();  
    // Apply again 4wire/SBW entry Sequence.
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
    // shift in JTAG mailbox exchange request      
    while(i < passwordLength)
    {
        if(i_WriteJmbIn16(password[i]) == STATUS_ERROR)
        {
            return STATUS_ERROR;
        }
        i++;    /* mcu-sim: [error de TI] sin esto el bucle no acaba (manda la primera palabra para siempre) */   
    }
    return(STATUS_OK);
}

//----------------------------------------------------------------------------
//! \brief Function to determine & compare core identification info 
//! \return word (STATUS_OK if correct JTAG ID was returned, STATUS_ERROR 
//! otherwise)
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
        if((JtagId == JTAG_ID91) || (JtagId == JTAG_ID99) || (JtagId == JTAG_ID98))                     //****************************
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
            // if no JTAG ID is returns -> apply magic pattern to stop user cd excecution 
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
    if((JtagId == JTAG_ID91) || (JtagId == JTAG_ID99) || (JtagId == JTAG_ID98))                        //****************************
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
    
    if ((IR_Shift(IR_CNTRL_SIG_CAPTURE) != JTAG_ID91) && 
        (IR_Shift(IR_CNTRL_SIG_CAPTURE) != JTAG_ID99) &&
        (IR_Shift(IR_CNTRL_SIG_CAPTURE) != JTAG_ID98))
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
void WriteMemQuick_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray)
{
    unsigned long i;
  
    for (i = 0; i < Length; i++)
    {
        WriteMem_430Xv2(F_WORD, StartAddr, DataArray[i]);
        StartAddr += 2;
    }
}

//----------------------------------------------------------------------------
//! \brief This function programs/verifies an array of words into the Fram
//! memory.
//! \param[in] word StartAddr (Start address of Fram memory)
//! \param[in] word Length (Number of words to be programmed)
//! \param[in] word *DataArray (Pointer to array with the data)
short WriteFram_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray)
{
    unsigned long Jmb = 0;
    unsigned long Timeout = 0;
    word loadAddr  = RAM_START_ADDRESS;               // RAM start address specified in config header file
    word startAddr = loadAddr + FramWrite_o[0];       // start address of the program in target RAM

    FramWrite_o[2] = (unsigned short)(StartAddr);     // set write start address
    FramWrite_o[3] = (unsigned short)(StartAddr>>16);
    FramWrite_o[4] = (unsigned short)(Length);        // set number of words to write
    FramWrite_o[5] = (unsigned short)(Length>>16);

    WriteMemQuick_430Xv2(loadAddr, FramWrite_o_length/2, (word*)FramWrite_o);
    ReleaseDevice_430Xv2(startAddr);

    Jmb = 0;
    Timeout = 0;
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
        }
    }
    Jmb = 0;
    Timeout = 0;
    
    do
    {
        Jmb = i_ReadJmbOut();
        Timeout++;
    }
    while(Jmb != 0xCAFEBABE && Timeout < 3000);

    if(SyncJtag_AssertPor() != STATUS_OK)
    {
        return(STATUS_ERROR);
    }

    // clear RAM here - init with JMP $
    {
        word i;

        for (i = 0; i < FramWrite_o_length/2; i++)
        {
            WriteMem_430Xv2(F_WORD, loadAddr, 0x3fff);
            loadAddr += 2;
        }
    }
    return(STATUS_OK);
}

//! \brief DownloadFile
//! Load program data int memory sections
//! \param[in] program Structure containing executable code and meta data
//! \return word (STATUS_OK if verification was successful,
//! STATUS_ERROR otherwise)

short DownloadProgram(struct_Program* program)
{
#ifdef MailBoxWriteFram
    int sectionIndex = 0;
    unsigned long memTarget;
    unsigned short const *dataPointer;

    dataPointer = program->textData;

    for(sectionIndex=0; sectionIndex < program->noSections; sectionIndex++)
    {
        //Setup start address of section
        memTarget = program->startAddress[sectionIndex];        
        if (WriteFram_430Xv2(memTarget, program->sectionLength[sectionIndex], dataPointer)!= STATUS_OK)
        {
            return(STATUS_ERROR);
        }
        dataPointer = dataPointer + program->sectionLength[sectionIndex];        
    }
    // verify data after download
    dataPointer = program->textData;   
    for(sectionIndex=0; sectionIndex < program->noSections; sectionIndex++)
    {
        //Setup start address of section
        memTarget = program->startAddress[sectionIndex];        
        if (!VerifyMem_430Xv2(memTarget, program->sectionLength[sectionIndex], dataPointer))
        {
            return(STATUS_ERROR);
        }
        dataPointer = dataPointer + program->sectionLength[sectionIndex];
    }
    return STATUS_OK;  
#else    
    int sectionIndex;
    unsigned long memTarget;
    unsigned short const *dataPointer;

    dataPointer = program->textData;
    
    //Loop over all sections to load
    for(sectionIndex=0; sectionIndex < program->noSections; sectionIndex++)
    {               
        //Setup start address of section
        memTarget = program->startAddress[sectionIndex];
        WriteMemQuick_430Xv2(memTarget, program->sectionLength[sectionIndex], dataPointer); 
        dataPointer = dataPointer + program->sectionLength[sectionIndex];
    }    
    // verify data after download
    dataPointer = program->textData;
    for(sectionIndex=0; sectionIndex < program->noSections; sectionIndex++)
    {               
        //Setup start address of section
        memTarget = program->startAddress[sectionIndex];
        if (!VerifyMem_430Xv2(memTarget, program->sectionLength[sectionIndex], dataPointer))          
        {
            return(STATUS_ERROR);
        }
        dataPointer = dataPointer + program->sectionLength[sectionIndex];
    }
    return STATUS_OK;
#endif
}
//! \brief Executable code and meta data about downloaded program
struct_Program prog;

//! \brief Download a program to target
//! \return received error of called funtion 
short DownloadMsp430Code()
{
    //Setup program data for code download
    prog.textData = (unsigned short const *)&msp430Code;
    prog.startAddress = (unsigned long const *)&msp430Code_address;
    prog.sectionLength = (unsigned long const *)&msp430Code_length_of_sections;
    prog.noSections = msp430Code_sections;
    prog.start = msp430Code_start;
    //Download program to target device
    return DownloadProgram(&prog);
}

//----------------------------------------------------------------------------
//! \brief This function reads one byte/word from a given address in memory
//! \param[in] word Format (F_BYTE or F_WORD)
//! \param[in] word Addr (address of memory)
//! \return word (content of the addressed memory location)
word ReadMem_430Xv2(word Format, unsigned long Addr)
{
    word TDOword = 0;
    MsDelay(1);
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
    if((IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID99) || (IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID98))
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
//! \brief This function performs an erase of a user defined FRAM memory section.
//! For FRAM devices the erase equals a write operation of 0xFFFF to the
//! respective memory section. (Could be extended with erase check via PSA)
//! This function should be used for "segment erases" only. For a "mass erase",
//! consider using EraseFRAMViaBootCode_430Xv2 instead.
//! \param[in] word StartAddr (start address for the erase)
//! \param[in] word Length (length of the memory section in WORDS)
void EraseFRAM_430Xv2(unsigned long EraseAddr, unsigned long Length)
{
    word loadAddr  = RAM_START_ADDRESS;           // RAM start address specified in config header file
    word startAddr = loadAddr + FramErase_o[0];   // start address of the program in target RAM

    FramErase_o[2] = (unsigned short)(EraseAddr);     // set dummy write address
    FramErase_o[3] = (unsigned short)(EraseAddr>>16);
    FramErase_o[4] = (unsigned short)(Length);      
    FramErase_o[5] = (unsigned short)(Length>>16);           
    
    WriteMemQuick_430Xv2(loadAddr, FramErase_o_length/2, (word*)FramErase_o);
    ReleaseDevice_430Xv2(startAddr);
    {
        unsigned long Jmb = 0;
        unsigned long Timeout = 0;

        do
        {
            Jmb = 0;
            Jmb = i_ReadJmbOut();
            Timeout++;
        }
        // original timeout: 3000
        while(Jmb != 0xCAFEBABE && Timeout < Length);
    }

    SyncJtag_AssertPor();

    // clear RAM here - init with JMP $
    {
        word i;

        for (i = 0; i < FramErase_o_length/2; i++)
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
word VerifyMem_430Xv2(unsigned long StartAddr, unsigned long Length, unsigned short const *DataArray)
{
    return (VerifyPSA_430Xv2(StartAddr, Length, DataArray));
}

//------------------------------------------------------------------------
//! \brief This function blows the security fuse.
//! \return word (TRUE if fuse blow was successful, FALSE otherwise)
word ProgramLockKey(void)
{
        word LockKey[2] = { 0x5555, 0x5555 };
   
        // write JTAG lock key
        WriteMemQuick_430Xv2(0xff80, 2, LockKey);
      
        // now perform a BOR via JTAG - we loose control of the device then...
        IR_Shift(IR_TEST_REG);
        DR_Shift16(0x0200);
        MsDelay(5);     // wait some time until Bootcode is executed
        
        // -> get it under JTAG control again
        // and return result of "is fuse blown?"
        return(GetDevice_430Xv2() == STATUS_FUSEBLOWN);
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
#define FR59xx_FR60xx_LOCKREGISTER 0x140

//------------------------------------------------------------------------
//! \brief This function disables the Memory Write protection (FRAM devices only FR6047, FR5994)
//! \return word (STATUS_OK if MPU was disabled successfully, STATUS_ERROR
//! otherwise)
word DisableFramWprod_430Xv2(void)
{
    if(IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID99)
    {
        volatile unsigned short newRegisterVal = ReadMem_430Xv2(F_WORD, FR59xx_FR60xx_LOCKREGISTER);

        if((newRegisterVal & 0x1) == 0x0)
        {
            return STATUS_OK;
        }
        newRegisterVal &= ~0xFF01;
        newRegisterVal |= 0xA500;
        WriteMem_430Xv2(F_WORD, FR59xx_FR60xx_LOCKREGISTER, newRegisterVal);

        newRegisterVal = ReadMem_430Xv2(F_WORD, FR59xx_FR60xx_LOCKREGISTER);
        if((newRegisterVal & 0x1))
        {
            return STATUS_ERROR;
        }
        else
        {
            return STATUS_OK;
        }
    }
    return STATUS_OK;
}

#define FR4xx_LOCKREGISTER 0x160
//------------------------------------------------------------------------
//! \brief This function disables the Memory Protection Unit (FRAM devices only)
//! \return word (STATUS_OK if MPU was disabled successfully, STATUS_ERROR 
//! otherwise)
word DisableMpu_430Xv2(void)
{
    if(IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID98)
    {
        unsigned short newRegisterVal = ReadMem_430Xv2(F_WORD, FR4xx_LOCKREGISTER);
        newRegisterVal &= ~0xFF03;        
        newRegisterVal |= 0xA500;
        // unlock MPU for FR4xx/FR2xx
        WriteMem_430Xv2(F_WORD, FR4xx_LOCKREGISTER, newRegisterVal);
        if((ReadMem_430Xv2(F_WORD, FR4xx_LOCKREGISTER) & 0x3) == 0x0)
        {
            return STATUS_OK;
        }
        return STATUS_ERROR;
    }
    else
    {
        unsigned short MPUCTL0 =0x0000;
        unsigned short FramCtlKey = 0xA500;      
      
        // first read out the MPU control register 0 
        MPUCTL0 = ReadMem_430Xv2(F_WORD,0x05A0); 
      
        // check MPUENA bit: if MPU is not enabled just return no error
        if((MPUCTL0 & 0x1)==0)
        {
            return(STATUS_OK);
        }  
        // check MPULOCK bit: if MPULOCK is set write access to all MPU
        // registers is disabled until a POR/BOR occurs
        if((MPUCTL0 & 0x3)!=0x1)
        {              
          // feed in magic pattern to stop code execution after BOR
            if( i_WriteJmbIn16(STOP_DEVICE) == STATUS_ERROR)
            {
                return(STATUS_ERROR);
            }
            // Apply BOR to reset the device
            SetTST();
            MsDelay(20); 
            ClrTST();
        
            SetRST(); 
            MsDelay(20);      
            ClrRST();
            MsDelay(20); 
             
            // connect to device again, apply entry sequence
            ConnectJTAG(); 
             
            // Apply again 4wire/SBW entry Sequence.                        
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
            // get jtag control back
            if(STATUS_ERROR == SyncJtag_AssertPor())
            {
               return(STATUS_ERROR);
            }
        }        
        // MPU Registers are unlocked. MPU can now be disabled.
        // Set MPUENA = 0, write Fram MPUCTL0 key 
        WriteMem_430Xv2(F_WORD, 0x05A0, FramCtlKey);
       
        MPUCTL0 = ReadMem_430Xv2(F_WORD,0x05A0);   
        // now check if MPU is disabled
        if((MPUCTL0 & 0x1)==0)
        {
            return STATUS_OK;  
        }   
        return STATUS_ERROR;
    }
}

//------------------------------------------------------------------------
//! \brief This function unlocks the BSL memory protection.
void UnlockBsl_430Xv2FRAM(void)
{
    word sysBslAddress = 0x0182;
    if(IR_Shift(IR_CNTRL_SIG_CAPTURE) == JTAG_ID98)
    {
        sysBslAddress =  0x0142;
    }
    unsigned short BslCur;
    // Read SYSBSLC register
    BslCur = ReadMem_430Xv2(F_WORD, sysBslAddress);
    // Disable BSL area protection
    BslCur &= ~SYSBSLPE;
    // Write back SYSBSLC register
    WriteMem_430Xv2(F_WORD, sysBslAddress, BslCur);
}

//! \brief This function performs a Erase of FRxx devices using the JTAG mailbox
//! \param[in] word mailBoxMode 32Bit 16Bit mode
//! \param[in] word data1 mailbox data - first 16BIT
//! \param[in] word data2 mailbox data - second 16BIT
//! \return word (STATUS_OK if erase was successful, STATUS_ERROR
//! otherwise)
word EraseFRAMViaBootCode_430Xv2(word mailBoxMode, word data1, word data2)
{
    short mailBoxError =  0;
    // restart device
    ClrTST();
    ClrRST();
    MsDelay(200);

    if(INTERFACE == SPYBIWIRE_IF)
    {
        EntrySequences_RstLow_SBW();
    }
    else
    {
        EntrySequences_RstLow_JTAG();
    } 
        
    ResetTAP();    
   
    if(mailBoxMode == MAIL_BOX_32BIT)// 32Bit Mode
    {
        mailBoxError = i_WriteJmbIn32(data1,data2);
    }
    else // 16 Bit Mode
    {
        mailBoxError = i_WriteJmbIn16(data1);
    }
    // restart device
    ClrTST();
    SetRST();
    MsDelay(200);

    if(INTERFACE == SPYBIWIRE_IF)
    {
        EntrySequences_RstHigh_SBW();
    }
    else
    {
        EntrySequences_RstHigh_JTAG();
    }     
    ResetTAP();
    // wait until erase is done 
    MsDelay(60);

    if(SyncJtag_AssertPor() != STATUS_OK)
    {
        return(STATUS_ERROR);
    }
    
    // check if mailbox input was ok
    if(mailBoxError == STATUS_ERROR)
    {
        return(STATUS_ERROR);
    }
    return STATUS_OK;
}

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
