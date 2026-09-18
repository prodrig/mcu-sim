// =============================================================================
// stm32f446.h — EL STM32F446
//
// «El F407 menos lo que no tiene, más su árbol de reloj.» La fase 2 puso lo
// primero [doc/stm32f407vg_vs_446re.md, §17] —sin Ethernet, sin RNG, sin CCM,
// sin los bloques de extensión del I2S, con 512 KB de Flash, con el LQFP64 del
// F446 y con las 97 posiciones de vector— y la fase 3 lo segundo (§18): el
// tercer PLL, el divisor R, los nueve selectores de RCC_DCKCFGR y DCKCFGR2, y
// el over-drive del PWR con el que llega a 180 MHz.
//
// POR QUÉ ES UNA CLASE Y NO UNA FILA MÁS DEL CATÁLOGO. Es la pregunta que este
// fichero tiene que contestar, porque la respuesta fácil —«un descriptor más y
// ya»— es la que el plan marcó como el tercer riesgo del puerto.
//
// Casi todo lo que distingue a un F446 de un F407 SÍ cabe en un descriptor: la
// Flash, la RAM, los pads, las posiciones de vector, qué bloques faltan, el
// IDCODE, cuántos maestros tiene la matriz. Todo eso está en `MCU_STM32F446RE`
// y no hay una línea de código nueva para ello: lo construye `SocF4`, el mismo
// die de siempre, leyendo el descriptor.
//
// Lo que NO cabía en un descriptor era el árbol de reloj, y esa es la razón de
// que esta clase exista. La fase 3 lo ha escrito, y el matiz importa: lo que
// hace que ahora SÍ quepa en el descriptor el rasgo `ArbolReloj` es que el
// modelo IMPLEMENTA cada uno de sus cinco puntos. Un booleano que dijera «este
// chip tiene DCKCFGR» sin que nadie decodificara el registro sería justo la
// mentira que este proyecto lleva toda su vida quitando; un booleano que
// enciende un registro que existe, con sus nueve selectores cambiando
// frecuencias observables, es un rasgo.
//
// Y LO QUE SIGUE FALTANDO, LO DICE. `limitaciones()` lo enumera y `sim` lo
// imprime cada vez que monta una placa con un F446. Un modelo incompleto no es
// un problema; uno que no lo dice, sí.
// =============================================================================
#ifndef STM32_SOC_STM32F446_H
#define STM32_SOC_STM32F446_H

#include "stm32f4_mcu.h"
#include "f446_mapa_perif.h"
#include "../periph/fmpi2c.h"
#include "../periph/quadspi.h"
#include "../periph/sai.h"
#include "../periph/bloque_declarado.h"

namespace stm32 {

class Stm32F446 : public SocF4 {
public:
    // -----------------------------------------------------------------------
    // LOS SIETE BLOQUES QUE SOLO ESTÁN AQUÍ [fase 4]
    //
    // Se declaran en la DERIVADA y no en `SocF4` a propósito. Un módulo de
    // SystemC existe desde que se construye, y construir en el die común un
    // QUADSPI que el F407 no lleva significaría tener que taparlo, darle reloj
    // y explicarlo en cada chip de la familia. Aquí no hace falta: el que no
    // es un F446 sencillamente no los tiene.
    //
    // El SPI4 es la excepción interesante: NO es un bloque nuevo, es otra
    // instancia del mismo `spi.h`, y por eso cuesta una línea. En un F446RE no
    // tiene pines —el LQFP64 no se los saca `[PINDATA]`—, así que el firmware
    // puede programarlo y no verá nada en ningún pad. Eso no es un fallo del
    // modelo: es lo que hace el silicio, y `limitaciones()` lo dice.
    // -----------------------------------------------------------------------
    FmpI2c  fmpi2c1{"fmpi2c1", addr446::FMPI2C1_B};
    QuadSpi qspi{"qspi", addr446::QUADSPI_B};
    Sai     sai1{"sai1", addr446::SAI1_B};
    Sai     sai2{"sai2", addr446::SAI2_B};
    SpiBase spi4{"spi4", addr446::SPI4_B, CAPS_SPI_APB2};
    // Los dos que el plan dejó «declarados y sin modelar, diciéndolo».
    BloqueDeclarado spdifrx{"spdifrx", addr446::SPDIFRX_B, 0x400, "SPDIF-RX",
        "Es un receptor de audio digital S/PDIF: sin una fuente externa que "
        "le mande trama no hay nada que recibir, y el plan de la fase 4 lo "
        "puso en el ultimo escalon de utilidad docente."};
    BloqueDeclarado cec{"cec", addr446::CEC_B, 0x400, "HDMI-CEC",
        "Es el canal de control de un televisor por HDMI: fuera de ese "
        "contexto no hay con quien hablar. Mismo escalon que el SPDIF-RX."};

    // El decodificador del puerto de memoria externa. En el F446 ese puerto de
    // la matriz es «FMC / QUADSPI» y lleva DOS ventanas -la mapeada en memoria
    // y la de registros-, de modo que hace falta repartir. En el F407 no lo
    // hay: allí ese puerto va directo al FSMC.
    AhbDecoder ahb3_dec{"ahb3_dec"};
    // El SAI tiene un vector por bloque de silicio y DOS bloques dentro, así
    // que sus dos interrupciones se suman, igual que las de los I2Sext.
    Or2 or_sai1{"or_sai1"}, or_sai2{"or_sai2"};
    sc_core::sc_signal<bool> s_sai_in[4];

    explicit Stm32F446(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
                       const Cableado& cab = Cableado(),
                       McuCaps caps = MCU_STM32F446RE)
        : SocF4(nm, dbg, cab, caps) {
        engancha_bus_446();
        engancha_relojes_446();
        engancha_irqs_446();
        engancha_af_446();
    }

private:
    // =======================================================================
    // EL BUS: dónde contesta cada uno de los siete
    // =======================================================================
    void engancha_bus_446() {
        // --- APB1 -----------------------------------------------------------
        // El FMPI2C1 en 0x4000_6000, que es la dirección que da la cabecera de
        // ST y que la tabla de fronteras del RM se deja sin nombrar [§5.3 del
        // documento de comparación].
        apb1_dec.add_slave("to_fmpi2c1", addr446::FMPI2C1_B, 0x400)
            ->bind(fmpi2c1.tsk);
        // 0x4000_4000. EN EL F407 ES EL I2S3ext Y AQUÍ ES EL SPDIF-RX. Es la
        // única dirección de todo el mapa que cambia de dueño entre las dos
        // piezas, y por eso el documento la marcó como el error más caro del
        // puerto: no falla, contesta otro periférico. Aquí contesta el que
        // toca, y el que toca dice que no está modelado.
        apb1_dec.add_slave("to_spdifrx", addr446::SPDIFRX_B, 0x400)
            ->bind(spdifrx.tsk);
        apb1_dec.add_slave("to_cec", addr446::CEC_B, 0x400)->bind(cec.tsk);

        // --- APB2 -----------------------------------------------------------
        apb2_dec.add_slave("to_spi4", addr446::SPI4_B, 0x400)->bind(spi4.tsk);
        // Cada SAI son TRES ventanas: el registro global del padre y los dos
        // bloques, que son esclavos de pleno derecho con su propia base. Se
        // dan de alta por separado porque cada uno decodifica lo suyo.
        for (unsigned k = 0; k < 2; ++k) {
            Sai& s = k ? sai2 : sai1;
            const uint32_t b = k ? addr446::SAI2_B : addr446::SAI1_B;
            const char* n  = k ? "to_sai2"   : "to_sai1";
            const char* na = k ? "to_sai2_a" : "to_sai1_a";
            const char* nb = k ? "to_sai2_b" : "to_sai1_b";
            apb2_dec.add_slave(n,  b,        0x04)->bind(s.tsk);
            apb2_dec.add_slave(na, b + 0x04, 0x20)->bind(s.a.tsk);
            apb2_dec.add_slave(nb, b + 0x24, 0x20)->bind(s.b.tsk);
        }

        // --- AHB3: el puerto que el FMC y el QUADSPI COMPARTEN --------------
        // `SocF4::bind_bus()` dejó este puerto sin atar y tapó el FSMC porque
        // el descriptor dice que este chip lleva QUADSPI. Aquí se ata, y va a
        // un decodificador propio porque detrás hay dos ventanas muy
        // distintas: 256 MB de memoria vista como memoria, y 1 KB de
        // registros. [RM0390, §2.1; vs_446re §15.1]
        matrix.to_slave[unsigned(BusSlaveId::FSMC_EXT)].bind(ahb3_dec.tsk);
        ahb3_dec.add_slave("to_qspi_mem", addr446::QUADSPI_MEM,
                           addr446::QUADSPI_MEM_SIZE)->bind(qspi.mem);
        ahb3_dec.add_slave("to_qspi_reg", addr446::QUADSPI_B, 0x400)
            ->bind(qspi.tsk);
        // Y lo que NO se da de alta importa tanto como lo que sí:
        // 0xA000_0000–0xA000_0FFF, los registros del FMC, se quedan sin
        // decodificar. Tocarlos da error de bus, que es lo correcto en un
        // F446RE: el datasheet dice que ese encapsulado no saca el bus
        // externo. [DS10693, tabla 2, fila «FMC memory controller»]
    }

    // =======================================================================
    // RELOJES, RESETS Y GATING
    // =======================================================================
    void engancha_relojes_446() {
        bind_bus_slave(fmpi2c1, s_pclk1, P_FMPI2C1);
        bind_bus_slave(spdifrx, s_pclk1, P_SPDIFRX);
        bind_bus_slave(cec,     s_pclk1, P_CEC);
        bind_bus_slave(spi4,    s_pclk2, P_SPI4);
        bind_bus_slave(sai1,    s_pclk2, P_SAI1);
        bind_bus_slave(sai1.a,  s_pclk2, P_SAI1);
        bind_bus_slave(sai1.b,  s_pclk2, P_SAI1);
        bind_bus_slave(sai2,    s_pclk2, P_SAI2);
        bind_bus_slave(sai2.a,  s_pclk2, P_SAI2);
        bind_bus_slave(sai2.b,  s_pclk2, P_SAI2);
        // El QUADSPI vive en AHB3 y va a HCLK, no a un APB.
        bind_bus_slave(qspi,    s_hclk,  P_QUADSPI);
        // El SPI4 es un SPI puro: no tiene mitad de audio ni pines de
        // sincronismo externo, igual que el SPI1 del F407.
        spi4.i2s_ext_clk(s_false); spi4.i2s_clk_hz(s_zero_hz);
        spi4.ext_ck(s_false);      spi4.ext_ws(s_false);

        // LOS RELOJES DE NÚCLEO. Esto es lo que hace que la fase 3 sirva para
        // algo: el FMPI2C1 no come de PCLK1 salvo que FMPI2C1SEL lo diga, y
        // los SAI no comen de PCLK nunca. Cambiar el selector en RCC_DCKCFGR2
        // cambia la frecuencia de SCL que este bloque genera, medida en el pin.
        fmpi2c1.ker_clk_hz(s_fmpi2c1_hz);
        sai1.a.sai_clk_hz(s_sai1_hz);  sai1.b.sai_clk_hz(s_sai1_hz);
        sai2.a.sai_clk_hz(s_sai2_hz);  sai2.b.sai_clk_hz(s_sai2_hz);
    }

    // =======================================================================
    // INTERRUPCIONES Y PETICIONES DE DMA
    // =======================================================================
    void engancha_irqs_446() {
        // Las trece posiciones de la 84 a la 96 dejan hoy de estar vacías.
        spi4.irq(s_irq[addr446::IRQ_SPI4]);
        qspi.irq(s_irq[addr446::IRQ_QUADSPI]);
        fmpi2c1.irq_ev(s_irq[addr446::IRQ_FMPI2C1_EV]);
        fmpi2c1.irq_er(s_irq[addr446::IRQ_FMPI2C1_ER]);
        // Un SAI, dos bloques, UN vector: se suman [RM0390, §10.1.3].
        sai1.a.irq(s_sai_in[0]); sai1.b.irq(s_sai_in[1]);
        or_sai1.a(s_sai_in[0]);  or_sai1.b(s_sai_in[1]);
        or_sai1.y(s_irq[addr446::IRQ_SAI1]);
        sai2.a.irq(s_sai_in[2]); sai2.b.irq(s_sai_in[3]);
        or_sai2.a(s_sai_in[2]);  or_sai2.b(s_sai_in[3]);
        or_sai2.y(s_irq[addr446::IRQ_SAI2]);
        // Las dos que quedan -93 (CEC) y 94 (SPDIF-RX)- siguen sin nadie que
        // las levante, y es coherente: sus bloques están declarados y no
        // modelados, de modo que tampoco tienen nada que pedir.

        // LAS PETICIONES DE DMA, que desde la fase 6 SÍ llegan a una celda.
        //
        // Las señales viven en `SocF4` y no aquí, y no es un capricho: el
        // multiplexado de celdas se cablea en el constructor de la clase base,
        // cuando estos periféricos todavía no existen, y un `sc_in` no se
        // reata. Así que el DMA se ató a la señal allí y el periférico se ata a
        // la misma señal aquí, que es cuando ya está construido.
        //
        // El mapa de celdas sale de la base de datos de STM32CubeMX y está
        // contrastado con las Tablas 28 y 29 de [RM0390] Rev 9, que coinciden
        // celda a celda [vs_446re, §21].
        fmpi2c1.dma_req_rx(q_fmpi2c1_rx); fmpi2c1.dma_req_tx(q_fmpi2c1_tx);
        sai1.a.dma_req(q_sai1_a);         sai1.b.dma_req(q_sai1_b);
        sai2.a.dma_req(q_sai2_a);         sai2.b.dma_req(q_sai2_b);
        spi4.dma_req_rx(q_spi4_rx);       spi4.dma_req_tx(q_spi4_tx);
        qspi.dma_req(q_qspi);
    }

    // =======================================================================
    // LOS PINES [PINDATA: los cuatro ficheros de referencia del F446 y
    //            GPIO-STM32F446_gpio_v1_0_Modes]
    //
    // La tabla de AF es DEL DIE, y el encapsulado decide cuál de sus entradas
    // llega a un pad. Por eso aquí se registran las **noventa y una** entradas
    // que el die tiene para estos siete bloques, y no las quince que saca un
    // LQFP64: un F446ZE con el SAI2 declarado y sin pines sería una mentira
    // silenciosa, de las que este proyecto lleva seis fases quitando.
    //
    // El mux ya sabe no sacar nada por un pad que el encapsulado no suelda, así
    // que registrar de más no miente; lo que mentiría es registrar de menos.
    //
    // La tabla de AF del F446 es la del F407 MÁS estas entradas, en ranuras que
    // en esos mismos pines estaban libres: se comprobó par a par y el número de
    // AF no difiere en ninguno de los 260 pares comunes [vs_446re, §9.2].
    //
    // LO QUE NO SE REGISTRA, y por qué:
    //
    //   * el **SMBA del FMPI2C1** (PD11, PF13): el modelo no tiene esa señal
    //     porque no implementa SMBus, y `limitaciones()` lo dice;
    //   * el **segundo banco del QUADSPI** (BK2_IO0..3, en PE7..PE10, PG9 y
    //     PG14): este modelo tiene UN banco. Registrar sus pines sugeriría un
    //     modo de ocho líneas que no existe aquí;
    //   * el **SPDIF-RX** y el **HDMI-CEC**: son `BloqueDeclarado`, y un bloque
    //     no modelado que moviera pads sería justo lo que esa clase existe para
    //     no contar.
    // =======================================================================
    void engancha_af_446() {
        auto od = [](sc_core::sc_signal<bool>& o, sc_core::sc_signal<bool>& e,
                     sc_core::sc_signal<bool>& i) {       // colector abierto
            return AfEndpoint{&o, &e, &i, true};
        };
        auto pp = [](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                     sc_core::sc_signal<bool>* i) {
            return AfEndpoint{o, e, i, false};
        };
        // Un pin se escribe 0xPN: puerto en el nibble alto (A = 0), número en
        // el bajo. Así la tabla cabe de un vistazo y el comentario de al lado
        // la dice en el idioma del datasheet.
        auto reg = [&](const AfEndpoint& ep, uint8_t af,
                       std::initializer_list<uint8_t> pines) {
            for (uint8_t p : pines) pinmux.connect_af(p >> 4, p & 0xF, af, ep);
        };

        reg(od(fmpi2c1.scl_out, fmpi2c1.scl_oe, fmpi2c1.scl_in),
            4, {0x26, 0x3C, 0x3E, 0x5E});   // FMPI2C1_SCL: PC6 PD12 PD14 PF14
        reg(od(fmpi2c1.sda_out, fmpi2c1.sda_oe, fmpi2c1.sda_in),
            4, {0x27, 0x3D, 0x3F, 0x5F});   // FMPI2C1_SDA: PC7 PD13 PD15 PF15
        reg(pp(&sai1.a.fs_out, &sai1.a.fs_oe, &sai1.a.fs_in),
            6, {0x03, 0x44});   // SAI1_FS_A: PA3 PE4
        reg(pp(&sai1.a.sck_out, &sai1.a.sck_oe, &sai1.a.sck_in),
            6, {0x1A, 0x45});   // SAI1_SCK_A: PB10 PE5
        reg(pp(&sai1.a.sd_out, &sai1.a.sd_oe, &sai1.a.sd_in),
            6, {0x12, 0x21, 0x36, 0x46});   // SAI1_SD_A: PB2 PC1 PD6 PE6
        reg(pp(&sai1.a.mclk_out, &sai1.a.mclk_oe, nullptr),
            6, {0x42});   // SAI1_MCLK_A: PE2
        reg(pp(&sai1.b.fs_out, &sai1.b.fs_oe, &sai1.b.fs_in),
            6, {0x19, 0x59});   // SAI1_FS_B: PB9 PF9
        reg(pp(&sai1.b.sck_out, &sai1.b.sck_oe, &sai1.b.sck_in),
            6, {0x1C, 0x58});   // SAI1_SCK_B: PB12 PF8
        reg(pp(&sai1.b.sd_out, &sai1.b.sd_oe, &sai1.b.sd_in),
            6, {0x09, 0x43, 0x56});   // SAI1_SD_B: PA9 PE3 PF6
        reg(pp(&sai1.b.mclk_out, &sai1.b.mclk_oe, nullptr),
            6, {0x20, 0x57});   // SAI1_MCLK_B: PC0 PF7
        reg(pp(&qspi.clk_out, &qspi.clk_oe, nullptr),
            9, {0x12, 0x33});   // QUADSPI_CLK: PB2 PD3
        reg(pp(&qspi.ncs_out, &qspi.ncs_oe, nullptr),
            10, {0x16, 0x66});   // QUADSPI_BK1_NCS: PB6 PG6
        reg(pp(&qspi.ncs_out, &qspi.ncs_oe, nullptr),
            9, {0x2B});   // QUADSPI_BK2_NCS: PC11
        reg(pp(&qspi.io_out[0], &qspi.io_oe[0], &qspi.io_in[0]),
            9, {0x29, 0x3B});   // QUADSPI_BK1_IO0: PC9 PD11
        reg(pp(&qspi.io_out[0], &qspi.io_oe[0], nullptr),
            10, {0x58});   // QUADSPI_BK1_IO0: PF8
        reg(pp(&qspi.io_out[1], &qspi.io_oe[1], &qspi.io_in[1]),
            9, {0x2A, 0x3C});   // QUADSPI_BK1_IO1: PC10 PD12
        reg(pp(&qspi.io_out[1], &qspi.io_oe[1], nullptr),
            10, {0x59});   // QUADSPI_BK1_IO1: PF9
        reg(pp(&qspi.io_out[2], &qspi.io_oe[2], &qspi.io_in[2]),
            9, {0x42, 0x57});   // QUADSPI_BK1_IO2: PE2 PF7
        reg(pp(&qspi.io_out[3], &qspi.io_oe[3], &qspi.io_in[3]),
            9, {0x01, 0x3D, 0x56});   // QUADSPI_BK1_IO3: PA1 PD13 PF6
        reg(pp(&spi4.sck_out, &spi4.sck_oe, &spi4.sck_in),
            5, {0x42, 0x4C});   // SPI4_SCK: PE2 PE12
        reg(pp(&spi4.sck_out, &spi4.sck_oe, nullptr),
            6, {0x6B});   // SPI4_SCK: PG11
        reg(pp(&spi4.miso_out, &spi4.miso_oe, &spi4.miso_in),
            5, {0x30, 0x45, 0x4D});   // SPI4_MISO: PD0 PE5 PE13
        reg(pp(&spi4.miso_out, &spi4.miso_oe, nullptr),
            6, {0x6C});   // SPI4_MISO: PG12
        reg(pp(&spi4.mosi_out, &spi4.mosi_oe, &spi4.mosi_in),
            5, {0x46, 0x4E});   // SPI4_MOSI: PE6 PE14
        reg(pp(&spi4.mosi_out, &spi4.mosi_oe, nullptr),
            6, {0x6D});   // SPI4_MOSI: PG13
        reg(pp(&spi4.nss_out, &spi4.nss_oe, &spi4.nss_in),
            5, {0x44, 0x4B});   // SPI4_NSS: PE4 PE11
        reg(pp(&spi4.nss_out, &spi4.nss_oe, nullptr),
            6, {0x6E});   // SPI4_NSS: PG14

        // EL SAI2, QUE NO ES CUESTIÓN DE PINES SINO DE REFERENCIA. En un F446RC
        // o RE la base de pines de ST no lista **una sola** señal de SAI2, ni
        // siquiera en los pines de PA que ese encapsulado sí saca; y la Tabla 2
        // del datasheet pone `1` en la fila del SAI para esa columna. El die es
        // el mismo y el bloque responde en el bus —por eso sigue construido y
        // decodificado—, pero no tiene dónde salir, y eso lo dice el rasgo.
        if (mcu.perif.sai2) {
            reg(pp(&sai2.a.fs_out, &sai2.a.fs_oe, &sai2.a.fs_in),
                10, {0x3C});   // SAI2_FS_A: PD12
            reg(pp(&sai2.a.sck_out, &sai2.a.sck_oe, &sai2.a.sck_in),
                10, {0x3D});   // SAI2_SCK_A: PD13
            reg(pp(&sai2.a.sck_out, &sai2.a.sck_oe, nullptr),
                8, {0x3E});   // SAI2_SCK_A: PD14
            reg(pp(&sai2.a.sd_out, &sai2.a.sd_oe, &sai2.a.sd_in),
                10, {0x3B});   // SAI2_SD_A: PD11
            reg(pp(&sai2.a.sd_out, &sai2.a.sd_oe, nullptr),
                8, {0x1B});   // SAI2_SD_A: PB11
            reg(pp(&sai2.a.mclk_out, &sai2.a.mclk_oe, nullptr),
                10, {0x40});   // SAI2_MCLK_A: PE0
            reg(pp(&sai2.b.fs_out, &sai2.b.fs_oe, &sai2.b.fs_in),
                10, {0x4D, 0x69});   // SAI2_FS_B: PE13 PG9
            reg(pp(&sai2.b.fs_out, &sai2.b.fs_oe, nullptr),
                8, {0x0C});   // SAI2_FS_B: PA12
            reg(pp(&sai2.b.sck_out, &sai2.b.sck_oe, &sai2.b.sck_in),
                10, {0x4C});   // SAI2_SCK_B: PE12
            reg(pp(&sai2.b.sck_out, &sai2.b.sck_oe, nullptr),
                8, {0x02});   // SAI2_SCK_B: PA2
            reg(pp(&sai2.b.sd_out, &sai2.b.sd_oe, &sai2.b.sd_in),
                10, {0x4B, 0x5B, 0x6A});   // SAI2_SD_B: PE11 PF11 PG10
            reg(pp(&sai2.b.mclk_out, &sai2.b.mclk_oe, nullptr),
                10, {0x01, 0x4E});   // SAI2_MCLK_B: PA1 PE14
        }

        // --- I2S1 (AF5) ----------------------------------------------------
        // Los pines de SPI1 YA están registrados por el mapa de la familia, y
        // son los mismos hilos: CK es SCK, WS es NSS y SD es MOSI. Lo único que
        // el F407 no tiene es MCK, que en el F446 sale por PC4.
        pinmux.connect_af(2, 4, 5, pp(&spi1.mck_out, &spi1.mck_oe, nullptr));
    }

public:
    // -----------------------------------------------------------------------
    // La deuda, enumerada. Cada línea es una cosa que el silicio hace y este
    // modelo todavía no, y cada una tiene su fase en el plan.
    //
    // No están aquí las diferencias que el modelo SÍ cubre —la Flash de 512 KB,
    // la ausencia de CCM, de Ethernet, de RNG y de los I2Sext, el LQFP64 propio,
    // los siete maestros de la matriz, las 97 posiciones de vector, el IDCODE—,
    // porque decir de una cosa terminada que falta es tan inexacto como callar
    // una que falta de verdad.
    // -----------------------------------------------------------------------
    std::vector<std::string> limitaciones() const override {
        // LO QUE FALTA NO ES LO MISMO EN LAS OCHO REFERENCIAS. Desde que la
        // familia entera está modelada, esta lista se construye y no se
        // devuelve entera: decirle a quien monta un F446ZE que su SAI2 no
        // tiene pines sería tan falso como callárselo en un F446RE.
        std::vector<std::string> v = {
            "los escalones de tension (PWR_CR.VOS) se leen y se escriben, pero "
            "el modelo NO limita la frecuencia por escala: en el silicio, la "
            "escala 2 y la 3 bajan el techo de SYSCLK, y aqui el unico techo "
            "que se mueve es el del over-drive",

            "RCC_CKGATENR se guarda y se devuelve, y no hace nada: son ocho "
            "bits de gating fino cuyo unico efecto observable es el consumo",

            "durante la conmutacion del over-drive (ODSWEN) el silicio PARA el "
            "reloj de sistema unos ciclos; el modelo espera el tiempo pero no "
            "lo para. Se nota solo si algo cuenta ciclos de HCLK a caballo de "
            "esa conmutacion",

            "el SPDIF-RX (0x4000_4000) y el HDMI-CEC (0x4000_6C00) estan "
            "DECLARADOS y no modelados: ocupan su ventana, sus registros leen "
            "cero, escribir en ellos no hace nada, y el primer acceso a cada "
            "uno saca un aviso por el informe de SystemC. No tienen pines ni "
            "levantan sus vectores (93 y 94)",

            "el FMC no esta modelado como tal: el modelo lleva el FSMC del "
            "F407, que tiene otros desplazamientos de registro por banco. En "
            "las referencias que no sacan el bus externo -M y R- eso no se "
            "nota, porque 0xA000_0000 se queda sin decodificar y tocarlo da "
            "error de bus, que es lo correcto",

            "del segundo banco del QUADSPI (BK2_IO0..3) no se modela nada: "
            "este modelo tiene UN banco, asi que el modo de doble Flash -ocho "
            "lineas- no existe aqui y sus pines no estan en el mux",

            "de los dos SAI se modela la temporizacion de trama (MCLK, SCK, "
            "FS y el hueco de cada ranura) y la FIFO, pero NO el companding "
            "(mu-law/A-law), ni la deteccion de silencio con MUTE, ni el modo "
            "AC'97, ni la sincronizacion entre los dos SAI por SYNCIN/SYNCOUT: "
            "GCR se guarda y se devuelve, y no encamina nada",

            "el FMPI2C1 hace de maestro y de esclavo a nivel de bit sobre los "
            "pines, con su TIMINGR de verdad, pero NO calcula el PEC (PECR "
            "lee cero) ni implementa los temporizadores de SMBus (TIMEOUTR se "
            "guarda y no vence), que son justo la parte que este IP anade "
            "sobre el I2C clasico",

            "las dos celdas de DMA del SPDIF-RX -DMA1 stream 1 canal 0 y "
            "stream 6 canal 0- estan en la tabla del chip y sin fuente en "
            "este modelo, porque su bloque esta declarado y no modelado. "
            "Armar un stream ahi saca un aviso que dice que celda es, en vez "
            "de quedarse esperando en silencio"
        };

        // --- Y lo que depende de QUÉ referencia sea esta ---------------------
        if (!mcu.perif.sai2)
            v.push_back(
                "esta referencia lleva UN SOLO SAI: el SAI2 responde en el bus "
                "-el die es el mismo- pero no tiene ni una senal en ningun pin, "
                "y el datasheet cuenta 1 en la fila del SAI para el LQFP64. Un "
                "firmware que lo configure no vera nada en ningun pad "
                "[DS10693 Rev 11, tabla 2; PINDATA]");
        if (!mcu.enc.hay_puerto(4))          // sin puerto E
            v.push_back(
                "el SPI4 esta completo y sin un solo pin en este encapsulado: "
                "se puede programar y no se ve nada. El die lo tiene y este "
                "plastico no se lo saca [PINDATA]");
        // El IO2 del primer banco sale por PE2 o por PF7, y por ningun otro
        // sitio. Si el encapsulado no suelda ninguno de los dos, no hay cuatro
        // lineas que valgan.
        if (!mcu.enc.bonded(4, 2) && !mcu.enc.bonded(5, 7))
            v.push_back(
                "el QUADSPI no tiene pin para IO2 en este encapsulado -es la "
                "nota 3 del datasheet, 'available with limited features'-, de "
                "modo que un comando en cuatro lineas escribe al aire por esa "
                "linea. El modelo lo deja pasar sin avisar: es el silicio el "
                "que no puede");
        if (mcu.perif.fsmc && !mcu.enc.hay_puerto(6))   // con FMC y sin puerto G
            v.push_back(
                "del bus externo, este encapsulado solo saca el BANCO 1 del "
                "FMC, con NE1 y en modo multiplexado, y sin linea de "
                "interrupcion porque el puerto G no sale [DS10693 Rev 11, "
                "tabla 2, nota 1]. El modelo lleva ademas el FSMC del F407 y "
                "no el FMC, que tiene otros desplazamientos de registro");
        return v;
    }
};

// La segunda familia. A partir de aqui `tipo=` en el XML DESPACHA de verdad:
// `STM32F407VG` y `STM32F446RE` no son el mismo objeto de C++.
REGISTRA_MCU(STM32F446, [](const char* nm, DebugCaps d, const Cableado& c,
                           const McuCaps& caps) -> mcu_if* {
    return new AdaptadorF4<Stm32F446>(nm, d, c, caps);
});

} // namespace stm32
#endif // STM32_SOC_STM32F446_H
