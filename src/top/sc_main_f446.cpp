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
#include <cmath>
#include <string>
#include "../common/asan_opciones.h"
#include "../soc/stm32f446.h"
#include "../verif/image_loader.h"
#include "../verif/bus_test_master.h"

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
// Para frecuencias: comparar dobles con `==` es pedir un fallo que no existe.
static bool check_near(double got, double exp, double tol, const char* q) {
    const bool c = (exp == 0.0) ? (got == 0.0)
                                : (std::fabs(got - exp) <= tol * std::fabs(exp));
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q); }
    else   { ++g_fallos;
             std::printf("  [FALLO] %s (obtenido %.6g, esperado %.6g)\n",
                         q, got, exp); }
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
    // Un maestro de bus para escribir registros sin firmware. Se engancha al
    // puerto de verificación de la matriz, igual que en el banco del F407.
    BusTestMaster tm{"tm"};
    // Un F407VG al lado, para que la comparación sea contra el modelo y no
    // contra unos números escritos a mano. NO se construye: se consulta su
    // DESCRIPTOR, que es todo lo que hace falta para la mitad de las pruebas.
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1;

    SC_CTOR(Tb446) {
        tm.isk.bind(dut.matrix.from_tb);
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
        // SPDIF-RX en el F446. Desde la fase 4 ahi contesta el SPDIF-RX -un
        // bloque DECLARADO y no modelado-, y lo que hay que comprobar es que
        // quien contesta es EL SUYO: si el que respondiera fuese el I2S3ext,
        // el firmware configuraria un periferico de audio creyendo hablar con
        // otro y el modelo no diria ni pio.
        check(dut.apb1_dec.decodes(0x40004000u),
              "0x4000_4000: en el F446 SI hay alguien, porque ahi vive el "
              "SPDIF-RX");
        check_eq(uint64_t(dut.spdifrx.base()), uint64_t(0x40004000u),
                 "y ese alguien es el SPDIF-RX, no el I2S3ext del F407");
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
    void apaga() {
        dut.pwr_pads.vdd.set_drive(d_vdd,   0.0f, 1.0f);
        dut.pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        dut.pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
    }

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

    // -----------------------------------------------------------------------
    // C. EL ARBOL DE RELOJ DEL F446 (fase 3)
    //
    // El criterio del plan, literal: «cada selector tiene que cambiar una
    // frecuencia observable, no solo guardar un bit». Asi que ninguna de estas
    // comprobaciones lee el registro que acaba de escribir: todas preguntan al
    // arbol POR UNA FRECUENCIA.
    // -----------------------------------------------------------------------
    uint32_t rd(uint32_t a) { uint32_t v = 0; tm.read32(a, v); return v; }
    void     wr(uint32_t a, uint32_t v) { tm.write32(a, v); }
    void     rcc_w(uint32_t off, uint32_t v) { wr(addr::RCC_B + off, v); }
    uint32_t rcc_r(uint32_t off) { return rd(addr::RCC_B + off); }

    // Arranca el HSI y engancha el PLL a 180 MHz pasando por el over-drive,
    // pero DESDE EL BANCO, sin firmware: es la misma secuencia que hara el
    // hito H4 con un programa de verdad, y sirve para tener un arbol vivo
    // sobre el que probar los selectores.
    void arranca_180() {
        // PWR: escala 1 y over-drive
        rcc_w(Rcc::R_APB1ENR, 1u << 28);                  // PWREN
        wr(addr::PWR_B + Pwr::R_CR, 0x0000C000u | Pwr::CR_ODEN);
        for (int i = 0; i < 100; ++i) {
            if (rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODRDY) break;
            wait(10, SC_US);
        }
        wr(addr::PWR_B + Pwr::R_CR,
           0x0000C000u | Pwr::CR_ODEN | Pwr::CR_ODSWEN);
        for (int i = 0; i < 100; ++i) {
            if (rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODSWRDY) break;
            wait(10, SC_US);
        }
        // PLL: 16/16 = 1 MHz, x360 = 360 MHz, P=2 -> 180, Q=8, R=2
        rcc_w(Rcc::R_PLLCFGR, 16u | (360u << 6) | (0u << 16) | (8u << 24) |
                              (2u << 28));
        rcc_w(Rcc::R_CR, rcc_r(Rcc::R_CR) | (1u << 24));  // PLLON
        for (int i = 0; i < 100; ++i) {
            if (rcc_r(Rcc::R_CR) & (1u << 25)) break;
            wait(10, SC_US);
        }
        // AHB /1, APB1 /4, APB2 /2, y SYSCLK = PLL
        rcc_w(Rcc::R_CFGR, (5u << 10) | (4u << 13) | 2u);
        wait(10, SC_US);
    }

    void arbol_f446() {
        grupo("C1 El over-drive: la secuencia de RM0390 5.1.3");

        // EL NUCLEO, APARCADO. Estas pruebas escriben los registros del RCC
        // desde el bus, y el blinky de la prueba anterior sigue vivo dentro:
        // dos amos para el mismo arbol de reloj no es una prueba, es una
        // carrera. Se le carga un `wfe` en bucle -lo mismo que hace `sim`
        // cuando no hay firmware- y el chip se queda quieto mirando.
        apaga();
        wait(50, SC_US);
        {
            ImageLoader ld(dut);
            const MapaRam& r = dut.mcu.memoria.ram;
            ld.write_reset_vector(r.sram1_base + r.sram1_size,
                                  dut.mcu.memoria.flash.base + 0x100u);
            ld.poke32(dut.mcu.memoria.flash.base + 0x100u, 0xE7FDBF20u);
        }
        enciende();
        wait(200, SC_US);

        rcc_w(Rcc::R_APB1ENR, 1u << 28);                  // PWREN
        check_eq(rd(addr::PWR_B + Pwr::R_CSR) & (Pwr::CSR_ODRDY |
                                                 Pwr::CSR_ODSWRDY), 0u,
                 "tras el reset no hay over-drive: ODRDY y ODSWRDY a cero");
        check(!dut.s_over_drive.read(),
              "y el RCC lo ve apagado, asi que rigen los topes de siempre");

        // ODEN, y la bandera NO sube en el mismo ciclo: si subiera, un firmware
        // que no la esperase pasaria aqui y fallaria en la placa.
        wr(addr::PWR_B + Pwr::R_CR, 0x0000C000u | Pwr::CR_ODEN);
        check_eq(rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODRDY, 0u,
                 "ODRDY no sube en el mismo ciclo en que se escribe ODEN");
        wait(200, SC_US);
        check_eq(rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODRDY, Pwr::CSR_ODRDY,
                 "pero sube, y por eso el firmware que la espera avanza");
        check_eq(rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODSWRDY, 0u,
                 "ODSWRDY NO sube sola: hace falta ODSWEN, que es el paso 4");

        wr(addr::PWR_B + Pwr::R_CR,
           0x0000C000u | Pwr::CR_ODEN | Pwr::CR_ODSWEN);
        wait(100, SC_US);
        check_eq(rd(addr::PWR_B + Pwr::R_CSR) & Pwr::CSR_ODSWRDY,
                 Pwr::CSR_ODSWRDY, "con ODSWEN sube ODSWRDY");
        check(dut.s_over_drive.read(),
              "y AHI es cuando el RCC sube los topes a 180/45/90");

        // Y se puede deshacer, que es lo que pasa al volver de Stop.
        wr(addr::PWR_B + Pwr::R_CR, 0x0000C000u);
        wait(10, SC_US);
        check(!dut.s_over_drive.read() &&
              !(rd(addr::PWR_B + Pwr::R_CSR) & (Pwr::CSR_ODRDY |
                                                Pwr::CSR_ODSWRDY)),
              "quitar ODEN se lleva por delante las dos banderas");

        grupo("C2 180 MHz, que sin over-drive no se pueden pedir");

        arranca_180();
        check_near(dut.rcc.sysclk_hz(), 180e6, 0.001,
                   "SYSCLK = 180 MHz: M=16, N=360, P=2 desde el HSI");
        check_near(dut.rcc.hclk_freq(), 180e6, 0.001, "HCLK = 180 MHz");
        check_near(dut.rcc.pclk1_freq(), 45e6, 0.001, "PCLK1 = 45 MHz (/4)");
        check_near(dut.rcc.pclk2_freq(), 90e6, 0.001, "PCLK2 = 90 MHz (/2)");
        check(dut.s_over_drive.read(),
              "y todo eso con el over-drive puesto, que es lo que lo permite");

        grupo("C3 El tercer PLL y el divisor R, que el F407 no tiene");

        // PLL_R con el arbol ya en marcha: 360 MHz / 2 = 180 MHz.
        check_near(dut.rcc.pll_r_freq(), 180e6, 0.001,
                   "PLL_R = VCO/2 = 180 MHz. En el F407 este divisor no existe");
        // PLLSAI: M=16, N=192, P=4, Q=4 -> VCO 192 MHz, P 48 MHz, Q 48 MHz
        rcc_w(Rcc::R_PLLSAICFGR, 16u | (192u << 6) | (1u << 16) | (4u << 24));
        rcc_w(Rcc::R_CR, rcc_r(Rcc::R_CR) | (1u << 28));   // PLLSAION
        for (int i = 0; i < 100; ++i) {
            if (rcc_r(Rcc::R_CR) & (1u << 29)) break;
            wait(10, SC_US);
        }
        check(rcc_r(Rcc::R_CR) & (1u << 29), "PLLSAIRDY sube: el tercer PLL engancha");
        check_near(dut.rcc.pllsai_p_freq(), 48e6, 0.001,
                   "PLLSAI_P = 192/4 = 48 MHz");
        check_near(dut.rcc.pllsai_q_freq(), 48e6, 0.001,
                   "PLLSAI_Q = 192/4 = 48 MHz");
        check(rcc_r(Rcc::R_CIR) & (1u << 6),
              "y levanta PLLSAIRDYF, que es el bit 6 de RCC_CIR [CMSIS]");

        // El PLLI2S con M propia: 16/8 = 2 MHz, x100 = 200 MHz, R=5 -> 40 MHz
        rcc_w(Rcc::R_PLLI2SCFGR, 8u | (100u << 6) | (1u << 16) | (4u << 24) |
                                 (5u << 28));
        rcc_w(Rcc::R_CR, rcc_r(Rcc::R_CR) | (1u << 26));   // PLLI2SON
        for (int i = 0; i < 100; ++i) {
            if (rcc_r(Rcc::R_CR) & (1u << 27)) break;
            wait(10, SC_US);
        }
        check_near(dut.rcc.plli2s_r_freq(), 40e6, 0.001,
                   "PLLI2S_R = 200/5 = 40 MHz, con M PROPIA (8, no la 16 del "
                   "PLL principal): en el F407 las dos VCO van atadas");
        check_near(dut.rcc.plli2s_q_freq(), 50e6, 0.001,
                   "y PLLI2S_Q = 200/4 = 50 MHz, que en el F407 no se programa");

        grupo("C4 Cada selector cambia una frecuencia, no guarda un bit");

        // CK48MSEL: PLL_Q (360/8 = 45) o PLLSAI_P (48)
        rcc_w(Rcc::R_DCKCFGR2, 0u);
        check_near(dut.rcc.ck48m_freq(), 45e6, 0.001,
                   "CK48MSEL = 0: los 48 MHz vienen de PLL_Q (45 MHz de "
                   "verdad, que es lo que este arbol da: el modelo no redondea "
                   "a 48 para que cuadre)");
        rcc_w(Rcc::R_DCKCFGR2, 1u << 27);
        check_near(dut.rcc.ck48m_freq(), 48e6, 0.001,
                   "CK48MSEL = 1: ahora vienen del PLLSAI_P, y son 48 de verdad");

        // SDIOSEL: los 48 MHz o SYSCLK
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (1u << 28));
        check_near(dut.rcc.sdio_freq(), 180e6, 0.001,
                   "SDIOSEL = 1: el SDIO pasa a comer de SYSCLK (180 MHz)");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27));
        check_near(dut.rcc.sdio_freq(), 48e6, 0.001,
                   "SDIOSEL = 0: vuelve a los 48");

        // SPDIFRXSEL: PLL_R (180) o PLLI2S_P (200/4 = 50)
        check_near(dut.rcc.spdifrx_freq(), 180e6, 0.001,
                   "SPDIFRXSEL = 0: PLL_R");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (1u << 29));
        check_near(dut.rcc.spdifrx_freq(), 50e6, 0.001,
                   "SPDIFRXSEL = 1: PLLI2S_P");

        // CECSEL: HSI/488 o LSE
        check_near(dut.rcc.cec_freq(), 16e6 / 488.0, 0.001,
                   "CECSEL = 0: HSI/488 = 32,786 kHz, casi los 32,768 del LSE");
        // FMPI2C1SEL: PCLK1 (45), SYSCLK (180) o HSI (16)
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (1u << 22));
        check_near(dut.rcc.fmpi2c1_freq(), 180e6, 0.001,
                   "FMPI2C1SEL = 01: SYSCLK");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (2u << 22));
        check_near(dut.rcc.fmpi2c1_freq(), 16e6, 0.001,
                   "FMPI2C1SEL = 10: HSI");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27));
        check_near(dut.rcc.fmpi2c1_freq(), 45e6, 0.001,
                   "FMPI2C1SEL = 00: PCLK1");

        // SAI1SRC y los divisores DIVQ
        rcc_w(Rcc::R_DCKCFGR, 0u);
        check_near(dut.rcc.sai1_freq(), 48e6, 0.001,
                   "SAI1SRC = 00: PLLSAI_Q sin dividir (DIVQ = 0 divide por 1)");
        rcc_w(Rcc::R_DCKCFGR, 3u << 8);          // PLLSAIDIVQ = 3 -> /4
        check_near(dut.rcc.sai1_freq(), 12e6, 0.001,
                   "PLLSAIDIVQ = 3 divide por CUATRO: el campo vale N-1");
        rcc_w(Rcc::R_DCKCFGR, (1u << 20) | 4u);  // SAI1SRC = PLLI2S, DIVQ=4 -> /5
        check_near(dut.rcc.sai1_freq(), 10e6, 0.001,
                   "SAI1SRC = 01: PLLI2S_Q (50 MHz) entre PLLI2SDIVQ+1 = 5");
        rcc_w(Rcc::R_DCKCFGR, (2u << 20));
        check_near(dut.rcc.sai1_freq(), 180e6, 0.001, "SAI1SRC = 10: PLL_R");
        rcc_w(Rcc::R_DCKCFGR, (3u << 20));
        check_eq(uint64_t(dut.rcc.sai1_freq()), 0u,
                 "SAI1SRC = 11: el pin I2S_CKIN, que aqui no tiene nada "
                 "conectado. Cero es la respuesta correcta, no un fallo");

        // SAI2SRC = 11 NO es el pin: es la fuente del PLL (aqui, el HSI)
        rcc_w(Rcc::R_DCKCFGR, (3u << 22));
        check_near(dut.rcc.sai2_freq(), 16e6, 0.001,
                   "SAI2SRC = 11 NO es el pin sino la fuente del PLL: el SAI2 "
                   "no tiene entrada de reloj externa");

        // I2S1SRC e I2S2SRC: uno por dominio de APB, que es la novedad
        rcc_w(Rcc::R_DCKCFGR, 0u);
        check_near(dut.rcc.i2s1_freq(), 40e6, 0.001, "I2S1SRC = 00: PLLI2S_R");
        rcc_w(Rcc::R_DCKCFGR, (2u << 25));
        check_near(dut.rcc.i2s1_freq(), 180e6, 0.001, "I2S1SRC = 10: PLL_R");
        check_near(dut.rcc.i2s2_freq(), 40e6, 0.001,
                   "y el I2S del APB2 sigue en PLLI2S_R: son DOS selectores "
                   "independientes, y el F407 solo tiene uno global");

        grupo("C5 TIMPRE: el unico selector que se nota en un periferico que ya existe");

        rcc_w(Rcc::R_DCKCFGR, 0u);
        wait(1, SC_US);
        check_near(dut.s_timclk1_hz.read(), 90e6, 0.001,
                   "TIMPRE = 0 con APB1 /4: los temporizadores van a 2*PCLK1 = "
                   "90 MHz");
        rcc_w(Rcc::R_DCKCFGR, 1u << 24);
        wait(1, SC_US);
        check_near(dut.s_timclk1_hz.read(), 180e6, 0.001,
                   "TIMPRE = 1: con PPRE1 dividiendo por 4 o menos, pasan a "
                   "HCLK. El doble, y se ve en el reloj que ya usan los TIM");
        check_near(dut.s_timclk2_hz.read(), 180e6, 0.001,
                   "lo mismo en el APB2, que divide por 2");
        rcc_w(Rcc::R_DCKCFGR, 0u);
        wait(1, SC_US);

        grupo("C6 Y en el F407 nada de esto existe");

        // La misma direccion, en el otro chip: reservada.
        check(!MCU_STM32F407VG.arbol.dckcfgr && !MCU_STM32F407VG.arbol.pllsai &&
              !MCU_STM32F407VG.arbol.pll_r && !MCU_STM32F407VG.arbol.over_drive,
              "el descriptor del F407 declara que no tiene ninguno de los "
              "cuatro rasgos");
        check(MCU_STM32F446RE.arbol.dckcfgr && MCU_STM32F446RE.arbol.pllsai &&
              MCU_STM32F446RE.arbol.pll_r && MCU_STM32F446RE.arbol.over_drive,
              "y el del F446 declara que tiene los cuatro");
        check(!RELOJ_STM32F407VG.hay_over_drive() &&
              RELOJ_STM32F446.hay_over_drive(),
              "los topes del F407 son una constante; los del F446, dos juegos");
        check_near(RELOJ_STM32F446.tope_hclk(false), 168e6, 0.001,
                   "sin over-drive, un F446 no pasa de 168 MHz");
        check_near(RELOJ_STM32F446.tope_hclk(true), 180e6, 0.001,
                   "y con over-drive llega a 180");
    }

    // -----------------------------------------------------------------------
    // E. LOS SIETE BLOQUES DE LA FASE 4
    //
    // Se ejecuta DESPUÉS de `arbol_f446()`, y no por casualidad: allí el
    // núcleo queda aparcado en un `wfe` y el chip corriendo a 180 MHz con los
    // tres PLL enganchados. Eso es justo lo que hace falta para preguntarle a
    // un periférico de qué reloj come, porque la respuesta interesante no es
    // «un número» sino «el que diga el selector».
    // -----------------------------------------------------------------------
    void perifericos_446() {
        grupo("E1 El mapa: siete ventanas que en el F407 no existen");

        check(dut.apb1_dec.decodes(addr446::FMPI2C1_B),
              "0x4000_6000 (FMPI2C1): contesta, aunque la tabla de fronteras "
              "del RM0390 rev.4 se deje ese rango sin nombrar");
        // Y los bits de reloj que van con ellas, sacados de stm32f446xx.h y no
        // de la memoria. El del SPDIF-RX -el 16 del APB1ENR- es el que la
        // primera version de la fase 4 se dejo fuera: con el bloque en su sitio
        // y su reloj sin poder encenderse, el modelo daba error de bus en una
        // ventana que el silicio atiende [I-44].
        check_eq(dut.rcc.bits_implementados(Rcc::R_APB1ENR), 0x3FFFC9FFu,
                 "RCC_APB1ENR del F446: 26 bits, con el 16 (SPDIFRXEN), el 24 "
                 "(FMPI2C1EN) y el 27 (CECEN) que el F407 no tiene");
        check_eq(dut.rcc.bits_implementados(Rcc::R_APB2ENR), 0x00C77F33u,
                 "RCC_APB2ENR: con SPI4EN, SAI1EN y SAI2EN");
        check_eq(dut.rcc.bits_implementados(Rcc::R_AHB3ENR), 0x00000003u,
                 "RCC_AHB3ENR: dos bits, FMC y QSPI, donde el F407 tiene uno");
        check_eq(dut.rcc.bits_implementados(Rcc::R_AHB1ENR), 0x606410FFu,
                 "y el AHB1ENR PIERDE bits: sin Ethernet y sin CCM, un F446 no "
                 "puede encender lo que no lleva");
        check(dut.apb1_dec.decodes(addr446::CEC_B) &&
              dut.apb2_dec.decodes(addr446::SPI4_B) &&
              dut.apb2_dec.decodes(addr446::SAI1_B) &&
              dut.apb2_dec.decodes(addr446::SAI2_B),
              "HDMI-CEC, SPI4, SAI1 y SAI2, cada uno en la suya");
        // Los dos bloques de cada SAI son esclavos con direccion propia: el A
        // en base+0x04 y el B en base+0x24 [stm32f446xx.h, SAI_Block_TypeDef].
        check(dut.apb2_dec.decodes(addr446::SAI1_B + 0x04) &&
              dut.apb2_dec.decodes(addr446::SAI1_B + 0x24),
              "y dentro de un SAI hay DOS bloques, no un banco de registros");

        // EL PUERTO COMPARTIDO. El septimo esclavo de la matriz del F446 es
        // «FMC / QUADSPI» y no dos [RM0390, §2.1], asi que las dos ventanas
        // del QUADSPI cuelgan del MISMO puerto, por un decodificador propio.
        check(dut.ahb3_dec.decodes(addr446::QUADSPI_MEM) &&
              dut.ahb3_dec.decodes(addr446::QUADSPI_B),
              "0x9000_0000 y 0xA000_1000 salen los dos por el puerto de "
              "memoria externa: el QUADSPI no anade un octavo esclavo");
        check(!dut.ahb3_dec.decodes(0xA0000000u),
              "y 0xA000_0000, los registros del FMC, NO se decodifica: en un "
              "F446RE ese encapsulado no saca el bus externo, asi que tocarlo "
              "da error de bus [DS10693, tabla 2]");

        grupo("E2 FMPI2C1: el reloj que elige FMPI2C1SEL se ve en SCL");

        // PCLK1 va a 45 MHz (180/4) desde el grupo C. Con PRESC=3 (divide por
        // 4) y SCLL=SCLH=9, el periodo son 20 pulsos de 4/45 MHz.
        wr(addr::RCC_B + Rcc::R_APB1ENR, rd(addr::RCC_B + Rcc::R_APB1ENR) |
                                         (1u << 24));          // FMPI2C1EN
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27));                     // FMPI2C1SEL=00
        wait(1, SC_US);
        wr(addr446::FMPI2C1_B + FmpI2c::R_TIMINGR,
           (3u << 28) | (9u << 8) | 9u);
        check_near(dut.fmpi2c1.scl_hz(), 45e6 / 4.0 / 20.0, 0.001,
                   "FMPI2C1SEL = 00: el bloque come de PCLK1 y su SCL sale a "
                   "562,5 kHz con ese TIMINGR");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (1u << 22));        // FMPI2C1SEL=01
        wait(1, SC_US);
        check_near(dut.fmpi2c1.scl_hz(), 180e6 / 4.0 / 20.0, 0.001,
                   "FMPI2C1SEL = 01: el MISMO TIMINGR da cuatro veces mas "
                   "porque ahora come de SYSCLK. El selector no guarda un bit");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27) | (2u << 22));        // FMPI2C1SEL=10
        wait(1, SC_US);
        check_near(dut.fmpi2c1.scl_hz(), 16e6 / 4.0 / 20.0, 0.001,
                   "FMPI2C1SEL = 10: y del HSI, que es el caso que sirve para "
                   "no perder el bus al cambiar de frecuencia");
        rcc_w(Rcc::R_DCKCFGR2, (1u << 27));
        wait(1, SC_US);

        // Y lo que NO es el I2C de siempre: ISR/ICR en vez de SR1/SR2, y un
        // TIMINGR que el silicio no deja tocar con el bloque encendido.
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR1, FmpI2c::C1_PE);
        wr(addr446::FMPI2C1_B + FmpI2c::R_TIMINGR, 0xDEADBEEFu);
        check_eq(rd(addr446::FMPI2C1_B + FmpI2c::R_TIMINGR),
                 (3u << 28) | (9u << 8) | 9u,
                 "TIMINGR con PE=1 no se escribe, igual que en el silicio "
                 "[RM0390, 23.7.5]");
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR1, 0);

        grupo("E3 QUADSPI: un comando que sale por los pines");

        wr(addr::RCC_B + Rcc::R_AHB3ENR, 3u);                   // FMC + QSPIEN
        wait(1, SC_US);
        const unsigned cmd0 = dut.qspi.n_comandos();
        wr(addr446::QUADSPI_B + QuadSpi::R_CR, QuadSpi::CR_EN);
        // Leer la identificacion de una memoria: instruccion 0x9F en una
        // linea, sin direccion, tres bytes de vuelta en una linea.
        wr(addr446::QUADSPI_B + QuadSpi::R_DLR, 2u);
        wr(addr446::QUADSPI_B + QuadSpi::R_CCR,
           0x9Fu | (1u << 8) | (1u << 24) | (1u << 26));
        wait(50, SC_US);
        check(dut.qspi.n_comandos() == cmd0 + 1,
              "escribir CCR sin fase de direccion LANZA el comando: es de las "
              "cosas del QUADSPI que mas despistan y aqui esta modelada");
        check_eq(dut.qspi.ultima_instruccion(), 0x9Fu,
                 "y por el pin ha salido el 0x9F, no otro byte");

        grupo("E4 SAI1: la trama de audio sale del reloj del selector");

        wr(addr::RCC_B + Rcc::R_APB2ENR, rd(addr::RCC_B + Rcc::R_APB2ENR) |
                                         (1u << 22));           // SAI1EN
        // SAI1SRC = 00 (PLLSAI_Q) con PLLSAIDIVQ = 1 -> el propio Q.
        rcc_w(Rcc::R_DCKCFGR, 0u);
        wait(1, SC_US);
        const double f_sai = dut.rcc.sai1_freq();
        check(f_sai > 0.0, "el SAI1 tiene reloj: el PLLSAI engancha de verdad");
        // MCKDIV = 1 -> MCLK = f/2, y fs = MCLK/256 con NODIV = 0.
        wr(addr446::SAI1_B + 0x04 + SaiBlock::R_CR1, (1u << 20));
        check_near(dut.sai1.a.mclk_hz(), f_sai / 2.0, 0.001,
                   "MCKDIV = 1 divide el reloj del bloque por dos");
        check_near(dut.sai1.a.fs_hz(), f_sai / 2.0 / 256.0, 0.001,
                   "y la frecuencia de muestreo es MCLK/256, que es la "
                   "relacion de la que viene el «256 fs» de los codecs");
        // El mismo bloque con OTRA fuente: SAI1SRC = 10 es PLL_R.
        rcc_w(Rcc::R_DCKCFGR, (2u << 20));
        wait(1, SC_US);
        check_near(dut.sai1.a.mclk_hz(), dut.rcc.pll_r_freq() / 2.0, 0.001,
                   "cambiar SAI1SRC cambia la trama sin tocar un registro del "
                   "SAI: es el arbol de la fase 3 llegando al pin");
        rcc_w(Rcc::R_DCKCFGR, 0u);

        grupo("E5 Lo declarado y no modelado, que lo dice");

        // Un bloque declarado sigue necesitando su reloj para contestar: sin
        // SPDIFRXEN ni CECEN, lo que hay en su ventana es un error de bus, que
        // es otra cosa distinta y tambien correcta.
        wr(addr::RCC_B + Rcc::R_APB1ENR, rd(addr::RCC_B + Rcc::R_APB1ENR) |
                                         (1u << 16) | (1u << 27));
        wait(1, SC_US);
        const unsigned a0 = dut.spdifrx.accesos();
        check_eq(rd(addr446::SPDIFRX_B + 0x04), 0u,
                 "un registro del SPDIF-RX lee cero, no basura ni lo que se "
                 "escribio antes");
        wr(addr446::SPDIFRX_B + 0x00, 0xFFFFFFFFu);
        check_eq(rd(addr446::SPDIFRX_B + 0x00), 0u,
                 "y escribir no se guarda: un firmware que lo configure y lo "
                 "lea de vuelta VE que no le ha hecho caso, en vez de creerse "
                 "configurado y quedarse esperando");
        check(dut.spdifrx.accesos() > a0,
              "el bloque cuenta sus accesos, y el primero saca un aviso por el "
              "informe de SystemC diciendo en que fase le toca");
        check_eq(rd(addr446::CEC_B + 0x00), 0u, "lo mismo el HDMI-CEC");

        grupo("E6 Lo que el die tiene y el LQFP64 no saca");

        // Esto es lo que separa «el chip no lo lleva» de «el encapsulado no le
        // saca pines», que son dos cosas distintas y el modelo las trata
        // distinto [vs_446re, §8.3].
        check(dut.apb2_dec.decodes(addr446::SPI4_B) &&
              !dut.pinmux.tiene_af(4, 2, 5) && !dut.pinmux.tiene_af(4, 4, 5) &&
              !dut.pinmux.tiene_af(4, 5, 5) && !dut.pinmux.tiene_af(4, 6, 5) &&
              !dut.pinmux.tiene_af(6, 12, 6) && !dut.pinmux.tiene_af(6, 13, 6),
              "el SPI4 responde en el bus y no tiene ni un pin: sus seis "
              "ranuras del die (PE2/4/5/6, PG12/13) no estan registradas "
              "porque el LQFP64 no las saca");
        check(!dut.pinmux.tiene_af(4, 2, 6) && !dut.pinmux.tiene_af(5, 7, 6),
              "y el SAI2 tampoco: ni una ranura de AF6 fuera de las del SAI1");

        // EL QUADSPI, QUE SI TIENE PINES PERO NO TODOS. Esta es, literalmente,
        // la nota 3 del datasheet: «For the LQFP64 package the Quad SPI is
        // available with limited features». Sin IO2 no hay cuatro lineas.
        check(dut.pinmux.tiene_af(1, 2, 9) && dut.pinmux.tiene_af(2, 9, 9) &&
              dut.pinmux.tiene_af(2, 10, 9) && dut.pinmux.tiene_af(0, 1, 9) &&
              dut.pinmux.tiene_af(1, 6, 10),
              "QUADSPI: CLK en PB2, IO0 en PC9, IO1 en PC10, IO3 en PA1 y NCS "
              "en PB6 [PINDATA: STM32F446R(C-E)Tx]");
        check(!dut.pinmux.tiene_af(2, 8, 9) && !dut.pinmux.tiene_af(4, 2, 9),
              "y IO2 NO tiene pin en este encapsulado: es la nota 3 del "
              "datasheet, «available with limited features»");
        check(dut.pinmux.tiene_af(2, 6, 4) && dut.pinmux.tiene_af(2, 7, 4),
              "el FMPI2C1 si los tiene: SCL en PC6 y SDA en PC7, en AF4, la "
              "misma ranura que ocupan los I2C de siempre en otros pines");
        check(MCU_STM32F446RE.perif.spi4 && MCU_STM32F446RE.perif.sai &&
              MCU_STM32F446RE.perif.quadspi && MCU_STM32F446RE.perif.fmpi2c1 &&
              MCU_STM32F446RE.perif.cec && MCU_STM32F446RE.perif.spdifrx &&
              MCU_STM32F446RE.perif.i2s1,
              "el descriptor del F446 declara los siete");
        check(!MCU_STM32F407VG.perif.spi4 && !MCU_STM32F407VG.perif.sai &&
              !MCU_STM32F407VG.perif.quadspi && !MCU_STM32F407VG.perif.fmpi2c1 &&
              !MCU_STM32F407VG.perif.cec && !MCU_STM32F407VG.perif.spdifrx &&
              !MCU_STM32F407VG.perif.i2s1,
              "y el del F407 no declara ninguno");
        // El SPI1 del F446 SI hace audio, y es el mismo IP: lo que cambia es
        // el rasgo, no la clase.
        check(std::string(dut.spi1.caps().kind) == "SPI/I2S (APB2)",
              "el SPI1 de un F446 es el I2S1: mismo bloque, rasgo distinto");
    }

    // -----------------------------------------------------------------------
    // D. EL HITO H4: los 180 MHz de un firmware de verdad, y el cuelgue
    // -----------------------------------------------------------------------
    void hito_h4() {
        grupo("D1 H4: el SystemClock_Config() de una Nucleo-F446RE");

        ImageLoader ld(dut);
        const long n = ld.load_file("verif/fw/clk446_demo/clk446.bin",
                                    dut.mcu.memoria.flash.base);
        if (!check(n > 0, "el firmware del reloj se carga")) return;

        // Reset frio: el chip vuelve a empezar con el firmware nuevo.
        apaga();
        wait(50, SC_US);
        enciende();
        wait(60, SC_MS);

        const uint32_t done   = dut.sram1.peek32(0);
        const uint32_t sysclk = dut.sram1.peek32(4);
        const uint32_t etapa  = dut.sram1.peek32(8);
        const uint32_t od     = dut.sram1.peek32(12);
        const uint32_t ticks  = dut.sram1.peek32(16);
        const uint32_t pclk1  = dut.sram1.peek32(20);
        std::printf("    buzon: done=%u sysclk=%u etapa=%u od=%u ticks=%u "
                    "pclk1=%u\n", done, sysclk, etapa, od, ticks, pclk1);

        check_eq(etapa, 6u, "el firmware recorre las seis etapas de RM0390 5.1.3");
        check_eq(od, 3u, "y llega con ODRDY y ODSWRDY puestas: paso por el "
                         "over-drive de verdad");
        check_eq(sysclk, 180000000u,
                 "SYSCLK = 180 MHz, calculado por el firmware con la cabecera "
                 "de ST y no por nosotros");
        check_eq(pclk1, 45000000u, "PCLK1 = 45 MHz");
        check_near(dut.rcc.hclk_freq(), 180e6, 0.001,
                   "y el modelo esta de acuerdo: HCLK = 180 MHz");
        check_eq(done, 1u, "el firmware termina");
        check(ticks >= 20u,
              "y sus veinte milisegundos de SysTick han pasado de verdad: la "
              "frecuencia no es solo un numero en un registro");

        // ------------------------------------------------------------------
        grupo("D2 H4, la otra mitad: si ODRDY no sube, el firmware NO avanza");
        // ------------------------------------------------------------------
        // El plan lo pide con estas palabras: «debe colgarse si el modelo no
        // levanta ODRDY — hay que comprobar las dos cosas, la que funciona y
        // la que debe fallar». Sin esta prueba, un modelo que levantara ODRDY
        // sin mirar -o que no la mirara nadie- pasaria por bueno.
        //
        // Se hace sin construir un segundo chip: el tiempo que tarda el
        // regulador es un PARAMETRO del PWR, y ponerlo en diez segundos es
        // exactamente «esta bandera no va a subir». El firmware es el mismo,
        // sin recompilar, y lo que se observa es lo que le pasaria en una
        // placa cuyo regulador no arrancara.
        const sc_time t_od_bueno = dut.pwr.t_od_ready;
        dut.pwr.t_od_ready = sc_time(10, SC_SEC);
        apaga();
        wait(50, SC_US);
        enciende();
        wait(60, SC_MS);

        const uint32_t done2  = dut.sram1.peek32(0);
        const uint32_t etapa2 = dut.sram1.peek32(8);
        std::printf("    buzon: done=%u etapa=%u (1 = escribio ODEN y sigue "
                    "esperando ODRDY)\n", done2, etapa2);
        check_eq(etapa2, 1u,
                 "sesenta milisegundos despues, el firmware SIGUE en el paso 3 "
                 "de RM0390 5.1.3: escribio ODEN y espera una bandera que no "
                 "va a subir");
        check_eq(done2, 0u, "no llega al final, que es lo correcto");
        check(dut.rcc.hclk_freq() < 100e6,
              "y NO hay 180 MHz: sin over-drive el chip no llega, y este "
              "modelo no se los regala");

        dut.pwr.t_od_ready = t_od_bueno;
    }

    void run() {
        cruzada();
        blinky();
        arbol_f446();
        perifericos_446();
        hito_h4();
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
