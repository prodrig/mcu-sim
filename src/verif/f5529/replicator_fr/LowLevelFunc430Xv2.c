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
| LowLevelFunc430X.c                                                         |
|                                                                            |
| Low Level Functions regarding user's Hardware                              |
|----------------------------------------------------------------------------|
| Project:              MSP430 Replicator                                    |
| Developed using:      IAR Embedded Workbench 6.20                          |
|             and:      Code Composer Studio 6.0                             |
|----------------------------------------------------------------------------|
| Version history:                                                           |
| 1.0 04/02 FRGR        Initial version.                                     |
| 1.1 04/02 FRGR        Included SPI mode to speed up shifting function by 2.|
| 1.2 06/02 ALB2        Formatting changes, added comments.                  |
| 1.3 08/02 ALB2        Initial code release with Lit# SLAA149.              |
| 1.4 09/05 SUN1        Software delays redesigned to use TimerA harware;    |
|                       see MsDelay() routine. Added TA setup                |
| 1.5 12/05 STO         Adapted for 2xx devices with SpyBiWire using 4JTAG   |
| 1.6 08/08 WLUT        Adapted InitTarget() for SpyBiWire capable devices   |
| 1.7 08/08 WLUT        Replaced Shift() and Shift_430X() with a common shift|
|                       function AllShifts().                                |
|                       Added Spy-Bi-Wire capability, can be switched with   |
|                       preprocessor definition SPYBIWIRE_MODE between       |
|                       4-wire or 2-wire functionality.                      |
| 1.8 05/09 GC (Elprotronic)  Added support for the new hardware - REP430F   |
| 1.9 08/09 FB          Erase SPI mode for more usability                    |
|----------------------------------------------------------------------------|
| Designed 2002 by Texas Instruments Germany                                 |
\*==========================================================================*/
//! \file LowLevelFunc430Xv2.c
//! \brief Low Level Functions regarding user's Hardware
/****************************************************************************/
/* INCLUDES                                                                 */
/****************************************************************************/

#include "LowLevelFunc430Xv2.h"

/****************************************************************************/
/* GLOBAL VARIABLES                                                         */
/****************************************************************************/

//! \brief Holds the value of TDO-bit
byte tdo_bit;
//! \brief Holds the last value of TCLK before entering a JTAG sequence
byte TCLK_saved = SBWDATO;

/****************************************************************************/
/* FUNCTIONS                                                                */
/****************************************************************************/

//  combinations of sbw-cycles (TMS, TDI, TDO)
//---------------------------------
void TMSL_TDIL(void)
{
    TMSL  TDIL  TDOsbw
}
//---------------------------------
void TMSH_TDIL(void)
{
    TMSH  TDIL  TDOsbw
}
//------------------------------------
void TMSL_TDIH(void)
{
    TMSL  TDIH  TDOsbw
}
//-------------------------------------
void TMSH_TDIH(void)
{
    TMSH  TDIH  TDOsbw
}
//------------------------------------
void TMSL_TDIH_TDOrd(void)
{
    TMSL  TDIH  TDO_RD
}
//------------------------------------
void TMSL_TDIL_TDOrd(void)
{
    TMSL  TDIL  TDO_RD
}
//------------------------------------
void TMSH_TDIH_TDOrd(void)
{
    TMSH  TDIH  TDO_RD
}
//------------------------------------
void TMSH_TDIL_TDOrd(void)
{
    TMSH  TDIL  TDO_RD
}

#ifdef SPYBIWIRE_MODE
//----------------------------------------------------------------------------
//! \brief Clear TCLK in Spy-Bi-Wire mode
//! \details enters with TCLK_saved and exits with TCLK = 0
void ClrTCLK_sbw(void)
{
    if (TCLK_saved & SBWDATO)
    {
        TMSLDH
    }
    else
    {
        TMSL
    }

    JTAGOUT &= ~SBWDATO;

    TDIL TDOsbw    //ExitTCLK
    TCLK_saved = (byte)(~SBWDATO);
}

//----------------------------------------------------------------------------
//! \brief Set TCLK in Spy-Bi-Wire mode
//! \details enters with TCLK_saved and exits with TCLK = 1
void SetTCLK_sbw(void)
{
   if (TCLK_saved & SBWDATO)
   {
        TMSLDH
   }
   else
   {
        TMSL
   }

   JTAGOUT |= SBWDATO;

   TDIH TDOsbw    //ExitTCLK
   TCLK_saved = SBWDATO;
}

//----------------------------------------------------------------------------
//! \brief Shift a value into TDI (MSB first) and simultaneously shift out a 
//! value from TDO (MSB first).
//! \param Format (number of bits shifted, 8 (F_BYTE), 16 (F_WORD), 
//! 20 (F_ADDR) or 32 (F_LONG))
//! \param Data (data to be shifted into TDI)
//! \return unsigned long (scanned TDO value)
unsigned long AllShifts(word Format, unsigned long Data)
{
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
   // shift in bits
   for (i = Format; i > 0; i--)
   {
        if (i == 1)                     // last bit requires TMS=1; TDO one bit before TDI
        {
          ((Data & MSB) == 0) ? TMSH_TDIL_TDOrd() : TMSH_TDIH_TDOrd();
        }
        else
        {
          ((Data & MSB) == 0) ? TMSL_TDIL_TDOrd() : TMSL_TDIH_TDOrd();
        }
        Data <<= 1;
        if (tdo_bit & SBWDATI)
            TDOword++;
        if (i > 1)
            TDOword <<= 1;               // TDO could be any port pin
   }
   TMSH_TDIH();                         // update IR
   if (TCLK_saved & SBWDATO)
   {
        TMSL_TDIH();
   }
   else
   {
        TMSL_TDIL();
   }

   // de-scramble bits on a 20bit shift
   if(Format == F_ADDR)
   {
     TDOword = ((TDOword << 16) + (TDOword >> 4)) & 0x000FFFFF;
   }
   
   return(TDOword);
}

#else
//----------------------------------------------------------------------------
//! \brief Shift a value into TDI (MSB first) and simultaneously shift out a 
//! value from TDO (MSB first).
//! \param Format (number of bits shifted, 8 (F_BYTE), 16 (F_WORD), 
//! 20 (F_ADDR) or 32 (F_LONG))
//! \param Data (data to be shifted into TDI)
//! \return unsigned long (scanned TDO value)
unsigned long AllShifts(word Format, unsigned long Data)
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
      if (i == 1)             // Last bit requires TMS=1
      {
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
#endif

//----------------------------------------------------------------------------
//! \brief Initialization of the Controller Board
volatile unsigned char trsl_dummy, tvcc_dummy;     /* mcu-sim */

/* mcu-sim: el F5529 de la F5529LP a 18 MHz desde REFO, como en
 * ../replicator/LowLevelFunc430.c (el F5437 del REP430F los saca de un
 * cristal de 12 MHz en XT1, que aquí no hay). */
void InitController(void)
{
    WDTCTL = WDTPW + WDTHOLD;
    SetVCoreUp( 1 );
    SetVCoreUp( 2 );
    UCSCTL3 = SELREF__REFOCLK;
    UCSCTL4 = SELA__REFOCLK + SELS__DCOCLKDIV + SELM__DCOCLKDIV;
    __bis_SR_register(SCG0);
    UCSCTL0 = 0;
    UCSCTL1 = DCORSEL_5;
    UCSCTL2 = FLLD_1 + 548;
    __bic_SR_register(SCG0);
    __delay_cycles(250000);
    do{
        UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + DCOFFG);
        SFRIFG1 &= ~OFIFG;
    }while( SFRIFG1 & OFIFG );

    TA0CTL = 0;
    TA0CTL = TASSEL_1;                  // ACLK = REFO, 32768 Hz
    TA0CCR0 = ONEMS;

    P4SEL |= BIT4 | BIT5;               // la UCA1 en P4.4/P4.5 (el PMAP de reset)
    UCA1CTL1 = UCSWRST | UCSSEL_1;
    UCA1BR0 = 3; UCA1BR1 = 0; UCA1MCTL = UCBRS_3;   // 9600 con ACLK [UG5 tabla 34-4]
    UCA1CTL1 &= ~UCSWRST;

    TRSLDIR = 0;
    P1DIR |= BIT0; P4DIR |= BIT7;       // LED1 y LED2
    P1OUT |= BIT0; P4OUT |= BIT7;       // los dos encendidos al empezar

    SetTargetVcc (0);
    SetVpp( 0 );
}

//----------------------------------------------------------------------------
//! \brief Function to set a specific voltage level via the PMM
//! \param level
void SetVCoreUp (word level)
{
        // Open PMM registers for write access
    PMMCTL0_H = 0xA5;
        // Set SVS/SVM high side new level
    SVSMHCTL = SVSHE + SVSHRVL0 * level + SVMHE + SVSMHRRL0 * level;
        // Set SVM low side to new level
    SVSMLCTL = SVSLE + SVMLE + SVSMLRRL0 * level;
        // Wait till SVM is settled
    while ((PMMIFG & SVSMLDLYIFG) == 0);
        // Clear already set flags
    PMMIFG &= ~(SVMLVLRIFG + SVMLIFG);
        // Set VCore to new level
    PMMCTL0_L = PMMCOREV0 * level;
        // Wait till new level reached
    if ((PMMIFG & SVMLIFG))
      while ((PMMIFG & SVMLVLRIFG) == 0);
        // Set SVS/SVM low side to new level
    SVSMLCTL = SVSLE + SVSLRVL0 * level + SVMLE + SVSMLRRL0 * level;
        // Lock PMM registers for write access
    PMMCTL0_H = 0x00;
}

//----------------------------------------------------------------------------
//! \brief Set target Vcc (supplied from REP430F)
//! \details input - 10*Vcc[V]-> range 2.1V to 3.6 V (data 21 to 36)
//! or data = 0 -> Vcc OFF  
//! \param level (requested VCC in V * 10)
void SetTargetVcc (word level)  //level - requested Vcc * 10 
{
    if( level == 0 )
        TVCC_EN_OUT |= TVCC_DIS_BIT; 
    else
        TVCC_EN_OUT &= ~TVCC_DIS_BIT; 
    TVCC_EN_DIR |= TVCC_DIS_BIT; 

    if( level < 21 ) level = 21;
    if( level > 36 ) level = 36;
    level = TVCC_MASK & ((level - 21)<<TVCC_SHIFT);
    TVCC_DIR |= TVCC_MASK;  
    TVCC_OUT |= TVCC_MASK;      //set min.Vcc ( 0xF0 )
    TVCC_OUT &= ~level;         //set desired Vcc - clear desired bits
    MsDelay( 50 );
}

//----------------------------------------------------------------------------
//! \brief Determine target VCC
//! \return word (target VCC in mV)
word    Get_target_Vcc(void)
{
    return( Get_Vx( ADC12INCH_14 ));
}

//----------------------------------------------------------------------------
//! \brief Determine external VCC
//! \return word (external VCC in mV)
word    Get_Ext_Vcc(void)
{
    return( Get_Vx( ADC12INCH_15 ));
}

//----------------------------------------------------------------------------
//! \brief Measure different voltages via ADC12
//! \return word (voltage in mV)
word    Get_Vx( word index )
{
  word y,x; 

    // ADC12  initialization
    UCSCTL8 |= MODOSCREQEN;     // Enable osc for ADC12 - in the Unifield Clock System
    ADC12CTL0 |= ADC12SHT0_8 + ADC12REFON + ADC12ON;    // Internal reference = 1.5V
    ADC12CTL1 = ADC12SHP;
    ADC12MCTL0 = ADC12SREF_1 + index;   // Input A14 or A15
    
    // Delay for needed ref start-up.
    usDelay(50);

    ADC12CTL0 |= ADC12ENC;  // Enable conversions
    ADC12CTL0 |= ADC12SC;   // Start conversion - sw trigger
    ADC12IFG &= ~BIT0;
    do{ 
    }while( (ADC12IFG & BIT0) == 0 );
    ADC12CTL0 &= ~ADC12ENC; // Disable ADC12

    // Vcc hardware divider - Vcc/ADCin = 3/1;
    // Ref Vcc = 1.5
    // x = x * 3 * 1.5 = x * 4.5 = x * 9 /2
    // x = x*3;

    x = (ADC12MEM0 * 9)>>1;

    // result Vcc =  x * 1000/4096   in mV
    // y = x * 1000/(4000+96) ~= x * 0.25 * 4000/(4000+96) = x * 0.25 * 1/(1+96/4000) != x * 0.25 * (1 - 96/4000)
    // y = x * 0.25 * (1 - 96/4000) ~= x/4 - x * 24/4000 = x/4 - x/167 ~= x/4 - 3*x/512 = x/4 - x/256 - x/512

    y = x>>2;           // y = x/4      
    x = x>>8;           // x = x/256
    y -= x + (x>>1);    // y = x/4 - x/256 - x/512;
    return(y);
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the TDI pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void TDI_dir(word dir)
{
    if (dir == 0) JTAGDIR &= ~TDI; else JTAGDIR |= TDI;   /* mcu-sim */
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the TDO pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void TDOI_dir(word dir)
{
    JTAGDIR &= ~TDO;                                       /* mcu-sim: siempre entrada */
    (void)dir;
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the TEST pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void TEST_dir(word dir)
{
    if (dir == 0) JTAGDIR &= ~TEST; else JTAGDIR |= TEST;  /* mcu-sim */
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the TMS pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void TMS_dir(word dir)
{
    if (dir == 0) JTAGDIR &= ~TMS; else JTAGDIR |= TMS;    /* mcu-sim */
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the RST pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void RST_dir(word dir)
{
    if (dir == 0) JTAGDIR &= ~RST; else JTAGDIR |= RST;    /* mcu-sim */
}

//----------------------------------------------------------------------------
//! \brief Set the direction for the TCK pin
//! \param dir (0 = IN - from target to REP430F, !0 = OUT)
void TCK_dir(word dir)
{
    if (dir == 0) JTAGDIR &= ~TCK; else JTAGDIR |= TCK;    /* mcu-sim */
}
//----------------------------------------------------------------------------
//! \brief function to set the fuse blow voltage Vpp
//! \param source (Select the pin to which Vpp is applied according to chosen interface)
void SetVpp(word source)
{
    if( source & (VPPONTEST | VPPONTDI ))           Enable_Vpp();
    if(( source & (VPPONTEST | VPPONTDI )) == 0 )   Disable_Vpp();

    if( source & VPPONTEST )    TEST_dir( 0 );
    if( source & VPPONTDI ) TDI_dir( 0 );

    VPPOUT &= ~( VPPONTDI | VPPONTEST );
    source &= VPPONTDI | VPPONTEST;
    VPPOUT |= source; 
    VPPDIR |= VPPONTDI | VPPONTEST;
    MsDelay( 2 );
    if(( source & VPPONTEST ) == 0 ) TEST_dir( 1 );
    if(( source & VPPONTDI ) == 0 )  TDI_dir( 1 );
}

//----------------------------------------------------------------------------
//! \brief Enable fuse blow voltage Vpp
void Enable_Vpp(void)
{
    SW_DIR |= SW_VPPEN;
    SW_OUT &= ~SW_VPPEN; 
    MsDelay( 20 );
}

//----------------------------------------------------------------------------
//! \brief Disable fuse blow voltage Vpp
void Disable_Vpp(void)
{
    SW_OUT |= SW_VPPEN; 
    SW_DIR &= ~SW_VPPEN;
}

//----------------------------------------------------------------------------
//! \brief Set JTAG pins to output direction - from REP430F to target
void configure_IO_JTAG(void)
{
    TDI_dir( 1 );
    TEST_dir( 1 );
    TMS_dir( 1 );
    RST_dir( 1 );
    TCK_dir( 1 );
}

//----------------------------------------------------------------------------
//! \brief Set SBW pins to output direction - from REP430F to target
void configure_IO_SBW(void)
{
    TDOI_dir( 1 );
    TCK_dir( 1 );
    TDI_dir( 1 );   /* mcu-sim: sin traductor, SBWTDIO es P6.5 y hay que sacarlo */
}

//----------------------------------------------------------------------------
//! \brief Set all JTAG pins to input direction - from target to REP430F
void IO_3state(void)
{
    TDI_dir( 0 );
    TDOI_dir( 0 );
    TEST_dir( 0 );
    TMS_dir( 0 );
    RST_dir( 0 );
    TCK_dir( 0 );
}

//----------------------------------------------------------------------------
//! \brief This function switches TDO to Input (used for fuse blowing)
void TDOisInput(void)
{
    TDI_dir( 0 );       // Release TDI pin on target
    TDOI_dir( 1 );      // Switch TDI --> TDO
}

//----------------------------------------------------------------------------
//! \brief Set up I/O pins for JTAG communication
void DrvSignals(void)
{
    SetVpp( 0 );  
    IO_3state();
    JTAGSEL  = 0x00;            // Pins all I/Os
#if ( INTERFACE == SPYBIWIRE_IF )
    JTAGOUT  |= TDI;
    JTAGOUT  &= ~TCK;
    configure_IO_SBW();
#else
    JTAGOUT  |=  TDI | TMS | TCK | TCLK | RST;
    JTAGOUT  &= ~ TEST;    
    configure_IO_JTAG();
#endif
}

//----------------------------------------------------------------------------
//! \brief Release I/O pins
void RlsSignals(void)
{
    SetVpp( 0 );
    Disable_Vpp();
    IO_3state();
}

//----------------------------------------------------------------------------
//! \brief Initialization of the Target Board (switch voltages on, preset JTAG 
//! pins)
//! \details For devices with normal 4wires JTAG  (JTAG4SBW=0)\n
//! For devices with Spy-Bi-Wire to work in 4wires JTAG (JTAG4SBW=1)
void InitTarget(void)
{
    DrvSignals();
    SetTargetVcc( VCC_LEVEL );  //level - requested Vcc * 10 
}

//----------------------------------------------------------------------------
//! \brief Release Target Board (switch voltages off, JTAG pins are HI-Z)
void ReleaseTarget(void)
{
    RlsSignals();
    SetTargetVcc( 0 );
}

//----------------------------------------------------------------------------
//! \brief Delay function (resolution is 1 ms)
//! \param milliseconds (number of ms, max number is 0xFFFF)
void MsDelay(word milliseconds)
{
   word i;
   for(i = milliseconds; i > 0; i--)
   {
        TA0CCTL0 &= ~CCIFG;             // Clear the interrupt flag
        TA0CTL |= TACLR+MC_1;           // Clear & start timer
        while ((TA0CCTL0 & CCIFG)==0);  // Wait until the Timer elapses
        TA0CTL &= ~MC_1;    // Stop Timer
   }
}

//----------------------------------------------------------------------------
//! \brief Delay function (resolution is ~1 us)
//! \param microseconds (number of ms, max number is 0xFFFF)
void usDelay(word microseconds)
{
    do
    {
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
#ifdef  MCLK_18MHZ
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
#endif
    }
    while (--microseconds > 0);
}

//----------------------------------------------------------------------------
//! \brief This function generates Amount strobes with the Flash Timing Generator
//! \details Frequency fFTG = 257..476kHz (t = 3.9..2.1us).
//! Used freq. in procedure - 400 kHz\n
//! User knows target frequency, instruction cycles, C implementation.\n
//! No. of MCKL cycles - 18MHz/400 kHz = 45 or 12MHz/400 kHz = 30
//! \param Amount (number of strobes to be generated)
void TCLKstrobes(word Amount)
{
    volatile word i;

    // This implementation has 45 (MCLK=18MHz)
    // or 30 (MCLK 12MHz) body cycles! -> 400kHz
    // DO NOT MODIFY IT !

    for (i = Amount; i > 0; i--)
    {
        JTAGOUT |=  TCLK;       // Set TCLK
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
#ifdef  MCLK_18MHZ
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
#endif
        JTAGOUT &= ~TCLK;       // Reset TCLK
        _NOP();
#ifdef  MCLK_18MHZ
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
        _NOP();
#endif
    }
}

//----------------------------------------------------------------------------
//! \brief This function controls the status LEDs depending on the status
//! argument. It stops program in error case.
//! \param status (4 stati - can be extended to 8 - possible for 3 LEDs - 
//! Yellow,Green,Red)
//! \param index (additional number for detailed diagnostics or watch 
//! variable during debugging phase)
static void rep_pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }   /* mcu-sim */
static void rep_cad(const char* t) { while (*t) rep_pon(*t++); }
void ShowStatus(word status, word index)
{
    /* mcu-sim: el estado también por el backchannel (los LED los ve la placa) */
    rep_cad(status == STATUS_ERROR ? "REP ERROR " : status == STATUS_OK ? "REP OK " :
            status == STATUS_ACTIVE ? "REP ACTIVO " : "REP REPOSO ");
    rep_pon('0' + (index / 10) % 10); rep_pon('0' + index % 10); rep_pon('\n');
    All_LEDs_off();                     
    switch (status)
    {
        case STATUS_ERROR:
            LED_red_on();               // Switch red LED on
            ReleaseTarget();            // Voltages off, JTAG HI-Z
            while(index);               // Stop program, index must be > 0
        case STATUS_ACTIVE:;            // Switch yellow LEDs on
            LED_yellow_on();
             break;
        case STATUS_OK:                 // Switch green LED on
            LED_green_on();
             break;
        case STATUS_IDLE:;              // Keep LEDs switched off
    }
}                                       // return if active, idle, ok

//----------------------------------------------------------------------------
//! \brief This function performs a Trigger Pulse for test/development
//! \param mode
#ifdef DEBUG
void TriggerPulse(word mode)
{
    switch (mode)
    {
        case 1: LEDOUT  |=  TRIGGER;    // mode = 1: set trigger
                break;
        case 2: LEDOUT  |=  TRIGGER;    // mode = 2: set/reset trigger
        case 0: LEDOUT  &= ~TRIGGER;    // mode = 0: reset trigger
    }       
}
#endif

/****************************************************************************/
/*                         END OF SOURCE FILE                               */
/****************************************************************************/
