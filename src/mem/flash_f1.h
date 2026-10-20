// =============================================================================
// flash_f1.h — La Flash de los STM32 de la familia F0/F1/F3 y su controlador
// (el FPEC): la lectura (fase S1 del F303) y la programacion (S2)
//
// LA MEMORIA: `size` bytes en 0x0800_0000, en páginas de 2 KB [HAL:
// FLASH_PAGE_SIZE], que se leen por líneas de 64 bits. Con LATENCY estados de
// espera cada línea que no esté en el búfer de prebúsqueda; con la prebúsqueda
// encendida (PRFTBE, que viene encendida de reset), el búfer guarda la línea
// en uso y trae la siguiente sin coste, de modo que el código secuencial no
// espera: el bufer de prebusqueda son dos bloques de 8 bytes, y con el CPU
// pidiendo una palabra la siguiente ya esta [RM0316 §4.2.2]. Los datos
// (literales, tablas) van por DCode y llevan un registro de línea propio.
//
// EL CONTROLADOR, en 0x4002_2000 [CMSIS: FLASH_TypeDef]:
//   0x00 ACR      LATENCY (2:0), HLFCYA (3), PRFTBE (4), PRFTBS (5)
//   0x04 KEYR     0x45670123 y luego 0xCDEF89AB abren CR [CMSIS: FLASH_KEY1/2]
//   0x08 OPTKEYR  las mismas, para los bytes de opción
//   0x0C SR       BSY, PGERR, WRPERR, EOP
//   0x10 CR       PG, PER, MER, OPTPG, OPTER, STRT, LOCK, OPTWRE, ERRIE,
//                 EOPIE, OBL_LAUNCH
//   0x14 AR       la página que borra PER; el FPEC deja ahí la última
//                 dirección que ha usado [RM0316 §4.5.6]
//   0x1C OBR      lo que se cargó de los bytes de opción: RDPRT, USER, DATA0/1
//   0x20 WRPR     la protección de escritura cargada
//
// LOS BYTES DE OPCIÓN, en 0x1FFF_F800: RDP, USER, DATA0, DATA1 y WRP0-3, cada
// uno en una media palabra con su complemento en el byte alto [CMSIS:
// OB_TypeDef] [RM0316 §5, tabla 13]. Se cargan en OBR/WRPR con CADA reset del
// sistema [RM0316 §5] (el del pin, los perros, SYSRESETREQ, OBL_LAUNCH y el de
// alimentación).
//
// LO QUE VIGILA (el silicio no lo hace y lee basura en silencio): que LATENCY
// alcance para el HCLK en vigor -0 hasta 24 MHz, 1 hasta 48, 2 hasta 72
// [RM0316 §4.5.1]-, y que el acceso de medio ciclo (HLFCYA) no se use por
// encima de 8 MHz. RM0316 §4.2.2 solo dice que es para cuando no hay un reloj
// de alta frecuencia y que no vale con preescalador en AHB: los 8 MHz son el
// HSI, y son del modelo.
//
// PROGRAMAR Y BORRAR (S2) [RM0316 §4.2.3, §4.3 y §4.5]:
//   - KEYR abre CR; una llave mal es un error de bus (HardFault) y deja el
//     FPEC cerrado hasta el siguiente reset. Una llave con CR ya abierto
//     tambien lo cierra [de modelo: RM0316 no lo precisa]. OPTKEYR, con CR
//     abierto, pone OPTWRE. Con CR cerrado solo se puede escribir OBL_LAUNCH,
//     y con BSY no se escribe ningun registro.
//   - PG: cada escritura de MEDIA PALABRA en la Flash la programa. Un byte o
//     una palabra dan error de bus, como en el silicio. Si la media palabra no
//     estaba borrada (y el dato no es 0x0000), PGERR y no se programa; en una
//     pagina protegida por WRP, WRPRTERR.
//   - PER + AR + STRT borra una pagina de 2 KB; MER + STRT, todas. STRT se
//     lee a uno mientras dura.
//   - OPTER + STRT borra los bytes de opcion; OPTPG programa uno con una
//     escritura de media palabra (el complemento lo pone el FPEC), y solo si
//     estaba borrado: aqui no hay excepcion para el cero. Ambos necesitan
//     OPTWRE. OBL_LAUNCH los recarga con un reset del sistema.
//   - BSY mientras dura; al acabar bien, EOP (con su interrupcion, EOPIE).
//     Quien lee la Flash mientras tanto espera, como el bus del silicio.
//   - La proteccion de escritura va por pares de paginas: un bit de WRPR por
//     cada dos (WRP0-WRP2 y los siete primeros de WRP3), y el ultimo, para las
//     paginas 62 a 127 [RM0316 §4.3.2 y §5, tabla 14].
//   - El nivel de RDP sale del byte y su complemento: 0xAA/0x55 es el 0,
//     0xCC/0x33 el 2 y cualquier otra cosa el 1 [RM0316 §4.3.1, tabla 8].
//     Volver al nivel 0 programando 0xAA borra la Flash entera; en el nivel 2
//     los bytes de opcion no se borran y RDP no se programa (WRPRTERR).
//   - Los tiempos [DS9118, tabla 47]: 53,5 us una media palabra (el tipico,
//     entre 40 y 60), y 20 ms una pagina, el borrado masivo y el de los bytes
//     de opcion (el minimo: la tabla da 20-40 ms y ningun tipico).
// Lo que NO hace: que el nivel 1 cierre la Flash al depurador y a quien
// arranque de la SRAM o del cargador, ni que el 2 apague la depuracion: esta
// DECLARADO.
// =============================================================================
#ifndef STM32_MEM_FLASH_F1_H
#define STM32_MEM_FLASH_F1_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../common/periph_base.h"
#include "../common/ahb_types.h"

namespace stm32 {

class FlashF1 : public BusSlave {
public:
    enum : uint32_t {
        R_ACR = 0x00, R_KEYR = 0x04, R_OPTKEYR = 0x08, R_SR = 0x0C, R_CR = 0x10,
        R_AR = 0x14, R_OBR = 0x1C, R_WRPR = 0x20
    };
    static constexpr uint32_t KEY1 = 0x45670123u, KEY2 = 0xCDEF89ABu;
    static constexpr uint32_t SR_BSY = 1u << 0, SR_PGERR = 1u << 2, SR_WRPERR = 1u << 4,
                              SR_EOP = 1u << 5;
    static constexpr uint32_t CR_PG = 1u << 0, CR_PER = 1u << 1, CR_MER = 1u << 2,
                              CR_OPTPG = 1u << 4, CR_OPTER = 1u << 5, CR_STRT = 1u << 6,
                              CR_LOCK = 1u << 7, CR_OPTWRE = 1u << 9, CR_ERRIE = 1u << 10,
                              CR_EOPIE = 1u << 12, CR_OBL_LAUNCH = 1u << 13;
    static constexpr uint32_t ACR_RESET = 0x30u;      // PRFTBE y PRFTBS [RM0316 §4.5.1]
    static constexpr unsigned LINEA = 8;              // 64 bits

    // La memoria (Flash principal) y la ventana de los bytes de opción
    tlm_utils::simple_target_socket<FlashF1> mem{"mem"};
    tlm_utils::simple_target_socket<FlashF1> ob{"ob"};
    sc_core::sc_out<bool> irq{"irq"};

    // Quién da los estados de espera mínimos para una frecuencia; lo pone el
    // SoC con los de su chip
    std::function<unsigned(double)> latencia_minima;
    // OBL_LAUNCH: el SoC pide al RCC un reset del sistema con OBLRSTF
    std::function<void()> al_obl;
    // Los tiempos del FPEC [DS9118, tabla 47]
    sc_core::sc_time t_prog{53500, sc_core::SC_NS};
    sc_core::sc_time t_pagina{20, sc_core::SC_MS};
    sc_core::sc_time t_masa{20, sc_core::SC_MS};

    FlashF1(sc_core::sc_module_name nm, uint32_t reg_base, uint32_t mem_base,
            uint32_t size, uint32_t pagina, uint32_t ob_base)
        : BusSlave(nm, reg_base, 0x400), mem_base_(mem_base), pagina_(pagina),
          ob_base_(ob_base), arr_(size, 0xFF) {
        SC_HAS_PROCESS(FlashF1);
        mem.register_b_transport(this, &FlashF1::bt_mem);
        mem.register_transport_dbg(this, &FlashF1::dbg_mem);
        ob.register_b_transport(this, &FlashF1::bt_ob);
        ob.register_transport_dbg(this, &FlashF1::dbg_ob);
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(hz_proc); sensitive << clk_hz; dont_initialize();
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(fin_proc); sensitive << fin_ev_; dont_initialize();
        ob_fabrica();
        carga_opciones();
    }

    // --- Para el SoC, el cargador y el banco --------------------------------------
    uint32_t size() const { return uint32_t(arr_.size()); }
    uint32_t mem_base() const { return mem_base_; }
    bool es_flash(uint32_t a) const { return a >= mem_base_ && a - mem_base_ < arr_.size(); }
    bool poke8(uint32_t a, uint8_t v) {
        if (!es_flash(a)) return false;
        arr_[a - mem_base_] = v;
        return true;
    }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= uint32_t(es_flash(a + i) ? arr_[a + i - mem_base_] : 0xFFu) << (8 * i);
        return v;
    }
    uint16_t ob_half(unsigned i) const { return i < 8 ? ob_[i] : 0xFFFFu; }
    void borra_todo() { std::fill(arr_.begin(), arr_.end(), uint8_t(0xFF)); }
    unsigned latencia() const { return acr_ & 7u; }
    bool prebusqueda() const { return (acr_ >> 4) & 1u; }
    uint64_t esperas() const { return n_esperas_; }
    unsigned avisos() const { return n_avisos_; }
    unsigned escrituras_rechazadas() const { return n_rech_; }
    unsigned programadas() const { return n_prog_; }
    unsigned borradas() const { return n_borr_; }
    bool ocupada() const { return sr_ & SR_BSY; }
    // Lo cargado de los bytes de opción: nBOOT1 (USER bit 4) para el arranque
    bool nboot1() const { return (obr_ >> 12) & 1u; }
    // El resto del byte USER en FLASH_OBR [RM0316 §4.5.7]: WDG_SW (bit 8; 0 =
    // el IWDG arranca solo), nRST_STOP (9) y nRST_STDBY (10; 0 = entrar en el
    // modo es un reset)
    bool wdg_sw() const { return (obr_ >> 8) & 1u; }
    bool nrst_stop() const { return (obr_ >> 9) & 1u; }
    bool nrst_stdby() const { return (obr_ >> 10) & 1u; }
    uint8_t rdp_cargado() const { return rdp_carg_; }
    unsigned nivel_rdp() const { return (obr_ >> 1) & 3u; }     // 0, 1 o 3 (= nivel 2)
    bool bloqueo_duro() const { return bloqueo_duro_; }
    // Con HCLK cambiado, revisar la latencia (también lo hace solo, por clk_hz)
    void revisa() { revisa_latencia(); }
    // El banco: un byte de opción como si se hubiera programado (con su
    // complemento). Vale en la siguiente carga: encendido u OBL_LAUNCH
    void pon_ob(unsigned i, uint8_t v) { if (i < 8) ob_[i] = uint16_t(v | ((~v & 0xFFu) << 8)); }
    // Y una media palabra tal cual, con el complemento que se quiera
    void pon_ob_crudo(unsigned i, uint16_t h) { if (i < 8) ob_[i] = h; }

    // Una llave mal en KEYR es un error de bus [RM0316 §4.2.3]
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        error_bus_ = false;
        BusSlave::b_transport(gp, t);
        if (error_bus_ && gp.get_response_status() == tlm::TLM_OK_RESPONSE)
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_ACR:  return acr_ | (prebusqueda() ? (1u << 5) : 0u);
            case R_SR:   return sr_;
            case R_CR:   return cr_ | ((sr_ & SR_BSY) && arranco_ ? CR_STRT : 0u);
            case R_AR:   return ar_;
            case R_OBR:  return obr_;
            case R_WRPR: return wrpr_;
            default:     return 0;          // KEYR y OPTKEYR: solo escritura
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu && off != R_KEYR && off != R_OPTKEYR) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        switch (off) {
            case R_ACR:
                acr_ = v & 0x1Fu;                   // LATENCY, HLFCYA, PRFTBE
                lin_i_[0] = lin_i_[1] = lin_d_ = ~0u;
                revisa_latencia();
                return;
            case R_KEYR:     escribe_llave(v); return;
            case R_OPTKEYR:  escribe_llave_ob(v); return;
            case R_SR:
                sr_ &= ~(v & (SR_PGERR | SR_WRPERR | SR_EOP));   // rc_w1
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            case R_CR:       escribe_cr(v); return;
            case R_AR:       if (!(sr_ & SR_BSY)) ar_ = v; return;     // [RM0316 §4.5.6]
            default:         return;
        }
    }

    // --- Programar y borrar (S2) ---------------------------------------------------
    void escribe_cr(uint32_t v) {
        // Cerrado, CR solo admite OBL_LAUNCH; con BSY, nada [RM0316 §4.2.3]
        if (sr_ & SR_BSY) return;
        if (cr_ & CR_LOCK) {
            if (v & CR_OBL_LAUNCH) { if (al_obl) al_obl(); else carga_opciones(); }
            return;
        }
        const uint32_t w = CR_PG | CR_PER | CR_MER | CR_OPTPG | CR_OPTER | CR_LOCK |
                           CR_ERRIE | CR_EOPIE;
        // OPTWRE solo lo pone OPTKEYR; escribirle un cero lo quita
        const uint32_t optwre = (v & CR_OPTWRE) ? (cr_ & CR_OPTWRE) : 0u;
        cr_ = (cr_ & ~(w | CR_OPTWRE)) | (v & w) | optwre;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        if (cr_ & CR_LOCK) { cr_ &= ~CR_OPTWRE; return; }
        if ((v & CR_STRT) && !(sr_ & SR_BSY)) {
            if (cr_ & CR_PER)        borra_pagina();
            else if (cr_ & CR_MER)   borra_masa();
            else if (cr_ & CR_OPTER) borra_ob();
        }
        if (v & CR_OBL_LAUNCH) {
            if (al_obl) al_obl();
            else carga_opciones();
        }
    }
    // Protegida por WRP: en el F303xC cada bit de WRPR guarda dos paginas
    // (4 KB) y el ultimo, el 31, todas las de la 62 en adelante [RM0316 §5,
    // tabla 14]
    bool protegida(uint32_t off) const {
        unsigned b = off / (2u * pagina_);
        if (b > 31) b = 31;
        return !((wrpr_ >> b) & 1u);
    }
    void empieza(sc_core::sc_time dur, sc_core::sc_time t_local = sc_core::SC_ZERO_TIME) {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp() + t_local;
        ocupada_hasta_ = ahora + dur;
        sr_ |= SR_BSY;
        arranco_ = arranco_siguiente_;
        arranco_siguiente_ = false;
        fin_ev_.cancel();
        fin_ev_.notify(dur + t_local);
    }
    void error(uint32_t bit) {
        sr_ |= bit;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void olvida_lineas() { lin_i_[0] = lin_i_[1] = lin_d_ = ~0u; }
    void borra_pagina() {
        const uint32_t base = ar_ - mem_base_;
        if (ar_ < mem_base_ || base >= arr_.size()) { error(SR_PGERR); return; }
        const uint32_t p0 = base & ~(pagina_ - 1u);
        if (protegida(p0)) { error(SR_WRPERR); return; }
        std::fill(arr_.begin() + p0, arr_.begin() + p0 + pagina_, uint8_t(0xFF));
        ++n_borr_;
        olvida_lineas();
        arranco_siguiente_ = true;
        empieza(t_pagina);
    }
    void borra_masa() {
        if (wrpr_ != 0xFFFFFFFFu) { error(SR_WRPERR); return; }
        borra_todo();
        n_borr_ += unsigned(arr_.size() / pagina_);
        olvida_lineas();
        arranco_siguiente_ = true;
        empieza(t_masa);
    }
    void borra_ob() {
        // En el nivel 2 los bytes de opcion no se borran [RM0316 §4.3.1]
        if (!(cr_ & CR_OPTWRE) || nivel_rdp() == 3) { error(SR_WRPERR); return; }
        for (uint16_t& h : ob_) h = 0xFFFFu;
        arranco_siguiente_ = true;
        empieza(t_pagina);
    }
    // Una escritura en la memoria: con PG, una media palabra programa
    bool escribe_mem(uint32_t a, const uint8_t* d, unsigned n, sc_core::sc_time& t) {
        if (!(cr_ & CR_PG) || n != 2 || (a & 1u)) {
            ++n_rech_;
            aviso_escritura(n, (cr_ & CR_PG) != 0);
            return false;                                   // error de bus
        }
        const uint32_t off = a - mem_base_;
        const uint16_t v = uint16_t(d[0] | (d[1] << 8));
        const uint16_t hay = uint16_t(arr_[off] | (arr_[off + 1] << 8));
        if (protegida(off)) { error(SR_WRPERR); return true; }
        if (hay != 0xFFFFu && v != 0) { error(SR_PGERR); return true; }
        arr_[off] = uint8_t(v);
        arr_[off + 1] = uint8_t(v >> 8);
        ar_ = a;                                            // [RM0316 §4.5.6]
        ++n_prog_;
        olvida_lineas();
        empieza(t_prog, t);
        return true;
    }
    // Un byte de opcion: OPTPG con OPTWRE, media palabra; el complemento del
    // byte alto lo pone el FPEC
    bool escribe_ob(uint32_t a, const uint8_t* d, unsigned n, sc_core::sc_time& t) {
        if (!(cr_ & CR_OPTPG) || n != 2 || (a & 1u)) {
            ++n_rech_;
            aviso_escritura(n, (cr_ & CR_OPTPG) != 0);
            return false;
        }
        if (!(cr_ & CR_OPTWRE)) { error(SR_WRPERR); return true; }
        const unsigned i = (a - ob_base_) / 2;
        const uint8_t b = d[0];
        // En el nivel 2, RDP no se programa [RM0316 §4.3.1]
        if (i == 0 && nivel_rdp() == 3) { error(SR_WRPERR); return true; }
        // Solo si estaba borrado, sin excepcion para el cero [RM0316 §4.2.3]
        if (ob_[i] != 0xFFFFu) { error(SR_PGERR); return true; }
        ob_[i] = uint16_t(b | ((~b & 0xFFu) << 8));
        ar_ = a;
        sc_core::sc_time dur = t_prog;
        // Volver al nivel 0 borra antes la Flash entera [RM0316 §4.3.1]
        if (i == 0 && b == 0xAA && nivel_rdp() != 0) {
            borra_todo();
            n_borr_ += unsigned(arr_.size() / pagina_);
            olvida_lineas();
            dur += t_masa;
        }
        empieza(dur, t);
        return true;
    }
    void aviso_escritura(unsigned n, bool con_pg) {
        if (avisado_esc_) return;
        avisado_esc_ = true;
        ++n_avisos_;
        char m[220];
        if (con_pg)
            std::snprintf(m, sizeof m, "escritura de %u bytes en la Flash con PG: el FPEC solo "
                          "programa medias palabras alineadas; error de bus [RM0316 §4.2.3]", n);
        else
            std::snprintf(m, sizeof m, "escritura en la Flash sin FLASH_CR.PG (ni OPTPG en los "
                          "bytes de opcion): error de bus");
        SC_REPORT_WARNING("flash_f1", m);
    }
    void fin_proc() {
        sr_ &= ~SR_BSY;
        arranco_ = false;
        sr_ |= SR_EOP;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // El estado
    uint32_t mem_base_, pagina_, ob_base_;
    std::vector<uint8_t> arr_;
    uint16_t ob_[8] = {};
    uint32_t acr_ = ACR_RESET, sr_ = 0, cr_ = CR_LOCK, ar_ = 0, obr_ = 0, wrpr_ = 0xFFFFFFFFu;
    uint8_t  rdp_carg_ = 0xAA;
    unsigned llave_ = 0, llave_ob_ = 0;
    bool     bloqueo_duro_ = false;          // una llave mal: cerrado hasta el reset
    bool     error_bus_ = false;             // la escritura en curso da error de bus
    bool     arranco_ = false, arranco_siguiente_ = false;   // STRT, mientras dura
    sc_core::sc_time ocupada_hasta_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_event irq_ev_, fin_ev_;
    unsigned n_avisos_ = 0, n_rech_ = 0, n_prog_ = 0, n_borr_ = 0;
    uint64_t n_esperas_ = 0;
    bool avisado_esc_ = false, avisado_lat_ = false, avisado_hlf_ = false;
    uint32_t lin_i_[2] = {~0u, ~0u};         // la línea en uso y la adelantada
    uint32_t lin_d_ = ~0u;

    // Los bytes de opción de fábrica: RDP nivel 0 (0xAA), USER = 0xFF (perro por
    // software, nBOOT1 = 1, monitor de VDDA, sin paridad de la SRAM), DATA a
    // 0xFF y sin protección de escritura [HAL: OB_*; RM0316 §4.3.1: el nivel 0
    // es la configuracion de fabrica] [1F: el USER de fábrica]
    void ob_fabrica() {
        const uint8_t v[8] = {0xAA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        for (unsigned i = 0; i < 8; ++i) ob_[i] = uint16_t(v[i] | ((~v[i] & 0xFFu) << 8));
    }
    // OBR y WRPR a partir de los bytes de opción. Un byte cuyo complemento no
    // cuadra da OPTERR y se carga como 0xFF; con los dos a 0xFF (borrado) el
    // comparador no mira [RM0316 §5]. El nivel de RDP, de la tabla 8 de §4.3.1:
    // 0xAA/0x55 el 0, 0xCC/0x33 el 2 (RDPRT = 11), cualquier otra cosa el 1
    void carga_opciones() {
        bool err = false;
        auto byte = [&](unsigned i) -> uint8_t {
            const uint16_t h = ob_[i];
            if (h == 0xFFFFu) return 0xFF;      // borrado: sin programar, sin error
            if (uint8_t(h) != uint8_t(~(h >> 8))) { err = true; return 0xFF; }
            return uint8_t(h);
        };
        rdp_carg_ = uint8_t(ob_[0]);
        const uint32_t nivel = ob_[0] == 0x55AAu ? 0u : (ob_[0] == 0x33CCu ? 3u : 1u);
        (void)byte(0);                          // OPTERR, tambien por RDP
        const uint8_t user = byte(1), d0 = byte(2), d1 = byte(3);
        uint32_t wrp = 0;
        for (unsigned i = 0; i < 4; ++i) wrp |= uint32_t(byte(4 + i)) << (8 * i);
        obr_ = (err ? 1u : 0u) | (nivel << 1) | (uint32_t(user) << 8) |
               (uint32_t(d0) << 16) | (uint32_t(d1) << 24);
        wrpr_ = wrp;
    }

    void escribe_llave(uint32_t v) {
        if (bloqueo_duro_ || !(cr_ & CR_LOCK)) {
            // Tras una secuencia mala, cerrado hasta el reset; una llave con CR
            // ya abierto tambien lo cierra [de modelo]
            if (!(cr_ & CR_LOCK)) { cr_ |= CR_LOCK; cr_ &= ~CR_OPTWRE; }
            bloqueo_duro_ = true;
            return;
        }
        if (llave_ == 0 && v == KEY1) { llave_ = 1; return; }
        if (llave_ == 1 && v == KEY2) { llave_ = 0; cr_ &= ~CR_LOCK; return; }
        // Una llave mal: error de bus en la primera escritura si KEY1 no
        // cuadra, o en la segunda si es KEY2 la que no; y el FPEC cerrado
        // hasta el siguiente reset [RM0316 §4.2.3]
        llave_ = 0;
        bloqueo_duro_ = true;
        error_bus_ = true;
    }
    void escribe_llave_ob(uint32_t v) {
        if (cr_ & CR_LOCK) return;
        if (llave_ob_ == 0 && v == KEY1) { llave_ob_ = 1; return; }
        if (llave_ob_ == 1 && v == KEY2) { llave_ob_ = 0; cr_ |= CR_OPTWRE; return; }
        llave_ob_ = 0;
    }

    void revisa_latencia() {
        // En reset no hay lecturas: el ACR vuelve a cero antes de que el RCC
        // baje el reloj, y eso no es una secuencia mala del firmware
        if (!rst_n.read()) return;
        const double f = domain_hz();
        if (f <= 0.0) return;
        const unsigned m = latencia_minima ? latencia_minima(f) : 0u;
        const bool mal = latencia() < m;
        if (mal && !avisado_lat_) {
            ++n_avisos_;
            char b[300];
            std::snprintf(b, sizeof b, "HCLK = %.3g MHz con FLASH_ACR.LATENCY = %u: hacen "
                          "falta %u estados de espera, y en el silicio se leen instrucciones "
                          "equivocadas. Sube LATENCY ANTES de subir el reloj "
                          "(HAL_RCC_ClockConfig con FLASH_LATENCY_%u)", f / 1e6, latencia(), m, m);
            SC_REPORT_WARNING("flash_f1", b);
        }
        avisado_lat_ = mal;
        const bool hlf = ((acr_ >> 3) & 1u) && f > 8e6 * (1 + 1e-9);
        if (hlf && !avisado_hlf_) {
            ++n_avisos_;
            SC_REPORT_WARNING("flash_f1", "FLASH_ACR.HLFCYA con HCLK por encima de 8 MHz: el "
                              "acceso de medio ciclo solo vale sin un reloj de alta frecuencia "
                              "[RM0316 §4.2.2; los 8 MHz, del modelo]");
        }
        avisado_hlf_ = hlf;
    }
    void hz_proc() { revisa_latencia(); }

    // --- El coste de una lectura ---------------------------------------------------
    // Una línea de 64 bits: un ciclo de HCLK, más LATENCY si no estaba en el
    // búfer. Con la prebúsqueda, la siguiente línea viene sola.
    sc_core::sc_time coste(uint32_t a, unsigned len, bool instr) {
        unsigned ciclos = 0;
        const uint32_t l0 = a / LINEA, l1 = (a + (len ? len - 1 : 0)) / LINEA;
        for (uint32_t l = l0; l <= l1; ++l) {
            ++ciclos;
            bool acierto;
            if (instr) {
                acierto = (l == lin_i_[0] || l == lin_i_[1]);
                lin_i_[0] = l;
                lin_i_[1] = prebusqueda() ? l + 1 : ~0u;
            } else {
                acierto = (l == lin_d_);
                lin_d_ = l;
            }
            if (!acierto) { ciclos += latencia(); n_esperas_ += latencia(); }
        }
        return cycles(ciclos);
    }

    void bt_mem(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (!es_flash(a) || !es_flash(a + n - 1)) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        // Mientras se programa o se borra, quien lee la Flash espera
        const sc_core::sc_time ahora = sc_core::sc_time_stamp() + t;
        if (ocupada_hasta_ > ahora) t += ocupada_hasta_ - ahora;
        if (gp.is_write()) {
            if (!escribe_mem(a, gp.get_data_ptr(), n, t)) {
                gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
                return;
            }
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        std::memcpy(gp.get_data_ptr(), &arr_[a - mem_base_], n);
        const AhbExt* e = gp.get_extension<AhbExt>();
        t += coste(a, n, e && e->instr);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg_mem(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (!es_flash(a) || !es_flash(a + n - 1)) return 0;
        if (gp.is_read()) std::memcpy(gp.get_data_ptr(), &arr_[a - mem_base_], n);
        else              std::memcpy(&arr_[a - mem_base_], gp.get_data_ptr(), n);
        return n;
    }
    bool es_ob(uint32_t a) const { return a >= ob_base_ && a < ob_base_ + 16u; }
    void bt_ob(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (!es_ob(a) || !es_ob(a + n - 1)) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (gp.is_write()) {
            if (!escribe_ob(a, gp.get_data_ptr(), n, t)) {
                gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
                return;
            }
        } else {
            for (unsigned i = 0; i < n; ++i) {
                const unsigned k = a + i - ob_base_;
                gp.get_data_ptr()[i] = uint8_t(ob_[k / 2] >> (8 * (k & 1u)));
            }
            t += cycles(1 + latencia());
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg_ob(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (!es_ob(a) || !es_ob(a + n - 1) || !gp.is_read()) return 0;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = a + i - ob_base_;
            gp.get_data_ptr()[i] = uint8_t(ob_[k / 2] >> (8 * (k & 1u)));
        }
        return n;
    }

    void irq_proc() {
        const bool eop = (cr_ & CR_EOPIE) && (sr_ & SR_EOP);
        const bool err = (cr_ & CR_ERRIE) && (sr_ & (SR_PGERR | SR_WRPERR));
        irq.write(eop || err);
    }

    void reset_proc() {
        if (rst_n.read()) return;
        // Un borrado o una programacion en curso acaba igual: en el modelo el
        // reset no la corta (en el silicio, el resultado queda indefinido)
        acr_ = ACR_RESET; sr_ &= SR_BSY; cr_ = CR_LOCK; ar_ = 0;
        llave_ = llave_ob_ = 0; bloqueo_duro_ = false;
        lin_i_[0] = lin_i_[1] = lin_d_ = ~0u;
        avisado_lat_ = avisado_hlf_ = false;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }

public:
    // El SoC, en cada reset de alimentación y en el de OBL_LAUNCH
    void recarga_opciones() { carga_opciones(); }
};

} // namespace stm32
#endif // STM32_MEM_FLASH_F1_H
