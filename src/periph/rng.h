// =============================================================================
// rng.h — Generador de números aleatorios (AHB2) [IR, §12.19]
//
// (Separado de `crc_rng.h` en la fase 1 del plan del F446: el F446 lleva CRC y
//  NO lleva RNG, así que este bloque tiene que poder quedarse fuera sin
//  arrastrar al otro. Véase doc/stm32f407vg_vs_446re.md §8.2.)
//
// Es de los dos periféricos más pequeños del dispositivo y, precisamente por
// eso, uno de los dos en los que es más tentador hacer trampa. Aquí no se ha
// hecho: el RNG NO devuelve `rand()`. Se modela lo que hay dentro —una FUENTE
// DE RUIDO que entrega un bit por ciclo de RNGCLK y un REGISTRO DE
// DESPLAZAMIENTO REALIMENTADO que lo digiere; cada 40 ciclos se cosecha una
// palabra— y de ahí salen solas la cadencia, la bandera DRDY y, lo que importa,
// las DOS condiciones de error del bloque, que son propiedades de la fuente y
// del reloj y no banderas puestas a mano.
//
// Sin parametrización por rasgos, por lo mismo que el CRC: el F407 lleva una
// instancia y el bloque no tiene más ajuste que encenderlo.
//
// Implementado en la fase F5: RNG_CR (IE, RNGEN), RNG_SR (DRDY, CECS/CEIS,
// SECS/SEIS con su semántica rc_w0), RNG_DR, cadencia de 40 ciclos de RNGCLK,
// error de reloj por PLL48CK < HCLK/16, error de semilla por ruido atascado, e
// IRQ 80 [IR, §12.19].
// =============================================================================
#ifndef STM32_PERIPH_RNG_H
#define STM32_PERIPH_RNG_H

#include <cstdint>
#include "../common/periph_base.h"

namespace stm32 {

// =============================================================================
// Generador de números aleatorios [IR, §12.19]
//
// Lo que hay dentro del bloque, y lo que se modela aquí, son tres piezas:
//
//   1. una FUENTE DE RUIDO analógica que entrega un bit por ciclo de RNGCLK;
//   2. un REGISTRO DE DESPLAZAMIENTO REALIMENTADO que lo digiere;
//   3. un CONTADOR que cosecha una palabra cada 40 ciclos.
//
// Las dos condiciones de error del bloque son propiedades de esas piezas, no
// banderas que alguien decida poner:
//
//   * ERROR DE SEMILLA (SECS/SEIS): la fuente se ha atascado. El detector de
//     salud del silicio la declara muerta cuando ve 64 bits consecutivos
//     iguales, y entonces el generador SE PARA. El banco puede atascar la
//     fuente a propósito, igual que rompe el CRC de la tarjeta SD.
//   * ERROR DE RELOJ (CECS/CEIS): RNGCLK va demasiado lento respecto de HCLK.
//     Si el reloj de la fuente no llega a HCLK/16, el bloque no puede garantizar
//     que lo que entrega sea aleatorio, y lo dice. A diferencia del anterior,
//     este NO para la generación [IR, §12.19].
//
// La fuente de ruido del modelo es DETERMINISTA y con semilla ajustable. Una
// simulación tiene que ser reproducible: dos ejecuciones del mismo banco han de
// dar el mismo flujo, o una regresión no vale para nada. Lo que se pide de ella
// no es que sea impredecible sino que tenga las PROPIEDADES ESTADÍSTICAS de la
// fuente real —equilibrio de unos y ceros, ausencia de rachas largas— y eso el
// banco lo comprueba sobre miles de palabras.
// =============================================================================
class Rng : public BusSlave {
public:
    sc_core::sc_out<bool>  irq{"irq"};              // HASH_RNG (IRQ 80)
    sc_core::sc_in<bool>   pll48ck{"pll48ck"};      // reloj propio [IR, §12.19]
    sc_core::sc_in<double> pll48ck_hz{"pll48ck_hz"};// frecuencia de RNGCLK
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};      // detección CECS

    enum : uint32_t { R_CR = 0x00, R_SR = 0x04, R_DR = 0x08 };
    enum CrBit : uint32_t { CR_RNGEN = 1u << 2, CR_IE = 1u << 3 };
    enum SrBit : uint32_t {
        SR_DRDY = 1u << 0, SR_CECS = 1u << 1, SR_SECS = 1u << 2,
        SR_CEIS = 1u << 5, SR_SEIS = 1u << 6
    };
    // Una palabra cada 40 ciclos de RNGCLK [IR, §12.19.1].
    static constexpr unsigned CICLOS_POR_PALABRA = 40;
    // El detector de salud declara muerta la fuente tras 64 bits iguales.
    static constexpr unsigned RACHA_MORTAL = 64;

    Rng(sc_core::sc_module_name nm) : BusSlave(nm, addr::RNG_B, 0x400) {
        SC_HAS_PROCESS(Rng);
        SC_THREAD(gen_proc);
        SC_METHOD(pub_proc);  sensitive << pub_ev_;
        SC_METHOD(rst_proc);  sensitive << rst_n;
        SC_METHOD(clk_proc);  sensitive << pll48ck_hz << hclk_hz;
        dont_initialize();
    }

    // --- Ventanas del banco de pruebas -------------------------------------
    // La semilla de la fuente: es lo que hace reproducible la simulación.
    void set_seed(uint32_t s) { ruido_ = s ? s : 1u; lfsr_ = 0x12345678u; }
    // Atasca la fuente de ruido: -1 normal, 0 pegada a cero, 1 pegada a uno.
    // Es la inyección de fallo que permite provocar el error de semilla de
    // verdad, haciendo que el detector de salud lo descubra él solo.
    void force_noise(int nivel) { forzado_ = nivel; }
    uint64_t words() const { return n_words_; }

protected:
    uint32_t cr_ = 0, sr_ = 0, dr_ = 0;
    uint32_t ruido_ = 0xACE1ACE1u;      // estado de la fuente de ruido
    uint32_t lfsr_  = 0x12345678u;      // registro de desplazamiento del bloque
    int      forzado_ = -1;
    unsigned racha_ = 0;                // bits iguales seguidos vistos
    bool     ult_bit_ = false;
    uint64_t n_words_ = 0;
    bool     o_irq_ = false;
    sc_core::sc_event pub_ev_, arranque_;

    // -----------------------------------------------------------------------
    // La fuente de ruido: un bit por ciclo de RNGCLK.
    //
    // Es un xorshift de 32 bits, del que se toma un bit. No pretende ser
    // criptográfico —el silicio tiene ruido térmico y esto es un modelo— sino
    // tener sus propiedades: equilibrio, sin rachas largas y reproducible.
    // -----------------------------------------------------------------------
    bool bit_de_ruido() {
        if (forzado_ >= 0) return forzado_ != 0;
        ruido_ ^= ruido_ << 13;
        ruido_ ^= ruido_ >> 17;
        ruido_ ^= ruido_ << 5;
        return (ruido_ >> 31) & 1u;
    }

    // El detector de salud del bloque: cuenta bits iguales seguidos y, al
    // llegar a 64, declara la fuente muerta [IR, §12.19].
    bool fuente_sana(bool b) {
        if (n_bits_ != 0 && b == ult_bit_) ++racha_; else racha_ = 1;
        ult_bit_ = b;
        ++n_bits_;
        return racha_ < RACHA_MORTAL;
    }
    uint64_t n_bits_ = 0;

    bool rngen() const { return (cr_ & CR_RNGEN) != 0; }
    bool ie()    const { return (cr_ & CR_IE) != 0; }

    // Periodo de RNGCLK. Si el RCC no publica frecuencia, no hay reloj y el
    // bloque no genera: es lo que pasa con el PLL apagado.
    double periodo_palabra() const {
        const double f = pll48ck_hz.read();
        return (f > 0.0) ? (double(CICLOS_POR_PALABRA) / f) : 0.0;
    }

    // -----------------------------------------------------------------------
    // El hilo generador. Dirigido por SUCESOS, no por flancos: calcula cuándo
    // toca la palabra siguiente y se cita una sola vez. Así el bloque sigue
    // funcionando cuando el banco apaga las ondas de reloj para ejecutar
    // firmware largo, que es la convención del proyecto.
    // -----------------------------------------------------------------------
    void gen_proc() {
        for (;;) {
            // Parado: sin habilitación, sin reloj, o con la fuente declarada
            // muerta. El error de semilla PARA la generación; el de reloj no.
            if (!rngen() || (sr_ & SR_SECS) || periodo_palabra() <= 0.0) {
                wait(arranque_);
                continue;
            }
            wait(sc_core::sc_time(periodo_palabra(), sc_core::SC_SEC));
            if (!rngen() || (sr_ & SR_SECS)) continue;

            // Cuarenta ciclos de fuente digeridos por el registro de
            // desplazamiento. Si el detector de salud encuentra la fuente
            // atascada durante el proceso, la palabra NO se entrega.
            bool sana = true;
            for (unsigned i = 0; i < CICLOS_POR_PALABRA; ++i) {
                const bool b = bit_de_ruido();
                if (!fuente_sana(b)) { sana = false; break; }
                // Realimentación de Galois: el bit de ruido entra por la cola y
                // la realimentación reparte su influencia por toda la palabra.
                const bool sal = (lfsr_ >> 31) & 1u;
                lfsr_ = uint32_t(lfsr_ << 1) | (b ? 1u : 0u);
                if (sal) lfsr_ ^= 0xA3000029u;
            }
            if (!sana) {
                // La fuente está muerta: SECS (estado) y SEIS (bandera con
                // memoria). El bloque se para hasta que el firmware borre SEIS
                // y vuelva a encender RNGEN [IR, §12.19].
                sr_ |= SR_SECS | SR_SEIS;
                sr_ &= ~SR_DRDY;
                actualiza_irq();
                continue;
            }
            dr_ = lfsr_;
            sr_ |= SR_DRDY;
            ++n_words_;
            actualiza_irq();
        }
    }

    // -----------------------------------------------------------------------
    // Vigilancia del reloj: RNGCLK tiene que llegar a HCLK/16. Es una
    // comparación de frecuencias, no de flancos, y por eso vive en su propio
    // método sensible a las dos señales de frecuencia [IR, §12.19].
    // -----------------------------------------------------------------------
    void clk_proc() {
        const double f48 = pll48ck_hz.read(), fh = hclk_hz.read();
        const bool malo = rngen() && (fh > 0.0) && (f48 < fh / 16.0);
        if (malo) sr_ |= SR_CECS | SR_CEIS;
        else      sr_ &= ~SR_CECS;          // CECS sigue a la condición; CEIS no
        actualiza_irq();
        arranque_.notify(sc_core::SC_ZERO_TIME);
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR: return cr_;
            case R_SR: return sr_;
            case R_DR: {
                // Leer el dato BORRA DRDY: el bloque solo garantiza una palabra
                // por cosecha [IR, §12.19.1]. Con DRDY a cero lo que se lee es
                // cero, no la palabra anterior: así el firmware que se olvida de
                // mirar la bandera falla igual que en la placa.
                if (!(sr_ & SR_DRDY)) return 0;
                sr_ &= ~SR_DRDY;
                const uint32_t v = dr_;
                actualiza_irq();
                return v;
            }
            default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read_sin_efecto(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR: {
                const bool antes = rngen();
                cr_ = v & (CR_RNGEN | CR_IE);
                // Encender RNGEN arranca la cosecha; apagarlo la detiene y, de
                // paso, es lo que rearma el bloque tras un error de semilla.
                if (!antes && rngen()) {
                    sr_ &= ~(SR_SECS | SR_DRDY);
                    racha_ = 0; n_bits_ = 0;
                    arranque_.notify(sc_core::SC_ZERO_TIME);
                }
                clk_proc();
                actualiza_irq();
                return;
            }
            case R_SR:
                // CEIS y SEIS son rc_w0: se borran escribiendo CERO, no uno.
                // Es al revés que casi todo el dispositivo —el SR del DAC, el
                // ICR del SDIO— y confundirlos deja la interrupción colgada
                // para siempre [IR, §12.19].
                if (!(v & SR_CEIS)) sr_ &= ~SR_CEIS;
                if (!(v & SR_SEIS)) sr_ &= ~SR_SEIS;
                actualiza_irq();
                return;
            default: return;      // RNG_DR es de solo lectura
        }
    }

    // Lectura auxiliar para los accesos parciales: NO puede tener el efecto
    // lateral de borrar DRDY, o un byte escrito en RNG_CR se llevaría el dato
    // por delante.
    uint32_t reg_read_sin_efecto(uint32_t off) const {
        switch (off) {
            case R_CR: return cr_;
            case R_SR: return sr_;
            case R_DR: return dr_;
            default:   return 0;
        }
    }

    void actualiza_irq() {
        // La interrupción sale de las tres banderas, no solo del dato listo
        // [IR, §12.19].
        const bool i = ie() && (sr_ & (SR_DRDY | SR_CEIS | SR_SEIS)) != 0;
        if (i != o_irq_) { o_irq_ = i; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    }
    void pub_proc() { irq.write(o_irq_); }

    void rst_proc() {
        if (rst_n.read()) return;
        cr_ = sr_ = dr_ = 0;
        lfsr_ = 0x12345678u;
        racha_ = 0; n_bits_ = 0; n_words_ = 0;
        forzado_ = -1;
        o_irq_ = false;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_RNG_H
