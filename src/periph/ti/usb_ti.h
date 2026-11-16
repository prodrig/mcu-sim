// =============================================================================
// usb_ti.h — El módulo USB de los MSP430F55xx/F563x/F663x [UG5 SLAU208Q
// cap. 42] (M5 de doc/analisis_msp430.md)
//
// Full Speed, solo dispositivo, con su PLL de 48 MHz, su LDO de 3,3 V y la
// UBM (el gestor de búferes) que contesta los paquetes. Los registros:
//
//   0x900 USBKEYPID   0x9628 abre los de configuración (0x902-0x915); se lee
//                     0xA528 si están abiertos [de modelo: 0x0000 si no]
//   0x902 USBCNF      USB_EN, PUR_EN (la placa pone 1,5 kOhm de PUR a D+),
//                     PUR_IN, BLKRDY, FNTEN
//   0x904 USBPHYCTL   se guarda (PU.0/PU.1 como E/S: no se modela)
//   0x908 USBPWRCTL   VUOVLIFG, VBONIFG, VBOFFIFG, USBBGVBV (VBUS válido),
//                     [de modelo: VBONIFG/VBOFFIFG con cada cambio de VBUS, y
//                     VBONIFG al habilitar VBONIE con VBUS ya puesto: la pila
//                     de TI conecta en esa interrupción]
//                     USBDETEN, OVLAOFF, SLDOAON, sus IE, VUSBEN, SLDOEN
//   0x910 USBPLLCTL   UPCS (0: XT2, 1: XT1 [de modelo, el comentario de
//                     USB_enable de TI]), UCLKSEL, UPLLEN, UPFDEN
//   0x912 USBPLLDIVB  UPMB (×(M+1)) y UPQB (÷1, 2, 3, 4, 6, 8, 12, 16)
//   0x914 USBPLLIR    OOL, LOS, OOR y sus IE: LOS sin referencia, OOR si la
//                     salida no da 48 MHz (±1 %)
//   0x920-0x923       IEPCNF_0, IEPCNT_0, OEPCNF_0, OEPCNT_0 (el EP0)
//   0x92E/0x92F       USBIEPIE/USBOEPIE; 0x930/0x931 USBIEPIFG/USBOEPIFG
//   0x932 USBVECINT   la fuente de más prioridad; leerlo la borra (salvo
//                     SETUPIFG, que borra el programa) [UG5 §42.3.?]
//   0x936 USBMAINT, 0x938 USBTSREG (se guardan), 0x93A USBFN (la trama)
//   0x93C USBCTL      DIR, FRSTE, RWUP, FEN
//   0x93D USBIE, 0x93E USBIFG  STPOW, SETUP, RESR, SUSR, RSTR
//   0x93F USBFUNADR
//
// En la RAM de USB (0x1C00-0x23FF, la del SoC): el SETUP en USBSUBLK
// (0x2380), los búferes del EP0 (IN 0x2378, OUT 0x2370) y los bloques de
// los EP1-7 (IN desde 0x23C8, OUT desde 0x2388, ocho bytes cada uno: CNF,
// BBAX, BCTX, -, -, BBAY, BCTY, SIZXY). Los búferes X e Y están en
// 0x1C00 + 8·BBA.
//
// LA UBM, por paquete (usb_dev_if, la frontera del proyecto: periph/otg.h):
//   * SETUP al EP0: siempre se acepta; a USBSUBLK, SETUPIFG (STPOWIFG si ya
//     estaba), los dos NAK del EP0 a uno y sus STALL a cero, TOGGLE a uno;
//   * con SETUPIFG puesto, el EP0 contesta NAK;
//   * IN: STALL, NAK (sin datos), o los datos del búfer (X o Y según TOGGLE
//     con DBUF), y luego NAK, TOGGLE cambia y, con USBIIE, su IFG;
//   * OUT: igual, al búfer libre (NAK a cero); el contador queda con NAK.
//   El reset del bus (SE0 más de 2,5 us en D+/D-) da RSTRIFG (y con FRSTE
//   la dirección a 0); 3 ms sin SOF ni reset, SUSRIFG; la actividad estando
//   suspendido, RESRIFG.
//
// NO CONTESTA sin USB_EN, sin FEN, sin la PLL enganchada (UPLLEN, 48 MHz) o
// con UCLKSEL distinto de PLLCLK.
//
// LO QUE NO ESTÁ [de modelo]: los isócronos, el temporizador de USB y la
// marca de tiempo (se guardan), la reanudación remota (RWUP), el LDO con
// sus sobrecargas (VUOVLIFG nunca), y PU.0/PU.1 como E/S.
// =============================================================================
#ifndef STM32_PERIPH_TI_USB_TI_H
#define STM32_PERIPH_TI_USB_TI_H

#include <systemc>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include "../otg.h"
#include "../../common/analog_net.h"

namespace stm32 {

SC_MODULE(UsbTi), public usb_dev_if {
    std::function<uint8_t&(uint32_t)> ram;      // la RAM de USB del SoC
    std::function<double(unsigned)> f_ref;      // 0: XT2, 1: XT1 (Hz; 0 si no hay)
    std::function<void()> al_cambiar;           // la petición de interrupción
    std::function<void()> al_bus;               // actividad: que el SoC mire su LPM

    UsbTi(sc_core::sc_module_name nm, analog_net_if& dp, analog_net_if& dm)
        : sc_core::sc_module(nm), dp_(dp), dm_(dm) {
        id_pur_ = dp_.register_driver((std::string(name()) + "_pur").c_str());
        dp_.set_hiz(id_pur_);
        SC_HAS_PROCESS(UsbTi);
        SC_METHOD(linea_proc); sensitive << dp_.value_changed_event() << dm_.value_changed_event() << ev_se0_;
        dont_initialize();
        SC_THREAD(suspension_proc);
        reset();
    }
    void reset() {
        abierto_ = false;
        cnf_ = 0; phy_ = 0; pwr_ = 0x1850; pll_ = 0; plldiv_ = 0; pllir_ = 0;
        iepcnf0_ = oepcnf0_ = 0; iepcnt0_ = oepcnt0_ = 0x80;
        iepie_ = oepie_ = iepifg_ = oepifg_ = 0;
        maint_ = ts_ = fn_ = 0; ctl_ = ie_ = ifg_ = 0; funadr_ = funadr_ant_ = 0;
        suspendido_ = false;
        pur();
        mira_pll();
    }
    // VBUS (el cable): lo pone quien enchufa
    void vbus(bool v) {
        if (v == vbus_) return;
        vbus_ = v;
        pwr_ |= v ? VBONIFG : VBOFFIFG;
        avisa();
    }
    bool hay_vbus() const { return vbus_; }
    // Los relojes han cambiado (XT1/XT2): la PLL se vuelve a mirar
    void relojes() { mira_pll(); }

    uint8_t lee8(uint32_t a, bool dbg) {
        switch (a) {
            case 0x900: return abierto_ ? 0x28 : 0x00;
            case 0x901: return abierto_ ? 0xA5 : 0x00;
            case 0x902: return uint8_t(cnf_);
            case 0x903: return uint8_t(cnf_ >> 8);
            case 0x904: return uint8_t(phy_);
            case 0x905: return uint8_t(phy_ >> 8);
            case 0x908: return uint8_t(pwr_lee());
            case 0x909: return uint8_t(pwr_lee() >> 8);
            case 0x910: return uint8_t(pll_);
            case 0x911: return uint8_t(pll_ >> 8);
            case 0x912: return uint8_t(plldiv_);
            case 0x913: return uint8_t(plldiv_ >> 8);
            case 0x914: return uint8_t(pllir_);
            case 0x915: return uint8_t(pllir_ >> 8);
            case 0x920: return iepcnf0_;
            case 0x921: return iepcnt0_;
            case 0x922: return oepcnf0_;
            case 0x923: return oepcnt0_;
            case 0x92E: return iepie_;
            case 0x92F: return oepie_;
            case 0x930: return iepifg_;
            case 0x931: return oepifg_;
            case 0x932: { const uint8_t v = vector(); if (!dbg) borra(v); return v; }
            case 0x933: return 0;
            case 0x936: return uint8_t(maint_);
            case 0x937: return uint8_t(maint_ >> 8);
            case 0x938: return uint8_t(ts_);
            case 0x939: return uint8_t(ts_ >> 8);
            case 0x93A: return uint8_t(fn_);
            case 0x93B: return uint8_t(fn_ >> 8);
            case 0x93C: return ctl_;
            case 0x93D: return ie_;
            case 0x93E: return ifg_;
            case 0x93F: return funadr_;
            default: return 0;
        }
    }
    void escribe8(uint32_t a, uint8_t v) {
        auto b = [&](uint16_t& r, bool alto, uint16_t m) {
            r = alto ? uint16_t((r & 0x00FF) | ((v << 8) & m)) : uint16_t((r & 0xFF00) | (v & m));
        };
        if (a == 0x900) { key_lo_ = v; return; }
        if (a == 0x901) { abierto_ = (uint16_t(v << 8) | key_lo_) == 0x9628; return; }
        if (a >= 0x902 && a < 0x916 && !abierto_) return;            // cerrados
        switch (a) {
            case 0x902: case 0x903: b(cnf_, a & 1, 0x001B); pur(); mira_pll(); avisa(); return;
            case 0x904: case 0x905: b(phy_, a & 1, 0x01A3); return;
            case 0x908: {
                // Las IFG se borran con un 0; USBBGVBV es de solo lectura
                if (!(a & 1)) pwr_ = uint16_t((pwr_ & 0xFF00) | (pwr_ & 0x07 & v) | (v & 0x70));
                else pon_pwr_alto(v);
                avisa();
                return;
            }
            case 0x909: pon_pwr_alto(v); avisa(); return;
            case 0x910: case 0x911: b(pll_, a & 1, 0x03C7); mira_pll(); avisa(); return;
            case 0x912: case 0x913: b(plldiv_, a & 1, 0x073F); mira_pll(); return;
            case 0x914: pllir_ = uint16_t((pllir_ & 0xFF00) | (v & 0x07)); mira_pll(); avisa(); return;
            case 0x915: pllir_ = uint16_t((pllir_ & 0x00FF) | ((v << 8) & 0x0700)); avisa(); return;
            case 0x920: iepcnf0_ = uint8_t(v & 0xAC); return;
            case 0x921: iepcnt0_ = uint8_t(v & 0x8F); nota_in(); return;
            case 0x922: oepcnf0_ = uint8_t(v & 0xAC); return;
            case 0x923: oepcnt0_ = uint8_t(v & 0x8F); return;
            case 0x92E: iepie_ = v; avisa(); return;
            case 0x92F: oepie_ = v; avisa(); return;
            case 0x930: iepifg_ = v; avisa(); return;
            case 0x931: oepifg_ = v; avisa(); return;
            case 0x932: case 0x933: return;
            case 0x936: case 0x937: b(maint_, a & 1, 0xEF03); return;
            case 0x938: case 0x939: b(ts_, a & 1, 0xFFFF); return;
            case 0x93A: case 0x93B: return;
            case 0x93C: ctl_ = uint8_t(v & 0x71); return;
            case 0x93D: ie_ = uint8_t(v & 0xE5); avisa(); return;
            case 0x93E: ifg_ = uint8_t(ifg_ & v); avisa(); return;     // se borran con un 0
            case 0x93F: funadr_ant_ = funadr_; funadr_ = uint8_t(v & 0x7F); return;
            default: return;
        }
    }

    bool pide() const { return vector() != 0; }
    // Un IN listo: el SoC avisa cuando el programa escribe en los bloques de
    // los EP de entrada (0x23C8-0x23FF) o en IEPCNT_0. El puente lo usa como
    // un PC, que pide IN de bulk sin parar (PuenteUsbCdc::ev_in)
    const sc_core::sc_event& ev_in() const { return ev_in_; }
    void nota_in() { ev_in_.notify(sc_core::SC_ZERO_TIME); }
    bool pll_enganchada() const { return pll_ok_; }
    bool funciona() const { return (cnf_ & USB_EN) && pll_ok_ && !(pll_ & 0x00C0) && (ctl_ & FEN); }
    uint64_t paquetes() const { return n_paq_; }

    // --- usb_dev_if: el anfitrión -------------------------------------------
    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep, const std::vector<uint8_t>& salida,
                        std::vector<uint8_t>& entrada) override {
        entrada.clear();
        actividad();
        if (!funciona()) { aviso_mudo(); return PID_NADIE; }
        if (addr != funadr_ && addr != funadr_ant_) return PID_NADIE;
        if (addr == funadr_) funadr_ant_ = funadr_;                   // la nueva ya vale
        ++n_paq_;
        ep &= 0x0F;
        if (ep > 7) return PID_NADIE;
        if (pid == PID_SETUP) {
            if (ep != 0 || salida.size() != 8) return PID_NADIE;
            for (unsigned i = 0; i < 8; ++i) ram(0x2380 + i) = salida[i];
            if (ifg_ & SETUPIFG) ifg_ |= STPOWIFG;
            ifg_ |= SETUPIFG;
            iepcnt0_ |= NAK; oepcnt0_ |= NAK;
            iepcnf0_ = uint8_t((iepcnf0_ & ~STALL) | TOGGLE);
            oepcnf0_ = uint8_t((oepcnf0_ & ~STALL) | TOGGLE);
            avisa();
            return PID_ACK;
        }
        if (ep == 0) {
            if (pid == PID_IN) {
                if (!(iepcnf0_ & UBME)) return PID_NADIE;
                if (iepcnf0_ & STALL) return PID_STALL;
                if ((iepcnt0_ & NAK) || (ifg_ & SETUPIFG)) return PID_NAK;
                const unsigned n = std::min(8u, unsigned(iepcnt0_ & 0x0F));
                for (unsigned i = 0; i < n; ++i) entrada.push_back(ram(0x2378 + i));
                iepcnt0_ |= NAK;
                iepcnf0_ ^= TOGGLE;
                if (iepcnf0_ & USBIIE) iepifg_ |= 1;
                avisa();
                return PID_ACK;
            }
            if (pid == PID_OUT) {
                if (!(oepcnf0_ & UBME)) return PID_NADIE;
                if (oepcnf0_ & STALL) return PID_STALL;
                if ((oepcnt0_ & NAK) || (ifg_ & SETUPIFG)) return PID_NAK;
                const unsigned n = std::min<unsigned>(8u, unsigned(salida.size()));
                for (unsigned i = 0; i < n; ++i) ram(0x2370 + i) = salida[i];
                oepcnt0_ = uint8_t(NAK | n);
                oepcnf0_ ^= TOGGLE;
                if (oepcnf0_ & USBIIE) oepifg_ |= 1;
                avisa();
                return PID_ACK;
            }
            return PID_NADIE;
        }
        // EP1-7: su bloque en la RAM de USB
        const bool in = pid == PID_IN;
        const uint32_t blq = (in ? 0x23C8u : 0x2388u) + 8u * (ep - 1u);
        uint8_t& cnf = ram(blq);
        if (!(cnf & UBME)) return PID_NADIE;
        if (cnf & STALL) return PID_STALL;
        const bool y = (cnf & DBUF) && (cnf & TOGGLE);
        uint8_t& cnt = ram(blq + (y ? 6u : 2u));
        const uint32_t buf = 0x1C00u + 8u * ram(blq + (y ? 5u : 1u));
        if (in) {
            if (cnt & NAK) return PID_NAK;
            const unsigned n = cnt & 0x7Fu;
            for (unsigned i = 0; i < n; ++i) entrada.push_back(ram(buf + i));
            cnt |= NAK;
            cnf ^= TOGGLE;
            if (cnf & USBIIE) iepifg_ |= uint8_t(1u << ep);
        } else {
            if (pid != PID_OUT) return PID_NADIE;
            if (cnt & NAK) return PID_NAK;
            const unsigned tam = ram(blq + 7) & 0x7Fu;
            const unsigned n = std::min<unsigned>(tam ? tam : 64u, unsigned(salida.size()));
            for (unsigned i = 0; i < n; ++i) ram(buf + i) = salida[i];
            cnt = uint8_t(NAK | n);
            cnf ^= TOGGLE;
            if (cnf & USBIIE) oepifg_ |= uint8_t(1u << ep);
        }
        avisa();
        return PID_ACK;
    }
    void sof(uint16_t trama) override {
        actividad();
        fn_ = uint16_t(trama & 0x7FF);
    }

private:
    enum : uint16_t { USB_EN = 0x01, PUR_EN = 0x02,
                      VUOVLIFG = 0x01, VBONIFG = 0x02, VBOFFIFG = 0x04, BGVBV = 0x08, DETEN = 0x10,
                      VUSBEN = 0x0800,
                      UPLLEN = 0x0100 };
    enum : uint8_t { USBIIE = 0x04, STALL = 0x08, DBUF = 0x10, TOGGLE = 0x20, UBME = 0x80, NAK = 0x80,
                     DIR = 0x01, FRSTE = 0x10, FEN = 0x40,
                     STPOWIFG = 0x01, SETUPIFG = 0x04, RESRIFG = 0x20, SUSRIFG = 0x40, RSTRIFG = 0x80 };
    analog_net_if &dp_, &dm_;
    int id_pur_ = -1;
    bool abierto_ = false, vbus_ = false, pll_ok_ = false, suspendido_ = false, avisado_mudo_ = false;
    uint8_t key_lo_ = 0;
    uint16_t cnf_ = 0, phy_ = 0, pwr_ = 0x1850, pll_ = 0, plldiv_ = 0, pllir_ = 0, maint_ = 0, ts_ = 0, fn_ = 0;
    uint8_t iepcnf0_ = 0, iepcnt0_ = 0x80, oepcnf0_ = 0, oepcnt0_ = 0x80;
    uint8_t iepie_ = 0, oepie_ = 0, iepifg_ = 0, oepifg_ = 0, ctl_ = 0, ie_ = 0, ifg_ = 0;
    uint8_t funadr_ = 0, funadr_ant_ = 0;
    uint64_t n_paq_ = 0;
    sc_core::sc_event ev_se0_, ev_act_, ev_in_;
    sc_core::sc_time t_act_;
    bool se0_ = false;

    void avisa() { if (al_cambiar) al_cambiar(); }
    // [de modelo] VBUS presente al habilitar VBONIE: su interrupción (la pila
    // de TI conecta en ella, y la espera con VBUS ya puesto al arrancar)
    void pon_pwr_alto(uint8_t v) {
        const bool ie_antes = (pwr_ & 0x0200) != 0;
        pwr_ = uint16_t((pwr_ & 0x00FF) | ((v << 8) & 0x1F00));
        if (!ie_antes && (pwr_ & 0x0200) && (pwr_lee() & BGVBV)) pwr_ |= VBONIFG;
    }
    uint16_t pwr_lee() const {
        return uint16_t((pwr_ & ~BGVBV) | ((vbus_ && (pwr_ & VUSBEN)) ? BGVBV : 0));
    }
    // La resistencia de 1,5 kOhm de la placa, de PUR a D+
    void pur() {
        if (cnf_ & PUR_EN) dp_.set_drive(id_pur_, 3.3f, 1500.0f);
        else dp_.set_hiz(id_pur_);
    }
    void mira_pll() {
        const bool antes = pll_ok_;
        pll_ok_ = false;
        if (!(pll_ & UPLLEN)) return;
        const double fr = f_ref ? f_ref((pll_ & 7u) == 0 ? 0u : 1u) : 0.0;
        if (fr <= 0) { pllir_ |= 0x02; avisa(); return; }             // USBLOSIFG
        static const unsigned Q[8] = {1, 2, 3, 4, 6, 8, 12, 16};
        const double f = fr * double((plldiv_ & 0x3F) + 1) / double(Q[(plldiv_ >> 8) & 7]);
        if (std::fabs(f - 48e6) > 0.01 * 48e6) {                      // USBOORIFG
            pllir_ |= 0x04; avisa();
            if (!avisado_pll_) { avisado_pll_ = true;
                std::fprintf(stderr, "[usb] la PLL da %.4g MHz con %.4g MHz de referencia: hacen falta 48\n", f / 1e6, fr / 1e6); }
            return;
        }
        pll_ok_ = true;
        (void)antes;
    }
    bool avisado_pll_ = false;
    void aviso_mudo() {
        if (avisado_mudo_) return;
        avisado_mudo_ = true;
        const char* q = !(cnf_ & USB_EN) ? "sin USB_EN" : !pll_ok_ ? "sin la PLL de 48 MHz" :
                        (pll_ & 0xC0) ? "con UCLKSEL distinto de PLLCLK" : "sin FEN";
        std::fprintf(stderr, "[usb] el modulo no contesta: %s (aviso una vez)\n", q);
    }
    // La fuente de más prioridad [UG5, USBVECINT]
    uint8_t vector() const {
        if ((pwr_ & 0x01) && (pwr_ & 0x0100)) return 0x02;
        if ((pllir_ & 0x01) && (pllir_ & 0x0100)) return 0x04;
        if ((pllir_ & 0x02) && (pllir_ & 0x0200)) return 0x06;
        if ((pllir_ & 0x04) && (pllir_ & 0x0400)) return 0x08;
        if ((pwr_ & 0x02) && (pwr_ & 0x0200)) return 0x0A;
        if ((pwr_ & 0x04) && (pwr_ & 0x0400)) return 0x0C;
        if ((maint_ & 0x01) && (maint_ & 0x02)) return 0x10;
        if (iepifg_ & iepie_ & 1) return 0x12;
        if (oepifg_ & oepie_ & 1) return 0x14;
        if (ifg_ & ie_ & RSTRIFG) return 0x16;
        if (ifg_ & ie_ & SUSRIFG) return 0x18;
        if (ifg_ & ie_ & RESRIFG) return 0x1A;
        if (ifg_ & ie_ & SETUPIFG) return 0x20;
        if (ifg_ & ie_ & STPOWIFG) return 0x22;
        for (unsigned n = 1; n < 8; ++n) if (iepifg_ & iepie_ & (1u << n)) return uint8_t(0x24 + 2 * (n - 1));
        for (unsigned n = 1; n < 8; ++n) if (oepifg_ & oepie_ & (1u << n)) return uint8_t(0x32 + 2 * (n - 1));
        return 0;
    }
    void borra(uint8_t v) {
        switch (v) {
            case 0x00: return;
            case 0x02: pwr_ &= uint16_t(~0x01); break;
            case 0x04: pllir_ &= uint16_t(~0x01); break;
            case 0x06: pllir_ &= uint16_t(~0x02); break;
            case 0x08: pllir_ &= uint16_t(~0x04); break;
            case 0x0A: pwr_ &= uint16_t(~0x02); break;
            case 0x0C: pwr_ &= uint16_t(~0x04); break;
            case 0x10: maint_ &= uint16_t(~0x01); break;
            case 0x12: iepifg_ &= uint8_t(~1); break;
            case 0x14: oepifg_ &= uint8_t(~1); break;
            case 0x16: ifg_ &= uint8_t(~RSTRIFG); break;
            case 0x18: ifg_ &= uint8_t(~SUSRIFG); break;
            case 0x1A: ifg_ &= uint8_t(~RESRIFG); break;
            case 0x20: return;                                         // SETUPIFG: lo borra el programa
            case 0x22: ifg_ &= uint8_t(~STPOWIFG); break;
            default:
                if (v >= 0x24 && v <= 0x30) iepifg_ &= uint8_t(~(1u << ((v - 0x24) / 2 + 1)));
                else if (v >= 0x32 && v <= 0x3E) oepifg_ &= uint8_t(~(1u << ((v - 0x32) / 2 + 1)));
                break;
        }
        avisa();
    }
    // Hay algo en el bus: fuera de la suspensión
    void actividad() {
        t_act_ = sc_core::sc_time_stamp();
        if (suspendido_) {
            suspendido_ = false;
            if (ctl_ & FEN) { ifg_ |= RESRIFG; avisa(); }
            if (al_bus) al_bus();
        }
        ev_act_.notify(sc_core::SC_ZERO_TIME);
    }
    // El SE0 de más de 2,5 us con el dispositivo conectado (PUR_EN): RESET
    void linea_proc() {
        const bool se0 = dp_.voltage() < 0.8f && dm_.voltage() < 0.8f && (cnf_ & PUR_EN);
        if (se0 && !se0_) { se0_ = true; ev_se0_.notify(sc_core::sc_time(2.5, sc_core::SC_US)); return; }
        if (!se0) { if (se0_) actividad(); se0_ = false; ev_se0_.cancel(); return; }
        // ev_se0_: sigue en SE0
        actividad();
        if (!(cnf_ & USB_EN) || !(ctl_ & FEN)) return;
        if (ctl_ & FRSTE) { funadr_ = funadr_ant_ = 0; }
        ifg_ |= RSTRIFG;
        avisa();
        if (al_bus) al_bus();
    }
    // Tres milisegundos sin nada en el bus (ni SOF ni reset): SUSPEND
    void suspension_proc() {
        for (;;) {
            wait(ev_act_);
            for (;;) {
                const sc_core::sc_time lim = t_act_ + sc_core::sc_time(3, sc_core::SC_MS);
                wait(lim - sc_core::sc_time_stamp(), ev_act_);
                if (sc_core::sc_time_stamp() < lim) continue;
                if (se0_) break;                                       // un reset largo no es suspensión
                if (!suspendido_ && (cnf_ & USB_EN) && (ctl_ & FEN) && (cnf_ & PUR_EN)) {
                    suspendido_ = true;
                    ifg_ |= SUSRIFG;
                    avisa();
                    if (al_bus) al_bus();
                }
                break;
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_USB_TI_H
