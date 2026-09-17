// =============================================================================
// sc_main_f446.cpp — LA SUITE DEL STM32F446, que empieza aquí
//
// Un banco aparte, y no unas pruebas más dentro de `top/sc_main.cpp`. La razón
// es de las que conviene dejar escritas, porque se descubrió intentando lo
// contrario: **construir un F446 dentro del banco del F407 movió el invariante
// del F407**. Un segundo chip no es inerte —sus relojes internos oscilan, su
// núcleo arranca— y el banco del F407 tiene alguna prueba cuyo resultado
// depende de lo rápido que vaya el anfitrión. Dos chips en la misma simulación
// son una placa de dos chips; para hacerle preguntas a uno, lo que hace falta
// es una simulación suya.
//
// Así que el criterio de la fase 5 del plan —«la suite del F407 no se toca y no
// puede moverse: 2336217899213 ps; una suite paralela para el F446»— empieza a
// cumplirse ya, en la fase 2, por necesidad y no por disciplina.
//
// Lo que hay aquí son dos cosas:
//
//   A. LA PRUEBA CRUZADA que el plan pide en §12, fase 5: que el puerto no se
//      coma al original. Se hace sobre el netlist ya montado, sin simular —qué
//      decodifica cada segmento, qué dice la matriz, qué IDCODE lleva— porque
//      preguntar no cuesta ni un evento.
//
//   B. EL HITO H3: un blinky compilado para F446RE con la cabecera de ST, que
//      arranca, programa su reloj y parpadea. Es el criterio de salida de la
//      fase 2, y es el que no se puede falsear: si una sola dirección del
//      modelo no fuera la del F446, este firmware no funcionaría.
//
//   make test446   (o: make -C . tb446 && ./build/tb446)
// =============================================================================
#include <systemc>
#include <cstdio>
#include <string>
#include "../common/asan_opciones.h"
#include "../soc/stm32f446.h"
#include "../verif/image_loader.h"

using namespace sc_core;
using namespace stm32;

// --- Contabilidad, igual de simple que en el banco del F407 -----------------
static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s ---\n", t); }
static bool check(bool c, const char* q) {
    if (c) { ++g_ok;  std::printf("  [OK  ] %s\n", q); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q); }
    return c;
}
static bool check_eq(uint64_t got, uint64_t exp, const char* q) {
    const bool c = (got == exp);
    if (c) std::printf("  [OK  ] %s\n", q);
    else   std::printf("  [FALLO] %s (obtenido 0x%llX, esperado 0x%llX)\n", q,
                       (unsigned long long)got, (unsigned long long)exp);
    if (c) ++g_ok; else ++g_fallos;
    return c;
}

SC_MODULE(Tb446) {
    Stm32F446 dut{"dut"};
    // Un F407VG al lado, para que la comparación sea contra el modelo y no
    // contra unos números escritos a mano. NO se construye: se consulta su
    // DESCRIPTOR, que es todo lo que hace falta para la mitad de las pruebas.
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1;

    SC_CTOR(Tb446) {
        d_vdd  = dut.pwr_pads.vdd.register_driver("tb_vdd");
        d_vdda = dut.pwr_pads.vdda.register_driver("tb_vdda");
        d_nrst = dut.pwr_pads.nrst.register_driver("tb_nrst");
        d_bt0  = dut.pwr_pads.boot0.register_driver("tb_boot0");
        SC_THREAD(run);
    }

    // -----------------------------------------------------------------------
    // A. LA PRUEBA CRUZADA: en qué se diferencian de verdad las dos piezas
    // -----------------------------------------------------------------------
    void cruzada() {
        grupo("A1 Lo que el F446 NO lleva: ventanas que no decodifica nadie");

        // El RNG. No existe en el F446 -ni un simbolo RNG en stm32f446xx.h- y
        // por tanto su ventana en AHB2 no la decodifica nadie: tocarla da
        // error de bus, que es lo que hace el silicio con espacio reservado.
        check(!dut.ahb2_dec.decodes(addr::RNG_B),
              "0x5006_0800 (RNG): en el F446 no hay quien conteste");
        check(!dut.ahb1_dec.decodes(addr::ETH_B),
              "0x4002_8000 (Ethernet MAC): tampoco");

        // LA TRAMPA DE DIRECCIONES, que es la que este documento marcó como el
        // error mas caro del puerto: 0x4000_4000 es el I2S3ext en el F407 y el
        // SPDIF-RX en el F446. Hoy, en el F446, no hay ninguno de los dos, y
        // eso es lo unico honesto mientras el segundo no este modelado: un
        // I2S3ext contestando ahi seria un modelo que funciona y miente.
        check(!dut.apb1_dec.decodes(0x40004000u),
              "0x4000_4000: en el F446 NO contesta el I2S3ext (ahi va el "
              "SPDIF-RX, que aun no esta: fase 4)");
        check(!dut.apb1_dec.decodes(addr::I2S2EXT_B),
              "0x4000_3400 (I2S2ext): reservado en el F446");

        // Y lo que si lleva, para que la prueba no sea solo de ausencias.
        check(dut.apb1_dec.decodes(addr::I2C1_B) &&
              dut.apb1_dec.decodes(addr::USART2_B) &&
              dut.apb2_dec.decodes(addr::SPI1_B) &&
              dut.ahb1_dec.decodes(addr::GPIOA_B),
              "lo que si comparten las dos piezas sigue en su sitio: I2C1, "
              "USART2, SPI1 y los GPIO");
        check(dut.ahb2_dec.decodes(addr::DCMI_B),
              "y la camara SI esta en el F446, aunque el F405 no la lleve");

        grupo("A2 La CCM: el -2 que no se puede confundir con el -1");

        // El aviso de vs_446re §4.2, comprobado. En el F407 la ventana
        // 0x1000_0000 devuelve -2 -«la CCM, que no cuelga de la matriz»-; en
        // el F446 NO existe, y devolver -2 ahi seria decir que hay una memoria
        // a la que este maestro no llega. Son dos cosas distintas.
        check_eq(uint64_t(int64_t(decodifica_mapa(RAM_STM32F407VG, true,
                                                  addr::CCM_BASE))),
                 uint64_t(int64_t(-2)),
                 "en el F407, 0x1000_0000 es la CCM (codigo -2)");
        check(decodifica_mapa(RAM_STM32F446, false, addr::CCM_BASE) != -2,
              "y en el F446 NO es la CCM: el -2 no se cuela donde no hay "
              "memoria que alcanzar");
        check(dut.matrix.decode_addr(addr::CCM_BASE) != -2,
              "la matriz montada del F446 dice lo mismo");
        check_eq(uint64_t(int64_t(dut.matrix.decode_addr(addr::FSMC_MEM))),
                 uint64_t(int64_t(-1)),
                 "0x6000_0000: sin bus externo en el LQFP64, espacio reservado");
        check(dut.matrix.decode_addr(addr::SRAM1_BASE) ==
              int(BusSlaveId::SRAM1) &&
              dut.matrix.decode_addr(addr::SRAM2_BASE) ==
              int(BusSlaveId::SRAM2),
              "y las dos SRAM siguen donde estaban: 112 + 16 KB");

        grupo("A3 La matriz: siete maestros, no ocho");

        check_eq(CONN_STM32F407VG.n_maestros(), 8u,
                 "el F407 tiene ocho maestros [RM0090 2.1]");
        check_eq(CONN_STM32F446.n_maestros(), 7u,
                 "el F446 tiene siete [RM0390 2.1]");
        check(!CONN_STM32F446.hay_maestro(BusMaster::ETH_DMA) &&
              CONN_STM32F407VG.hay_maestro(BusMaster::ETH_DMA),
              "y el que falta es exactamente el DMA del Ethernet: una fila a "
              "cero, sin tocar el enum ni renumerar nada");
        check(CONN_STM32F446.hay_maestro(BusMaster::OTG_HS_DMA) &&
              CONN_STM32F446.puede(BusMaster::DMA2_MEM, BusSlaveId::FLASH_DCODE),
              "los otros siete alcanzan lo mismo que alcanzaban");

        grupo("A4 Los pines: dos LQFP64 que no son el mismo");

        check(ENC_LQFP64.coherente() && ENC_LQFP64_F446.coherente(),
              "los dos cuadran con el recuento de su datasheet");
        check_eq(ENC_LQFP64.cuenta_gpio(),      51u, "el del F405RG saca 51 E/S");
        check_eq(ENC_LQFP64_F446.cuenta_gpio(), 50u, "el del F446RE saca 50");
        check(ENC_LQFP64.bonded(1, 11) && !ENC_LQFP64_F446.bonded(1, 11),
              "y la diferencia es PB11: sale en uno y no en el otro. Por eso "
              "ULPI_D4 esta en PB11 en el F407 y en PB2 en el F446RE");
        check(ENC_LQFP64_F446.bonded(0, 5) && ENC_LQFP64_F446.bonded(2, 13),
              "PA5 (el LED de la Nucleo) y PC13 (el pulsador) si salen");
        check(!ENC_LQFP64_F446.bonded(3, 12) && ENC_LQFP64_F446.bonded(3, 2),
              "del puerto D sale UN pin, PD2: la placa Discovery no cabe aqui");
        check(!ENC_LQFP64_F446.hay_puerto(8),
              "y no hay puerto I en ningun encapsulado del F446");

        grupo("A5 El nucleo, la memoria y quien dice ser");

        check_eq(dut.mcu.nucleo.n_irq, 97u,
                 "97 posiciones de vector (0..96), once de ellas reservadas");
        check_eq(CORE_STM32F407VG.n_irq, 82u, "frente a las 82 del F407");
        check_eq(dut.core.scs.caps.prio_bits, 4u,
                 "los mismos 4 bits de prioridad: es el mismo Cortex-M4F");
        check_eq(dut.mcu.memoria.flash.size, 512u * 1024u, "512 KB de Flash");
        check_eq(dut.mcu.memoria.flash.n_sectores, 8u, "en ocho sectores");
        check_eq(dut.mcu.memoria.ram.ccm_size, 0u, "y sin CCM");
        check_eq(dut.mcu.memoria.ram.total(),
                 MCU_STM32F407VG.memoria.ram.total() - 64u * 1024u,
                 "que son exactamente los 64 KB que le faltan al F446");

        // El IDCODE, que era el punto bloqueante del plan: con el valor
        // equivocado STM32CubeIDE no da un error claro, da un «Could not
        // verify ST device».
        check_eq(dut.core.debug.idcode(), 0x10000421u,
                 "DBGMCU_IDCODE = 0x1000 0421: DEV_ID 0x421, REV_ID 0x1000 "
                 "[RM0390 33.6.1]");
        check_eq(IDCODE_STM32F40X, 0x10016413u,
                 "y el del F405/407 sigue siendo 0x1001 6413 [RM0090 32.6.1]");

        grokupo_vectores();
    }

    // La tabla de vectores: donde divergen las dos piezas. Se comprueba sobre
    // los descriptores porque lo que decide el tamano del NVIC es el numero de
    // posiciones, y lo que decide quien las levanta son los perifericos.
    void grokupo_vectores() {
        grupo("A6 La tabla de vectores: ninguna posicion cambia de dueno");
        check(CORE_STM32F446.n_irq > CORE_STM32F407VG.n_irq,
              "el enum de IRQ se EXTIENDE, no se reescribe: las altas del F446 "
              "van todas por encima de la 81, donde el F407 no llega");
        check_eq(CORE_STM32F446.n_excepciones(), 16u + 97u,
                 "113 excepciones en total: 16 de sistema y 97 posiciones");
        // La 80 es el RNG en el F407 y esta reservada en el F446. Lo que el
        // modelo puede comprobar hoy es la causa: el periferico no esta.
        check(MCU_STM32F407VG.perif.rng && !MCU_STM32F446RE.perif.rng,
              "la posicion 80 es del RNG, que el F446 no lleva: reservada");
        check(MCU_STM32F407VG.perif.eth && !MCU_STM32F446RE.perif.eth,
              "y las 61 y 62 son del Ethernet, que tampoco: reservadas");
    }

    // -----------------------------------------------------------------------
    // B. EL HITO H3: el blinky de la Nucleo-F446RE
    // -----------------------------------------------------------------------
    void enciende() {
        dut.pwr_pads.vdd.set_drive(d_vdd,   0.0f, 1.0f);
        dut.pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        dut.pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);
        dut.pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(10, SC_US);
        dut.pwr_pads.vdd.set_drive(d_vdd,   3.3f, 0.1f);
        dut.pwr_pads.vdda.set_drive(d_vdda, 3.3f, 0.1f);
        wait(100, SC_US);
        dut.pwr_pads.nrst.set_hiz(d_nrst);
    }

    void blinky() {
        grupo("B1 El blinky de la NUCLEO-F446RE, compilado con la cabecera de ST");

        ImageLoader ld(dut);
        const long n = ld.load_file("verif/fw/blinky446/blinky446.bin",
                                    dut.mcu.memoria.flash.base);
        if (!check(n > 0, "el firmware se carga en la Flash")) return;

        dut.rcc.set_internal_waveforms(false);
        enciende();
        wait(900, SC_MS);

        // El buzon vive al principio de la SRAM1 (véase verif/fw/stm32f446.ld)
        const uint32_t done    = dut.sram1.peek32(0);
        const uint32_t sysclk  = dut.sram1.peek32(4);
        const uint32_t toggles = dut.sram1.peek32(8);
        const uint32_t boton   = dut.sram1.peek32(12);
        const uint32_t ticks   = dut.sram1.peek32(16);
        std::printf("    buzon: done=%u sysclk=%u toggles=%u boton=%u ticks=%u\n",
                    done, sysclk, toggles, boton, ticks);
        std::printf("    modelo: hclk=%.0f Hz pclk1=%.0f pclk2=%.0f\n",
                    dut.s_hclk_hz.read(), dut.s_pclk1_hz.read(),
                    dut.s_pclk2_hz.read());

        check_eq(sysclk, 84000000u,
                 "el firmware mide su propio SYSCLK con SystemCoreClockUpdate() "
                 "y le salen 84 MHz: el arbol de reloj del modelo y el que "
                 "describe la cabecera de ST dicen lo mismo");
        check_eq(toggles, 6u, "las seis conmutaciones del LED");
        // El SysTick no para cuando el firmware termina: sigue contando
        // mientras el banco espera. Lo que se comprueba es que haya dado al
        // menos los 600 tics de las seis esperas y ni uno mas que milisegundos
        // simulados lleva encendido el chip.
        check(ticks >= 600u && ticks <= 900u,
              "y el SysTick ha dado un tic por milisegundo: 600 para las seis "
              "esperas de 100 ms, y los demas mientras el banco miraba");
        check_eq(done, 1u, "el firmware llega al final");
        check_eq(boton, 1u,
                 "PC13 se lee ALTO: el pull-up interno lo sujeta arriba porque "
                 "el pulsador de la Nucleo esta suelto");

        const float v_pa5 = dut.pinmux.analog(0, 5).voltage();
        check(v_pa5 > 3.0f,
              "y PA5 se queda arriba, que es donde el firmware deja LD2");

        // Ninguna linea de interrupcion colgada. Importa especialmente en un
        // chip al que se le han quitado bloques: un periferico inalcanzable
        // cuya linea se quedara alta seria una interrupcion que el firmware no
        // puede quitar de ninguna manera, porque no puede ni escribirle.
        unsigned altas = 0;
        for (unsigned i = 0; i < dut.mcu.nucleo.n_irq; ++i)
            if (dut.s_irq[i].read()) ++altas;
        check_eq(altas, 0u,
                 "y ninguna de las 97 lineas de IRQ se queda alta: los bloques "
                 "que este chip no lleva tampoco interrumpen");
    }

    void run() {
        cruzada();
        blinky();
        std::printf("\n=====================================================\n");
        std::printf("TOTAL F446 : %u comprobaciones OK, %u fallos\n",
                    g_ok, g_fallos);
        std::printf("=====================================================\n");
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    Tb446 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    return g_fallos ? 1 : 0;
}
