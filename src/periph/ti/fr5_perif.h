// =============================================================================
// fr5_perif.h — Las caras de FR5xx/FR6xx de módulos que ya estaban (M8 de
// doc/analisis_msp430.md)
//
//   RefAFr   el REF_A del MSP432P4 (ref_a.h) con las tensiones de las FR5xx:
//            REFVSEL 00 1,2 V, 01 2,0 V, 1x 2,5 V [UG FR5 SLAU367P §33.3.1]
//   RtcB     el RTC_A de las 5xx (rtc_a.h) solo en calendario [UG FR5 cap.
//            29]: RTCCTL1.5 (el RTCMODE del RTC_A) está reservado y se lee 0;
//            RTCSSEL no existe (cuenta el LFXT por ACLK); BIN2BCD y BCD2BIN en
//            +1C/+1E (los del RTC_C). RTCOFIFG (la falta del LFXT) no se
//            modela: el RTC lee la frecuencia que le dé el SoC.
//   RtcC5    el RTC_C del MSP432P4 (rtc_c.h), el de las FR5994/FR6989 [UG FR5
//            cap. 30]: el mismo bloque, en RTC_BASE (0x4A0)
//   Crc32Fr  el CRC32 del MSP432P4 (crc32.h) con el mapa de las FR5994 [DS
//            SLASE54D tabla 6-56]: las palabras de DIRB y RESR van al revés
//            (W1 primero), y DIRB16 en +16
//
// Rtc5If: lo que el SoC necesita de cualquiera de los dos RTC (el despertar
// de LPM3.5 y el reset de su dominio).
// =============================================================================
#ifndef STM32_PERIPH_TI_FR5_PERIF_H
#define STM32_PERIPH_TI_FR5_PERIF_H

#include "ref_a.h"
#include "rtc_a.h"
#include "crc32.h"

namespace stm32 {

class RefAFr : public RefAP4 {
public:
    explicit RefAFr(sc_core::sc_module_name nm, uint32_t base) : RefAP4(nm, base, 0x10) {}
    // La tensión de la referencia: 0 si no está activa
    double v() const {
        if (!activa()) return 0.0;
        const unsigned s = (ctl0_ >> 4) & 3u;
        return s == 0 ? 1.2 : s == 1 ? 2.0 : 2.5;
    }
    uint16_t lee(bool dbg = true) { return lee16(0, dbg); }
};

struct Rtc5If {
    virtual ~Rtc5If() = default;
    virtual bool en_marcha5() const = 0;
    virtual bool pide() const = 0;
    virtual void reset_bor() = 0;
    virtual void recalcula5() = 0;
};

class RtcC5 : public RtcCP4, public Rtc5If {
public:
    explicit RtcC5(sc_core::sc_module_name nm, uint32_t base) : RtcCP4(nm, base, 0x20) {}
    bool en_marcha5() const override { return en_marcha(); }
    bool pide() const override {
        return (ctl0_ & ctl0_ >> 4 & 0x0Fu) || ((ps0ctl_ & PSIFG) && (ps0ctl_ & PSIE)) || ((ps1ctl_ & PSIFG) && (ps1ctl_ & PSIE));
    }
    void reset_bor() override { valores_de_reset(); irq_ev_.notify(sc_core::SC_ZERO_TIME); ev_.notify(sc_core::SC_ZERO_TIME); }
    void recalcula5() override { recalcula(); }
};

class Crc32Fr : public Crc32P4 {
public:
    explicit Crc32Fr(sc_core::sc_module_name nm, uint32_t base) : Crc32P4(nm, base, 0x20) {}
protected:
    static uint32_t mapa(uint32_t off) {
        switch (off) {
        case 0x04: return 0x06; case 0x06: return 0x04;     // DIRBW1/DIRBW0
        case 0x0C: return 0x0E; case 0x0E: return 0x0C;     // RESRW1/RESRW0
        case 0x16: return 0x14;                             // CRC16DIRBW0
        default: return off;
        }
    }
    uint16_t lee16(uint32_t off, bool dbg) override { return Crc32P4::lee16(mapa(off), dbg); }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override { Crc32P4::escribe16(mapa(off), v, m); }
};

class RtcB : public RtcA, public Rtc5If {
public:
    explicit RtcB(sc_core::sc_module_name nm, uint32_t base) : RtcA(nm, base, 0x20) {}
    // El reset del dominio del RTC (un BOR de verdad, no el despertar de
    // LPM3.5): el SoC lo ata a un rst_n propio que no baja
    bool en_marcha5() const override { return en_marcha(); }
    void recalcula5() override { recalcula_a(); }
    void reset_bor() override {
        valores_de_reset(); a_valores(); calendario_ = true;
        irq_ev_.notify(sc_core::SC_ZERO_TIME); ev_.notify(sc_core::SC_ZERO_TIME); ev_cnt_.notify(sc_core::SC_ZERO_TIME);
    }
    // ¿Cuenta? (el despertar de LPM3.5 lo necesita en marcha)
    bool en_marcha() const { return calendario_ && !hold_; }
    bool pide() const override {
        return (ctl0_ & ctl0_ >> 4 & 0x07u) || ((ps0ctl_ & PSIFG) && (ps0ctl_ & PSIE)) || ((ps1ctl_ & PSIFG) && (ps1ctl_ & PSIE));
    }

protected:
    uint16_t lee16(uint32_t off, bool dbg) override {
        calendario_ = true;
        if (off == 0x1C || off == 0x1E) return RtcCP4::lee16(off, dbg);
        const uint16_t v = RtcA::lee16(off, dbg);
        return off == 0x00 ? uint16_t(v & ~0x2000u) : v;
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        calendario_ = true;
        if (off == 0x1C || off == 0x1E) { RtcCP4::escribe16(off, v, m); return; }
        if (off == 0x00 && (m & 0xFF00u)) v = uint16_t(v | 0x2000u);   // siempre calendario
        RtcA::escribe16(off, v, m);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_FR5_PERIF_H
