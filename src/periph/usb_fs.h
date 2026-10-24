// =============================================================================
// usb_fs.h — El USB de dispositivo de los STM32 de segunda generación (FSDEV):
//            el del STM32F303xB/C (fase S6)
//
// [RM0316 §32; CMSIS stm32f303xc.h, USB_*; la base de pines de ST lo llama
// usb1_v2_0_STM32F3xx]
//
// NO ES EL OTG del F4 (`otg.h`), y no se parece: solo dispositivo, Full Speed,
// sin FIFOs que partir y sin DMA. Lo que tiene es una MEMORIA DE PAQUETES (PMA)
// de 512 bytes que el firmware reparte a mano con una TABLA DE BÚFERES que vive
// dentro de ella, y ocho registros de endpoint con una semántica de escritura
// que hay que conocer para no romper nada:
//
//   EPnR  EA (3:0), STAT_TX (5:4), DTOG_TX (6), CTR_TX (7), EP_KIND (8),
//         EP_TYPE (10:9), SETUP (11), STAT_RX (13:12), DTOG_RX (14), CTR_RX (15)
//         * STAT_x y DTOG_x CAMBIAN con un 1 y no con un 0 (t, «toggle»);
//         * CTR_x se borran con un 0 y un 1 no hace nada (rc_w0);
//         * SETUP es de solo lectura; EA, EP_TYPE y EP_KIND, normales.
//   CNTR  CTRM PMAOVRM ERRM WKUPM SUSPM RESETM SOFM ESOFM (15:8), RESUME (4),
//         FSUSP (3), LP_MODE (2), PDWN (1), FRES (0); de reset 0x0003
//   ISTR  CTR (15) DIR (4) EP_ID (3:0), de solo lectura, y PMAOVR ERR WKUP
//         SUSP RESET SOF ESOF (14:8), que se borran con un 0
//   FNR   FN (10:0), LSOF, LCK, RXDM, RXDP
//   DADDR ADD (6:0), EF (7): sin EF el dispositivo no contesta a nadie
//   BTABLE dónde empieza la tabla, en la PMA
//
// LA PMA está en 0x4000_6000 con el esquema «1 x 16 bits/palabra»: cada media
// palabra de la memoria ocupa una palabra de 32 bits del bus, así que los 512
// bytes ocupan 1 KB de direcciones y la media alta de cada palabra lee cero
// [RM0316 §32.5.3]. En la tabla, cuatro medias palabras por endpoint: ADDRn_TX,
// COUNTn_TX, ADDRn_RX y COUNTn_RX (BL_SIZE y NUM_BLOCK dicen cuánto cabe).
//
// EL OTRO LADO DEL CABLE llama a `transaccion()` (usb_dev_if, periph/otg.h: la
// frontera de paquetes del proyecto). Por cada testigo:
//
//   * SETUP, a un endpoint de control: se acepta SIEMPRE (USB 2.0 §8.5.3), se
//     copia al búfer de RX, SETUP y CTR_RX a uno, los dos sentidos a NAK y los
//     dos DTOG a uno, que es lo que espera la pila (TinyUSB lo dice así);
//   * OUT: según STAT_RX: DISABLED no contesta, STALL, NAK, o VALID: los datos
//     a la PMA, COUNTn_RX, CTR_RX, STAT_RX a NAK y DTOG_RX cambia;
//   * IN: según STAT_TX, igual: VALID saca COUNTn_TX bytes de ADDRn_TX, pone
//     CTR_TX y STAT_TX a NAK.
//
// LO ELÉCTRICO: el F303xB/C no tiene resistencia de subida propia (es la placa
// la que pone 1,5 kOhm en D+: R16 de la Discovery). El bloque mira los nodos de
// PA12 (D+) y PA11 (D-): un SE0 de más de 2,5 us es el RESET del bus; tres
// milisegundos sin nada, SUSP; algo en el bus estando FSUSP, WKUP (y la línea
// 18 del EXTI). Cada milisegundo sin SOF, ESOF.
//
// NO CONTESTA si le falta algo que el silicio necesita: el reloj del bus
// (USBEN), los 48 MHz del PLL (USBPRE), el bloque encendido (PDWN y FRES a
// cero) y PA11/PA12 en AF14. Lo primero que falte, lo dice una vez.
//
// NO SE MODELA [de modelo]: el doble búfer (EP_KIND en un bulk) y los
// isócronos, que se avisan y van como un búfer simple; los errores de CRC y de
// relleno del cable (ERR), que la frontera de paquetes no ve; la RESUME del
// dispositivo hacia el anfitrión; el tiempo de cable, que pone quien llama.
// =============================================================================
#ifndef STM32_PERIPH_USB_FS_H
#define STM32_PERIPH_USB_FS_H

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "../common/periph_base.h"
#include "../common/analog_net.h"
#include "otg.h"

namespace stm32 {

class UsbFs : public BusSlave, public usb_dev_if {
public:
    static constexpr unsigned N_EP = 8, PMA_BYTES = 512;
    enum : uint32_t { R_EP0R = 0x00, R_CNTR = 0x40, R_ISTR = 0x44, R_FNR = 0x48, R_DADDR = 0x4C,
                      R_BTABLE = 0x50 };
    enum : uint32_t {
        EP_EA = 0x000F, EP_STAT_TX = 0x0030, EP_DTOG_TX = 0x0040, EP_CTR_TX = 0x0080,
        EP_KIND = 0x0100, EP_TYPE = 0x0600, EP_SETUP = 0x0800, EP_STAT_RX = 0x3000,
        EP_DTOG_RX = 0x4000, EP_CTR_RX = 0x8000
    };
    enum : uint32_t {
        C_FRES = 1u << 0, C_PDWN = 1u << 1, C_LPMODE = 1u << 2, C_FSUSP = 1u << 3,
        C_RESUME = 1u << 4, C_ESOFM = 1u << 8, C_SOFM = 1u << 9, C_RESETM = 1u << 10,
        C_SUSPM = 1u << 11, C_WKUPM = 1u << 12, C_ERRM = 1u << 13, C_PMAOVRM = 1u << 14,
        C_CTRM = 1u << 15
    };
    enum : uint32_t {
        I_ESOF = 1u << 8, I_SOF = 1u << 9, I_RESET = 1u << 10, I_SUSP = 1u << 11,
        I_WKUP = 1u << 12, I_ERR = 1u << 13, I_PMAOVR = 1u << 14, I_CTR = 1u << 15,
        I_BORRABLES = 0x7F00u
    };
    enum Stat : unsigned { ST_DISABLED = 0, ST_STALL = 1, ST_NAK = 2, ST_VALID = 3 };
    enum Tipo : unsigned { T_BULK = 0, T_CONTROL = 1, T_ISO = 2, T_INTERRUPT = 3 };

    // USB_HP (CTR de isócronos y bulk con doble búfer), USB_LP (todo lo demás)
    // y el despertar, hacia la línea 18 del EXTI [RM0316 §32.4.2]
    sc_core::sc_out<bool> irq_hp{"irq_hp"}, irq_lp{"irq_lp"}, wkup{"wkup"};

    // ---- La memoria de paquetes, su propia ventana ----------------------------
    class Pma : public BusSlave {
    public:
        Pma(sc_core::sc_module_name nm, uint32_t base, std::array<uint8_t, PMA_BYTES>& m)
            : BusSlave(nm, base, 2 * PMA_BYTES), m_(m) {}
    protected:
        // Media palabra en la parte baja de cada palabra; la alta lee cero
        uint32_t reg_read(uint32_t off) override {
            const unsigned b = off / 2;
            if (b + 1 >= PMA_BYTES + 1) return 0;
            return uint32_t(m_[b]) | (uint32_t(m_[b + 1]) << 8);
        }
        void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
            const unsigned b = off / 2;
            if (b + 1 >= PMA_BYTES + 1) return;
            if (be & 1u) m_[b] = uint8_t(v);
            if (be & 2u) m_[b + 1] = uint8_t(v >> 8);
        }
    private:
        std::array<uint8_t, PMA_BYTES>& m_;
    };
    Pma pma;

    UsbFs(sc_core::sc_module_name nm, uint32_t base, uint32_t base_pma)
        : BusSlave(nm, base, 0x400), pma("pma", base_pma, mem_) {
        mem_.fill(0);
        reinicia();
        SC_HAS_PROCESS(UsbFs);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_THREAD(vigia);
        SC_METHOD(se0_fin); sensitive << se0_ev_; dont_initialize();
    }

    // ---- Lo que cablea el SoC (elaboración) ------------------------------------
    // Los nodos de D+ (PA12) y D- (PA11)
    void bind_pines(analog_net_if& dp, analog_net_if& dm) {
        dp_ = &dp; dm_ = &dm;
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&dp.value_changed_event());
        o.set_sensitivity(&dm.value_changed_event());
        sc_core::sc_spawn([this] { linea(); }, sc_core::sc_gen_unique_name("usb_linea"), &o);
    }
    std::function<double()> f48 = [] { return 48e6; };     // el reloj de USB del RCC
    std::function<bool()> pines_af = [] { return true; };  // PA11/PA12 en AF14

    // ---- Observación -----------------------------------------------------------
    unsigned resets() const { return n_reset_; }
    unsigned setups() const { return n_setup_; }
    uint64_t bytes_in() const { return n_in_; }
    uint64_t bytes_out() const { return n_out_; }
    uint8_t direccion() const { return uint8_t(daddr_ & 0x7Fu); }
    bool suspendido() const { return cntr_ & C_FSUSP; }
    uint32_t epr(unsigned n) const { return n < N_EP ? epr_[n] : 0; }

    // ---- usb_dev_if: el anfitrión ----------------------------------------------
    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep, const std::vector<uint8_t>& salida,
                        std::vector<uint8_t>& entrada) override {
        const uint8_t r = transaccion_(pid, addr, ep, salida, entrada);
        if (traza_) {
            std::printf("    [usb %s] %s a%u ep%u %zu/%zu -> %s  EP0R %04X\n",
                        sc_core::sc_time_stamp().to_string().c_str(),
                        pid == PID_SETUP ? "SETUP" : pid == PID_IN ? "IN" : "OUT", addr, ep, salida.size(),
                        entrada.size(), r == PID_ACK ? "ACK" : r == PID_NAK ? "NAK" : r == PID_STALL ? "STALL" : "-",
                        unsigned(epr_[0]));
        }
        return r;
    }
    // MCUSIM_USB_TRAZA=1: cada transacción y cada escritura de EP0R/EP1R, para depurar
    bool traza_ = std::getenv("MCUSIM_USB_TRAZA") != nullptr;
    uint8_t transaccion_(uint8_t pid, uint8_t addr, uint8_t ep, const std::vector<uint8_t>& salida,
                         std::vector<uint8_t>& entrada) {
        entrada.clear();
        if (!vivo()) return PID_NADIE;
        actividad();
        if (!(daddr_ & 0x80u) || addr != (daddr_ & 0x7Fu)) return PID_NADIE;
        const bool in = pid == PID_IN;
        const int n = busca(ep, in);
        if (n < 0) return PID_NADIE;
        uint32_t& r = epr_[n];
        const unsigned tipo = (r & EP_TYPE) >> 9;
        aviso_tipo(r, tipo);
        if (pid == PID_SETUP) {
            if (tipo != T_CONTROL) return PID_NADIE;
            if (!a_pma(unsigned(n), salida)) return PID_NADIE;
            r |= EP_SETUP | EP_CTR_RX | EP_DTOG_RX | EP_DTOG_TX;
            pon_stat(r, false, ST_NAK);
            pon_stat(r, true, ST_NAK);
            ++n_setup_;
            n_out_ += salida.size();
            actualiza();
            return PID_ACK;
        }
        if (pid == PID_OUT) {
            switch (stat(r, false)) {
                case ST_DISABLED: return PID_NADIE;
                case ST_STALL: return PID_STALL;
                case ST_NAK: return PID_NAK;
                default: break;
            }
            if (!a_pma(unsigned(n), salida)) return PID_NADIE;
            r = (r & ~EP_SETUP) | EP_CTR_RX;
            r ^= EP_DTOG_RX;
            if (tipo != T_ISO) pon_stat(r, false, ST_NAK);
            n_out_ += salida.size();
            actualiza();
            return PID_ACK;
        }
        if (in) {
            switch (stat(r, true)) {
                case ST_DISABLED: return PID_NADIE;
                case ST_STALL: return PID_STALL;
                case ST_NAK: return PID_NAK;
                default: break;
            }
            const unsigned a = h(bt(unsigned(n)) + 0), c = h(bt(unsigned(n)) + 2) & 0x3FFu;
            for (unsigned i = 0; i < c; ++i) entrada.push_back(mem_[(a + i) % PMA_BYTES]);
            r |= EP_CTR_TX;
            r ^= EP_DTOG_TX;
            if (tipo != T_ISO) pon_stat(r, true, ST_NAK);
            n_in_ += c;
            actualiza();
            return PID_ACK;
        }
        return PID_NADIE;
    }
    void sof(uint16_t trama) override {
        if (!vivo()) return;
        actividad();
        fn_ = trama & 0x7FFu;
        if (n_sof_seguidos_ < 2) ++n_sof_seguidos_;
        t_sof_ = sc_core::sc_time_stamp();
        istr_ |= I_SOF;
        actualiza();
    }

protected:
    std::array<uint8_t, PMA_BYTES> mem_{};
    uint32_t epr_[N_EP] = {};
    uint32_t cntr_ = 0, istr_ = 0, daddr_ = 0, btable_ = 0, fn_ = 0;
    unsigned n_sof_seguidos_ = 0;
    analog_net_if *dp_ = nullptr, *dm_ = nullptr;
    sc_core::sc_event pub_ev_, actividad_ev_, se0_ev_;
    sc_core::sc_time t_act_, t_sof_, t_se0_;
    bool se0_ = false, susp_dicho_ = false;
    unsigned n_reset_ = 0, n_setup_ = 0;
    uint64_t n_in_ = 0, n_out_ = 0;
    bool av_reloj_ = false, av_48_ = false, av_pines_ = false, av_off_ = false, av_dbl_ = false,
         av_iso_ = false, av_pma_ = false;

    void reinicia() {
        for (uint32_t& e : epr_) e = 0;
        cntr_ = C_FRES | C_PDWN;
        istr_ = 0; daddr_ = 0; btable_ = 0; fn_ = 0; n_sof_seguidos_ = 0;
        susp_dicho_ = false;
    }

    // ---- ¿Puede contestar? -----------------------------------------------------
    bool vivo() {
        if (!rst_n.read()) return false;
        if (!clock_enabled()) { aviso(av_reloj_, "sin reloj (RCC_APB1ENR.USBEN): no contesta"); return false; }
        if (cntr_ & (C_PDWN | C_FRES)) {
            aviso(av_off_, "el anfitrion habla con el bloque apagado (CNTR.PDWN o FRES): no contesta");
            return false;
        }
        const double f = f48();
        if (f < 48e6 * 0.9975 || f > 48e6 * 1.0025) {
            char b[160];
            std::snprintf(b, sizeof b, "el reloj de USB es de %.3f MHz, no de 48 (RCC_CFGR.USBPRE y el "
                          "PLL): no contesta", f / 1e6);
            aviso(av_48_, b);
            return false;
        }
        if (!pines_af()) {
            aviso(av_pines_, "PA11/PA12 no estan en AF14 (USB_DM/USB_DP): el transceptor no llega a "
                  "los pines y no contesta");
            return false;
        }
        return true;
    }
    void actividad() {
        t_act_ = sc_core::sc_time_stamp();
        susp_dicho_ = false;
        if (cntr_ & C_FSUSP) { istr_ |= I_WKUP; actualiza(); }
        actividad_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // ---- La tabla de búferes ----------------------------------------------------
    unsigned bt(unsigned n) const { return (btable_ & 0xFFF8u) + 8u * n; }
    unsigned h(unsigned a) const { return unsigned(mem_[a % PMA_BYTES]) | (unsigned(mem_[(a + 1) % PMA_BYTES]) << 8); }
    void pon_h(unsigned a, unsigned v) { mem_[a % PMA_BYTES] = uint8_t(v); mem_[(a + 1) % PMA_BYTES] = uint8_t(v >> 8); }
    bool a_pma(unsigned n, const std::vector<uint8_t>& d) {
        const unsigned a = h(bt(n) + 4), c = h(bt(n) + 6);
        const unsigned cabe = (c & 0x8000u) ? 32u * (((c >> 10) & 0x1Fu) + 1u) : 2u * ((c >> 10) & 0x1Fu);
        if (d.size() > cabe) {
            // No cabe: el silicio lo trata como un error y no contesta [RM0316 §32.4.3]
            istr_ |= I_PMAOVR;
            aviso(av_pma_, "un paquete no cabe en el bufer de RX que dice COUNTn_RX: PMAOVR y sin respuesta");
            actualiza();
            return false;
        }
        for (size_t i = 0; i < d.size(); ++i) mem_[(a + i) % PMA_BYTES] = d[i];
        pon_h(bt(n) + 6, (c & 0xFC00u) | unsigned(d.size()));
        return true;
    }

    // ---- Los endpoints ------------------------------------------------------------
    static unsigned stat(uint32_t r, bool tx) { return tx ? (r >> 4) & 3u : (r >> 12) & 3u; }
    static void pon_stat(uint32_t& r, bool tx, unsigned s) {
        if (tx) r = (r & ~EP_STAT_TX) | (s << 4);
        else    r = (r & ~EP_STAT_RX) | (s << 12);
    }
    // El registro que lleva ese número de endpoint en ese sentido
    int busca(uint8_t ep, bool tx) const {
        int primero = -1;
        for (unsigned n = 0; n < N_EP; ++n) {
            if ((epr_[n] & EP_EA) != ep) continue;
            if (stat(epr_[n], tx) != ST_DISABLED) return int(n);
            if (primero < 0) primero = int(n);
        }
        return primero;
    }
    void aviso_tipo(uint32_t r, unsigned tipo) {
        if (tipo == T_BULK && (r & EP_KIND))
            aviso(av_dbl_, "un bulk con doble bufer (EP_KIND): no se modela, va como bufer simple");
        if (tipo == T_ISO) aviso(av_iso_, "un endpoint isocrono: no se modela su doble bufer");
    }

    // ---- El bus ---------------------------------------------------------------------
    uint32_t reg_read(uint32_t off) override {
        if (off < 4 * N_EP) return epr_[off / 4];
        switch (off) {
            case R_CNTR: return cntr_;
            case R_ISTR: return istr_leido();
            case R_FNR: {
                uint32_t v = fn_;
                if (n_sof_seguidos_ >= 2) v |= 1u << 13;              // LCK
                if (dm_ && dm_->voltage() > 1.6f) v |= 1u << 14;      // RXDM
                if (dp_ && dp_->voltage() > 1.6f) v |= 1u << 15;      // RXDP
                return v;
            }
            case R_DADDR: return daddr_;
            case R_BTABLE: return btable_;
            default: return 0;
        }
    }
    // CTR, DIR y EP_ID salen de los registros de endpoint: el de número más bajo
    // con algo pendiente [RM0316 §32.6.2]
    uint32_t istr_leido() const {
        uint32_t v = istr_ & I_BORRABLES;
        for (unsigned n = 0; n < N_EP; ++n) {
            if (epr_[n] & (EP_CTR_RX | EP_CTR_TX)) {
                v |= I_CTR | n | ((epr_[n] & EP_CTR_RX) ? (1u << 4) : 0u);
                break;
            }
        }
        return v;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (!(be & 3u)) return;                               // todo es de 16 bits
        if (!(be & 1u)) v = (v & 0xFF00u) | (reg_read(off) & 0xFFu);
        if (!(be & 2u)) v = (v & 0x00FFu) | (reg_read(off) & 0xFF00u);
        v &= 0xFFFFu;
        if (traza_ && off < 8)   // EP0R y EP1R
            std::printf("    [usb %s] EP%uR <- %04X (era %04X)\n", sc_core::sc_time_stamp().to_string().c_str(),
                        unsigned(off / 4), unsigned(v), unsigned(epr_[off / 4]));
        if (off < 4 * N_EP) {
            uint32_t& r = epr_[off / 4];
            const uint32_t normales = EP_EA | EP_KIND | EP_TYPE;
            const uint32_t cambian = EP_STAT_TX | EP_DTOG_TX | EP_STAT_RX | EP_DTOG_RX;
            uint32_t n = (v & normales) | (r & EP_SETUP);
            n |= (r ^ v) & cambian;
            n |= r & v & (EP_CTR_RX | EP_CTR_TX);
            r = n;
            actualiza();
            return;
        }
        switch (off) {
            case R_CNTR: {
                const uint32_t ant = cntr_;
                cntr_ = v & 0xFF1Fu;
                if (!(ant & C_FRES) && (cntr_ & C_FRES)) reset_usb(false);
                actualiza();
                return;
            }
            case R_ISTR: istr_ &= v | ~I_BORRABLES; actualiza(); return;
            case R_DADDR: daddr_ = v & 0xFFu; return;
            case R_BTABLE: btable_ = v & 0xFFF8u; return;
            default: return;
        }
    }

    // ---- El reset del bus -----------------------------------------------------------
    void reset_usb(bool del_bus) {
        for (uint32_t& e : epr_) e = 0;
        daddr_ = 0;
        n_sof_seguidos_ = 0;
        if (del_bus) { istr_ |= I_RESET; ++n_reset_; }
        actualiza();
    }
    // D+ y D- a la vez abajo: un SE0. El que pasa de 2,5 us es un reset
    void linea() {
        if (!dp_ || !dm_) return;
        const bool se0 = !dp_->floating() && !dm_->floating() && dp_->voltage() < 0.8f && dm_->voltage() < 0.8f;
        if (se0 && !se0_) {
            t_se0_ = sc_core::sc_time_stamp();
            se0_ev_.notify(sc_core::sc_time(2500, sc_core::SC_NS));
        }
        if (!se0 && se0_) se0_ev_.cancel();
        se0_ = se0;
        if (se0 || (dp_->voltage() < 1.6f && dm_->voltage() > 1.6f)) {
            // SE0 o K: actividad en el bus (el reset y la reanudación)
            if (rst_n.read() && !(cntr_ & (C_PDWN | C_FRES)) && clock_enabled()) actividad();
        }
    }
    void se0_fin() {
        if (!se0_ || !rst_n.read() || (cntr_ & (C_PDWN | C_FRES)) || !clock_enabled()) return;
        reset_usb(true);
    }
    // Suspensión (3 ms sin nada) y ESOF (un milisegundo sin SOF) [RM0316 §32.4.5]
    void vigia() {
        for (;;) {
            const bool on = rst_n.read() && clock_enabled() && !(cntr_ & (C_PDWN | C_FRES));
            // Apagado no hay nada que vigilar: espera a que lo toquen
            if (!on) { wait(actividad_ev_ | pub_ev_); continue; }
            wait(sc_core::sc_time(1, sc_core::SC_MS));
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (!(cntr_ & C_FSUSP) && ahora - t_sof_ > sc_core::sc_time(1100, sc_core::SC_US) &&
                n_sof_seguidos_ > 0) {
                istr_ |= I_ESOF;
                n_sof_seguidos_ = 0;
                actualiza();
            }
            if (!susp_dicho_ && !(cntr_ & C_FSUSP) && ahora - t_act_ >= sc_core::sc_time(3, sc_core::SC_MS)) {
                susp_dicho_ = true;
                istr_ |= I_SUSP;
                actualiza();
            }
        }
    }

    // ---- Salidas ----------------------------------------------------------------
    bool o_hp_ = false, o_lp_ = false, o_wk_ = false;
    void actualiza() {
        const uint32_t i = istr_leido();
        bool hp = false;
        if ((i & I_CTR) && (cntr_ & C_CTRM)) {
            const uint32_t r = epr_[i & 0xFu];
            const unsigned t = (r & EP_TYPE) >> 9;
            hp = t == T_ISO || (t == T_BULK && (r & EP_KIND));
        }
        const uint32_t pend = i & cntr_ & 0xFF00u;          // cada bit con su máscara
        o_hp_ = hp;
        o_lp_ = hp ? (pend & I_BORRABLES) != 0 : pend != 0;
        o_wk_ = (istr_ & I_WKUP) != 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() { irq_hp.write(o_hp_); irq_lp.write(o_lp_); wkup.write(o_wk_); }
    void reset_proc() {
        if (rst_n.read()) return;
        reinicia();
        actualiza();
    }
    void aviso(bool& ya, const char* q) {
        if (ya) return;
        ya = true;
        const std::string m = std::string(name()) + ": " + q;
        SC_REPORT_WARNING("usb_fs", m.c_str());
    }
};

} // namespace stm32
#endif // STM32_PERIPH_USB_FS_H
