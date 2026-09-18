// =============================================================================
// quadspi.h — QUADSPI: memoria serie externa (fase 4 del plan del F446)
//
// Una Flash serie de cuatro hilos colgada del MCU, y un controlador que sabe
// hablarle. Es el periférico que más ha cambiado la forma de hacer las cosas en
// los microcontroladores de los últimos años: con él, un chip de 512 KB de
// Flash interna puede ejecutar desde —o leer— decenas de megabytes externos por
// seis pines, y el firmware ni se entera, porque el controlador presenta esa
// memoria **mapeada en el espacio de direcciones**.
//
// LOS CUATRO MODOS, que son los que hay que entender y los que este modelo
// implementa (`CCR.FMODE`):
//
//   00  INDIRECTO DE ESCRITURA. El firmware arma un comando en CCR —cuántas
//       líneas para la instrucción, cuántas para la dirección, cuántos ciclos
//       de espera, cuántas para los datos—, escribe la dirección en AR y va
//       metiendo bytes por DR. Es como se programa la Flash.
//   01  INDIRECTO DE LECTURA. Igual, pero los bytes salen por DR.
//   10  SONDEO AUTOMÁTICO DE ESTADO. El controlador repite un comando —el de
//       leer el registro de estado— cada `PIR` ciclos y compara con `PSMAR`
//       bajo la máscara `PSMKR`, hasta que casa. Sirve para esperar a que una
//       escritura termine **sin que el firmware haga nada**, y es de las cosas
//       que más sorprenden de este bloque.
//   11  MAPEADO EN MEMORIA. La ventana `0x9000_0000` deja de ser espacio
//       reservado y pasa a ser la memoria externa: leer de ahí dispara el
//       comando de lectura por debajo. Eso es lo que permite ejecutar código
//       desde una Flash serie.
//
// EL HALLAZGO DE LA FASE 0 QUE ESTO MATERIALIZA: el QUADSPI **no añade un
// octavo esclavo a la matriz**. En el F446, el séptimo puerto de esclavo es
// «FMC / QUADSPI»: los dos comparten uno solo [RM0390, §2.1]. Por eso el modelo
// engancha la ventana mapeada en memoria al mismo `BusSlaveId::FSMC_EXT` que
// usaba el bus externo, y no a uno nuevo.
//
// A NIVEL DE PIN, como el resto del modelo. El controlador mueve CLK, NCS y los
// cuatro IO de verdad, con el número de líneas que diga cada fase del comando;
// lo que conteste lo pone quien esté soldado al otro lado (véase `QspiFlash` en
// `parts/ext_parts.h`). Sin nada soldado, las líneas quedan en alto por los
// pull-ups y lo que se lee son unos, que es lo que se lee en una placa sin
// memoria.
//
// LO QUE NO HAY, dicho aquí y en `limitaciones()`: el modo de dos Flash en
// paralelo (`DFM`), el de doble flanco (`DDRM`), el temporizador de bajo
// consumo (`LPTR`) y las peticiones de DMA. Los registros están y se guardan;
// lo que no hay es el comportamiento.
// =============================================================================
#ifndef STM32_PERIPH_QUADSPI_H
#define STM32_PERIPH_QUADSPI_H

#include <deque>
#include <cstring>
#include "../common/periph_base.h"

namespace stm32 {

class QuadSpi : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    // Los seis pines, por el mux. Cada IO es bidireccional: `oe` dice si el
    // controlador conduce o escucha, que es lo que cambia entre una fase de
    // comando y una de datos leídos.
    sc_core::sc_signal<bool> clk_out{"clk_out"}, clk_oe{"clk_oe"};
    sc_core::sc_signal<bool> ncs_out{"ncs_out"}, ncs_oe{"ncs_oe"};
    // Los cuatro IO, como vectores: `sc_vector` es lo que permite darles
    // nombre propio -io_out(0)..io_out(3)- sin escribir cuatro miembros.
    sc_core::sc_vector<sc_core::sc_signal<bool>> io_out{"io_out", 4};
    sc_core::sc_vector<sc_core::sc_signal<bool>> io_oe{"io_oe", 4};
    sc_core::sc_vector<sc_core::sc_signal<bool>> io_in{"io_in", 4};

    // La ventana mapeada en memoria: un esclavo más, que el top engancha al
    // puerto de memoria externa de la matriz.
    tlm_utils::simple_target_socket<QuadSpi> mem{"mem"};

    // ---- Offsets [RM0390, §13.5; stm32f446xx.h, QUADSPI_TypeDef] ----------
    enum : uint32_t {
        R_CR = 0x00, R_DCR = 0x04, R_SR = 0x08, R_FCR = 0x0C, R_DLR = 0x10,
        R_CCR = 0x14, R_AR = 0x18, R_ABR = 0x1C, R_DR = 0x20,
        R_PSMKR = 0x24, R_PSMAR = 0x28, R_PIR = 0x2C, R_LPTR = 0x30
    };
    static constexpr uint32_t CR_EN = 1u << 0, CR_ABORT = 1u << 1,
        CR_TCIE = 1u << 17, CR_FTIE = 1u << 18, CR_SMIE = 1u << 19,
        CR_APMS = 1u << 22;
    static constexpr uint32_t SR_TEF = 1u << 0, SR_TCF = 1u << 1,
        SR_FTF = 1u << 2, SR_SMF = 1u << 3, SR_TOF = 1u << 4, SR_BUSY = 1u << 5;

    explicit QuadSpi(sc_core::sc_module_name nm, uint32_t base)
        : BusSlave(nm, base, 0x400) {
        mem.register_b_transport(this, &QuadSpi::mem_b_transport);
        mem.register_transport_dbg(this, &QuadSpi::mem_dbg);
        SC_HAS_PROCESS(QuadSpi);
        SC_THREAD(cmd_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_; dont_initialize();
        SC_METHOD(rst_proc); sensitive << rst_n;   dont_initialize();
    }

    // Para la verificación: cuántos comandos se han lanzado y el último.
    unsigned n_comandos() const { return n_cmd_; }
    uint8_t  ultima_instruccion() const { return ultima_inst_; }
    uint32_t peek_sr() const { return sr_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            case R_DCR: return dcr_;
            case R_SR:  return sr_ | (uint32_t(fifo_.size() & 0x3Fu) << 8);
            case R_FCR: return 0;
            case R_DLR: return dlr_;
            case R_CCR: return ccr_;
            case R_AR:  return ar_;
            case R_ABR: return abr_;
            case R_DR: {
                // Sale un byte de la FIFO. Con la FIFO vacía el silicio
                // devuelve lo último y no avanza; aquí, cero.
                uint32_t v = 0;
                for (unsigned i = 0; i < 4 && !fifo_.empty(); ++i) {
                    v |= uint32_t(fifo_.front()) << (8 * i);
                    fifo_.pop_front();
                }
                actualiza_flags();
                return v;
            }
            case R_PSMKR: return psmkr_;
            case R_PSMAR: return psmar_;
            case R_PIR:   return pir_;
            case R_LPTR:  return lptr_;
            default:      return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR:
                cr_ = v & 0xFFFFFFFBu;
                if (v & CR_ABORT) { aborta_ = true; cmd_ev_.notify(sc_core::SC_ZERO_TIME); }
                actualiza_flags();
                break;
            case R_DCR: dcr_ = v & 0x001F07FFu; break;
            case R_FCR: sr_ &= ~(v & (SR_TEF | SR_TCF | SR_SMF | SR_TOF));
                        actualiza_flags(); break;
            case R_DLR: dlr_ = v; break;
            case R_CCR:
                ccr_ = v;
                // ESCRIBIR CCR ES LANZAR EL COMANDO, salvo que la fase de
                // dirección esté activa: entonces el disparo es escribir AR.
                // Es de las cosas del QUADSPI que más despistan.
                if ((cr_ & CR_EN) && admode() == 0) lanza();
                break;
            case R_AR:
                ar_ = v;
                if (cr_ & CR_EN) lanza();
                break;
            case R_ABR: abr_ = v; break;
            case R_DR:
                for (unsigned i = 0; i < 4; ++i)
                    fifo_.push_back(uint8_t(v >> (8 * i)));
                cmd_ev_.notify(sc_core::SC_ZERO_TIME);
                actualiza_flags();
                break;
            case R_PSMKR: psmkr_ = v; break;
            case R_PSMAR: psmar_ = v; break;
            case R_PIR:   pir_ = v & 0xFFFFu; break;
            case R_LPTR:  lptr_ = v & 0xFFFFu; break;
            default: break;
        }
    }
    bool responds_without_clock() const override { return false; }

private:
    uint32_t cr_ = 0, dcr_ = 0, sr_ = 0, dlr_ = 0, ccr_ = 0, ar_ = 0, abr_ = 0;
    uint32_t psmkr_ = 0, psmar_ = 0, pir_ = 0, lptr_ = 0;
    std::deque<uint8_t> fifo_;
    bool     o_irq_ = false, aborta_ = false, pedido_ = false;
    unsigned n_cmd_ = 0;
    uint8_t  ultima_inst_ = 0;
    bool     o_clk_ = false, o_ncs_ = true;
    bool     o_io_[4] = {true, true, true, true};
    bool     o_ioe_[4] = {false, false, false, false};
    sc_core::sc_event pub_ev_, cmd_ev_;

    unsigned fmode()  const { return (ccr_ >> 26) & 3u; }
    unsigned dmode()  const { return (ccr_ >> 24) & 3u; }
    unsigned dcyc()   const { return (ccr_ >> 18) & 0x1Fu; }
    unsigned adsize() const { return (ccr_ >> 12) & 3u; }
    unsigned admode() const { return (ccr_ >> 10) & 3u; }
    unsigned imode()  const { return (ccr_ >> 8) & 3u; }
    uint8_t  instr()  const { return uint8_t(ccr_ & 0xFFu); }
    unsigned presc()  const { return ((cr_ >> 24) & 0xFFu) + 1u; }

    sc_core::sc_time t_sck() const {
        const double f = domain_hz();
        if (f <= 0.0) return sc_core::sc_time(10, sc_core::SC_NS);
        return sc_core::sc_time(double(presc()) / f, sc_core::SC_SEC);
    }

    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        clk_out.write(o_clk_); clk_oe.write(true);
        ncs_out.write(o_ncs_); ncs_oe.write(true);
        for (unsigned i = 0; i < 4; ++i) {
            io_out[i].write(o_io_[i]);
            io_oe[i].write(o_ioe_[i]);
        }
        irq.write(o_irq_);
    }
    void rst_proc() {
        if (rst_n.read()) return;
        cr_ = dcr_ = sr_ = dlr_ = ccr_ = ar_ = abr_ = 0;
        psmkr_ = psmar_ = pir_ = lptr_ = 0;
        fifo_.clear();
        o_ncs_ = true; o_clk_ = false;
        for (unsigned i = 0; i < 4; ++i) { o_io_[i] = true; o_ioe_[i] = false; }
        o_irq_ = false; n_cmd_ = 0; pedido_ = false;
        publica();
    }
    void actualiza_flags() {
        const unsigned umbral = ((cr_ >> 8) & 0x1Fu) + 1u;
        if (fmode() == 1 || fmode() == 3) {
            if (fifo_.size() >= umbral) sr_ |= SR_FTF; else sr_ &= ~SR_FTF;
        } else {
            if (fifo_.size() <= (32u - umbral)) sr_ |= SR_FTF; else sr_ &= ~SR_FTF;
        }
        o_irq_ = ((cr_ & CR_TCIE) && (sr_ & SR_TCF)) ||
                 ((cr_ & CR_FTIE) && (sr_ & SR_FTF)) ||
                 ((cr_ & CR_SMIE) && (sr_ & SR_SMF));
        publica();
    }
    void lanza() {
        if (fmode() == 3) {                    // mapeado: no hay disparo
            sr_ &= ~SR_BUSY;
            return;
        }
        pedido_ = true;
        cmd_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // ---- La capa serie -----------------------------------------------------
    void medio_ciclo() { wait(t_sck() / 2.0); }
    void pon_lineas(unsigned n, uint8_t nibble) {
        for (unsigned i = 0; i < 4; ++i) {
            o_ioe_[i] = (i < n);
            if (i < n) o_io_[i] = ((nibble >> i) & 1u) != 0;
        }
        publica();
    }
    void escucha(unsigned n) {
        for (unsigned i = 0; i < 4; ++i) o_ioe_[i] = (i < n) ? false : o_ioe_[i];
        for (unsigned i = 0; i < n; ++i) o_ioe_[i] = false;
        publica();
    }
    // Un byte hacia fuera, por `lineas` hilos (1, 2 o 4).
    void envia(uint8_t v, unsigned lineas) {
        const unsigned paso = lineas;
        for (int b = 8 - int(paso); b >= 0; b -= int(paso)) {
            uint8_t trozo = 0;
            for (unsigned i = 0; i < paso; ++i)
                trozo = uint8_t(trozo | (((v >> (b + i)) & 1u) << i));
            pon_lineas(paso, trozo);
            o_clk_ = false; publica(); medio_ciclo();
            o_clk_ = true;  publica(); medio_ciclo();
        }
    }
    // Un byte hacia dentro. Lo que se lee lo pone quien esté soldado; sin nada,
    // los pull-ups dejan unos y sale 0xFF.
    uint8_t recibe(unsigned lineas) {
        escucha(lineas);
        uint8_t v = 0;
        for (unsigned b = 0; b < 8; b += lineas) {
            o_clk_ = false; publica(); medio_ciclo();
            o_clk_ = true;  publica(); medio_ciclo();
            for (unsigned i = 0; i < lineas; ++i)
                v = uint8_t(v | ((io_in[i].read() ? 1u : 0u) << (8 - lineas - b + i)));
        }
        return v;
    }
    void ciclos_vacios(unsigned n, unsigned lineas) {
        escucha(lineas);
        for (unsigned i = 0; i < n; ++i) {
            o_clk_ = false; publica(); medio_ciclo();
            o_clk_ = true;  publica(); medio_ciclo();
        }
    }

    // El comando entero: instrucción, dirección, bytes alternativos, ciclos
    // vacíos y datos, cada fase con el número de líneas que diga CCR.
    void ejecuta(uint32_t direccion, unsigned n_datos, bool leer,
                 std::deque<uint8_t>* salida) {
        sr_ |= SR_BUSY;
        publica();
        o_ncs_ = false; publica();                    // NCS abajo: empieza
        if (imode()) { envia(instr(), imode()); ultima_inst_ = instr(); }
        if (admode()) {
            const unsigned nb = adsize() + 1u;        // 1..4 bytes
            for (int i = int(nb) - 1; i >= 0; --i)
                envia(uint8_t(direccion >> (8 * i)), admode());
        }
        const unsigned abmode = (ccr_ >> 14) & 3u;
        if (abmode) {
            const unsigned nb = ((ccr_ >> 16) & 3u) + 1u;
            for (int i = int(nb) - 1; i >= 0; --i)
                envia(uint8_t(abr_ >> (8 * i)), abmode);
        }
        if (dcyc()) ciclos_vacios(dcyc(), dmode() ? dmode() : 1u);
        if (dmode() && n_datos) {
            for (unsigned i = 0; i < n_datos; ++i) {
                if (leer) {
                    const uint8_t v = recibe(dmode());
                    if (salida) salida->push_back(v);
                } else {
                    if (fifo_.empty()) break;
                    envia(fifo_.front(), dmode());
                    fifo_.pop_front();
                }
            }
        }
        o_ncs_ = true; publica();                     // NCS arriba: termina
        pon_lineas(0, 0);
        ++n_cmd_;
        sr_ &= ~SR_BUSY;
        sr_ |= SR_TCF;
        actualiza_flags();
    }

    void cmd_proc() {
        for (;;) {
            wait(cmd_ev_);
            if (!pedido_ || !(cr_ & CR_EN)) { pedido_ = false; continue; }
            pedido_ = false;
            aborta_ = false;
            const unsigned n = (dlr_ == 0xFFFFFFFFu) ? 0u : dlr_ + 1u;
            switch (fmode()) {
                case 0:                                    // indirecto escritura
                    ejecuta(ar_, dmode() ? n : 0u, false, nullptr);
                    break;
                case 1: {                                  // indirecto lectura
                    std::deque<uint8_t> out;
                    ejecuta(ar_, dmode() ? n : 0u, true, &out);
                    fifo_ = out;
                    actualiza_flags();
                    break;
                }
                case 2: {                                  // sondeo automático
                    // Repite el comando cada PIR ciclos hasta que el estado
                    // leído case con PSMAR bajo PSMKR. Es el modo que permite
                    // esperar a una escritura SIN que el firmware haga nada.
                    for (unsigned intento = 0; intento < 10000; ++intento) {
                        std::deque<uint8_t> out;
                        ejecuta(ar_, dmode() ? n : 1u, true, &out);
                        uint32_t st = 0;
                        for (unsigned i = 0; i < out.size() && i < 4; ++i)
                            st |= uint32_t(out[i]) << (8 * i);
                        if ((st & psmkr_) == (psmar_ & psmkr_)) {
                            sr_ |= SR_SMF;
                            if (cr_ & CR_APMS) sr_ &= ~SR_BUSY;   // parar al casar
                            actualiza_flags();
                            break;
                        }
                        if (aborta_ || !(cr_ & CR_EN)) break;
                        wait(t_sck() * double(pir_ ? pir_ : 16u));
                    }
                    break;
                }
                default: break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // MODO MAPEADO EN MEMORIA: la ventana 0x9000_0000
    //
    // Leer de ahí no es leer una memoria interna: es lanzar por debajo el
    // comando de lectura que CCR describe, con la dirección que el bus pide.
    // Por eso una lectura cuesta lo que cuesta el comando, y por eso este
    // periférico ocupa el puerto de esclavo de la matriz que en el F407 era
    // del FSMC: en el F446 los dos comparten uno [RM0390, §2.1].
    // -----------------------------------------------------------------------
    void mem_b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        if (!(cr_ & CR_EN) || fmode() != 3) {
            // Sin el controlador encendido y en modo mapeado, esa ventana no
            // es de nadie: error de bus, igual que el espacio reservado.
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.is_read()) {
            std::deque<uint8_t> out;
            ejecuta(uint32_t(a - 0x90000000ull), len, true, &out);
            for (unsigned i = 0; i < len; ++i)
                d[i] = (i < out.size()) ? out[i] : 0xFFu;
        } else {
            // El silicio NO admite escribir en modo mapeado: la Flash serie se
            // programa por el modo indirecto, con su comando.
            gp.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        t += sc_core::SC_ZERO_TIME;
    }
    unsigned mem_dbg(tlm::tlm_generic_payload& gp) {
        // Sin efectos sobre los pines: la depuración no debe mover el bus.
        std::memset(gp.get_data_ptr(), 0xFF, gp.get_data_length());
        return gp.get_data_length();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_QUADSPI_H
