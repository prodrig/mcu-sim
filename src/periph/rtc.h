// =============================================================================
// rtc.h — Reloj de tiempo real del dominio de backup [IR, §12.9]
//
// El RTC es el único periférico del dispositivo que vive FUERA del dominio de
// alimentación principal. Cuelga de VBAT y de su propio reloj (LSE, LSI o
// HSE/RTCPRE), así que sigue contando cuando el resto del chip está apagado, y
// un reset de sistema no lo toca: solo lo borra el reset del dominio de backup.
// Ese aislamiento es lo que le da sentido, y es lo primero que modela este
// bloque —el banco de pruebas lo comprueba reseteando el MCU y viendo que el
// calendario y los veinte registros de backup siguen donde estaban—.
//
// De ahí salen las tres rarezas del bloque, que no son caprichos sino
// consecuencias de estar al otro lado de una frontera de dominio:
//
//   1. PROTECCIÓN POR LLAVE. Los registros están bloqueados; hay que escribir
//      0xCA y después 0x53 en RTC_WPR para abrirlos [IR, §12.9.2]. Cualquier
//      otro valor vuelve a cerrar. Como el dominio sobrevive a los resets, un
//      programa desbocado podría corromper la hora de forma permanente.
//   2. MODO DE INICIALIZACIÓN. El calendario no se puede escribir en marcha:
//      hay que pedir ISR.INIT, esperar a INITF —que es el acuse de la otra
//      orilla— y solo entonces cargar TR y DR.
//   3. TODO EN BCD. La hora y la fecha se guardan en decimal codificado en
//      binario, no en binario, porque así se leen directamente sin dividir.
//
// El calendario avanza EVENTO A EVENTO, no flanco a flanco: se programa una
// cita por segundo de calendario y los subsegundos (SSR) se interpolan al
// leerlos. Con los prescaladores por defecto (128 x 256 sobre 32768 Hz) eso es
// un evento por segundo; bajándolos —que es una configuración legítima del
// manual— el calendario corre miles de veces más rápido, y es así como se puede
// verificar un año entero de vueltas de fecha en una simulación corta.
//
// Implementado en la fase F5:
//   * protección por llave (WPR) y modo de inicialización (INIT/INITF/INITS);
//   * prescaladores asíncrono y síncrono, ck_apre y ck_spre;
//   * calendario BCD completo con formato de 12 y 24 horas, día de la semana,
//     longitudes de mes y años bisiestos;
//   * subsegundos (SSR) interpolados;
//   * alarmas A y B con sus cuatro máscaras y selección de día del mes o día de
//     la semana;
//   * temporizador de despertar (WUT) con sus cinco fuentes de WUCKSEL;
//   * marca de tiempo (timestamp) y detección de manipulación (tamper) por el
//     pin RTC_AF1 (PC13);
//   * salida de calibración/alarma por RTC_OUT (COE, OSEL, POL);
//   * banderas de ISR con su semántica de borrado y las tres líneas de EXTI
//     (17 alarma, 21 tamper/timestamp, 22 despertar);
//   * los veinte registros de backup, que sobreviven al reset de sistema.
// =============================================================================
#ifndef STM32_PERIPH_RTC_H
#define STM32_PERIPH_RTC_H

#include <array>
#include "../common/periph_base.h"
#include "../common/guarda_tick.h"

namespace stm32 {

class Rtc : public BusSlave {
public:
    sc_core::sc_in<bool>   rtcclk{"rtcclk"};
    sc_core::sc_in<double> rtcclk_hz{"rtcclk_hz"};
    sc_core::sc_in<bool>   bkp_rst_n{"bkp_rst_n"};   // reset de dominio backup
    sc_core::sc_in<bool>   dbp{"dbp"};               // PWR_CR.DBP (protección)
    sc_core::sc_out<bool>  exti17_alarm{"exti17_alarm"};
    sc_core::sc_out<bool>  exti21_tamp_ts{"exti21_tamp_ts"};
    sc_core::sc_out<bool>  exti22_wakeup{"exti22_wakeup"};
    // AF adicionales: RTC_OUT/RTC_TAMP1/RTC_TS en PC13 [IR, §2.1]
    sc_core::sc_signal<bool> af1_out{"af1_out"}, af1_in{"af1_in"};

    // ---- Offsets [IR, §12.9.3] --------------------------------------------
    enum : uint32_t {
        R_TR = 0x00, R_DR = 0x04, R_CR = 0x08, R_ISR = 0x0C, R_PRER = 0x10,
        R_WUTR = 0x14, R_CALIBR = 0x18, R_ALRMAR = 0x1C, R_ALRMBR = 0x20,
        R_WPR = 0x24, R_SSR = 0x28, R_SHIFTR = 0x2C, R_TSTR = 0x30,
        R_TSDR = 0x34, R_TSSSR = 0x38, R_CALR = 0x3C, R_TAFCR = 0x40,
        R_ALRMASSR = 0x44, R_ALRMBSSR = 0x48, R_BKP0R = 0x50
    };
    enum IsrBit : uint32_t {
        I_ALRAWF = 1u << 0, I_ALRBWF = 1u << 1, I_WUTWF = 1u << 2,
        I_SHPF = 1u << 3, I_INITS = 1u << 4, I_RSF = 1u << 5,
        I_INITF = 1u << 6, I_INIT = 1u << 7, I_ALRAF = 1u << 8,
        I_ALRBF = 1u << 9, I_WUTF = 1u << 10, I_TSF = 1u << 11,
        I_TSOVF = 1u << 12, I_TAMP1F = 1u << 13
    };

    Rtc(sc_core::sc_module_name nm) : BusSlave(nm, addr::RTC_B, 0x400) {
        SC_HAS_PROCESS(Rtc);
        SC_THREAD(cal_proc);
        SC_THREAD(wut_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(bkp_rst_proc); sensitive << bkp_rst_n;
        SC_METHOD(clk_proc);    sensitive << rtcclk_hz;      dont_initialize();
        SC_METHOD(tamper_proc); sensitive << af1_in;          dont_initialize();
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    // Fecha de reset del silicio: 1 de enero de 2000, lunes -> DR = 0x2101
    struct Cal { unsigned s = 0, mi = 0, h = 0, d = 1, mo = 1, y = 0, wd = 1; };
    const Cal& calendar() const { return c_; }
    double   spre_hz() const;                       // ck_spre: el "segundo"
    uint64_t seconds() const { return n_sec_; }     // segundos de calendario
    bool     locked() const { return !unlocked_; }

protected:
    // ---- Registros ----
    uint32_t cr_ = 0, isr_ = I_ALRAWF | I_ALRBWF | I_WUTWF;   // reset: 0x0007
    uint32_t prer_ = 0x007F00FFu, wutr_ = 0x0000FFFFu, calibr_ = 0;
    uint32_t alrmar_ = 0, alrmbr_ = 0, calr_ = 0, tafcr_ = 0;
    uint32_t alrmassr_ = 0, alrmbssr_ = 0, shiftr_ = 0;
    uint32_t tstr_ = 0, tsdr_ = 0, tsssr_ = 0;
    std::array<uint32_t, 20> bkp_{};

    Cal      c_{};                       // calendario vivo
    uint64_t n_sec_ = 0;                 // segundos de calendario transcurridos
    sc_core::sc_time t_sec_{sc_core::SC_ZERO_TIME};   // instante del último
    unsigned wut_cnt_ = 0xFFFFu;
    unsigned wpr_step_ = 0;              // avance de la secuencia 0xCA / 0x53
    bool     unlocked_ = false;
    bool     o_l17_ = false, o_l21_ = false, o_l22_ = false, o_af1_ = false;
    // Debe arrancar con el MISMO nivel que la señal, o el primer flanco de
    // verdad se compara contra un valor que nunca existió.
    bool     tamp_prev_ = false;
    sc_core::sc_event ev_cal_, ev_wut_, pub_ev_;

    // =======================================================================
    // Relojes
    // =======================================================================
    unsigned prediv_a() const { return (prer_ >> 16) & 0x7Fu; }
    unsigned prediv_s() const { return prer_ & 0x7FFFu; }
    // ck_apre = RTCCLK / (PREDIV_A + 1); ck_spre = ck_apre / (PREDIV_S + 1)
    double apre_hz() const {
        const double f = rtcclk_hz.read();
        return (f > 0.0) ? f / double(prediv_a() + 1u) : 0.0;
    }
    bool     init_mode() const { return (isr_ & I_INIT) != 0; }
    bool     fmt12()  const { return (cr_ >> 6) & 1u; }
    bool     alrae()  const { return (cr_ >> 8) & 1u; }
    bool     alrbe()  const { return (cr_ >> 9) & 1u; }
    bool     wute()   const { return (cr_ >> 10) & 1u; }
    bool     tse()    const { return (cr_ >> 11) & 1u; }
    bool     alraie() const { return (cr_ >> 12) & 1u; }
    bool     alrbie() const { return (cr_ >> 13) & 1u; }
    bool     wutie()  const { return (cr_ >> 14) & 1u; }
    bool     tsie()   const { return (cr_ >> 15) & 1u; }
    unsigned wucksel() const { return cr_ & 7u; }
    unsigned osel()   const { return (cr_ >> 21) & 3u; }
    bool     pol()    const { return (cr_ >> 20) & 1u; }
    bool     coe()    const { return (cr_ >> 23) & 1u; }

    // =======================================================================
    // BCD. El calendario se guarda en decimal codificado en binario porque es
    // como lo entrega el silicio: así el firmware lo enseña sin dividir.
    // =======================================================================
    static uint32_t to_bcd(unsigned v) { return ((v / 10u) << 4) | (v % 10u); }
    static unsigned from_bcd(uint32_t v) { return ((v >> 4) & 0xFu) * 10u + (v & 0xFu); }

    uint32_t pack_tr() const {
        unsigned h = c_.h;
        unsigned pm = 0;
        if (fmt12()) {                       // formato de 12 horas con AM/PM
            pm = (h >= 12u) ? 1u : 0u;
            h = h % 12u; if (h == 0u) h = 12u;
        }
        return (pm << 22) | (to_bcd(h) << 16) | (to_bcd(c_.mi) << 8) | to_bcd(c_.s);
    }
    uint32_t pack_dr() const {
        return (to_bcd(c_.y) << 16) | (c_.wd << 13) |
               (to_bcd(c_.mo) << 8) | to_bcd(c_.d);
    }
    void unpack_tr(uint32_t v) {
        unsigned h = from_bcd((v >> 16) & 0x3Fu);
        if (fmt12()) {                       // de 12 horas a la cuenta interna
            const bool pm = (v >> 22) & 1u;
            if (h == 12u) h = 0u;
            if (pm) h += 12u;
        }
        c_.h  = h % 24u;
        c_.mi = from_bcd((v >> 8) & 0x7Fu) % 60u;
        c_.s  = from_bcd(v & 0x7Fu) % 60u;
    }
    void unpack_dr(uint32_t v) {
        c_.y  = from_bcd((v >> 16) & 0xFFu) % 100u;
        c_.wd = (v >> 13) & 7u; if (c_.wd == 0u) c_.wd = 1u;
        c_.mo = from_bcd((v >> 8) & 0x1Fu); if (c_.mo == 0u || c_.mo > 12u) c_.mo = 1u;
        c_.d  = from_bcd(v & 0x3Fu); if (c_.d == 0u) c_.d = 1u;
    }
    // El año del RTC son dos dígitos; el bisiesto se decide sobre 20xx, que es
    // lo que hace el silicio [IR, §12.9].
    static bool leap(unsigned y2) {
        const unsigned y = 2000u + y2;
        return (y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u);
    }
    static unsigned days_in(unsigned mo, unsigned y2) {
        static const unsigned t[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        if (mo == 2u && leap(y2)) return 29u;
        return t[mo <= 12u ? mo : 1u];
    }

    // =======================================================================
    // Banco de registros
    // =======================================================================
    // La protección por llave deja fuera unos pocos registros: las banderas
    // altas de ISR, TAFCR, el propio WPR y los de backup [IR, §12.9-mapa].
    static bool always_writable(uint32_t off) {
        return off == R_WPR || off == R_TAFCR || off >= R_BKP0R;
    }
    uint32_t reg_read(uint32_t off) override {
        if (off >= R_BKP0R && off < R_BKP0R + 80u) return bkp_[(off - R_BKP0R) / 4];
        switch (off) {
            case R_TR:  return pack_tr();
            case R_DR:  return pack_dr();
            case R_CR:  return cr_;
            case R_ISR: return isr_;
            case R_PRER: return prer_;
            case R_WUTR: return wutr_;
            case R_CALIBR: return calibr_;
            case R_ALRMAR: return alrmar_;
            case R_ALRMBR: return alrmbr_;
            case R_WPR: return 0;                    // solo escritura
            case R_SSR: return ssr_now();
            case R_SHIFTR: return 0;                 // solo escritura
            case R_TSTR: return tstr_;
            case R_TSDR: return tsdr_;
            case R_TSSSR: return tsssr_;
            case R_CALR: return calr_;
            case R_TAFCR: return tafcr_;
            case R_ALRMASSR: return alrmassr_;
            case R_ALRMBSSR: return alrmbssr_;
            default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        // Sin DBP en PWR_CR el dominio de backup no se deja escribir siquiera
        // [IR, §12.9-integración].
        if (!dbp.read()) return;

        if (off == R_WPR) {
            // La secuencia es 0xCA y después 0x53. Cualquier otro valor, en
            // cualquier punto, vuelve a cerrar [IR, §12.9.2].
            const uint32_t k = v & 0xFFu;
            if (wpr_step_ == 0 && k == 0xCAu) { wpr_step_ = 1; }
            else if (wpr_step_ == 1 && k == 0x53u) { wpr_step_ = 0; unlocked_ = true; }
            else { wpr_step_ = 0; unlocked_ = false; }
            return;
        }
        if (off >= R_BKP0R && off < R_BKP0R + 80u) {
            bkp_[(off - R_BKP0R) / 4] = v;           // no los tapa la llave
            return;
        }
        if (off == R_ISR) { write_isr(v); return; }  // las banderas tampoco
        if (!unlocked_ && !always_writable(off)) return;

        switch (off) {
            case R_TR:
                // El calendario SOLO se puede escribir en modo de
                // inicialización: en marcha, la escritura se ignora.
                if (isr_ & I_INITF) { unpack_tr(v); n_sec_ = 0; }
                return;
            case R_DR:
                if (isr_ & I_INITF) unpack_dr(v);
                return;
            case R_CR: {
                const uint32_t old = cr_;
                cr_ = v & 0x00FFFFDFu;
                if ((old ^ cr_) & (1u << 10)) {      // WUTE
                    if (wute()) { wut_cnt_ = wutr_ & 0xFFFFu; isr_ &= ~I_WUTWF; }
                    else        { isr_ |= I_WUTWF; }
                    ev_wut_.notify(sc_core::SC_ZERO_TIME);
                }
                if (!alrae()) isr_ |= I_ALRAWF; else isr_ &= ~I_ALRAWF;
                if (!alrbe()) isr_ |= I_ALRBWF; else isr_ &= ~I_ALRBWF;
                update_out();
                update_irq();
                return;
            }
            case R_PRER:
                if (isr_ & I_INITF) { prer_ = v & 0x007F7FFFu; ev_cal_.notify(sc_core::SC_ZERO_TIME); }
                return;
            case R_WUTR:
                // WUTR solo se toca con el temporizador parado (WUTWF = 1)
                if (isr_ & I_WUTWF) wutr_ = v & 0xFFFFu;
                return;
            case R_CALIBR:  calibr_ = v & 0x0001E1FFu; return;
            case R_ALRMAR:  if (isr_ & I_ALRAWF) alrmar_ = v; return;
            case R_ALRMBR:  if (isr_ & I_ALRBWF) alrmbr_ = v; return;
            case R_SHIFTR:  shiftr_ = v; return;     // sin efecto: véase el informe
            case R_CALR:    calr_ = v & 0x0000E1FFu; return;
            case R_TAFCR:   tafcr_ = v & 0x0007FF0Fu; return;
            case R_ALRMASSR: if (isr_ & I_ALRAWF) alrmassr_ = v & 0x0F007FFFu; return;
            case R_ALRMBSSR: if (isr_ & I_ALRBWF) alrmbssr_ = v & 0x0F007FFFu; return;
            default: return;
        }
    }
    unsigned access_cycles(bool) const override { return 2; }

    void write_isr(uint32_t v) {
        // INIT lo pone y lo quita el firmware; INITF es el acuse del otro lado
        // de la frontera de dominio, y aquí llega de inmediato.
        const bool want = (v & I_INIT) != 0;
        if (want && !(isr_ & I_INIT)) {
            isr_ |= I_INIT | I_INITF;
            ev_cal_.notify(sc_core::SC_ZERO_TIME);
        } else if (!want && (isr_ & I_INIT)) {
            isr_ &= ~(I_INIT | I_INITF);
            isr_ |= I_INITS | I_RSF;             // el calendario ya está puesto
            t_sec_ = sc_core::sc_time_stamp();
            ev_cal_.notify(sc_core::SC_ZERO_TIME);
        }
        // Las banderas de suceso son rc_w0: escribir CERO las borra y escribir
        // uno las deja como estaban. Los demás bits de ISR no los toca esta
        // escritura [IR, §12.9-mapa].
        const uint32_t w0 = I_ALRAF | I_ALRBF | I_WUTF | I_TSF | I_TSOVF |
                            I_TAMP1F | I_RSF;
        isr_ = (isr_ & ~w0) | (isr_ & w0 & v);
        update_irq();
    }

    // =======================================================================
    // Calendario
    // =======================================================================
    // Subsegundos: cuenta descendente desde PREDIV_S, interpolada [IR, §12.9].
    uint32_t ssr_now() const {
        const double fa = apre_hz();
        if (fa <= 0.0 || init_mode()) return 0;
        const double dt = (sc_core::sc_time_stamp() - t_sec_).to_seconds();
        const unsigned n = unsigned(dt * fa + GUARDA_TICK);
        const unsigned ps = prediv_s();
        return (n > ps) ? 0u : (ps - n);
    }

    void tick_second() {
        ++n_sec_;
        if (++c_.s < 60u) return;
        c_.s = 0;
        if (++c_.mi < 60u) return;
        c_.mi = 0;
        if (++c_.h < 24u) return;
        c_.h = 0;
        c_.wd = (c_.wd % 7u) + 1u;               // 1 = lunes .. 7 = domingo
        if (++c_.d <= days_in(c_.mo, c_.y)) return;
        c_.d = 1;
        if (++c_.mo <= 12u) return;
        c_.mo = 1;
        c_.y = (c_.y + 1u) % 100u;
    }

    void cal_proc() {
        for (;;) {
            const double f = spre_hz();
            if (f <= 0.0 || init_mode()) { wait(ev_cal_); continue; }
            wait(sc_core::sc_time(1.0 / f, sc_core::SC_SEC), ev_cal_);
            if (init_mode() || spre_hz() <= 0.0) continue;
            t_sec_ = sc_core::sc_time_stamp();
            tick_second();
            check_alarms();
            if (wute() && wucksel() >= 4u) tick_wut();   // WUT sobre ck_spre
        }
    }

    // --- Alarmas -----------------------------------------------------------
    // Cada máscara apaga un campo de la comparación: con MSK4..MSK1 a uno la
    // alarma salta cada segundo, y quitándolas se va afinando hasta un instante
    // concreto del mes [IR, §12.9-mapa].
    bool alarm_hit(uint32_t a) const {
        const bool msk1 = (a >> 7) & 1u, msk2 = (a >> 15) & 1u;
        const bool msk3 = (a >> 23) & 1u, msk4 = (a >> 31) & 1u;
        const bool wdsel = (a >> 30) & 1u;
        if (!msk1 && from_bcd(a & 0x7Fu) != c_.s) return false;
        if (!msk2 && from_bcd((a >> 8) & 0x7Fu) != c_.mi) return false;
        if (!msk3) {
            unsigned h = from_bcd((a >> 16) & 0x3Fu);
            if (fmt12()) {
                const bool pm = (a >> 22) & 1u;
                if (h == 12u) h = 0u;
                if (pm) h += 12u;
            }
            if (h != c_.h) return false;
        }
        if (!msk4) {
            const unsigned f = from_bcd((a >> 24) & 0x3Fu);
            if (wdsel) { if (((a >> 24) & 0xFu) != c_.wd) return false; }
            else       { if (f != c_.d) return false; }
        }
        return true;
    }
    void check_alarms() {
        bool ch = false;
        if (alrae() && alarm_hit(alrmar_) && !(isr_ & I_ALRAF)) { isr_ |= I_ALRAF; ch = true; }
        if (alrbe() && alarm_hit(alrmbr_) && !(isr_ & I_ALRBF)) { isr_ |= I_ALRBF; ch = true; }
        if (ch) update_irq();
        update_out();
    }

    // --- Temporizador de despertar ----------------------------------------
    // WUCKSEL elige entre cuatro divisiones de RTCCLK (16, 8, 4, 2) y el propio
    // segundo del calendario, con o sin los 2^16 añadidos [IR, §12.9-mapa].
    double wut_hz() const {
        const double f = rtcclk_hz.read();
        switch (wucksel() & 3u) {
            case 0: return f / 16.0;
            case 1: return f / 8.0;
            case 2: return f / 4.0;
            default: return f / 2.0;
        }
    }
    void tick_wut() {
        if (wut_cnt_ == 0u) {
            wut_cnt_ = wutr_ & 0xFFFFu;
            if (!(isr_ & I_WUTF)) { isr_ |= I_WUTF; update_irq(); }
        } else {
            --wut_cnt_;
        }
    }
    void wut_proc() {
        for (;;) {
            if (!wute() || wucksel() >= 4u || wut_hz() <= 0.0) { wait(ev_wut_); continue; }
            wait(sc_core::sc_time(1.0 / wut_hz(), sc_core::SC_SEC), ev_wut_);
            if (!wute() || wucksel() >= 4u) continue;
            tick_wut();
        }
    }

    // --- Timestamp y tamper -------------------------------------------------
    void tamper_proc() {
        const bool now = af1_in.read();
        const bool prev = tamp_prev_;
        tamp_prev_ = now;
        // Marca de tiempo: captura el calendario en el flanco que pida TSEDGE.
        if (tse()) {
            const bool falling = (cr_ >> 3) & 1u;       // TSEDGE
            const bool edge = falling ? (prev && !now) : (!prev && now);
            if (edge) {
                if (isr_ & I_TSF) isr_ |= I_TSOVF;      // no se leyó la anterior
                tstr_ = pack_tr(); tsdr_ = pack_dr(); tsssr_ = ssr_now();
                isr_ |= I_TSF;
                update_irq();
            }
        }
        // Manipulación: TAMP1E con su polaridad en TAFCR [IR, §12.9-mapa].
        if (tafcr_ & 1u) {
            const bool trg = (tafcr_ >> 1) & 1u;        // TAMP1TRG
            const bool edge = trg ? (prev && !now) : (!prev && now);
            if (edge && !(isr_ & I_TAMP1F)) {
                isr_ |= I_TAMP1F;
                update_irq();
            }
        }
    }

    // =======================================================================
    // Salidas
    // =======================================================================
    void update_out() {
        // RTC_OUT saca la alarma seleccionada por OSEL, con la polaridad de POL
        // [IR, §12.9-mapa]. Es la salida que se lleva a PC13.
        bool o = false;
        if (coe() || osel() != 0u) {
            switch (osel()) {
                case 1: o = (isr_ & I_ALRAF) != 0; break;
                case 2: o = (isr_ & I_ALRBF) != 0; break;
                case 3: o = (isr_ & I_WUTF)  != 0; break;
                default: o = false; break;
            }
            if (pol()) o = !o;
        }
        if (o != o_af1_) { o_af1_ = o; publish(); }
    }
    void update_irq() {
        const bool l17 = ((isr_ & I_ALRAF) && alraie()) || ((isr_ & I_ALRBF) && alrbie());
        const bool l21 = ((isr_ & I_TSF) && tsie()) || ((isr_ & I_TAMP1F) && (tafcr_ & (1u << 2)));
        const bool l22 = (isr_ & I_WUTF) && wutie();
        if (l17 != o_l17_ || l21 != o_l21_ || l22 != o_l22_) {
            o_l17_ = l17; o_l21_ = l21; o_l22_ = l22;
            publish();
        }
        update_out();
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        exti17_alarm.write(o_l17_);
        exti21_tamp_ts.write(o_l21_);
        exti22_wakeup.write(o_l22_);
        af1_out.write(o_af1_);
    }
    void clk_proc() { ev_cal_.notify(sc_core::SC_ZERO_TIME);
                      ev_wut_.notify(sc_core::SC_ZERO_TIME); }

    // El reset de SISTEMA no aparece por aquí a propósito: al RTC solo lo borra
    // el reset del DOMINIO DE BACKUP (BDRST en RCC_BDCR, o la pérdida de VBAT).
    // Es la propiedad que define al bloque [IR, §12.9.1].
    void bkp_rst_proc() {
        if (bkp_rst_n.read()) return;
        cr_ = 0; isr_ = I_ALRAWF | I_ALRBWF | I_WUTWF;
        prer_ = 0x007F00FFu; wutr_ = 0x0000FFFFu; calibr_ = 0;
        alrmar_ = alrmbr_ = calr_ = tafcr_ = 0;
        alrmassr_ = alrmbssr_ = shiftr_ = 0;
        tstr_ = tsdr_ = tsssr_ = 0;
        bkp_.fill(0);
        c_ = Cal{};                              // 1 de enero de 2001, 00:00:00
        n_sec_ = 0; wut_cnt_ = 0xFFFFu;
        wpr_step_ = 0; unlocked_ = false;
        tamp_prev_ = af1_in.read();
        o_l17_ = o_l21_ = o_l22_ = o_af1_ = false;
        t_sec_ = sc_core::sc_time_stamp();
        publish();
        ev_cal_.notify(sc_core::SC_ZERO_TIME);
        ev_wut_.notify(sc_core::SC_ZERO_TIME);
    }
};

// ck_spre = RTCCLK / ((PREDIV_A + 1) * (PREDIV_S + 1)). Con los valores de
// reset y un LSE de 32768 Hz sale exactamente 1 Hz [IR, §12.9-mapa].
inline double Rtc::spre_hz() const {
    const double fa = apre_hz();
    return (fa > 0.0) ? fa / double(prediv_s() + 1u) : 0.0;
}

} // namespace stm32
#endif // STM32_PERIPH_RTC_H
