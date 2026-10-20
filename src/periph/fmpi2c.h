// =============================================================================
// fmpi2c.h — FMPI2C1: el I2C MODERNO de ST (fase 4 del plan del F446)
//
// POR QUÉ ESTO NO ES `periph/i2c.h` CON OTRA VELOCIDAD, que es la confusión que
// el nombre invita a hacer. «FMP» es *Fast-mode Plus*, y suena a «el de siempre
// a 1 MHz». No lo es: **es otro IP**, el que ST estrenó en las familias F0 y F3
// y que luego llevó a la L4, la G4 y todas las posteriores. Se ve en tres
// sitios, y cualquiera de los tres basta para saber que no se puede reutilizar
// el modelo del I2C clásico:
//
//   * OTRO BANCO DE REGISTROS. Aquí hay `TIMINGR`, `ISR`, `ICR`, `RXDR` y
//     `TXDR`; allí había `CCR`, `TRISE`, `SR1`, `SR2` y un único `DR`. No es
//     que cambien de nombre: es que la forma de programar el reloj y la de
//     borrar una bandera son distintas.
//   * OTRA MANERA DE CONTAR. El clásico transmite hasta que el firmware dice
//     basta; este lleva `NBYTES`, y el hardware **cuenta los bytes solo**, pone
//     `TC` cuando termina y hasta genera el STOP por su cuenta si `AUTOEND`
//     está puesto. Media rutina de firmware desaparece.
//   * OTRO MODELO DE BANDERAS. Allí, leer `SR1` y después `SR2` borraba
//     `ADDR` —el famoso baile que todo el mundo copia sin entender—; aquí hay
//     un registro de limpieza explícito, `ICR`, y se borra escribiendo un uno.
//
// ST le dedica un capítulo aparte del capítulo del I2C clásico (RM0390 cap. 23
// frente al 24), y la base de pines de CubeMX le da otra versión de IP
// (`i2c2_v1_1` frente a `i2c1_v1_5`). Reutilizar el modelo del otro habría sido
// un error de diseño, y de los caros: el firmware de un alumno que venga de una
// F0 o de una G4 espera ESTOS registros.
//
// EL VALOR DIDÁCTICO, que es por lo que el plan lo puso el primero de la fase:
// es el I2C que el alumno se va a encontrar en cualquier familia que aprenda
// después. Poder practicarlo contra un modelo —con la misma EEPROM de
// `parts/ext_parts.h` colgada de los mismos hilos— es exactamente para lo que
// este simulador existe.
//
// QUÉ HAY MODELADO
//   * el banco entero: CR1, CR2, OAR1, OAR2, TIMINGR, TIMEOUTR, ISR, ICR,
//     PECR, RXDR y TXDR;
//   * el reloj de `TIMINGR` de verdad: PRESC divide el reloj del periférico
//     —que en el F446 lo elige `RCC_DCKCFGR2.FMPI2C1SEL`, véase la fase 3— y
//     SCLL y SCLH miden los dos semiperiodos en esos tics;
//   * MAESTRO a nivel de bit sobre los pines en colector abierto: START, START
//     repetido, STOP, direcciones de 7 y de 10 bits, y la cuenta de NBYTES con
//     RELOAD y AUTOEND;
//   * ESCLAVO: reconocimiento de START y STOP por los flancos del bus,
//     comparación con OAR1 (7 y 10 bits) y OAR2 (con su máscara), dirección
//     leída en ADDCODE, sentido en DIR, los datos en los dos sentidos y
//     estiramiento del reloj mientras el firmware no atiende (completado en
//     la fase S3 del F303; véase el esclavo, abajo);
//   * las banderas de ISR con su semántica de borrado por ICR, las dos líneas
//     de interrupción —evento y error, que en el F446 son los vectores 95 y
//     96— y las peticiones de DMA.
//
// QUÉ NO
//   * el PEC de SMBus: los registros están y `PECEN` se guarda, pero el CRC-8
//     no se calcula. Se dice en `limitaciones()`;
//   * los tiempos de `TIMEOUTR`, por lo mismo;
//   * `SDADEL` y `SCLDEL` se guardan y no retrasan nada: son retardos de
//     picosegundos frente a un bit de microsegundos, y fingir que los aplicamos
//     daría una precisión que el modelo no tiene.
//
// El bus es ABIERTO EN COLECTOR, igual que en el I2C clásico: el periférico
// nunca fuerza un uno. Pone su salida a cero para tirar de la línea y la suelta
// para dejarla subir, y el nivel alto lo dan las resistencias de la placa.
// =============================================================================
#ifndef STM32_PERIPH_FMPI2C_H
#define STM32_PERIPH_FMPI2C_H

#include "../common/periph_base.h"

namespace stm32 {

class FmpI2c : public BusSlave {
public:
    // Dos líneas de interrupción, como el I2C clásico, y en el F446 son dos
    // vectores distintos: 95 (evento) y 96 (error).
    sc_core::sc_out<bool> irq_ev{"irq_ev"}, irq_er{"irq_er"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // Pines, por el mux de funciones alternativas. `oe` va siempre a uno: quien
    // decide si el pad tira o suelta es el propio nivel de `out`, porque el pad
    // está en colector abierto.
    sc_core::sc_signal<bool> scl_out{"scl_out"}, scl_oe{"scl_oe"}, scl_in{"scl_in"};
    sc_core::sc_signal<bool> sda_out{"sda_out"}, sda_oe{"sda_oe"}, sda_in{"sda_in"};
    // El reloj del periférico. NO es PCLK1 necesariamente: en el F446 lo elige
    // FMPI2C1SEL entre PCLK1, SYSCLK y el HSI [RM0390, §6.3.28]. El top le
    // engancha el que corresponda, que es lo que hace que cambiar el selector
    // cambie la velocidad del bus de verdad.
    sc_core::sc_in<double> ker_clk_hz{"ker_clk_hz"};

    // ---- Offsets [RM0390, §23.7; stm32f446xx.h, FMPI2C_TypeDef] -----------
    enum : uint32_t {
        R_CR1 = 0x00, R_CR2 = 0x04, R_OAR1 = 0x08, R_OAR2 = 0x0C,
        R_TIMINGR = 0x10, R_TIMEOUTR = 0x14, R_ISR = 0x18, R_ICR = 0x1C,
        R_PECR = 0x20, R_RXDR = 0x24, R_TXDR = 0x28
    };
    // ---- CR1 ---------------------------------------------------------------
    static constexpr uint32_t C1_PE = 1u << 0, C1_TXIE = 1u << 1,
        C1_RXIE = 1u << 2, C1_ADDRIE = 1u << 3, C1_NACKIE = 1u << 4,
        C1_STOPIE = 1u << 5, C1_TCIE = 1u << 6, C1_ERRIE = 1u << 7,
        C1_TXDMAEN = 1u << 14, C1_RXDMAEN = 1u << 15, C1_SBC = 1u << 16,
        C1_NOSTRETCH = 1u << 17, C1_GCEN = 1u << 19, C1_PECEN = 1u << 23;
    // ---- CR2 ---------------------------------------------------------------
    static constexpr uint32_t C2_RD_WRN = 1u << 10, C2_ADD10 = 1u << 11,
        C2_HEAD10R = 1u << 12, C2_START = 1u << 13, C2_STOP = 1u << 14,
        C2_NACK = 1u << 15, C2_RELOAD = 1u << 24, C2_AUTOEND = 1u << 25,
        C2_PECBYTE = 1u << 26;
    // ---- ISR ---------------------------------------------------------------
    static constexpr uint32_t I_TXE = 1u << 0, I_TXIS = 1u << 1,
        I_RXNE = 1u << 2, I_ADDR = 1u << 3, I_NACKF = 1u << 4,
        I_STOPF = 1u << 5, I_TC = 1u << 6, I_TCR = 1u << 7,
        I_BERR = 1u << 8, I_ARLO = 1u << 9, I_OVR = 1u << 10,
        I_PECERR = 1u << 11, I_TIMEOUT = 1u << 12, I_ALERT = 1u << 13,
        I_BUSY = 1u << 15, I_DIR = 1u << 16;

    explicit FmpI2c(sc_core::sc_module_name nm, uint32_t base)
        : BusSlave(nm, base, 0x400) {
        SC_HAS_PROCESS(FmpI2c);
        SC_THREAD(master_proc);
        SC_THREAD(slave_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;   dont_initialize();
        SC_METHOD(rst_proc);   sensitive << rst_n;     dont_initialize();
        reset_regs();
    }

    // --- Consulta para la verificación --------------------------------------
    double scl_hz() const {
        const double t = t_presc();
        if (t <= 0.0) return 0.0;
        const double per = t * double((scll() + 1u) + (sclh() + 1u));
        return per > 0.0 ? 1.0 / per : 0.0;
    }
    uint32_t peek_isr() const { return isr_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR1:      return cr1_;
            case R_CR2:      return cr2_;
            case R_OAR1:     return oar1_;
            case R_OAR2:     return oar2_;
            case R_TIMINGR:  return timingr_;
            case R_TIMEOUTR: return timeoutr_;
            case R_ISR:      return isr_;
            case R_ICR:      return 0;          // solo escritura
            case R_PECR:     return pec_;
            case R_RXDR:     leido_rx_ = true;
                             isr_ &= ~I_RXNE;
                             actualiza_irq();
                             return rxdr_;
            case R_TXDR:     return txdr_;
            default:         return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: {
                const bool antes = pe();
                // Todos los campos: hasta PECEN (23), sin el 13, que no existe
                // [RM0316 §28.7.1]. La mascara de antes, 0x00F07FFF, se
                // comia RXDMAEN (15), SBC, NOSTRETCH, WUPEN y GCEN (16-19)
                cr1_ = v & 0x00FFDFFFu;
                // PE a cero PARA el periférico y borra la mayoría del estado
                // [RM0390, §23.7.1]. No es un detalle: es la forma canónica de
                // sacar al bloque de un bus atascado.
                if (antes && !pe()) apaga();
                actualiza_irq();
                break;
            }
            case R_CR2:
                cr2_ = v & 0x07FFFFFFu;
                if (pe() && (cr2_ & C2_START)) despierta_maestro();
                if (pe() && (cr2_ & C2_STOP))  despierta_maestro();
                actualiza_irq();
                break;
            case R_OAR1:     oar1_ = v & 0x000087FFu; break;   // OA1EN, OA1MODE, OA1 [RM0316 §28.7.3]
            case R_OAR2:     oar2_ = v & 0x000087FEu; break;
            case R_TIMINGR:
                // Solo se programa con el periférico apagado, igual que el
                // silicio: cambiar el reloj a mitad de una trama no tiene
                // significado definido.
                if (!pe()) timingr_ = v & 0xF0FFFFFFu;
                else SC_REPORT_WARNING("fmpi2c",
                        "FMPI2C_TIMINGR escrito con PE=1: el silicio no lo "
                        "admite [RM0390, 23.7.5]");
                break;
            case R_TIMEOUTR: timeoutr_ = v & 0x8FFF8FFFu; break;
            case R_ICR: {
                // El registro de limpieza: un uno borra su bandera. Es LA
                // diferencia de estilo con el I2C clásico, donde había que leer
                // SR1 y luego SR2 en el orden correcto.
                const uint32_t m = v & 0x00003F38u;
                isr_ &= ~m;
                actualiza_irq();
                break;
            }
            case R_TXDR:
                txdr_ = v & 0xFFu;
                isr_ &= ~(I_TXE | I_TXIS);
                hay_tx_ = true;
                actualiza_irq();
                despierta_maestro();
                break;
            default: break;
        }
    }

    bool responds_without_clock() const override { return false; }

private:
    // ---- Estado ------------------------------------------------------------
    uint32_t cr1_ = 0, cr2_ = 0, oar1_ = 0, oar2_ = 0;
    uint32_t timingr_ = 0, timeoutr_ = 0, isr_ = I_TXE, pec_ = 0;
    uint32_t rxdr_ = 0, txdr_ = 0;
    bool hay_tx_ = false, leido_rx_ = true;
    bool o_scl_ = true, o_sda_ = true;      // true = soltada (colector abierto)
    bool o_irq_ev_ = false, o_irq_er_ = false;
    bool o_drq_rx_ = false, o_drq_tx_ = false;
    unsigned nbytes_ = 0;                   // los que quedan de esta tanda
    bool en_transferencia_ = false;         // el maestro tiene el bus tomado
    sc_core::sc_event pub_ev_, wake_ev_;
    // Esclavo
    bool sl_scl_prev_ = true, sl_sda_prev_ = true;
    bool sl_activo_ = false;

    bool pe()        const { return (cr1_ & C1_PE) != 0; }
    unsigned presc() const { return (timingr_ >> 28) & 0xFu; }
    unsigned scll()  const { return timingr_ & 0xFFu; }
    unsigned sclh()  const { return (timingr_ >> 8) & 0xFFu; }
    unsigned nbytes_cfg() const { return (cr2_ >> 16) & 0xFFu; }
    bool rd_wrn()    const { return (cr2_ & C2_RD_WRN) != 0; }
    bool autoend()   const { return (cr2_ & C2_AUTOEND) != 0; }
    bool reload()    const { return (cr2_ & C2_RELOAD) != 0; }

    // El tic del prescaler. `ker_clk_hz` es el reloj del periférico, que en el
    // F446 NO tiene por qué ser PCLK1: lo elige FMPI2C1SEL.
    double t_presc() const {
        const double f = ker_clk_hz.read();
        if (f <= 0.0) return 0.0;
        return double(presc() + 1u) / f;
    }
    sc_core::sc_time t_bajo() const {
        return sc_core::sc_time(t_presc() * double(scll() + 1u), sc_core::SC_SEC);
    }
    sc_core::sc_time t_alto() const {
        return sc_core::sc_time(t_presc() * double(sclh() + 1u), sc_core::SC_SEC);
    }

    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void despierta_maestro() { wake_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        scl_out.write(o_scl_); scl_oe.write(true);
        sda_out.write(o_sda_); sda_oe.write(true);
        irq_ev.write(o_irq_ev_); irq_er.write(o_irq_er_);
        dma_req_rx.write(o_drq_rx_); dma_req_tx.write(o_drq_tx_);
    }
    void set_scl(bool v) { o_scl_ = v; publica(); }
    void set_sda(bool v) { o_sda_ = v; publica(); }

    void reset_regs() {
        cr1_ = cr2_ = oar1_ = oar2_ = 0;
        timingr_ = timeoutr_ = 0; pec_ = 0;
        isr_ = I_TXE;                 // TXE nace a uno: el buffer está vacío
        rxdr_ = txdr_ = 0;
        hay_tx_ = false; leido_rx_ = true;
        nbytes_ = 0; en_transferencia_ = false;
        o_scl_ = o_sda_ = true;
        o_irq_ev_ = o_irq_er_ = false;
        o_drq_rx_ = o_drq_tx_ = false;
    }
    void apaga() {
        isr_ = I_TXE;
        en_transferencia_ = false; sl_activo_ = false;
        o_scl_ = o_sda_ = true;
        publica();
    }
    void rst_proc() {
        if (rst_n.read()) return;
        reset_regs();
        publica();
    }

    void actualiza_irq() {
        // Evento: las banderas que el firmware atiende en la rutina normal.
        bool ev = false;
        if ((cr1_ & C1_TXIE)   && (isr_ & I_TXIS))  ev = true;
        if ((cr1_ & C1_RXIE)   && (isr_ & I_RXNE))  ev = true;
        if ((cr1_ & C1_ADDRIE) && (isr_ & I_ADDR))  ev = true;
        if ((cr1_ & C1_NACKIE) && (isr_ & I_NACKF)) ev = true;
        if ((cr1_ & C1_STOPIE) && (isr_ & I_STOPF)) ev = true;
        if ((cr1_ & C1_TCIE)   && (isr_ & (I_TC | I_TCR))) ev = true;
        // Error: BERR, ARLO y OVR (y los de SMBus, que aquí no se levantan).
        const bool er = (cr1_ & C1_ERRIE) &&
                        (isr_ & (I_BERR | I_ARLO | I_OVR | I_PECERR | I_TIMEOUT));
        o_irq_ev_ = pe() && ev;
        o_irq_er_ = pe() && er;
        // DMA: una petición por cada hueco. El DMA escribe TXDR o lee RXDR, y
        // con eso la bandera se va sola.
        o_drq_tx_ = pe() && (cr1_ & C1_TXDMAEN) && (isr_ & I_TXIS);
        o_drq_rx_ = pe() && (cr1_ & C1_RXDMAEN) && (isr_ & I_RXNE);
        publica();
    }

    // =======================================================================
    // MAESTRO, a nivel de bit
    // =======================================================================
    bool suelta_scl_y_espera() {
        set_scl(true);
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        // Un esclavo puede estirar: en el silicio, sin límite; aquí, 400
        // semiperiodos o 25 ms, lo que sea más (un esclavo de este mismo IP
        // estira mientras su firmware atiende ADDR)
        sc_core::sc_time tope = t_bajo() * 400.0;
        if (tope < sc_core::sc_time(25, sc_core::SC_MS)) tope = sc_core::sc_time(25, sc_core::SC_MS);
        while (!scl_in.read()) {
            if (sc_core::sc_time_stamp() - t0 > tope) return false;
            wait(t_alto() / 4.0, scl_in.value_changed_event());
        }
        return true;
    }
    bool bit_out(bool b) {
        set_scl(false);
        wait(t_bajo() / 2.0);
        set_sda(b);
        wait(t_bajo() / 2.0);
        if (!suelta_scl_y_espera()) return false;
        wait(t_alto() / 2.0);
        if (b && !sda_in.read()) {               // arbitraje perdido
            isr_ |= I_ARLO;
            pierde_bus();
            return false;
        }
        wait(t_alto() / 2.0);
        set_scl(false);
        return true;
    }
    bool bit_in() {
        set_scl(false);
        wait(t_bajo() / 2.0);
        set_sda(true);                           // soltar para escuchar
        wait(t_bajo() / 2.0);
        if (!suelta_scl_y_espera()) return true;
        wait(t_alto() / 2.0);
        const bool b = sda_in.read();
        wait(t_alto() / 2.0);
        set_scl(false);
        return b;
    }
    void pierde_bus() {
        set_sda(true); set_scl(true);
        en_transferencia_ = false;
        isr_ &= ~I_BUSY;
        actualiza_irq();
    }
    // Devuelve true si el receptor reconoce
    bool envia_byte(uint8_t v, bool& perdido) {
        perdido = false;
        for (int i = 7; i >= 0; --i)
            if (!bit_out((v >> i) & 1u)) { perdido = true; return false; }
        return !bit_in();                        // ACK = SDA a cero
    }
    uint8_t recibe_byte(bool ack) {
        uint8_t v = 0;
        for (int i = 0; i < 8; ++i) v = uint8_t((v << 1) | (bit_in() ? 1u : 0u));
        bool perdido = false;
        (void)perdido;
        bit_out(!ack);                           // ACK = cero
        return v;
    }
    void condicion_start() {
        // El bus es nuestro desde ya: el esclavo de este mismo bloque no tiene
        // que tomar por suyo el START que pone el maestro
        en_transferencia_ = true;
        set_sda(true);
        // Un START REPETIDO empieza con SCL abajo, y puede que un esclavo lo
        // esté estirando (el de 10 bits, mientras su firmware atiende ADDR):
        // hay que esperar a que suba de verdad, o SDA bajaría con SCL bajo y
        // eso no es un START (S3)
        suelta_scl_y_espera();
        wait(t_alto());
        set_sda(false);                          // SDA baja con SCL alto
        wait(t_alto() / 2.0);
        set_scl(false);
        isr_ |= I_BUSY;
        en_transferencia_ = true;
    }
    void condicion_stop() {
        set_scl(false);
        wait(t_bajo() / 2.0);
        set_sda(false);
        wait(t_bajo() / 2.0);
        set_scl(true);
        wait(t_alto() / 2.0);
        set_sda(true);                           // SDA sube con SCL alto
        wait(t_alto());
        en_transferencia_ = false;
        isr_ &= ~I_BUSY;
        isr_ |= I_STOPF;
        actualiza_irq();
    }

    // -----------------------------------------------------------------------
    // La secuencia completa de una transferencia, que es donde se ve lo que
    // este IP hace y el clásico no: el hardware CUENTA LOS BYTES.
    // -----------------------------------------------------------------------
    void master_proc() {
        for (;;) {
            wait(wake_ev_);
            if (!pe()) continue;
            if (!(cr2_ & C2_START)) {
                // STOP suelto: el firmware quiere cerrar una transferencia que
                // dejó abierta con AUTOEND = 0.
                if ((cr2_ & C2_STOP) && en_transferencia_) {
                    condicion_stop();
                    cr2_ &= ~C2_STOP;
                    isr_ &= ~(I_TC | I_TCR);
                    actualiza_irq();
                }
                continue;
            }
            cr2_ &= ~C2_START;
            if (t_presc() <= 0.0) {
                SC_REPORT_WARNING("fmpi2c",
                    "START con TIMINGR sin programar o sin reloj de periferico: "
                    "no hay con que generar SCL");
                continue;
            }
            condicion_start();

            // --- La dirección ---------------------------------------------
            const unsigned sadd = cr2_ & 0x3FFu;
            bool perdido = false, ack = false;
            if (cr2_ & C2_ADD10) {
                // Cabecera de 10 bits: 11110xx0 y luego los ocho de abajo.
                const uint8_t h = uint8_t(0xF0u | ((sadd >> 8) & 3u) << 1);
                ack = envia_byte(h, perdido);
                if (!perdido && ack) ack = envia_byte(uint8_t(sadd & 0xFFu), perdido);
                if (!perdido && ack && rd_wrn()) {
                    // Para LEER hay que repetir el START con el bit de lectura.
                    // Antes, SCL se queda abajo su semiperiodo: sin esto el
                    // START repetido salía pegado al noveno pulso, sin flanco
                    // de bajada, y un esclavo de 10 bits no lo veía (S3)
                    set_sda(true);
                    wait(t_bajo());
                    condicion_start();
                    ack = envia_byte(uint8_t(h | 1u), perdido);
                }
            } else {
                const uint8_t a = uint8_t(((sadd >> 1) & 0x7Fu) << 1) |
                                  (rd_wrn() ? 1u : 0u);
                ack = envia_byte(a, perdido);
            }
            if (perdido) { actualiza_irq(); continue; }
            if (!ack) {
                // NACK a la dirección: no hay nadie. El silicio levanta NACKF y,
                // con AUTOEND, cierra con un STOP.
                isr_ |= I_NACKF;
                actualiza_irq();
                if (autoend()) condicion_stop();
                continue;
            }

            // --- Los datos, con NBYTES ------------------------------------
            nbytes_ = nbytes_cfg();
            bool corta = false;
            while (nbytes_ > 0 && !corta) {
                if (rd_wrn()) {
                    // Recepción: el último byte de la tanda se NACKea salvo que
                    // haya RELOAD -entonces viene otra tanda y se reconoce-.
                    const bool ultimo = (nbytes_ == 1) && !reload();
                    const uint8_t v = recibe_byte(!ultimo);
                    rxdr_ = v;
                    isr_ |= I_RXNE;
                    leido_rx_ = false;
                    actualiza_irq();
                    // Estiramos el reloj hasta que el firmware lea RXDR, que es
                    // lo que hace el silicio con NOSTRETCH = 0.
                    if (!espera_a_que_lea()) { corta = true; break; }
                } else {
                    // Transmisión: TXIS pide el byte siguiente.
                    isr_ |= I_TXIS;
                    actualiza_irq();
                    if (!espera_a_que_escriba()) { corta = true; break; }
                    const uint8_t v = uint8_t(txdr_);
                    hay_tx_ = false;
                    isr_ |= I_TXE;
                    const bool a = envia_byte(v, perdido);
                    if (perdido) { corta = true; break; }
                    if (!a) {
                        isr_ |= I_NACKF;
                        actualiza_irq();
                        if (autoend()) condicion_stop();
                        corta = true;
                        break;
                    }
                }
                --nbytes_;
                if (nbytes_ == 0 && reload()) {
                    // RELOAD: se paran los bytes pero NO el bus. El firmware
                    // escribe otro NBYTES y la cosa sigue: es como se mandan
                    // tramas de más de 255 bytes.
                    isr_ |= I_TCR;
                    actualiza_irq();
                    if (!espera_nueva_tanda()) { corta = true; break; }
                    nbytes_ = nbytes_cfg();
                }
            }
            if (corta) { actualiza_irq(); continue; }

            // --- El final -------------------------------------------------
            if (autoend()) {
                // El hardware pone el STOP por su cuenta. Media rutina de
                // firmware que en el I2C clásico había que escribir.
                condicion_stop();
            } else {
                // TC: transferencia hecha, bus tomado. El firmware decide si
                // repite el START -para un cambio de sentido- o cierra.
                isr_ |= I_TC;
                actualiza_irq();
            }
        }
    }

    // Esperas con tope: si el firmware no atiende, el modelo no se queda
    // colgado para siempre; suelta el bus y lo dice.
    bool espera_a_que_lea() {
        const sc_core::sc_time tope = t_bajo() * 10000.0;
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        while (!leido_rx_) {
            wait(t_bajo(), wake_ev_);
            if (!pe()) return false;
            if (sc_core::sc_time_stamp() - t0 > tope) {
                SC_REPORT_WARNING("fmpi2c", "nadie lee RXDR: se suelta el bus");
                pierde_bus();
                return false;
            }
        }
        return true;
    }
    bool espera_a_que_escriba() {
        const sc_core::sc_time tope = t_bajo() * 10000.0;
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        while (!hay_tx_) {
            wait(t_bajo(), wake_ev_);
            if (!pe()) return false;
            if (sc_core::sc_time_stamp() - t0 > tope) {
                SC_REPORT_WARNING("fmpi2c", "nadie escribe TXDR: se suelta el bus");
                pierde_bus();
                return false;
            }
        }
        return true;
    }
    bool espera_nueva_tanda() {
        const sc_core::sc_time tope = t_bajo() * 10000.0;
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        while (isr_ & I_TCR) {
            wait(t_bajo(), wake_ev_);
            if (!pe()) return false;
            if (sc_core::sc_time_stamp() - t0 > tope) { pierde_bus(); return false; }
        }
        return true;
    }

    // =======================================================================
    // ESCLAVO: lo gobiernan los flancos del bus, no un registro
    //
    // (S3 del F303.) La fase 4 del F446 reconocía la dirección y levantaba
    // ADDR, y ahí se quedaba. Ahora el esclavo es entero, el que pide la HAL
    // de cualquier familia con este IP [RM0316 §28.4.9]:
    //   * direcciones de 7 bits en OAR1 y OAR2 (con OA2MSK) y la llamada
    //     general; y de 10 bits en OAR1, con la cabecera 11110xx0, el segundo
    //     byte, y el START repetido con la cabecera de lectura 11110xx1
    //   * ADDR con DIR y ADDCODE, y el reloj estirado hasta que el firmware lo
    //     borra (con NOSTRETCH = 0, que es lo que usa la HAL)
    //   * recibir: RXNE por byte, y el reloj estirado antes del ACK si el
    //     anterior sigue sin leer; CR2.NACK manda un NACK
    //   * transmitir: TXIS cuando hace falta el siguiente, el reloj estirado
    //     hasta que llega a TXDR, y NACKF cuando el maestro dice basta
    //   * STOPF con el STOP, y otra vez ADDR con un START repetido
    // =======================================================================
    enum : int { EV_STOP = -1, EV_START = -2, EV_FUERA = -3 };
    bool sl_dir10_ = false;                // la cabecera de 10 bits ya cuadró

    void slave_proc() {
        for (;;) {
            wait(scl_in.value_changed_event() | sda_in.value_changed_event());
            if (!pe() || en_transferencia_) {     // de maestro no se escucha
                sl_scl_prev_ = scl_in.read(); sl_sda_prev_ = sda_in.read();
                continue;
            }
            const bool scl = scl_in.read(), sda = sda_in.read();
            // START: SDA baja con SCL alto. STOP: SDA sube con SCL alto.
            if (scl && sl_scl_prev_ && sl_sda_prev_ && !sda) {
                sl_dir10_ = false;
                int r = EV_START;
                while (r == EV_START) r = sesion_esclavo();
                if (r == EV_STOP) fin_esclavo();
            } else if (scl && sl_scl_prev_ && !sl_sda_prev_ && sda) {
                fin_esclavo();
            }
            sl_scl_prev_ = scl_in.read(); sl_sda_prev_ = sda_in.read();
        }
    }
    void fin_esclavo() {
        if (sl_activo_) isr_ |= I_STOPF;
        sl_activo_ = false;
        isr_ &= ~I_BUSY;
        set_scl(true); set_sda(true);
        actualiza_irq();
    }
    // Un bit: el nivel de SDA en el flanco de subida de SCL; o la condición
    // que el maestro ha puesto mientras SCL estaba alto
    int lee_bit() {
        while (!scl_in.read()) {
            wait(scl_in.value_changed_event() | wake_ev_);
            if (!pe()) return EV_FUERA;
        }
        const bool v = sda_in.read();
        while (scl_in.read()) {
            wait(scl_in.value_changed_event() | sda_in.value_changed_event() | wake_ev_);
            if (!pe()) return EV_FUERA;
            if (scl_in.read() && sda_in.read() != v) return sda_in.read() ? EV_STOP : EV_START;
        }
        return v ? 1 : 0;
    }
    int lee_byte(uint8_t& b) {
        b = 0;
        for (int i = 0; i < 8; ++i) {
            const int r = lee_bit();
            if (r < 0) return r;
            b = uint8_t((b << 1) | unsigned(r));
        }
        return 0;
    }
    // El noveno pulso, con SDA abajo (ACK) o suelta (NACK); empieza con SCL
    // bajo y acaba en su flanco de bajada
    bool pulso_ack(bool ack) {
        set_sda(!ack);
        if (!espera_scl(true)) return false;
        if (!espera_scl(false)) return false;
        set_sda(true);
        return true;
    }
    bool espera_scl(bool nivel) {
        while (scl_in.read() != nivel) {
            wait(scl_in.value_changed_event() | wake_ev_);
            if (!pe()) return false;
        }
        return true;
    }
    // Estirar el reloj (SCL abajo) mientras `sigue` diga que sí, con tope
    template <class F> bool estira(F sigue) {
        if (cr1_ & C1_NOSTRETCH) return true;
        set_scl(false);
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        const sc_core::sc_time tope = sc_core::sc_time(25, sc_core::SC_MS);
        while (sigue()) {
            wait(sc_core::sc_time(1, sc_core::SC_US), wake_ev_);
            if (!pe()) { set_scl(true); return false; }
            if (sc_core::sc_time_stamp() - t0 > tope) {
                SC_REPORT_WARNING("fmpi2c", "esclavo: el firmware no atiende y el reloj "
                                  "lleva 25 ms estirado; se suelta el bus");
                set_scl(true);
                return false;
            }
        }
        return true;
    }

    bool mia_7(uint8_t a) const {
        const unsigned d7 = a >> 1;
        if ((oar1_ & (1u << 15)) && !(oar1_ & (1u << 10)) && d7 == ((oar1_ >> 1) & 0x7Fu)) return true;
        if (oar2_ & (1u << 15)) {
            // OA2MSK: cuántos bits bajos de OA2 no se comparan [RM0316 §28.7.4]
            const unsigned msk = (oar2_ >> 8) & 7u;
            if (msk == 7) return (d7 & 0x78u) != 0x78u && d7 > 7u;   // todas las no reservadas
            const unsigned m = 0x7Fu & ~((1u << msk) - 1u);
            if ((d7 & m) == (((oar2_ >> 1) & 0x7Fu) & m)) return true;
        }
        return (cr1_ & C1_GCEN) && a == 0;
    }

    // Una sesión: de un START (o un START repetido) hasta el siguiente o el STOP
    int sesion_esclavo() {
        isr_ |= I_BUSY;
        // El START acaba cuando el maestro baja SCL: el primer bit va después
        if (!espera_scl(false)) return EV_FUERA;
        uint8_t a = 0;
        int r = lee_byte(a);
        if (r < 0) return r;
        const bool leer = a & 1u;
        const bool d10 = (oar1_ & (1u << 15)) && (oar1_ & (1u << 10));
        bool mia = false;
        if (d10 && (a & 0xF8u) == 0xF0u) {
            const bool cab = ((a >> 1) & 3u) == ((oar1_ >> 8) & 3u);
            if (!leer) {
                if (!cab) return espera_fin();
                if (!pulso_ack(true)) return EV_FUERA;
                uint8_t b = 0;
                r = lee_byte(b);
                if (r < 0) return r;
                if (b != (oar1_ & 0xFFu)) { sl_dir10_ = false; return espera_fin(); }
                sl_dir10_ = true;
                mia = true;
            } else {
                mia = cab && sl_dir10_;
            }
        } else {
            mia = mia_7(a);
        }
        if (!mia) return espera_fin();
        // ACK, ADDR y el reloj estirado hasta que el firmware borre ADDR
        sl_activo_ = true;
        if (!pulso_ack(true)) return EV_FUERA;
        const unsigned code = (d10 && (a & 0xF8u) == 0xF0u) ? (a >> 1) : (a >> 1);
        isr_ = (isr_ & ~(0x7Fu << 17)) | (uint32_t(code & 0x7Fu) << 17);
        if (leer) isr_ |= I_DIR; else isr_ &= ~I_DIR;
        isr_ |= I_ADDR;
        if (leer) { isr_ |= I_TXE; hay_tx_ = false; }   // el transmisor empieza vacío
        actualiza_irq();
        if (!estira([&] { return (isr_ & I_ADDR) != 0; })) return EV_FUERA;
        return leer ? transmite_esclavo() : recibe_esclavo();
    }
    // No era para nosotros: se espera, sin tocar el bus, al STOP o al START
    int espera_fin() {
        for (;;) {
            const int r = lee_bit();
            if (r < 0) return r;
        }
    }
    int recibe_esclavo() {
        set_scl(true);
        for (;;) {
            uint8_t v = 0;
            const int r = lee_byte(v);
            if (r < 0) return r;
            // El anterior sin leer: se estira antes del ACK [RM0316 §28.4.9]
            if (isr_ & I_RXNE) {
                if (!estira([&] { return (isr_ & I_RXNE) != 0; })) return EV_FUERA;
            }
            rxdr_ = v;
            isr_ |= I_RXNE;
            leido_rx_ = false;
            actualiza_irq();
            const bool ack = !(cr2_ & C2_NACK);
            cr2_ &= ~C2_NACK;
            set_sda(!ack);
            set_scl(true);
            if (!espera_scl(true) || !espera_scl(false)) return EV_FUERA;
            set_sda(true);
        }
    }
    int transmite_esclavo() {
        for (;;) {
            // Hace falta el siguiente: TXIS, y el reloj estirado hasta que llega
            if (!hay_tx_) {
                isr_ |= I_TXIS | I_TXE;
                actualiza_irq();
                if (!estira([&] { return !hay_tx_; })) return EV_FUERA;
            }
            const uint8_t v = uint8_t(txdr_);
            hay_tx_ = false;
            isr_ |= I_TXE;
            actualiza_irq();
            for (int i = 7; i >= 0; --i) {
                set_sda((v >> i) & 1u);
                set_scl(true);
                if (!espera_scl(true) || !espera_scl(false)) return EV_FUERA;
            }
            set_sda(true);
            // El noveno: ¿el maestro reconoce?
            if (!espera_scl(true)) return EV_FUERA;
            const bool ack = !sda_in.read();
            if (!espera_scl(false)) return EV_FUERA;
            if (!ack) {
                isr_ |= I_NACKF;
                isr_ &= ~I_TXIS;
                actualiza_irq();
                return espera_fin();
            }
            // Pide ya el siguiente, para que el firmware lo prepare
            isr_ |= I_TXIS;
            actualiza_irq();
            set_scl(false);
            if (!estira([&] { return !hay_tx_; })) return EV_FUERA;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_FMPI2C_H
