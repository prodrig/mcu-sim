// =============================================================================
// periph_base.h — Clases base para esclavos de bus del modelo
//
// BusSlave: esclavo TLM genérico (target socket + reloj/reset/gating). Todos
// los periféricos y memorias derivan de él y sobrescriben reg_read/reg_write.
//
// Fase F1: el transporte base pasa a ser completo y reutilizable por todos los
// periféricos de fases posteriores:
//   * accesos de 1, 2 y 4 bytes con extracción/inserción sobre la palabra de
//     32 bits del registro (los periféricos siguen viendo palabras alineadas);
//   * byte enables del payload combinados con los del tamaño de acceso;
//   * respuestas AHB: ERROR por periférico sin reloj (gating) [IR, §4.8] o por
//     offset fuera del bloque [IR, §6.5-nota];
//   * anotación temporal en ciclos del reloj del dominio.
// [plan §5; IR, §12-Implicaciones]
// =============================================================================
#ifndef STM32_COMMON_PERIPH_BASE_H
#define STM32_COMMON_PERIPH_BASE_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include "ahb_types.h"

namespace stm32 {

class BusSlave : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<BusSlave> tsk;  // puerto esclavo (bus)
    sc_core::sc_in<bool> clk{"clk"};                // reloj del dominio (HCLK/PCLKx)
    sc_core::sc_in<bool> rst_n{"rst_n"};            // reset del dominio
    sc_core::sc_in<bool> clk_en{"clk_en"};          // gating RCC_xxxENR
    // Frecuencia del dominio [Hz]. La anotación temporal de cada acceso se hace
    // con ella, de modo que un periférico en APB1 a 42 MHz y otro en AHB a
    // 168 MHz facturan ciclos distintos aunque compartan el código base
    // [IR, §4.4, §6.4]. La escribe el RCC; el top la enlaza (F3).
    sc_core::sc_in<double> clk_hz{"clk_hz"};
    // Valor "vivo" del gating. El bit de RCC_xxxENR habilita la puerta de reloj
    // de forma combinacional: la instrucción siguiente a la que escribe el ENR
    // ya accede al periférico. Con un sc_signal el efecto llegaría un delta más
    // tarde, y como el b_transport del RCC corre en el proceso del maestro (que
    // no cede el control entre instrucciones), el acceso siguiente vería el
    // periférico todavía sin reloj. El top enlaza aquí el estado interno del
    // RCC; si es nulo se usa el puerto clk_en [IR, §4.8].
    const bool* clk_en_live = nullptr;
    bool clock_enabled() const { return clk_en_live ? *clk_en_live : clk_en.read(); }

    BusSlave(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : sc_core::sc_module(nm), tsk("tsk"), base_(base), size_(size) {
        tsk.register_b_transport(this, &BusSlave::b_transport);
        tsk.register_transport_dbg(this, &BusSlave::transport_dbg);
        SC_HAS_PROCESS(BusSlave);
        SC_METHOD(dom_hz_proc);
        sensitive << clk_hz;
    }

    uint32_t base() const { return base_; }
    uint32_t size() const { return size_; }

    // Acceso de depuración/verificación sin consumir tiempo ni pasar por el bus.
    uint32_t dbg_read(uint32_t off)              { return reg_read(off & ~3u); }
    void     dbg_write(uint32_t off, uint32_t v) { reg_write(off & ~3u, v, 0xFu); }

protected:
    // --- API que implementa cada periférico ---------------------------------
    // reg_read/reg_write trabajan siempre con la palabra de 32 bits alineada.
    // byte_en: máscara de 4 bits (bit i => byte i de la palabra es válido).
    virtual uint32_t reg_read(uint32_t /*off*/) { return 0; }
    virtual void     reg_write(uint32_t /*off*/, uint32_t /*val*/,
                               uint32_t /*byte_en*/) {}
    // Latencia del acceso, en ciclos del reloj del dominio.
    virtual unsigned access_cycles(bool /*write*/) const { return 1; }
    // ¿Responde el bloque aunque su reloj esté cortado? (memorias sí)
    virtual bool responds_without_clock() const { return false; }

    // Periodo del reloj del dominio; si no está bindeado o parado, 0.
    sc_core::sc_time clk_period() const {
        // El periodo real lo conoce el ClockGen; aquí se usa una estimación por
        // el semiperiodo observado. Los esclavos que necesitan latencia exacta
        // (Flash) reciben la frecuencia por un puerto sc_in<double> propio.
        return sc_core::SC_ZERO_TIME;
    }

    virtual void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        if (!check_common(gp)) return;
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        unsigned char* d   = gp.get_data_ptr();

        if (gp.is_read()) {
            // Agrupar los bytes por palabra, igual que en la escritura: hay
            // registros cuya LECTURA tiene efecto lateral (el ADC_CDR de los
            // modos multiples entrega un dato distinto en cada lectura, y leer
            // ADC_DR o I2C_SR2 borra banderas). Con una llamada por byte, un
            // acceso de 32 bits disparaba el efecto CUATRO veces y el dato
            // salia troceado de cuatro lecturas consecutivas.
            unsigned i = 0;
            while (i < len) {
                const uint32_t wbase = (off + i) & ~3u;
                const uint32_t w = reg_read(wbase);
                while (i < len && ((off + i) & ~3u) == wbase) {
                    d[i] = uint8_t(w >> (8u * ((off + i) & 3u)));
                    ++i;
                }
            }
        } else {
            // Agrupar los bytes por palabra para respetar los efectos laterales
            // de escritura (una sola llamada reg_write por palabra tocada).
            unsigned i = 0;
            while (i < len) {
                const uint32_t a    = off + i;
                const uint32_t wbase= a & ~3u;
                uint32_t val = 0, ben = 0;
                while (i < len && ((off + i) & ~3u) == wbase) {
                    const unsigned b = (off + i) & 3u;
                    if (byte_enabled(gp, i)) {
                        val |= uint32_t(d[i]) << (8u * b);
                        ben |= 1u << b;
                    }
                    ++i;
                }
                if (ben) reg_write(wbase, val, ben);
            }
        }
        t += cycles(access_cycles(gp.is_write()));
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    virtual unsigned transport_dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        if (uint64_t(off) + len > size_) return 0;
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) {
            const uint32_t a = off + i;
            if (gp.is_read()) d[i] = uint8_t(reg_read(a & ~3u) >> (8u * (a & 3u)));
            else {
                uint32_t w = reg_read(a & ~3u);
                const unsigned b = a & 3u;
                w = (w & ~(0xFFu << (8u * b))) | (uint32_t(d[i]) << (8u * b));
                reg_write(a & ~3u, w, 1u << b);
            }
        }
        return len;
    }

    // Comprobaciones comunes a todos los esclavos. Devuelve false si ya se ha
    // fijado una respuesta de error en el payload.
    bool check_common(tlm::tlm_generic_payload& gp) {
        gp.set_dmi_allowed(false);
        if (gp.get_byte_enable_ptr() && gp.get_byte_enable_length() == 0) {
            gp.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (gp.get_streaming_width() < gp.get_data_length() &&
            gp.get_streaming_width() != 0) {
            gp.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (!clock_enabled() && !responds_without_clock()) {
            // Periférico sin reloj: el acceso no obtiene respuesta -> BusFault
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return false;
        }
        const uint64_t off = gp.get_address() - base_;
        if (gp.get_address() < base_ || off + gp.get_data_length() > size_) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    static bool byte_enabled(const tlm::tlm_generic_payload& gp, unsigned i) {
        const unsigned char* be = gp.get_byte_enable_ptr();
        if (!be) return true;
        const unsigned n = gp.get_byte_enable_length();
        return be[i % n] == TLM_BYTE_ENABLED;
    }

    // Convierte ciclos del dominio en tiempo usando la frecuencia declarada.
    sc_core::sc_time cycles(unsigned n) const {
        return (dom_hz_ > 0.0)
                   ? sc_core::sc_time(double(n) * 1.0e12 / dom_hz_, sc_core::SC_PS)
                   : sc_core::SC_ZERO_TIME;
    }
    // Los módulos que reciben la frecuencia del dominio la publican aquí.
    void set_domain_hz(double hz) { dom_hz_ = hz; }
    double domain_hz() const { return dom_hz_; }

private:
    void dom_hz_proc() { dom_hz_ = clk_hz.read(); }

protected:

    uint32_t base_, size_;
    double   dom_hz_ = 0.0;
};

} // namespace stm32
#endif // STM32_COMMON_PERIPH_BASE_H
