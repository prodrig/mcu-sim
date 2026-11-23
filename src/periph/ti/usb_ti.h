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
//   0x904 USBPHYCTL   PUSEL, PUIPE, PUOPE, PUIN0/1, PUOUT0/1: con PUSEL = 0 y
//                     sin USB_EN, PU.0 y PU.1 son E/S (las salidas, del
//                     carril VUSB) [de modelo: con USB_EN son D+ y D-, como
//                     hace la pila de TI, que pone PUSEL «aunque no haga
//                     falta»] [UG5 §42.2.1]
//   0x908 USBPWRCTL   VUOVLIFG, VBONIFG, VBOFFIFG, USBBGVBV (VBUS válido),
//                     [de modelo: VBONIFG/VBOFFIFG con cada cambio de VBUS, y
//                     VBONIFG al habilitar VBONIE con VBUS ya puesto: la pila
//                     de TI conecta en esa interrupción]
//                     USBDETEN, OVLAOFF, SLDOAON, sus IE, VUSBEN, SLDOEN.
//                     La sobrecarga del LDO (`sobrecarga()`, el banco: un
//                     corto en VUSB) da VUOVLIFG, y con OVLAOFF borra VUSBEN
//                     y el módulo deja de contestar [UG5 §42.2.2.5]
//   0x910 USBPLLCTL   UPCS (0: XT2, 1: XT1 [de modelo, el comentario de
//                     USB_enable de TI]), UCLKSEL, UPLLEN, UPFDEN
//   0x912 USBPLLDIVB  UPMB (×(M+1)) y UPQB (÷1, 2, 3, 4, 6, 8, 12, 16)
//   0x914 USBPLLIR    OOL, LOS, OOR y sus IE: LOS sin referencia, OOR si la
//                     salida no da 48 MHz (±1 %)
//   0x920-0x923       IEPCNF_0, IEPCNT_0, OEPCNF_0, OEPCNT_0 (el EP0)
//   0x92E/0x92F       USBIEPIE/USBOEPIE; 0x930/0x931 USBIEPIFG/USBOEPIFG
//   0x932 USBVECINT   la fuente de más prioridad; leerlo la borra (salvo
//                     SETUPIFG, que borra el programa) [UG5 §42.3.?]
//   0x936 USBMAINT    el temporizador de USB: 16 bits a USBCLK/3 (16 MHz),
//                     a cero con cada SOF; UTIFG cada 4096/2^UTSEL us (UTIE,
//                     su interrupción); la marca de tiempo con TSGEN: el
//                     disparo de un canal de DMA (TSESEL 0-2, `evento_dma`) o
//                     TSE3 por programa (TSESEL 3) copia el temporizador a
//   0x938 USBTSREG    [UG5 §42.2.4.4]
//   0x93A USBFN       la trama
//   0x93C USBCTL      DIR, FRSTE, RWUP (la reanudación remota: se borra
//                     sola; estando suspendido, el anfitrión la contesta
//                     [de modelo: al momento] y llega RESRIFG), FEN
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
// NO CONTESTA sin USB_EN, sin VUSBEN, sin FEN, sin la PLL enganchada
// (UPLLEN, 48 MHz) o con UCLKSEL distinto de PLLCLK.
//
// El módulo no tiene transferencias isócronas: solo control, bulk e
// interrupción [UG5 cap. 42].
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
#include "../../common/verbosidad.h"

namespace stm32 {

SC_MODULE(UsbTi), public usb_dev_if {
    std::function<uint8_t&(uint32_t)> ram;      // la RAM de USB del SoC
    std::function<double(unsigned)> f_ref;      // 0: XT2, 1: XT1 (Hz; 0 si no hay)
    std::function<void()> al_cambiar;           // la petición de interrupción
    std::function<void()> al_bus;               // actividad: que el SoC mire su LPM
    // El instante de un acceso de la CPU a los registros: el del núcleo, que
    // va por delante de sc_time_stamp() con su tiempo local (lo pone el SoC
    // antes de cada lee8/escribe8; el temporizador y la marca de tiempo lo usan)
    sc_core::sc_time t_acceso;

    UsbTi(sc_core::sc_module_name nm, analog_net_if& dp, analog_net_if& dm)
        : sc_core::sc_module(nm), dp_(dp), dm_(dm) {
        id_pur_ = dp_.register_driver((std::string(name()) + "_pur").c_str());
        dp_.set_hiz(id_pur_);
        id_pu_[0] = dp_.register_driver((std::string(name()) + "_pu0").c_str());
        id_pu_[1] = dm_.register_driver((std::string(name()) + "_pu1").c_str());
        dp_.set_hiz(id_pu_[0]); dm_.set_hiz(id_pu_[1]);
        SC_HAS_PROCESS(UsbTi);
        SC_METHOD(linea_proc); sensitive << dp_.value_changed_event() << dm_.value_changed_event() << ev_se0_;
        dont_initialize();
        SC_THREAD(suspension_proc);
        SC_THREAD(temporizador_proc);
        reset();
    }
    void reset() {
        abierto_ = false;
        cnf_ = 0; phy_ = 0; pwr_ = 0x1850; pll_ = 0; plldiv_ = 0; pllir_ = 0;
        iepcnf0_ = oepcnf0_ = 0; iepcnt0_ = oepcnt0_ = 0x80;
        iepie_ = oepie_ = iepifg_ = oepifg_ = 0;
        maint_ = ts_ = fn_ = 0; ctl_ = ie_ = ifg_ = 0; funadr_ = funadr_ant_ = 0;
        suspendido_ = false;
        t_tmr_ = sc_core::sc_time_stamp(); tmr_marca_ = 0; t_acceso = t_tmr_;
        pur();
        port_u();
        mira_pll();
        ev_tmr_.notify(sc_core::SC_ZERO_TIME);
    }
    // Un corto en VUSB: el LDO se limita a IDET
    void sobrecarga(bool v) {
        if (!v || !(pwr_ & VUSBEN)) return;
        pwr_ |= VUOVLIFG;
        if (pwr_ & OVLAOFF) pwr_ &= uint16_t(~VUSBEN);
        port_u();
        avisa();
    }
    // El disparo de un canal de DMA (TSE0-TSE2): la marca de tiempo
    void evento_dma(unsigned n) { if (((maint_ >> 9) & 3u) == n) marca(t_acceso); }
    // Las reanudaciones remotas que ha visto el anfitrión
    unsigned reanudaciones_remotas() const { return n_rwup_; }
    std::function<void()> al_rwup;                 // el anfitrión, si quiere enterarse
    bool suspendido() const { return suspendido_; }
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
            case 0x904: return uint8_t((phy_ & ~0x0Cu) | pu_in());
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
            case 0x936: tmr_al_dia(t_acceso); return uint8_t(maint_);
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
            case 0x902: case 0x903: b(cnf_, a & 1, 0x001B); pur(); port_u(); mira_pll(); tmr_reinicia(t_acceso); avisa(); return;
            case 0x904: case 0x905: b(phy_, a & 1, 0x01A3); port_u(); return;
            case 0x908: {
                // Las IFG se borran con un 0; USBBGVBV es de solo lectura
                if (!(a & 1)) pwr_ = uint16_t((pwr_ & 0xFF00) | (pwr_ & 0x07 & v) | (v & 0x70));
                else pon_pwr_alto(v);
                port_u();
                avisa();
                return;
            }
            case 0x909: pon_pwr_alto(v); port_u(); avisa(); return;
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
            case 0x936: case 0x937: {
                        if (!(cnf_ & USB_EN)) return;                          // solo con USB_EN
                tmr_al_dia(t_acceso);
                b(maint_, a & 1, 0xEF03);
                if (maint_ & 0x0800) { maint_ &= uint16_t(~0x0800); if (((maint_ >> 9) & 3u) == 3u) marca(t_acceso); }   // TSE3
                ev_tmr_.notify(sc_core::SC_ZERO_TIME);
                avisa();
                return;
            }
            case 0x938: case 0x939: return;                            // USBTSREG es de solo lectura
            case 0x93A: case 0x93B: return;
            case 0x93C:
                if (!(cnf_ & USB_EN)) return;
                ctl_ = uint8_t(v & 0x51);                              // RWUP se borra solo
                if (v & RWUP) reanuda_remota();
                return;
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
    bool funciona() const { return (cnf_ & USB_EN) && (pwr_ & VUSBEN) && pll_ok_ && !(pll_ & 0x00C0) && (ctl_ & FEN); }
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
        if (funciona()) tmr_reinicia(sc_core::sc_time_stamp());        // el temporizador, a cero
    }

private:
    enum : uint16_t { USB_EN = 0x01, PUR_EN = 0x02,
                      VUOVLIFG = 0x01, VBONIFG = 0x02, VBOFFIFG = 0x04, BGVBV = 0x08, DETEN = 0x10, OVLAOFF = 0x20,
                      VUSBEN = 0x0800,
                      UPLLEN = 0x0100 };
    enum : uint8_t { USBIIE = 0x04, STALL = 0x08, DBUF = 0x10, TOGGLE = 0x20, UBME = 0x80, NAK = 0x80,
                     DIR = 0x01, FRSTE = 0x10, RWUP = 0x20, FEN = 0x40,
                     STPOWIFG = 0x01, SETUPIFG = 0x04, RESRIFG = 0x20, SUSRIFG = 0x40, RSTRIFG = 0x80 };
    analog_net_if &dp_, &dm_;
    int id_pur_ = -1, id_pu_[2] = {-1, -1};
    unsigned n_rwup_ = 0;
    // El temporizador de USB: corre a 16 MHz desde t_tmr_; tmr_marca_, los
    // periodos de UTSEL ya marcados
    sc_core::sc_time t_tmr_;
    uint64_t tmr_marca_ = 0;
    sc_core::sc_event ev_tmr_;
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
    // --- Port U [UG5 §42.2.1] -------------------------------------------------
    bool es_port_u() const { return !(phy_ & 0x80) && !(cnf_ & USB_EN); }
    void port_u() {
        const bool sale = es_port_u() && (phy_ & 0x20) && (pwr_ & VUSBEN);
        for (unsigned k = 0; k < 2; ++k) {
            analog_net_if& n = k ? dm_ : dp_;
            if (sale) n.set_drive(id_pu_[k], ((phy_ >> k) & 1u) ? 3.3f : 0.0f, 30.0f);
            else n.set_hiz(id_pu_[k]);
        }
    }
    uint8_t pu_in() const {
        if (!es_port_u() || !(phy_ & 0x0100)) return 0;
        auto alto = [](analog_net_if& n) { return !n.floating() && n.voltage() > 1.65f; };
        return uint8_t((alto(dp_) ? 0x04 : 0) | (alto(dm_) ? 0x08 : 0));
    }
    // --- El temporizador y la marca de tiempo [UG5 §42.2.4.4] ------------------
    bool reloj_usb() const { return (cnf_ & USB_EN) && pll_ok_ && !(pll_ & 0x00C0); }
    uint64_t tmr_cuentas(const sc_core::sc_time& ahora) const {
        if (!reloj_usb() || ahora <= t_tmr_) return 0;
        return uint64_t((ahora - t_tmr_).to_seconds() * 16e6 + 1e-6);
    }
    unsigned tmr_bits() const { return 16u - ((maint_ >> 13) & 7u); }   // UTSEL 0: 4096 us = 2^16 cuentas
    void tmr_reinicia(const sc_core::sc_time& ahora) {
        tmr_al_dia(ahora);
        t_tmr_ = ahora;
        tmr_marca_ = 0;
        ev_tmr_.notify(sc_core::SC_ZERO_TIME);
    }
    void tmr_al_dia(const sc_core::sc_time& ahora) {
        const uint64_t n = tmr_cuentas(ahora) >> tmr_bits();
        if (n > tmr_marca_) { tmr_marca_ = n; maint_ |= 0x0001; avisa(); }    // UTIFG
    }
    void marca(const sc_core::sc_time& ahora) {
        if (!(maint_ & 0x0100)) return;                                // TSGEN
        ts_ = uint16_t(tmr_cuentas(ahora));
    }
    void temporizador_proc() {
        for (;;) {
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (ahora >= t_tmr_) tmr_al_dia(ahora);
            if (!(maint_ & 0x0002) || !reloj_usb()) { wait(ev_tmr_); continue; }
            const uint64_t sig = (tmr_marca_ + 1) << tmr_bits();
            const double falta = double(sig) / 16e6 - (ahora >= t_tmr_ ? (ahora - t_tmr_).to_seconds() : -(t_tmr_ - ahora).to_seconds());
            wait(sc_core::sc_time(std::max(falta, 0.0), sc_core::SC_SEC) + sc_core::sc_time(1, sc_core::SC_PS), ev_tmr_);
        }
    }
    // --- La reanudación remota ----------------------------------------------------
    void reanuda_remota() {
        if (!suspendido_) return;                                      // solo tiene sentido suspendido
        ++n_rwup_;
        if (al_rwup) al_rwup();
        actividad();                                                   // el anfitrión contesta: RESRIFG
    }
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
                ::stm32::avisa_consola("[usb] la PLL da %.4g MHz con %.4g MHz de referencia: hacen falta 48\n", f / 1e6, fr / 1e6); }
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
        ::stm32::avisa_consola("[usb] el modulo no contesta: %s (aviso una vez)\n", q);
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
