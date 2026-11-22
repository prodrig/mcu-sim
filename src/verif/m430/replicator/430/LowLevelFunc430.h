// =============================================================================
// LowLevelFunc430.h — DE mcu-sim, NO de TI: sustituye al LowLevelFunc430.h /
// LowLevelFunc430.c del Replicator430 (que mueven el puerto 5 de un
// MSP430F5437) por el puerto simulado de hil_rep.h. JTAGfunc430.c y
// Devices430.c, que sí son de TI, lo incluyen por su nombre y no cambian.
//
// Las funciones de abajo son las de LowLevelFunc430.c 1.9 del Replicator430
// (Copyright (C) 2016 Texas Instruments Incorporated, BSD-3: véase
// Readme_TI.txt y la cabecera de JTAGfunc430.c), con el mismo orden de
// escrituras en el puerto y los mismos `_NOP()`: lo único distinto es que el
// puerto es un objeto. Lo que no toca los pines del chip (los LED, el ADC de
// las tensiones, la configuración del F5437) no está.
//
// Se incluye DENTRO de un espacio de nombres (replicador430.h), una vez por
// interfaz: REP_INTERFACE dice cuál (1 JTAG, 2 JTAG en un chip con SBW, 3 SBW).
// =============================================================================

#undef INTERFACE
#define INTERFACE REP_INTERFACE
#define JTAG_IF             1
#define SPYBIWIREJTAG_IF    2
#define SPYBIWIRE_IF        3
#if (INTERFACE == SPYBIWIRE_IF)
#define SPYBIWIRE_MODE
#endif

#ifndef __DATAFORMATS__
#define __DATAFORMATS__
#define F_BYTE                     8
#define F_WORD                     16
#define F_ADDR                     20
#define F_LONG                     32
#endif
#define STATUS_ERROR     0
#define STATUS_OK        1
#define STATUS_FUSEBLOWN 2
#define STATUS_ACTIVE    2
#define STATUS_IDLE      3

// VerifyPSA es `static` en la rama de SBW y no en el prototipo: lo acepta el
// compilador de TI y no el de C++. Con otro nombre, las dos cosas valen.
#define VerifyPSA VerifyPSA_rep

// --- El puerto ------------------------------------------------------------------
inline ::stm32::HilRep* hil = nullptr;
// La traza, en el formato de la sonda de bancada (m430/sonda): la IR con su
// código de [JTAG] (el que se desplaza es el de JTAGfunc430.h, al revés)
inline void rep_apunta_shift(word f, word ent, word sal) {
    char b[32];
    if (f == F_BYTE) {
        unsigned r = 0;
        for (unsigned i = 0; i < 8; ++i) if ((ent >> i) & 1u) r |= 0x80u >> i;
        std::snprintf(b, sizeof b, "I%02X>%02X", r, unsigned(sal) & 0xFFu);
    } else std::snprintf(b, sizeof b, "D%u %04X>%04X", unsigned(f), unsigned(ent), unsigned(sal));
    hil->apunta(b);
}
struct PuertoRep {
    PuertoRep& operator|=(unsigned b) { hil->salida(static_cast<unsigned char>(hil->salida() | b)); return *this; }
    PuertoRep& operator&=(unsigned b) { hil->salida(static_cast<unsigned char>(hil->salida() & b)); return *this; }
    operator unsigned() const { return hil->salida(); }
};
struct EntradaRep { operator unsigned() const { return hil->entrada(); } };
struct TraductorRep {
    unsigned v = 0;
    TraductorRep& operator|=(unsigned b) { v |= b; hil->tdoi_entrada((v & 0x40) != 0); return *this; }
    TraductorRep& operator&=(unsigned b) { v &= b; hil->tdoi_entrada((v & 0x40) != 0); return *this; }
};
inline PuertoRep JTAGOUT;
inline EntradaRep JTAGIN;
inline TraductorRep TRSLDIR;
#define TDOI_DIR        0x40

#define TMS             0x20
#define TDI             0x80
#define TDO             0x40
#define TCK             0x10
#define TEST            0x04
#define RST             0x08
#define TCLK            TDI

#define _NOP()   hil->nop()
#define _DINT()  ((void)0)
#define _EINT()  ((void)0)

#define ClrTMS()    ((JTAGOUT) &= (~TMS))
#define SetTMS()    ((JTAGOUT) |= (TMS))
#define ClrTDI()    ((JTAGOUT) &= (~TDI))
#define SetTDI()    ((JTAGOUT) |= (TDI))
#define ClrTCK()    ((JTAGOUT) &= (~TCK))
#define SetTCK()    ((JTAGOUT) |= (TCK))
#define StoreTCLK() ((JTAGOUT  &   TCLK))
#define RestoreTCLK(x)  (x == 0 ? (JTAGOUT &= ~TCLK) : (JTAGOUT |= TCLK))
#define ScanTDO()   ((JTAGIN   &   TDO))
#define SetRST()    ((JTAGOUT) |= (RST))
#define ClrRST()    ((JTAGOUT) &= (~RST))
#define SetTST()    ((JTAGOUT) |= (TEST))
#define ClrTST()    ((JTAGOUT) &= (~TEST))
#define VPPon(x)    (hil->vpp_en_test(true))
#define VPPoff()    (hil->vpp_en_test(false))

#define SBW_DELAY   { _NOP();_NOP();_NOP();_NOP();_NOP();}
#define   SBWDATO   TDI
#define   SBWDATI   TDO
#define   SBWCLK    TCK
#define   TMSH    JTAGOUT |= SBWDATO;  SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWCLK;
#define   TMSL    JTAGOUT &= ~SBWDATO; SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWCLK;
#define   TMSLDH  JTAGOUT &= ~SBWDATO;  SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWDATO; JTAGOUT |= SBWCLK;
#define   TDIH    JTAGOUT |= SBWDATO;   SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWCLK;
#define   TDIL    JTAGOUT &= ~SBWDATO;  SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWCLK;
#define   TDOsbw  TRSLDIR |= TDOI_DIR;  SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; JTAGOUT |= SBWCLK; TRSLDIR &= ~TDOI_DIR;
#define   TDO_RD  TRSLDIR |= TDOI_DIR;  SBW_DELAY; JTAGOUT &= ~SBWCLK; SBW_DELAY; tdo_bit = JTAGIN; SBW_DELAY; JTAGOUT |= SBWCLK; TRSLDIR &= ~TDOI_DIR;
#define   SetSBWTCK()     (JTAGOUT |=   SBWCLK)
#define   ClrSBWTCK()     (JTAGOUT &=  ~SBWCLK)
#define   SetSBWTDIO()    (JTAGOUT |=   SBWDATO)
#define   ClrSBWTDIO()    (JTAGOUT &=  ~SBWDATO)

inline byte TCLK_saved = SBWDATO;
inline unsigned tdo_bit = 0;

inline void ClrTCLK_sbw(void);
inline void SetTCLK_sbw(void);
#ifdef SPYBIWIRE_MODE
#define ClrTCLK()  (hil->apunta("T0"), ClrTCLK_sbw())
#define SetTCLK()  (hil->apunta("T1"), SetTCLK_sbw())
#else
#define ClrTCLK()  (hil->apunta("T0"), ((JTAGOUT) &= ~(TCLK)))
#define SetTCLK()  (hil->apunta("T1"), ((JTAGOUT) |=  (TCLK)))
#endif

// --- Las funciones de LowLevelFunc430.c -----------------------------------------
inline void MsDelay(word milliseconds) { hil->espera_us(1000.0 * milliseconds); }
inline void usDelay(word microseconds) { hil->nop(14u * (microseconds ? microseconds : 1u)); hil->espera_us(0); }

// Las de SBW existen siempre (CheckJtagFuse_SBW las nombra en las dos interfaces)
inline void TMSL_TDIL(void) { TMSL  TDIL  TDOsbw }
inline void TMSH_TDIL(void) { TMSH  TDIL  TDOsbw }
inline void TMSL_TDIH(void) { TMSL  TDIH  TDOsbw }
inline void TMSH_TDIH(void) { TMSH  TDIH  TDOsbw }
inline void TMSL_TDIH_TDOrd(void) { TMSL  TDIH  TDO_RD }
inline void TMSL_TDIL_TDOrd(void) { TMSL  TDIL  TDO_RD }
inline void TMSH_TDIH_TDOrd(void) { TMSH  TDIH  TDO_RD }
inline void TMSH_TDIL_TDOrd(void) { TMSH  TDIL  TDO_RD }

inline void ClrTCLK_sbw(void)
{
    if (TCLK_saved & SBWDATO) { TMSLDH } else { TMSL }
    JTAGOUT &= ~SBWDATO;
    TDIL TDOsbw
    TCLK_saved = (byte)(~SBWDATO);
}
inline void SetTCLK_sbw(void)
{
    if (TCLK_saved & SBWDATO) { TMSLDH } else { TMSL }
    JTAGOUT |= SBWDATO;
    TDIH TDOsbw
    TCLK_saved = SBWDATO;
}
#ifdef SPYBIWIRE_MODE
inline void IR_Ex_Blow_SBW_Shift(void)
{
    word MSB;
    word Data, i;
    Data = IR_EX_BLOW;
    if (TCLK_saved & SBWDATO) TMSH_TDIH(); else TMSH_TDIL();
    TMSH_TDIH();
    TMSL_TDIH();
    TMSL_TDIH();
    MSB = 0x80;
    for (i = 8; i > 1; i--) { ((Data & MSB) == 0) ? TMSL_TDIL_TDOrd() : TMSL_TDIH_TDOrd(); Data <<= 1; }
    ((Data & MSB) == 0) ? TMSH_TDIL_TDOrd() : TMSH_TDIH_TDOrd();
    TMSH_TDIL();
    TMSL_TDIL();
    TMSL
}
inline word Shift(word Format, word Data)
{
   word TDOword = 0x0000;
   word MSB = 0x0000;
   word i;
   const word ent = Data;
   (Format == F_WORD) ? (MSB = 0x8000) : (MSB = 0x80);
   for (i = Format; i > 0; i--)
   {
        if (i == 1) { ((Data & MSB) == 0) ? TMSH_TDIL_TDOrd() : TMSH_TDIH_TDOrd(); }
        else        { ((Data & MSB) == 0) ? TMSL_TDIL_TDOrd() : TMSL_TDIH_TDOrd(); }
        Data <<= 1;
        if (tdo_bit & SBWDATI) TDOword++;
        if (i > 1) TDOword <<= 1;
   }
   TMSH_TDIH();
   if (TCLK_saved & SBWDATO) TMSL_TDIH(); else TMSL_TDIL();
   rep_apunta_shift(Format, ent, TDOword);
   return(TDOword);
}
inline void TCLKstrobes(word Amount)
{
   word i;
   if (TCLK_saved & SBWDATO) { TMSLDH } else { TMSL }
   for (i = Amount; i > 0; i--)
   {
        JTAGOUT &= ~SBWDATO;
        hil->nop(18);
        JTAGOUT |= SBWDATO;
        hil->nop(8);
   }
   TDIH TDOsbw
   TCLK_saved = SBWDATO;
}
#else
inline word Shift(word Format, word Data)
{
    word tclk = StoreTCLK();
    word TDOword = 0x0000;
    word MSB = 0x0000;
    const word ent = Data;
    word i;
   (Format == F_WORD) ? (MSB = 0x8000) : (MSB = 0x80);
   for (i = Format; i > 0; i--)
   {
       ((Data & MSB) == 0) ? ClrTDI() : SetTDI();
        Data <<= 1;
        if (i == 1) SetTMS();
        ClrTCK();
        SetTCK();
        TDOword <<= 1;
        if (ScanTDO() != 0) TDOword++;
    }
    RestoreTCLK(tclk);
    ClrTCK();
    SetTCK();
    ClrTMS();
    ClrTCK();
    SetTCK();
    rep_apunta_shift(Format, ent, TDOword);
    return(TDOword);
}
inline void TCLKstrobes(word Amount)
{
    word i;
    for (i = Amount; i > 0; i--)
    {
        JTAGOUT |=  TCLK;
        hil->nop(18);
        JTAGOUT &= ~TCLK;
        hil->nop(8);
    }
}
#endif

inline void TDOisInput(void) {}
inline void DrvSignals(void)
{
    hil->vpp_en_test(false);
    hil->conduce(true);
#if ( INTERFACE == SPYBIWIRE_IF )
    JTAGOUT  |= TDI;
    JTAGOUT  &= ~TCK;
#else
    JTAGOUT  |=  TDI | TMS | TCK | TCLK | RST;
    JTAGOUT  &= ~ TEST;
#endif
}
inline void RlsSignals(void) { hil->tres_estados(); }
inline void InitTarget(void) { DrvSignals(); if (hil->alimenta) hil->alimenta(true); }
inline void ReleaseTarget(void) { RlsSignals(); if (hil->alimenta) hil->alimenta(false); }
inline void InitController(void) {}
inline void ShowStatus(word, word) {}
