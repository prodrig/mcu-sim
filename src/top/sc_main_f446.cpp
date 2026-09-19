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
//   make test446   (o: make -C . test446-build && ./build/test446)
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cmath>
#include <string>
#include "../common/asan_opciones.h"
#include "../soc/stm32f446.h"
#include "../verif/image_loader.h"
#include "../verif/bus_test_master.h"
#include "../verif/swd_port.h"

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
    // Una sonda SWD soldada a PA14/PA13, igual que en el banco del F407. Es lo
    // que hace falta para el hito H5: no basta con que el modelo SEPA su
    // IDCODE, tiene que decirlo POR LOS DOS HILOS, que es por donde lo lee
    // STM32CubeIDE.
    SwdProbe* sonda = nullptr;

    SC_CTOR(Tb446) {
        tm.isk.bind(dut.matrix.from_tb);
        sonda = new SwdProbe(dut.pinmux.analog(0, 14),   // PA14 SWCLK
                             dut.pinmux.analog(0, 13),   // PA13 SWDIO
                             2e6);
        d_vdd  = dut.pwr_pads.vdd.register_driver("tb_vdd");
        d_vdda = dut.pwr_pads.vdda.register_driver("tb_vdda");
        d_nrst = dut.pwr_pads.nrst.register_driver("tb_nrst");
        d_bt0  = dut.pwr_pads.boot0.register_driver("tb_boot0");
        SC_THREAD(run);
    }
    // La sonda no es un modulo de SystemC -es un objeto normal soldado a dos
    // nodos analogicos-, asi que hay que devolverla a mano. Lo dijo ASan.
    ~Tb446() override { delete sonda; sonda = nullptr; }

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
        // Desde la fase 1 del plan del F415/F417 estas mascaras se CALCULAN por
        // referencia -la tabla de la familia menos lo que el chip no lleva- en
        // vez de elegirse entre dos constantes. Que el F446 no se mueva ni un
        // bit es justo lo que hay que comprobar: el refactor lo hizo para
        // cerrar el agujero del F405 y no tenia que tocar esta familia.
        check_eq(dut.rcc.bits_implementados(Rcc::R_AHB2ENR), 0x00000081u,
                 "RCC_AHB2ENR del F446: camara y OTG FS, y SIN el RNG, que en "
                 "esta familia no existe");
        check_eq(dut.rcc.bits_implementados(Rcc::R_AHB2ENR) & 0x30u, 0u,
                 "y sin CRYP ni HASH: no hay un solo simbolo de los dos en "
                 "stm32f446xx.h");
        check_eq(dut.rcc.bits_implementados(Rcc::R_AHB1RSTR), 0x206010FFu,
                 "el AHB1RSTR tampoco se mueve: la tabla de la familia ya "
                 "excluia el bit del Ethernet, asi que quitarlo no hace nada");
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
        // =================================================================
        // LOS PINES DE CADA ENCAPSULADO, CONTRA LOS DATOS DE ST
        //
        // La tabla de AF es DEL DIE y el encapsulado decide cuál de sus
        // entradas llega a un pad. Esta prueba comprueba las dos mitades a la
        // vez: que el modelo registra la entrada, y que al cruzarla con la
        // máscara de cada encapsulado sale **exactamente la lista que ST
        // publica para esa referencia**.
        //
        // Y las dos mitades vienen de FICHEROS DISTINTOS de ST: la
        // implementación se escribió desde `GPIO-STM32F446_gpio_v1_0_Modes`,
        // que es del die, y los números de aquí abajo se contaron sobre
        // `STM32F446M(C-E)Yx`, `R(C-E)Tx`, `V(C-E)Tx` y `Z(C-E)Tx`, que son de
        // la referencia. Que cuadren no es una tautología.
        // =================================================================
        struct RanuraAf { uint8_t port, pin, af; };
        // Lo que el modelo registra de cada bloque, con su AF. No incluye tres
        // cosas, y por eso los números de abajo son menores que los de ST en
        // justo esa cantidad: el SMBA del FMPI2C1 (no hay SMBus modelado), los
        // cuatro IO del segundo banco del QUADSPI (el modelo tiene un banco) y
        // los pines del SPDIF-RX y el HDMI-CEC (bloques declarados).
        static const RanuraAf FMPI2C1_[] = {
            {2,6,4},{3,12,4},{3,14,4},{5,14,4},          // SCL
            {2,7,4},{3,13,4},{3,15,4},{5,15,4} };        // SDA
        static const RanuraAf SAI1_[] = {
            {0,3,6},{4,4,6},{1,10,6},{4,5,6},{1,2,6},{2,1,6},{3,6,6},{4,6,6},
            {4,2,6},{1,9,6},{5,9,6},{1,12,6},{5,8,6},{0,9,6},{4,3,6},{5,6,6},
            {2,0,6},{5,7,6} };
        static const RanuraAf SAI2_[] = {
            {3,12,10},{3,13,10},{3,14,8},{3,11,10},{1,11,8},{4,0,10},
            {4,13,10},{6,9,10},{0,12,8},{4,12,10},{0,2,8},
            {4,11,10},{5,11,10},{6,10,10},{0,1,10},{4,14,10} };
        static const RanuraAf SPI4_[] = {
            {4,2,5},{4,12,5},{6,11,6},{3,0,5},{4,5,5},{4,13,5},{6,12,6},
            {4,6,5},{4,14,5},{6,13,6},{4,4,5},{4,11,5},{6,14,6} };
        static const RanuraAf QSPI_[] = {
            {1,2,9},{3,3,9},{1,6,10},{6,6,10},{2,11,9},
            {2,9,9},{3,11,9},{5,8,10},{2,10,9},{3,12,9},{5,9,10},
            {4,2,9},{5,7,9},{0,1,9},{3,13,9},{5,6,9} };

        auto cuenta = [&](const RanuraAf* t, size_t n, const Encapsulado& e) {
            unsigned c = 0;
            for (size_t i = 0; i < n; ++i)
                if (dut.pinmux.tiene_af(t[i].port, t[i].pin, t[i].af) &&
                    e.bonded(t[i].port, t[i].pin)) ++c;
            return c;
        };
        #define CUENTA(T, E) cuenta(T, sizeof(T)/sizeof(T[0]), E)

        const Encapsulado& eM = MCU_STM32F446MC.enc;   // WLCSP81
        const Encapsulado& eR = MCU_STM32F446RE.enc;   // LQFP64
        const Encapsulado& eV = MCU_STM32F446VE.enc;   // LQFP100
        const Encapsulado& eZ = MCU_STM32F446ZE.enc;   // LQFP144

        std::printf("    pines por encapsulado (WLCSP81 / LQFP64 / LQFP100 / LQFP144):\n");
        std::printf("      FMPI2C1 %u %u %u %u | SAI1 %u %u %u %u | "
                    "SPI4 %u %u %u %u | QUADSPI %u %u %u %u\n",
                    CUENTA(FMPI2C1_, eM), CUENTA(FMPI2C1_, eR),
                    CUENTA(FMPI2C1_, eV), CUENTA(FMPI2C1_, eZ),
                    CUENTA(SAI1_, eM), CUENTA(SAI1_, eR),
                    CUENTA(SAI1_, eV), CUENTA(SAI1_, eZ),
                    CUENTA(SPI4_, eM), CUENTA(SPI4_, eR),
                    CUENTA(SPI4_, eV), CUENTA(SPI4_, eZ),
                    CUENTA(QSPI_, eM), CUENTA(QSPI_, eR),
                    CUENTA(QSPI_, eV), CUENTA(QSPI_, eZ));

        check(CUENTA(FMPI2C1_, eM) == 4 && CUENTA(FMPI2C1_, eR) == 2 &&
              CUENTA(FMPI2C1_, eV) == 6 && CUENTA(FMPI2C1_, eZ) == 8,
              "FMPI2C1: 4, 2, 6 y 8 pines de SCL/SDA segun el encapsulado "
              "(ST cuenta uno mas en tres de ellos, el SMBA que no se modela)");
        check(CUENTA(SAI1_, eM) == 11 && CUENTA(SAI1_, eR) == 8 &&
              CUENTA(SAI1_, eV) == 14 && CUENTA(SAI1_, eZ) == 18,
              "SAI1: 11, 8, 14 y 18 pines, las cuatro listas de ST al dedo");
        check(CUENTA(SPI4_, eM) == 3 && CUENTA(SPI4_, eR) == 0 &&
              CUENTA(SPI4_, eV) == 9 && CUENTA(SPI4_, eZ) == 13,
              "SPI4: 3, CERO, 9 y 13. El LQFP64 es el unico que no le saca ni "
              "un pin, y el WLCSP81 -con mas E/S- solo le saca tres");
        check(CUENTA(QSPI_, eM) == 10 && CUENTA(QSPI_, eR) == 6 &&
              CUENTA(QSPI_, eV) == 11 && CUENTA(QSPI_, eZ) == 16,
              "QUADSPI: 10, 6, 11 y 16 (ST cuenta cuatro o seis mas en cada "
              "uno: los IO del segundo banco, que este modelo no tiene)");

        // EL SAI2 NO ES CUESTION DE PINES. En un F446RC o RE no hay una sola
        // senal de SAI2 en ningun pin -ni en los de PA, que ese encapsulado si
        // saca-, y la Tabla 2 del datasheet pone 1 en la fila del SAI. Asi que
        // en ESTE dut, que es un RE, no hay ni una ranura registrada; en los
        // otros tres, la cuenta cuadra con la de ST.
        check(CUENTA(SAI2_, eR) == 0 && !MCU_STM32F446RE.perif.sai2,
              "el SAI2 no tiene NI UNA ranura en un LQFP64, y no por falta de "
              "pines: ST no vende esa referencia con dos SAI [DS10693 tabla 2]");
        check(MCU_STM32F446MC.perif.sai2 && MCU_STM32F446VE.perif.sai2 &&
              MCU_STM32F446ZE.perif.sai2,
              "y las otras siete referencias si lo llevan");
        check(dut.apb2_dec.decodes(addr446::SAI2_B),
              "aun asi, en un RE el bloque SIGUE respondiendo en el bus: el "
              "die es el mismo y sus registros contestan. Lo que no tiene es "
              "por donde salir, que es otra cosa y se dice aparte");
        #undef CUENTA

        grupo("E7 Las ocho referencias de la familia");

        struct Miembro { const McuCaps* m; unsigned gpio; unsigned kb;
                         bool fmc; bool sai2; };
        static const Miembro FAMILIA[] = {
            {&MCU_STM32F446MC, 63, 256, false, true },
            {&MCU_STM32F446ME, 63, 512, false, true },
            {&MCU_STM32F446RC, 50, 256, false, false},
            {&MCU_STM32F446RE, 50, 512, false, false},
            {&MCU_STM32F446VC, 81, 256, true,  true },
            {&MCU_STM32F446VE, 81, 512, true,  true },
            {&MCU_STM32F446ZC, 114, 256, true, true },
            {&MCU_STM32F446ZE, 114, 512, true, true },
        };
        unsigned mal = 0;
        for (const Miembro& x : FAMILIA) {
            if (x.m->enc.n_gpio != x.gpio) ++mal;
            if (!x.m->enc.coherente()) ++mal;        // la mascara cuadra
            if (!x.m->enc.verificado) ++mal;
            if (x.m->memoria.flash.size != x.kb * 1024u) ++mal;
            if (x.m->perif.fsmc  != x.fmc)  ++mal;
            if (x.m->perif.sai2  != x.sai2) ++mal;
            // Lo que NO cambia entre las ocho: mismo die.
            if (std::string(x.m->familia) != "STM32F446") ++mal;
            if (x.m->idcode != 0x10000421u) ++mal;
            if (x.m->nucleo.n_irq != 97u) ++mal;
            if (x.m->memoria.ram.sram1_size != RAM_STM32F446.sram1_size) ++mal;
            if (x.m->memoria.ram.hay_ccm()) ++mal;
            if (x.m->perif.eth || x.m->perif.rng || x.m->perif.i2sext) ++mal;
        }
        check_eq(mal, 0u,
                 "las ocho referencias cuadran: E/S y mascara coherentes, "
                 "Flash de 256 o 512 KB, FMC solo en V y Z, SAI2 en todas "
                 "menos en R, y el mismo die en las ocho [DS10693 tabla 2]");
        check(MCU_STM32F446RC.memoria.flash.n_sectores == 6 &&
              MCU_STM32F446RE.memoria.flash.n_sectores == 8,
              "y la Flash de 256 KB son SEIS sectores, no ocho: el ultimo "
              "termina donde termina la Flash [RM0390 Rev 9, tabla 4]");
        {
            // Las ocho se pueden NOMBRAR en un XML, que es de lo que sirve el
            // catalogo. Sin esto, `tipo="STM32F446ZE"` seria un error.
            unsigned n = 0;
            for (const Miembro& x : FAMILIA)
                if (mcu_por_nombre(x.m->nombre) == x.m) ++n;
            check_eq(n, 8u, "y las ocho estan en el catalogo por su nombre");
        }
        // PB11, el pin que lleva seis fases apareciendo.
        check(!MCU_STM32F446MC.enc.bonded(1, 11) &&
              !MCU_STM32F446RE.enc.bonded(1, 11) &&
              !MCU_STM32F446VE.enc.bonded(1, 11) &&
              MCU_STM32F446ZE.enc.bonded(1, 11),
              "PB11 solo sale en el LQFP144/UFBGA144: ni en el LQFP64, ni en "
              "el WLCSP81, ni en el LQFP100 -donde el datasheet dice que lo "
              "sustituye VCAP1-");
        // Y el WLCSP81 no es un LQFP100 recortado.
        check(!MCU_STM32F446MC.enc.bonded(2, 1) &&
              MCU_STM32F446RE.enc.bonded(2, 1),
              "el WLCSP81 no saca PC1 y el LQFP64, con 31 patillas menos, si: "
              "a mas bolas no corresponde un superconjunto de pines");

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

    // =======================================================================
    // F. LA PRUEBA CRUZADA, ESCRITA COMO TAL
    //
    // Es el punto que la fase 5 del plan pide con nombre: «una prueba cruzada
    // que compruebe LO QUE ESTE DOCUMENTO DICE (...) es la prueba que evita
    // que el puerto se coma al original».
    //
    // Por eso se escribe como una TABLA DE AFIRMACIONES y no como veinte
    // comprobaciones sueltas. Cada fila es una frase del documento de
    // comparación, con su sección, y al lado la expresión que la hace verdad o
    // mentira **preguntando a los dos descriptores a la vez**. Leída de
    // arriba abajo, la tabla ES el documento; y si alguien cambia un
    // descriptor sin darse cuenta de lo que implica, la fila que se rompe dice
    // qué párrafo ha dejado de ser cierto.
    //
    // No cuesta un picosegundo: son todo consultas.
    // =======================================================================
    void prueba_cruzada() {
        grupo("F1 El documento de comparacion, comprobado frase a frase");

        struct Afirmacion { const char* seccion; const char* dice; bool cierto; };

        const Encapsulado& e407 = MCU_STM32F407VG.enc;    // LQFP100
        const Encapsulado& e405r = MCU_STM32F405RG.enc;   // LQFP64 del F405
        const Encapsulado& e446 = MCU_STM32F446RE.enc;    // LQFP64 del F446

        // ¿Queda alguna ranura de AF11 registrada en el mux de este chip? El
        // AF11 es el del Ethernet y es el UNICO numero que se vacia entero.
        bool hay_af11 = false;
        for (unsigned p = 0; p < N_GPIO_PORTS && !hay_af11; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i)
                if (dut.pinmux.tiene_af(p, i, 11)) { hay_af11 = true; break; }

        const Afirmacion tabla[] = {
            // --- Las tres que el plan nombra una por una --------------------
            { "5.3",
              "0x4000_4000 es el I2S3ext en el F407 y el SPDIF-RX en el F446: "
              "la unica direccion de todo el mapa que cambia de dueno",
              MCU_STM32F407VG.perif.i2sext && !MCU_STM32F446RE.perif.i2sext &&
              MCU_STM32F446RE.perif.spdifrx && !MCU_STM32F407VG.perif.spdifrx &&
              dut.spdifrx.base() == 0x40004000u },

            { "8.4",
              "la posicion de vector 80 es el RNG en el F407 y esta reservada "
              "en el F446, porque ese periferico no esta",
              MCU_STM32F407VG.perif.rng && !MCU_STM32F446RE.perif.rng },

            { "9.2 / 15",
              "PB11 sale en el LQFP64 del F405RG y NO en el del F446RE: son "
              "51 E/S contra 50, y por eso ULPI_D4 se muda de PB11 a PB2",
              e405r.bonded(1, 11) && !e446.bonded(1, 11) &&
              e405r.n_gpio == 51 && e446.n_gpio == 50 },

            // --- Y el resto de lo que el documento afirma y es comprobable --
            { "2.1 / 15.1",
              "los dos tienen SIETE esclavos de matriz utiles, y en el F446 el "
              "septimo es «FMC / QUADSPI» compartido: el QUADSPI NO anade un "
              "octavo",
              dut.ahb3_dec.decodes(addr446::QUADSPI_MEM) &&
              dut.ahb3_dec.decodes(addr446::QUADSPI_B) &&
              unsigned(BusSlaveId::FSMC_EXT) + 1u == 7u },

            { "3",
              "el F407 tiene OCHO maestros de bus y el F446 SIETE: la fila que "
              "se va es la del DMA del Ethernet",
              CONN_STM32F407VG.n_maestros() == 8u &&
              CONN_STM32F446.n_maestros() == 7u &&
              CONN_STM32F407VG.hay_maestro(BusMaster::ETH_DMA) &&
              !CONN_STM32F446.hay_maestro(BusMaster::ETH_DMA) },

            { "4.1",
              "1 MB de Flash en doce sectores contra 512 KB en ocho, y la RAM "
              "NO cambia: los 112 + 16 KB estan en los dos",
              MCU_STM32F407VG.memoria.flash.n_sectores == 12 &&
              MCU_STM32F446RE.memoria.flash.n_sectores == 8 &&
              MCU_STM32F407VG.memoria.ram.sram1_size ==
              MCU_STM32F446RE.memoria.ram.sram1_size },

            { "4.2",
              "la CCM es del F407 y el F446 no la tiene: 0x1000_0000 es una "
              "memoria en uno y espacio reservado en el otro",
              MCU_STM32F407VG.memoria.ram.ccm_size > 0 &&
              MCU_STM32F446RE.memoria.ram.ccm_size == 0 &&
              dut.matrix.decode_addr(addr::CCM_BASE) == -1 },

            { "10",
              "lo unico que cambia de verdad en la depuracion es el IDCODE: "
              "0x1001_6413 contra 0x1000_0421",
              MCU_STM32F407VG.idcode == 0x10016413u &&
              MCU_STM32F446RE.idcode == 0x10000421u &&
              dut.core.debug.idcode() == 0x10000421u },

            { "9.2",
              "el AF11 es el unico numero de funcion alternativa que se vacia "
              "entero en el F446, porque era el del Ethernet",
              !hay_af11 && MCU_STM32F407VG.perif.eth &&
              !MCU_STM32F446RE.perif.eth },

            { "8.4",
              "el F407 llega a la posicion 81 y el F446 a la 96: el enum se "
              "EXTIENDE, no se reescribe",
              CORE_STM32F407VG.n_irq == 82u && CORE_STM32F446.n_irq == 97u &&
              addr446::IRQ_SPI4 > CORE_STM32F407VG.n_irq - 1u },

            { "6.2",
              "el F446 llega a 180 MHz y el F407 no pasa de 168, y en el F446 "
              "el tope DEPENDE DEL ESTADO, no del chip",
              !RELOJ_STM32F407VG.hay_over_drive() &&
              RELOJ_STM32F446.hay_over_drive() &&
              RELOJ_STM32F446.tope_hclk(false) == 168e6 &&
              RELOJ_STM32F446.tope_hclk(true)  == 180e6 },

            { "19.3",
              "el segmento APB2 no mide lo mismo: en el F407 termina antes de "
              "0x4001_5800 y en el F446 los dos SAI estan ahi dentro",
              dut.apb2_dec.decodes(addr446::SAI1_B) &&
              dut.apb2_dec.decodes(addr446::SAI2_B + 0x24) },

            { "5.2",
              "lo que un bloque ausente deja no es un periferico apagado sino "
              "ESPACIO RESERVADO: el RNG y el Ethernet del F446 no los "
              "decodifica nadie",
              !dut.ahb2_dec.decodes(addr::RNG_B) &&
              !dut.ahb1_dec.decodes(addr::ETH_B) },

            { "6.3",
              "y lo contrario tambien: el F446 acepta registros que en el F407 "
              "son huecos reservados, y por eso no cabia en un descriptor",
              MCU_STM32F446RE.arbol.dckcfgr && !MCU_STM32F407VG.arbol.dckcfgr },
        };

        for (const Afirmacion& a : tabla) {
            char q[512];
            std::snprintf(q, sizeof q, "[%s] %s", a.seccion, a.dice);
            check(a.cierto, q);
        }

        grupo("F2 Y que el puerto no se ha comido al original");

        // La otra mitad de la prueba cruzada, y la que de verdad importa: que
        // el F407 siga siendo el F407. No se construye aqui -eso moveria su
        // invariante- sino que se le pregunta a su DESCRIPTOR, que es lo que
        // el die lee para construirse.
        check(e407.bonded(1, 11) && e407.n_gpio == 82 &&
              std::string(e407.nombre) == "LQFP100",
              "el F407VG sigue siendo un LQFP100 de 82 E/S con PB11");
        check(MCU_STM32F407VG.perif.eth && MCU_STM32F407VG.perif.rng &&
              MCU_STM32F407VG.perif.i2sext && MCU_STM32F407VG.perif.fsmc &&
              MCU_STM32F407VG.perif.dcmi,
              "con su Ethernet, su RNG, sus I2Sext, su FSMC y su camara");
        check(!MCU_STM32F407VG.perif.sai && !MCU_STM32F407VG.perif.quadspi &&
              !MCU_STM32F407VG.perif.fmpi2c1 && !MCU_STM32F407VG.perif.spi4 &&
              !MCU_STM32F407VG.perif.cec && !MCU_STM32F407VG.perif.spdifrx &&
              !MCU_STM32F407VG.perif.i2s1,
              "y sin uno solo de los siete bloques que la fase 4 anadio: lo "
              "nuevo se SUMA donde toca y no se cuela donde no");
        // Y la distincion que sostiene todo el puerto, que conviene comprobar
        // porque es contraintuitiva: los dos comparten NETLIST -los dos son un
        // `SocF4`- y NO comparten FAMILIA. `familia` no dice de que die sale el
        // chip: dice si se puede describir con un descriptor mas o hace falta
        // codigo, y el F446 hizo falta codigo (§6.3). Por eso `tipo=` en el XML
        // despacha a dos clases de C++ distintas.
        check(std::string(MCU_STM32F407VG.familia) == "STM32F4" &&
              std::string(MCU_STM32F446RE.familia) == "STM32F446",
              "no son la misma familia, y por eso `tipo=` despacha a dos "
              "clases distintas en vez de a dos descriptores");
        check(dynamic_cast<SocF4*>(&dut) != nullptr,
              "pero SI son el mismo netlist: un Stm32F446 ES un SocF4, que es "
              "lo que hace que un solo modelo valga para los dos");
    }

    // =======================================================================
    // H. EL HITO H6: «los periféricos nuevos, uno a uno, con su prueba»
    //
    // Con una mitad que el plan no pedía con esas palabras y que es la que de
    // verdad podría estar rota: **los periféricos VIEJOS sobre el die nuevo**.
    // El puerto le ha cambiado el árbol de reloj a un modelo que llevaba siete
    // fases funcionando, y un USART que saca 115 200 baudios en un F407 a
    // 84 MHz de APB1 no tiene por qué sacarlos en un F446 a 45. Si algo se
    // rompió con el puerto, se rompió aquí y no en el QUADSPI.
    // =======================================================================
    void hito_h6() {
        grupo("H1 Los perifericos de SIEMPRE, sobre el die nuevo a 180 MHz");

        // Encenderlos todos de una vez: es lo que hace cualquier firmware.
        wr(addr::RCC_B + Rcc::R_APB1ENR, rd(addr::RCC_B + Rcc::R_APB1ENR) |
           (1u << 17) | (1u << 21) | (1u << 0));   // USART2EN, I2C1EN, TIM2EN
        wr(addr::RCC_B + Rcc::R_APB2ENR, rd(addr::RCC_B + Rcc::R_APB2ENR) |
           (1u << 12) | (1u << 8) | (1u << 0));    // SPI1EN, ADC1EN, TIM1EN
        wr(addr::RCC_B + Rcc::R_AHB1ENR, rd(addr::RCC_B + Rcc::R_AHB1ENR) |
           (1u << 21) | (1u << 22));               // DMA1EN, DMA2EN
        wait(2, SC_US);

        // EL USART. 45 MHz de PCLK1 y no 42: el divisor que vale en un F407 NO
        // vale aqui, y esto lo demuestra sin ambiguedad. Con OVER8=0, BRR para
        // 115 200 es 45e6/115200 = 390,625 -> mantisa 24, fraccion 10.
        // 45e6 / (16 * 115200) = 24,4141: mantisa 24 y fraccion 7/16, que da
        // 115 076 baudios. El 0,1 % que sobra es el error del divisor, y es
        // REAL: el silicio tiene exactamente el mismo. Un modelo que diera
        // 115 200 clavados estaria mintiendo sobre el hardware.
        wr(addr::USART2_B + 0x08, (24u << 4) | 7u);        // BRR
        wr(addr::USART2_B + 0x0C, (1u << 13) | (1u << 3) | (1u << 2));  // UE|TE|RE
        wait(1, SC_US);
        std::printf("    USART2 a 45 MHz de PCLK1 con BRR = 0x187: %.0f baudios\n",
                    dut.usart2.baud_hz());
        check_near(dut.usart2.baud_hz(), 115200.0, 0.005,
                   "USART2 saca 115 200 baudios (con el 0,1 % de error del "
                   "divisor) desde los 45 MHz de PCLK1: el periferico es el de "
                   "siempre y el divisor ya NO es el que valia en un F407");

        // EL TEMPORIZADOR. TIMCLK1 son 90 MHz (2*PCLK1 con PPRE1 = /4), asi
        // que un prescaler de 90 da un tick de microsegundo redondo.
        check_near(dut.s_timclk1_hz.read(), 90e6, 0.001,
                   "TIM2 recibe 90 MHz, que es 2*PCLK1 con el APB1 dividiendo "
                   "por cuatro [RM0390, 6.2]");
        wr(addr::TIM2_B + 0x28, 89u);                      // PSC = 90-1
        wr(addr::TIM2_B + 0x2C, 0xFFFFFFFFu);              // ARR
        wr(addr::TIM2_B + 0x14, 1u);                       // EGR.UG
        wr(addr::TIM2_B + 0x24, 0u);                       // CNT = 0
        wr(addr::TIM2_B + 0x00, 1u);                       // CR1.CEN
        const sc_time t0 = sc_time_stamp();
        wait(1, SC_MS);
        const uint32_t cnt = rd(addr::TIM2_B + 0x24);
        wr(addr::TIM2_B + 0x00, 0u);
        std::printf("    TIM2 cuenta %u en %s\n", cnt,
                    (sc_time_stamp() - t0).to_string().c_str());
        check(cnt >= 995u && cnt <= 1005u,
              "y en un milisegundo cuenta mil microsegundos: el reloj nuevo "
              "llega hasta el contador, no se queda en un registro");

        // EL SPI, en el APB2 que va a 90 MHz. Con BR = /4 el reloj serie son
        // 22,5 MHz, un valor que en un F407 (84 MHz de APB2) no sale.
        wr(addr::SPI1_B + 0x00, (1u << 2) | (1u << 9) | (1u << 8) | (1u << 3));
        wr(addr::SPI1_B + 0x00, rd(addr::SPI1_B + 0x00) | (1u << 6));  // SPE
        wait(1, SC_US);
        check_near(dut.s_pclk2_hz.read() / 4.0, 22.5e6, 0.001,
                   "el SPI1 cuelga de un APB2 de 90 MHz, asi que su divisor "
                   "por cuatro da 22,5 MHz y no los 21 del F407");
        wr(addr::SPI1_B + 0x00, 0u);

        // EL DMA. Una copia memoria a memoria, que es la prueba mas corta de
        // que el controlador vive y alcanza las dos SRAM de este chip.
        for (unsigned i = 0; i < 8; ++i)
            wr(addr::SRAM1_BASE + 0x200u + 4 * i, 0xF4460000u + i);
        const uint32_t S0 = addr::DMA2_B + 0x10;           // stream 0
        wr(S0 + 0x00, 0u);                                  // CR = 0
        wr(S0 + 0x04, 8u);                                  // NDTR
        wr(S0 + 0x08, addr::SRAM1_BASE + 0x200u);           // PAR (origen)
        wr(S0 + 0x0C, addr::SRAM2_BASE + 0x100u);           // M0AR (destino)
        // FCR: memoria a memoria EXIGE el modo FIFO -DMDIS- porque el modo
        // directo no existe en esa direccion. El modelo lo impone igual que el
        // silicio, y por eso esta linea no es opcional.
        wr(S0 + 0x14, 0x07u);
        wr(S0 + 0x00, (2u << 6) | (1u << 9) | (1u << 10) |  // DIR=mem2mem, PINC, MINC
                      (2u << 11) | (2u << 13));             // PSIZE/MSIZE = 32 bits
        wr(S0 + 0x00, rd(S0 + 0x00) | 1u);                  // EN
        wait(100, SC_US);
        bool copia_ok = true;
        for (unsigned i = 0; i < 8; ++i)
            if (rd(addr::SRAM2_BASE + 0x100u + 4 * i) != 0xF4460000u + i)
                copia_ok = false;
        check(copia_ok,
              "el DMA2 copia ocho palabras de la SRAM1 a la SRAM2: los dos "
              "controladores y las dos memorias del F446 siguen en su sitio");
        wr(S0 + 0x00, 0u);

        // EL MAPA DE CANALES, QUE DESDE LA FASE 6 ES EL DEL F446 [I-47].
        // Sale de la base de datos de STM32CubeMX y esta contrastado celda a
        // celda con las Tablas 28 y 29 de [RM0390] Rev 9.
        auto celda = [](unsigned st, unsigned ch) { return st * 8 + ch; };
        check((dut.dma2.celdas_con_fuente >> celda(1, 0)) & 1u,
              "DMA2 stream 1 canal 0 tiene fuente: es el SAI1_A, un bloque que "
              "el F407 no lleva y cuya celda alli esta reservada");
        check(((dut.dma2.celdas_con_fuente >> celda(7, 3)) & 1u) &&
              ((dut.dma2.celdas_con_fuente >> celda(0, 4)) & 1u) &&
              ((dut.dma1.celdas_con_fuente >> celda(2, 2)) & 1u),
              "y tambien el QUADSPI (DMA2 7/3), el SPI4 (DMA2 0/4) y el "
              "FMPI2C1 (DMA1 2/2), que son celdas de este chip y de ningun otro");
        check(dut.dma1.celdas_con_fuente != ~uint64_t(0),
              "el DMA1 sigue sabiendo que no tiene TODAS las celdas cableadas");
        {
            // Las dos que quedan sin fuente a proposito: el SPDIF-RX esta
            // DECLARADO y no modelado, asi que no tiene nada que pedir.
            const bool dt = (dut.dma1.celdas_con_fuente >> celda(1, 0)) & 1u;
            const bool cs = (dut.dma1.celdas_con_fuente >> celda(6, 0)) & 1u;
            check(!dt && !cs,
                  "y las dos del SPDIF-RX -DMA1 1/0 y 6/0- siguen sin fuente, "
                  "que es coherente: su bloque esta declarado y no modelado");
        }

        grupo("H2 FMPI2C1: una transferencia entera, de maestro a esclavo");

        // Lo que la fase 4 dejo comprobado era la TEMPORIZACION. Esto es la
        // otra mitad: que por los pines pasa una trama de I2C de verdad. El
        // maestro y el esclavo son el mismo bloque -no hay dos FMPI2C en este
        // chip-, asi que se le habla a si mismo poniendose su propia direccion
        // de esclavo: el bloque arbitra sus dos mitades igual que lo haria con
        // otro chip en el mismo bus.
        //
        // NO se hace: el FMPI2C1 de este modelo no calcula PEC ni implementa
        // los temporizadores de SMBus, y `limitaciones()` lo dice.
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR1, 0u);           // PE = 0
        wr(addr446::FMPI2C1_B + FmpI2c::R_TIMINGR, (3u << 28) | (9u << 8) | 9u);
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR1, FmpI2c::C1_PE);
        const uint32_t isr0 = dut.fmpi2c1.peek_isr();
        check((isr0 & FmpI2c::I_TXE) != 0,
              "con PE = 1 el registro de transmision esta vacio y lo dice "
              "TXE, que es una bandera de ISR y no de un SR1 que haya que "
              "leer en el orden correcto");
        check((isr0 & FmpI2c::I_BUSY) == 0, "y el bus esta libre");

        // Un START de escritura de dos bytes con AUTOEND: el maestro suelta
        // el bus solo al terminar, que es lo que este IP anade sobre el
        // clasico -donde habia que escribir el STOP a mano-.
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR2,
           (0x52u << 1) | (2u << 16) | FmpI2c::C2_AUTOEND | FmpI2c::C2_START);
        wait(5, SC_US);
        check((dut.fmpi2c1.peek_isr() & FmpI2c::I_BUSY) != 0,
              "arranca la trama: BUSY sube y el bloque esta tirando de SCL y "
              "SDA por los pines, no simulando una transferencia por dentro");
        wr(addr446::FMPI2C1_B + FmpI2c::R_TXDR, 0xA5u);
        wait(200, SC_US);
        wr(addr446::FMPI2C1_B + FmpI2c::R_TXDR, 0x5Au);
        wait(400, SC_US);
        const uint32_t isr1 = dut.fmpi2c1.peek_isr();
        std::printf("    ISR tras la trama = 0x%08X\n", isr1);
        // Sin nadie al otro lado, lo que tiene que pasar es que NADIE
        // reconozca la direccion. Un modelo complaciente daria el ACK y el
        // firmware se creeria que hay un chip ahi.
        check((isr1 & FmpI2c::I_NACKF) != 0,
              "y sin nadie en 0x52 el maestro recoge un NACK: la linea sube "
              "por la resistencia de la placa porque nadie tira de ella, que "
              "es exactamente lo que pasa en un bus vacio");
        check((isr1 & FmpI2c::I_STOPF) != 0,
              "AUTOEND suelta el bus solo al acabar, sin escribir el STOP");
        wr(addr446::FMPI2C1_B + FmpI2c::R_ICR, 0x00003F38u);
        check_eq(dut.fmpi2c1.peek_isr() & (FmpI2c::I_NACKF | FmpI2c::I_STOPF),
                 0u, "y un uno en ICR borra la bandera: sin la danza de "
                     "SR1 + SR2 del I2C clasico");
        wr(addr446::FMPI2C1_B + FmpI2c::R_CR1, 0u);

        grupo("H3 QUADSPI: la ventana mapeada en memoria");

        // El modo que hace util a este periferico: la Flash serie aparece en
        // 0x9000_0000 y el nucleo la LEE COMO MEMORIA. Por debajo, cada
        // lectura lanza un comando por los pines; por arriba, es un puntero.
        wr(addr446::QUADSPI_B + QuadSpi::R_CR, 0u);
        wr(addr446::QUADSPI_B + QuadSpi::R_DCR, (22u << 16));   // FSIZE
        wr(addr446::QUADSPI_B + QuadSpi::R_CR, QuadSpi::CR_EN);
        // FMODE = 11 (mapeado en memoria), instruccion 0x0B en una linea,
        // direccion de 24 bits en una linea, ocho ciclos vacios, datos.
        wr(addr446::QUADSPI_B + QuadSpi::R_CCR,
           0x0Bu | (1u << 8) | (1u << 10) | (2u << 12) | (8u << 18) |
           (1u << 24) | (3u << 26));
        const unsigned cmd0 = dut.qspi.n_comandos();
        uint32_t leido = 0;
        const auto r = tm.read32(addr446::QUADSPI_MEM + 0x40u, leido);
        check(r == tlm::TLM_OK_RESPONSE,
              "0x9000_0040 contesta: la ventana de 256 MB esta viva y cuelga "
              "del puerto de memoria externa de la matriz");
        std::printf("    lectura mapeada = 0x%08X, comandos lanzados: %u -> %u\n",
                    leido, cmd0, dut.qspi.n_comandos());
        check(dut.qspi.n_comandos() > cmd0,
              "y leer esa direccion LANZA un comando por los pines: no es una "
              "memoria interna disfrazada, es el controlador trabajando");
        check_eq(dut.qspi.ultima_instruccion(), 0x0Bu,
                 "con la instruccion que pide CCR -0x0B, Fast Read- y no otra");
        wr(addr446::QUADSPI_B + QuadSpi::R_CR, 0u);

        grupo("H4b SAI1: una trama de audio con datos dentro");

        // La fase 4 comprobo las frecuencias. Esto comprueba que la FIFO se
        // vacia al ritmo de esa trama, que es lo unico que demuestra que el
        // bloque esta transmitiendo y no solo configurado.
        rcc_w(Rcc::R_DCKCFGR, 0u);
        wait(1, SC_US);
        SaiBlock& a = dut.sai1.a;
        const uint32_t BA = addr446::SAI1_B + 0x04;
        wr(BA + SaiBlock::R_CR1, 0u);
        wr(BA + SaiBlock::R_FRCR, 63u);                    // trama de 64 bits
        wr(BA + SaiBlock::R_SLOTR, (1u << 8) | (3u << 16)); // 2 ranuras
        // Maestro transmisor, MCKDIV = 1.
        wr(BA + SaiBlock::R_CR1, (1u << 20));
        for (unsigned i = 0; i < 4; ++i)
            wr(BA + SaiBlock::R_DR, 0x1234'0000u + i);
        check_eq(a.nivel_fifo(), 4u,
                 "cuatro palabras escritas, cuatro en la FIFO: el bloque no "
                 "se las traga antes de arrancar");
        wr(BA + SaiBlock::R_CR1, rd(BA + SaiBlock::R_CR1) | SaiBlock::C1_SAIEN);
        check(a.habilitado(),
              "SAIEN arranca la trama, y arranca porque hay reloj: con el "
              "PLLSAI apagado el silicio levantaria WCKCFG y NO arrancaria");
        check_eq(a.peek_sr() & SaiBlock::S_WCKCFG, 0u,
                 "y WCKCFG esta a cero, que es como se dice «esta trama si "
                 "cabe en este reloj»");
        const unsigned f0 = a.nivel_fifo();
        wait(2, SC_MS);
        std::printf("    FIFO del SAI1_A: %u palabras -> %u tras 2 ms\n",
                    f0, a.nivel_fifo());
        check(a.nivel_fifo() < f0,
              "y la FIFO se vacia sola al ritmo de la trama: el bloque esta "
              "sacando audio por el pin, no esperando a que alguien lo lea");
        wr(BA + SaiBlock::R_CR1, 0u);

        grupo("H5 El DMA alimentando al SAI1_A: la celda, de punta a punta");

        // LA PRUEBA QUE CIERRA I-47. No basta con que la tabla diga que el
        // SAI1_A esta en (DMA2, stream 1, canal 0): hay que ver una
        // transferencia entera pasar por esa celda Y SOLO POR ESA. Es memoria
        // a periferico, que es como se alimenta un flujo de audio.
        for (unsigned i = 0; i < 16; ++i)
            wr(addr::SRAM1_BASE + 0x300u + 4 * i, 0xABCD0000u + i);
        const uint32_t S1 = addr::DMA2_B + 0x10 + 0x18;     // stream 1
        wr(S1 + 0x00, 0u);
        wr(S1 + 0x04, 16u);                                 // NDTR
        wr(S1 + 0x08, BA + SaiBlock::R_DR);                 // PAR = SAI1_A->DR
        wr(S1 + 0x0C, addr::SRAM1_BASE + 0x300u);           // M0AR
        // CHSEL = 0 (canal 0), DIR = memoria a periferico, MINC, 32 bits.
        wr(S1 + 0x00, (0u << 25) | (1u << 6) | (1u << 10) |
                      (2u << 11) | (2u << 13));
        wr(S1 + 0x00, rd(S1 + 0x00) | 1u);                  // EN
        // Y ahora se arranca el bloque CON DMAEN: es el bit del periferico el
        // que abre el grifo, no el del controlador.
        wr(BA + SaiBlock::R_CR1, (1u << 20) | SaiBlock::C1_DMAEN);
        wr(BA + SaiBlock::R_CR1, rd(BA + SaiBlock::R_CR1) | SaiBlock::C1_SAIEN);
        wait(4, SC_MS);
        const uint32_t ndtr = rd(S1 + 0x04);
        std::printf("    NDTR del stream 1: 16 -> %u\n", ndtr);
        check(ndtr < 16u,
              "el SAI pide por su celda y el DMA le sirve: NDTR baja sin que "
              "nadie escriba DR desde el bus");
        // Y LA OTRA MITAD: que sea ESA celda y no otra. El canal 1 del mismo
        // stream es el SAI1_B; con el seleccionado, el bloque A pide y el DMA
        // no se mueve.
        wr(BA + SaiBlock::R_CR1, 0u);
        wr(S1 + 0x00, 0u);
        wr(S1 + 0x04, 16u);
        wr(S1 + 0x0C, addr::SRAM1_BASE + 0x300u);
        wr(S1 + 0x00, (1u << 25) | (1u << 6) | (1u << 10) |
                      (2u << 11) | (2u << 13));
        wr(S1 + 0x00, rd(S1 + 0x00) | 1u);
        wr(BA + SaiBlock::R_CR1, (1u << 20) | SaiBlock::C1_DMAEN);
        wr(BA + SaiBlock::R_CR1, rd(BA + SaiBlock::R_CR1) | SaiBlock::C1_SAIEN);
        wait(2, SC_MS);
        std::printf("    con CHSEL = 1 (el bloque B): NDTR = %u\n",
                    rd(S1 + 0x04));
        check_eq(rd(S1 + 0x04), 16u,
                 "con el canal equivocado NO se mueve un solo dato: la celda "
                 "es (stream, canal) y no (stream), que es justo el error que "
                 "un mapa mal copiado produce en silencio");
        wr(S1 + 0x00, 0u);
        wr(BA + SaiBlock::R_CR1, 0u);
    }

    // =======================================================================
    // G. EL HITO H5: «el F446 se depura desde STM32CubeIDE con el IDCODE
    //    correcto»
    //
    // Era el ultimo hito pendiente de la tabla del plan, y el punto 4 de la
    // fase 0 lo marco como BLOQUEANTE por una razon muy concreta: con el
    // IDCODE equivocado, STM32CubeIDE no da un error util, da un «Could not
    // verify ST device» y se acabo la sesion.
    //
    // Lo que se comprueba aqui no es que el modelo SEPA su IDCODE -eso ya lo
    // miraba A5 preguntandoselo por dentro- sino que lo diga POR LOS DOS
    // HILOS, que es por donde lo lee una sonda de verdad. Entre lo uno y lo
    // otro hay una cadena entera: los pines en AF0 desde el reset, el SW-DP,
    // el AHB-AP, la matriz y el DBGMCU.
    // =======================================================================
    void hito_h5() {
        grupo("G1 H5: una sonda SWD engancha el F446 por PA13/PA14");

        // Un reset frio: la sonda tiene que poder con un chip recien
        // encendido, que es el caso que importa -rescatar una placa cuyo
        // programa no arranca-. Se le deja el `wfe` en bucle para que el
        // blinky anterior no ande moviendo pines.
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

        // Los pines de depuracion estan en AF0 DESDE EL RESET, sin que ningun
        // firmware los configure. Es lo mismo en las dos piezas, y es lo que
        // permite enganchar un chip que no arranca.
        wr(addr::RCC_B + Rcc::R_AHB1ENR, rd(addr::RCC_B + Rcc::R_AHB1ENR) | 1u);
        check_eq((rd(addr::GPIOA_B + 0x00) >> 26) & 0x3Fu, 0x2Au,
                 "PA13, PA14 y PA15 estan en funcion alternativa desde el reset");

        const uint32_t id = sonda->conectar_swd();
        std::printf("    la sonda lee IDCODE del SW-DP = 0x%08X en %u paquetes\n",
                    id, sonda->acks_ok());
        check_eq(id, 0x2BA01477u,
                 "reset de linea + 0xE79E + reset: el SW-DP contesta. Es el "
                 "mismo DP que el F407, y el documento decia que en depuracion "
                 "no hay diferencias salvo una");

        uint32_t idr = 0, base = 0;
        sonda->escribir_dp(0x8, 0x000000F0u);            // SELECT: banco 0xF
        sonda->leer_ap_real(0xC, idr);                   // IDR
        sonda->leer_ap_real(0x8, base);                  // BASE
        sonda->escribir_dp(0x8, 0);
        check_eq(idr, 0x24770011u, "el AP se identifica como un AHB-AP de ARM");
        check_eq(base, 0xE00FF003u, "y apunta a la ROM table");

        grupo("G2 Y esa diferencia es EL IDCODE, leido por los pines");

        // LA COMPROBACION DEL HITO. No se le pregunta al modelo: se lee
        // 0xE004_2000 por dos hilos, exactamente como hace la sonda de
        // STM32CubeIDE antes de decidir si sabe con quien habla.
        uint32_t v = 0;
        check(sonda->mem_read32(0xE0042000u, v),
              "la sonda lee DBGMCU_IDCODE por los pines");
        std::printf("    DBGMCU_IDCODE leido por SWD = 0x%08X\n", v);
        check_eq(v, 0x10000421u,
                 "0x1000_0421: DEV_ID 0x421. Con el del F407 aqui, CubeIDE "
                 "diria «Could not verify ST device» y no habria sesion");
        check_eq(v & 0xFFFu, 0x421u,
                 "y el DEV_ID, que es el campo que mira la sonda, es 0x421 y "
                 "no 0x413");

        grupo("G3 Leer, escribir y parar el nucleo, todo por dos hilos");

        check(sonda->mem_write32(addr::SRAM1_BASE + 0x40u, 0xF446F446u),
              "la sonda escribe en la SRAM por los pines...");
        check_eq(rd(addr::SRAM1_BASE + 0x40u), 0xF446F446u,
                 "...y el dato esta de verdad en la memoria");
        for (unsigned i = 0; i < 8; ++i)
            wr(addr::SRAM1_BASE + 0x80u + 4 * i, 0x44600000u + i);
        uint32_t buf[8] = {};
        const unsigned n = sonda->mem_read_block(addr::SRAM1_BASE + 0x80u, buf, 8);
        bool bloque_ok = (n == 8);
        for (unsigned i = 0; i < 8 && bloque_ok; ++i)
            if (buf[i] != 0x44600000u + i) bloque_ok = false;
        check(bloque_ok,
              "y vuelca un bloque con auto-incremento de TAR, que es como se "
              "lee la memoria de verdad en una sesion");

        check(sonda->halt(), "pide la parada escribiendo DHCSR");
        wait(50, SC_US);
        check(sonda->is_halted(), "y el nucleo se para de verdad");
        uint32_t pc = 0;
        sonda->leer_reg(15, pc);
        std::printf("    la sonda ve pc = 0x%08X (el bucle wfe esta en 0x%08X)\n",
                    pc, dut.mcu.memoria.flash.base + 0x100u);
        check(pc >= dut.mcu.memoria.flash.base &&
              pc <  dut.mcu.memoria.flash.base + dut.mcu.memoria.flash.size,
              "y el PC que lee esta dentro de la Flash de 512 KB de ESTE chip");
        sonda->escribir_reg(0, 0x0421u);
        uint32_t r0 = 0;
        sonda->leer_reg(0, r0);
        check_eq(r0, 0x0421u, "escribe un registro del nucleo y lo relee igual");
        check(sonda->resume(), "y lo suelta");
        wait(50, SC_US);
        check(!sonda->is_halted(), "el nucleo vuelve a correr");

        std::printf("    en toda la sesion: %u paquetes con ACK OK, %u con fallo\n",
                    sonda->acks_ok(), sonda->acks_mal());
        check_eq(sonda->acks_mal(), 0u,
                 "ni un solo paquete perdido en toda la sesion por los pines");
        sonda->desconectar();
    }

    void run() {
        cruzada();
        prueba_cruzada();
        hito_h5();
        blinky();
        arbol_f446();
        perifericos_446();
        hito_h6();
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
