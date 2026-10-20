// =============================================================================
// flash_f1.h — La Flash de los STM32 de la familia F0/F1/F3 y su controlador
// (el FPEC), fase S1 del F303: la LECTURA
//
// LA MEMORIA: `size` bytes en 0x0800_0000, en páginas de 2 KB [HAL:
// FLASH_PAGE_SIZE], que se leen por líneas de 64 bits. Con LATENCY estados de
// espera cada línea que no esté en el búfer de prebúsqueda; con la prebúsqueda
// encendida (PRFTBE, que viene encendida de reset), el búfer guarda la línea
// en uso y trae la siguiente sin coste, de modo que el código secuencial no
// espera [1F: RM0316 §3.2.2 y PM0063]. Los datos (literales, tablas) llevan un
// registro de línea propio.
//
// EL CONTROLADOR, en 0x4002_2000 [CMSIS: FLASH_TypeDef]:
//   0x00 ACR      LATENCY (2:0), HLFCYA (3), PRFTBE (4), PRFTBS (5)
//   0x04 KEYR     0x45670123 y luego 0xCDEF89AB abren CR [CMSIS: FLASH_KEY1/2]
//   0x08 OPTKEYR  las mismas, para los bytes de opción
//   0x0C SR       BSY, PGERR, WRPERR, EOP
//   0x10 CR       PG, PER, MER, OPTPG, OPTER, STRT, LOCK, OPTWRE, ERRIE,
//                 EOPIE, OBL_LAUNCH
//   0x14 AR       la página que borra PER
//   0x1C OBR      lo que se cargó de los bytes de opción: RDPRT, USER, DATA0/1
//   0x20 WRPR     la protección de escritura cargada
//
// LOS BYTES DE OPCIÓN, en 0x1FFF_F800: RDP, USER, DATA0, DATA1 y WRP0-3, cada
// uno en una media palabra con su complemento en el byte alto [CMSIS:
// OB_TypeDef]. Se cargan en OBR/WRPR al encender y con OBL_LAUNCH.
//
// LO QUE VIGILA (el silicio no lo hace y lee basura en silencio): que LATENCY
// alcance para el HCLK en vigor -0 hasta 24 MHz, 1 hasta 48, 2 hasta 72-, y
// que el acceso de medio ciclo (HLFCYA) no se use por encima de 8 MHz.
//
// En esta fase la Flash se LEE. Programarla y borrarla (PG, PER, MER, OPTPG,
// OPTER) es la fase S2: una escritura en la memoria da error de bus y lo dice.
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
    static constexpr uint32_t ACR_RESET = 0x30u;      // PRFTBE y PRFTBS [1F: RM0316]
    static constexpr unsigned LINEA = 8;              // 64 bits

    // La memoria (Flash principal) y la ventana de los bytes de opción
    tlm_utils::simple_target_socket<FlashF1> mem{"mem"};
    tlm_utils::simple_target_socket<FlashF1> ob{"ob"};
    sc_core::sc_out<bool> irq{"irq"};

    // Quién da los estados de espera mínimos para una frecuencia; lo pone el
    // SoC con los de su chip
    std::function<unsigned(double)> latencia_minima;

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
    // Lo cargado de los bytes de opción: nBOOT1 (USER bit 4) para el arranque
    bool nboot1() const { return (obr_ >> 12) & 1u; }
    uint8_t rdp_cargado() const { return rdp_carg_; }
    // Con HCLK cambiado, revisar la latencia (también lo hace solo, por clk_hz)
    void revisa() { revisa_latencia(); }
    // El banco: un byte de opción como si se hubiera programado (con su
    // complemento). Vale en la siguiente carga: encendido u OBL_LAUNCH
    void pon_ob(unsigned i, uint8_t v) { if (i < 8) ob_[i] = uint16_t(v | ((~v & 0xFFu) << 8)); }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_ACR:  return acr_ | (prebusqueda() ? (1u << 5) : 0u);
            case R_SR:   return sr_;
            case R_CR:   return cr_;
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
            case R_AR:       if (!(sr_ & SR_BSY)) ar_ = v; return;
            default:         return;
        }
    }

    // --- Lo que hace S2 ------------------------------------------------------------
    virtual void escribe_cr(uint32_t v) {
        if (cr_ & CR_LOCK) return;                          // cerrado: no cambia
        const uint32_t w = CR_PG | CR_PER | CR_MER | CR_OPTPG | CR_OPTER | CR_LOCK |
                           CR_ERRIE | CR_EOPIE;
        cr_ = (cr_ & ~w) | (v & w);
        if (!(cr_ & CR_LOCK)) {
            if (v & (CR_STRT | CR_OBL_LAUNCH)) aviso_s2();
        } else cr_ &= ~CR_OPTWRE;
    }
    virtual bool escribe_mem(uint32_t /*a*/, const uint8_t* /*d*/, unsigned /*n*/,
                             sc_core::sc_time& /*t*/) {
        aviso_s2();
        return false;
    }
    virtual bool escribe_ob(uint32_t, const uint8_t*, unsigned, sc_core::sc_time&) {
        aviso_s2();
        return false;
    }
    void aviso_s2() {
        ++n_rech_;
        if (avisado_s2_) return;
        avisado_s2_ = true;
        ++n_avisos_;
        SC_REPORT_WARNING("flash_f1", "programar o borrar la Flash del F303 todavia no "
                          "esta modelado (fase S2): la escritura da error de bus");
    }

    // El estado
    uint32_t mem_base_, pagina_, ob_base_;
    std::vector<uint8_t> arr_;
    uint16_t ob_[8] = {};
    uint32_t acr_ = ACR_RESET, sr_ = 0, cr_ = CR_LOCK, ar_ = 0, obr_ = 0, wrpr_ = 0xFFFFFFFFu;
    uint8_t  rdp_carg_ = 0xAA;
    unsigned llave_ = 0, llave_ob_ = 0;
    bool     bloqueo_duro_ = false;          // una llave mal: cerrado hasta el reset
    sc_core::sc_time ocupada_hasta_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_event irq_ev_;
    unsigned n_avisos_ = 0, n_rech_ = 0;
    uint64_t n_esperas_ = 0;
    bool avisado_s2_ = false, avisado_lat_ = false, avisado_hlf_ = false;
    uint32_t lin_i_[2] = {~0u, ~0u};         // la línea en uso y la adelantada
    uint32_t lin_d_ = ~0u;

    // Los bytes de opción de fábrica: RDP nivel 0 (0xAA), USER = 0xFF (perro por
    // software, nBOOT1 = 1, monitor de VDDA, sin paridad de la SRAM), DATA a
    // 0xFF y sin protección de escritura [HAL: OB_*] [1F: el USER de fábrica]
    void ob_fabrica() {
        const uint8_t v[8] = {0xAA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        for (unsigned i = 0; i < 8; ++i) ob_[i] = uint16_t(v[i] | ((~v[i] & 0xFFu) << 8));
    }
    // OBR y WRPR a partir de los bytes de opción. Un byte cuyo complemento no
    // cuadra da OPTERR y se carga como 0xFF [1F: RM0316 §4.1]
    void carga_opciones() {
        bool err = false;
        auto byte = [&](unsigned i) -> uint8_t {
            const uint16_t h = ob_[i];
            if (uint8_t(h) != uint8_t(~(h >> 8))) { err = true; return 0xFF; }
            return uint8_t(h);
        };
        const uint8_t rdp = uint8_t(ob_[0]);     // RDP: el byte, sin mirar el complemento
        rdp_carg_ = rdp;
        const uint32_t nivel = rdp == 0xAA ? 0u : (rdp == 0xCC ? 3u : 1u);
        const uint8_t user = byte(1), d0 = byte(2), d1 = byte(3);
        uint32_t wrp = 0;
        for (unsigned i = 0; i < 4; ++i) wrp |= uint32_t(byte(4 + i)) << (8 * i);
        obr_ = (err ? 1u : 0u) | (nivel << 1) | (uint32_t(user) << 8) |
               (uint32_t(d0) << 16) | (uint32_t(d1) << 24);
        wrpr_ = wrp;
    }

    void escribe_llave(uint32_t v) {
        if (bloqueo_duro_ || !(cr_ & CR_LOCK)) {
            // Una llave con CR ya abierto, o tras una secuencia mala: el FPEC
            // se cierra hasta el siguiente reset [1F: RM0316 §4.2.2]
            if (!(cr_ & CR_LOCK)) { cr_ |= CR_LOCK; cr_ &= ~CR_OPTWRE; }
            bloqueo_duro_ = true;
            return;
        }
        if (llave_ == 0 && v == KEY1) { llave_ = 1; return; }
        if (llave_ == 1 && v == KEY2) { llave_ = 0; cr_ &= ~CR_LOCK; return; }
        llave_ = 0;
        bloqueo_duro_ = true;
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
                              "acceso de medio ciclo solo vale con relojes lentos [1F: RM0316]");
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

    virtual void reset_proc() {
        if (rst_n.read()) return;
        acr_ = ACR_RESET; sr_ = 0; cr_ = CR_LOCK; ar_ = 0;
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
