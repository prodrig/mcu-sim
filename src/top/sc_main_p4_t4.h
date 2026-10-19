// =============================================================================
// sc_main_p4_t4.h — Los grupos de testp4 de la fase T4: la ROM del P4
//
// Se incluye desde top/sc_main_p4.cpp, como sc_main_p4_t3.h. El cierre de T4
// [doc/analisis_msp432.md, §8]:
//
//   * un ejemplo del SDK SIN MODIFICAR, que usa MAP_, funciona con R1
//     (P63-P65: cuatro, de verif/fw/msp432p4/sdk_ejemplos);
//   * con la ROM de TI (R2), el mismo ejemplo funciona, y se anota la
//     diferencia de tiempo simulado entre R1 y R2. La ROM de TI no se
//     versiona, asi que eso no puede ir en la suite: va aparte, con
//     `TESTP4_SOLO=rom_ti P4_ROM_TI=<fichero>` (t4_rom_ti). En la suite, P66
//     comprueba el MECANISMO de R2 -cargar de fichero, binario o ELF- con la
//     propia R1 como fichero, y R3;
//   * un ejemplo con FreeRTOS arranca y conmuta tareas (P67).
//
// Y el TLV por fichero, en P64.
// =============================================================================
#ifndef STM32_TOP_SC_MAIN_P4_T4_H
#define STM32_TOP_SC_MAIN_P4_T4_H

#include <fstream>

static const char* const FW_SDK_BLINK =
    "verif/fw/msp432p4/sdk_ejemplos/systick_interrupt_gpio_blink/systick_interrupt_gpio_blink.out";
static const char* const FW_SDK_DCO =
    "verif/fw/msp432p4/sdk_ejemplos/cs_dco_frequency_tune/cs_dco_frequency_tune.out";
static const char* const FW_SDK_T32 =
    "verif/fw/msp432p4/sdk_ejemplos/timer32_periodic_mode_led_toggle/"
    "timer32_periodic_mode_led_toggle.out";
static const char* const FW_SDK_UART =
    "verif/fw/msp432p4/sdk_ejemplos/uart_pc_echo_12mhz_brclk/uart_pc_echo_12mhz_brclk.out";
static const char* const FW_RTOS = "verif/fw/msp432p4/freertos_blinky/freertos_blinky.out";
static const char* const ROM_R1_BIN = "verif/fw/msp432p4/rom_r1/rom_r1.bin";
static const char* const ROM_R1_ELF = "verif/fw/msp432p4/rom_r1/rom_r1.out";

// Espera el primer cambio de P1.0 (el LED1 de la LaunchPad) y devuelve el
// tiempo desde `t0`; SC_ZERO_TIME si no llega antes de `max_ms`. Se llama con
// t0 <= ahora: un cambio en el mismo instante que t0 tambien devuelve cero.
// t0 va POR VALOR: sc_time_stamp() devuelve una referencia a la hora del
// nucleo, que avanza, y con una referencia t0 seria siempre «ahora».
inline sc_core::sc_time TbP4::primer_cambio(sc_core::sc_time t0, double max_ms) {
    const sc_time t_lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
    // La tension del nodo, no dut.dio.nivel(): esa va un delta por detras
    const bool ant = dut.dio.nodo_de(0, 0).voltage() > 1.65f;
    while (sc_time_stamp() < t_lim) {
        wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
        if ((dut.dio.nodo_de(0, 0).voltage() > 1.65f) != ant) return sc_time_stamp() - t0;
    }
    return SC_ZERO_TIME;
}

// Cuanto tiempo esta en alto P1.0 desde el proximo flanco de subida
inline double TbP4::ancho_alto(double max_ms) {
    const sc_time t_lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
    auto alto = [this] { return dut.dio.nodo_de(0, 0).voltage() > 1.65f; };
    while (alto() && sc_time_stamp() < t_lim)
        wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
    while (!alto() && sc_time_stamp() < t_lim)
        wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
    if (!alto()) return 0.0;
    const sc_time t0 = sc_time_stamp();
    while (alto() && sc_time_stamp() < t_lim)
        wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
    return alto() ? 0.0 : (sc_time_stamp() - t0).to_seconds();
}

// =============================================================================
// P62  La ROM R1, por dentro
// =============================================================================
inline void TbP4::t4_rom() {
    grupo("P62 La ROM R1: DriverLib en 0x0200_0000 con las tablas de rom.h");
    dut.rom.pon_r1();
    std::printf("    origen: %s; huella 0x%08X\n", dut.rom.nombre_origen(), dut.rom.huella());
    check(dut.rom.origen() == RomP4::Origen::R1 && dut.rom.huella() == rom_r1::FNV,
          "por omision la ventana lleva la R1, y su huella es la de rom_r1.bin");
    // ROM_APITABLE: la version y 28 tablas, todas dentro de la ventana
    const uint32_t ver = rd(p4::ROM_B + 0x800);
    bool dentro = true;
    for (unsigned k = 1; k <= 28; ++k) {
        const uint32_t t = rd(p4::ROM_B + 0x800 + 4 * k);
        if (t < p4::ROM_B || t >= p4::ROM_B + p4::ROM_SIZE) dentro = false;
    }
    check(ver == 1 && dentro,
          "ROM_APITABLE (0x0200_0800): ROM_VERSION = 1 y 28 tablas dentro de la ROM");
    // ROM_GPIOTABLE es la 9; GPIO_setAsOutputPin, su casilla 0 [rom.h]
    const uint32_t gpio = rd(p4::ROM_B + 0x800 + 4 * 9);
    const uint32_t f = rd(gpio);
    check((f & 1u) && f >= p4::ROM_B && f < p4::ROM_B + p4::ROM_SIZE,
          "ROM_GPIOTABLE[0] (GPIO_setAsOutputPin) apunta a codigo Thumb de la ROM");
    // Escribir: error de bus, y la imagen no cambia
    const unsigned m0 = dut.rom.fallos();
    check(tm.write32(p4::ROM_B + 0x800, 0x12345678u) == tlm::TLM_ADDRESS_ERROR_RESPONSE &&
          dut.rom.fallos() == m0 + 1 && rd(p4::ROM_B + 0x800) == 1u,
          "escribir en la ROM es un error de bus, y no la cambia");
}

// =============================================================================
// P63-P65  Ejemplos del SDK sin modificar, con MAP_, contra la R1
// =============================================================================
inline void TbP4::t4_sdk() {
    grupo("P63 SDK systick_interrupt_gpio_blink, sin modificar: MAP_ por la ROM R1");
    dut.rom.pon_r1();
    const uint64_t i0 = dut.rom.instrucciones();
    arranca(0, FW_SDK_BLINK);
    const double per = periodo(0, 0, 1, 2000);
    std::printf("    P1.0: periodo %.6f s; %llu lecturas de instruccion de la ROM\n", per,
                (unsigned long long)(dut.rom.instrucciones() - i0));
    check(std::fabs(per - 1.0) < 1e-4,
          "P1.0 cambia cada 1 500 000 ciclos de MCLK (3 MHz): periodo de 1 s");
    check(dut.rom.instrucciones() - i0 >= 20,
          "y el codigo que lo hace se lee de la ROM: MAP_ salta por ROM_APITABLE");

    grupo("P64 SDK cs_dco_frequency_tune: CS_setDCOFrequency(8,33 MHz) y el TLV de fichero");
    // Con el TLV sintetico
    arranca(0, FW_SDK_DCO);
    const double p_sint = periodo(0, 0, 1, 2000);
    const uint32_t tune_sint = rd(p4::CS_B + 0x04) & 0x1FFFu;
    std::printf("    TLV sintetico: DCOTUNE = %u, P1.0 cada %.6f s\n", tune_sint, p_sint);
    check(std::fabs(p_sint - 1.0) < 1e-3 && tune_sint != 0,
          "CS_setDCOFrequency calcula DCOTUNE con el TLV, y MCLK sale a 8,33 MHz (+-0,1 %)");
    // Un TLV de otra pieza: el mismo, con otra calibracion del DCO. Se
    // escribe en build/, que es de quien compila
    const char* ruta = "build/p4_tlv_otra_pieza.bin";
    {
        std::vector<uint32_t> t(1024);
        for (unsigned i = 0; i < 1024; ++i) t[i] = dut.flash.peek32(p4::TLV_B + 4 * i);
        t[19] = 340;                                  // DCOIR_FCAL_RSEL04: 400 -> 340
        std::ofstream o(ruta, std::ios::binary);
        o.write(reinterpret_cast<const char*>(t.data()), std::streamsize(t.size() * 4));
    }
    check(mcu.opcion("tlv", ruta).empty() && mcu.describe_opcion("tlv") == ruta,
          "--tlv carga el TLV de una placa (4 KB de 0x0020_1000)");
    arranca(0, FW_SDK_DCO);
    const double p_otra = periodo(0, 0, 1, 2000);
    const uint32_t tune_otra = rd(p4::CS_B + 0x04) & 0x1FFFu;
    std::printf("    TLV de otra pieza: DCOTUNE = %u, P1.0 cada %.6f s\n", tune_otra, p_otra);
    check(tune_otra != tune_sint && std::fabs(p_otra - 1.0) < 1e-3,
          "con otra calibracion, otro DCOTUNE, y la MISMA frecuencia: el DCO del modelo "
          "sigue la calibracion de esa pieza, como el silicio");
    // Un TLV que no lo es
    {
        std::vector<uint32_t> t(1024, 0xFFFFFFFFu);
        t[0] = 0; t[1] = 11; t[2] = 4000;            // DEVINFO con una longitud absurda
        std::ofstream o("build/p4_tlv_roto.bin", std::ios::binary);
        o.write(reinterpret_cast<const char*>(t.data()), std::streamsize(t.size() * 4));
    }
    const std::string e = mcu.opcion("tlv", "build/p4_tlv_roto.bin");
    std::printf("    TLV roto: %s\n", e.c_str());
    check(!e.empty() && mcu.describe_opcion("tlv") == ruta,
          "uno cuya cadena de etiquetas no llega al fin se rechaza, y se queda el anterior");
    check(mcu.opcion("tlv", "sintetico").empty() && mcu.describe_opcion("tlv") == "sintetico" &&
          dut.flash.peek32(p4::TLV_B + 19 * 4) == 400u,
          "y `sintetico` devuelve el de omision");

    grupo("P65 SDK timer32_periodic_mode_led_toggle y uart_pc_echo_12mhz_brclk");
    // Con MCLK = 128 kHz, el arranque y las diez llamadas MAP_ duran decenas
    // de milisegundos: se pulsa cuando ya duerme en LPM0. El ejemplo no
    // escribe P1IES, cuyo valor tras el reset el TRM da por indefinido; el
    // modelo lo deja a 0, flanco de SUBIDA, asi que la interrupcion llega al
    // SOLTAR S1 (en una placa puede ser al pulsar: depende de la pieza)
    arranca(0, FW_SDK_T32);
    wait(200, SC_MS);
    const bool antes = dut.dio.nivel(0, 0);
    s1->press();
    wait(10, SC_MS);
    const bool pulsado = dut.dio.nivel(0, 0);
    s1->release();
    const sc_time t_suelta = sc_time_stamp();
    const sc_time t_on = primer_cambio(t_suelta, 50);
    const sc_time t_dur = primer_cambio(sc_time_stamp(), 1500);
    std::printf("    al soltar S1, P1.0 se enciende en %.3f ms y se apaga %.6f s despues\n",
                t_on.to_seconds() * 1e3, t_dur.to_seconds());
    check(!antes && !pulsado && t_on > SC_ZERO_TIME && t_on < sc_time(5, SC_MS),
          "S1 despierta al nucleo de LPM0 por PORT1 (con P1IES = 0, al soltar) y enciende el LED");
    // Mas lo que tarda la ISR, a 128 kHz, desde encender el LED hasta
    // arrancar el Timer32 (setCount, enableInterrupt, startTimer): ~1 ms
    check(t_dur.to_seconds() > 1.0 && t_dur.to_seconds() < 1.002,
          "y Timer32 lo apaga 128 000 ciclos de MCLK = REFO a 128 kHz despues: 1 s, mas "
          "el resto de la ISR");

    bcl->set_baudios(9600);
    bcl->set_formato(FormatoUart{});
    bcl->borra_recibido();
    bcl->set_enabled(true);
    arranca(0, FW_SDK_UART);
    wait(5, SC_MS);
    bcl->envia("Hola, SDK");
    wait(15, SC_MS);
    std::printf("    eco: \"%s\"\n", bcl->recibido().c_str());
    check(bcl->recibido() == "Hola, SDK" && bcl->errores_trama() == 0,
          "el eco a 9600 con SMCLK = 12 MHz (BRDIV 78, BRF 2), sin errores de trama");
    bcl->set_enabled(false);
    bcl->set_baudios(115200);
}

// =============================================================================
// P66  R3 y R2 (el mecanismo, con la propia R1 como fichero)
// =============================================================================
inline void TbP4::t4_r2_r3() {
    grupo("P66 --rom ninguna (R3) y --rom FICHERO (R2): binario y ELF");
    check(mcu.opcion("rom", "ninguna").empty(), "--rom ninguna");
    const unsigned m0 = dut.rom.fallos();
    arranca(0, FW_SDK_BLINK);
    const sc_time t = primer_cambio(sc_time_stamp(), 700);
    check(t == SC_ZERO_TIME && dut.rom.fallos() > m0,
          "sin ROM, el primer MAP_ da error de bus y P1.0 no se mueve");

    check(mcu.opcion("rom", ROM_R1_BIN).empty() && dut.rom.origen() == RomP4::Origen::Volcado &&
          dut.rom.huella() == rom_r1::FNV,
          "un volcado binario de 32 KB (aqui, rom_r1.bin) se carga tal cual");
    arranca(0, FW_SDK_BLINK);
    const double per = periodo(0, 0, 1, 2000);
    check(std::fabs(per - 1.0) < 1e-4, "y el ejemplo funciona igual");
    check(mcu.opcion("rom", ROM_R1_ELF).empty() && dut.rom.huella() == rom_r1::FNV,
          "un ELF con segmentos en la ventana (como el de CCS): la misma imagen");

    const std::string e1 = mcu.opcion("rom", FW_T3B);
    const std::string e2 = mcu.opcion("rom", FW_DEMO_BIN);
    std::printf("    %s\n    %s\n", e1.c_str(), e2.c_str());
    check(!e1.empty() && !e2.empty() && dut.rom.huella() == rom_r1::FNV,
          "un ELF que cae fuera de la ventana, o un binario que no llega a ROM_APITABLE, "
          "se rechazan sin tocar la ROM");
    check(mcu.opcion("rom", "r1").empty() && dut.rom.origen() == RomP4::Origen::R1,
          "y --rom r1 vuelve a la sustituta");
}

// =============================================================================
// P67  FreeRTOS
// =============================================================================
inline void TbP4::t4_freertos() {
    grupo("P67 FreeRTOS: la demo blinky de la MSP432 LaunchPad, con tickless idle");
    dut.rom.pon_r1();
    arranca(0, FW_RTOS);
    const double per = periodo(0, 0, 2, 3500);
    std::printf("    P1.0 sube cada %.6f s\n", per);
    check(std::fabs(per - 1.0) < 0.01,
          "el LED parpadea cada 1 s: la tarea que envia (vTaskDelayUntil, 1000 ms) "
          "despierta a la que recibe por la cola");
    const double ancho = ancho_alto(1500);
    std::printf("    y esta encendido %.4f s\n", ancho);
    check(std::fabs(ancho - 0.05) < 0.011, "y cada parpadeo dura 50 ms (vTaskDelay, 5 ticks)");
    // S1 saca al nucleo del bajo consumo por PORT1, y el calendario no cambia
    s1->press();
    wait(10, SC_MS);
    s1->release();
    const double per2 = periodo(0, 0, 1, 2500);
    check(std::fabs(per2 - 1.0) < 0.01,
          "pulsar S1 despierta al nucleo fuera de su tick, y el reparto sigue igual");
}

// =============================================================================
// Fuera de la suite: R1 frente a la ROM de TI
//
//   TESTP4_SOLO=rom_ti P4_ROM_TI=.../msp432_driverlib_rom_image.out ./build/testp4
//
// Los mismos firmwares con R1 y con R2. Para cada uno, el tiempo desde que se
// suelta el reset hasta que P1.0 se enciende por primera vez: un plazo fijo
// del firmware (0,5 s o 1 s) mas el arranque y lo que tardan sus llamadas
// MAP_, que es lo que cambia entre R1 y R2; y cuantas instrucciones ejecuta
// el nucleo y cuantas lecturas de instruccion van a la ROM hasta entonces.
// =============================================================================
inline void TbP4::t4_rom_ti(const char* ruta) {
    grupo("R1 frente a la ROM de TI (R2)");
    struct Medida { double t_ms; uint64_t instr, rom; uint32_t tune; };
    // Desde que se suelta el reset hasta el primer flanco de SUBIDA de P1.0.
    // inst_count lo pone a cero el propio nucleo al salir del reset
    // (Cpu::do_reset), asi que al final es el de este arranque
    auto mide = [this](const char* fw) {
        mcu.reset_pin(true);
        wait(20, SC_US);
        mcu.carga_firmware(fw);
        mcu.reset_pin(false);
        const sc_time t0 = sc_time_stamp();
        const uint64_t r0 = dut.rom.instrucciones();
        auto alto = [this] { return dut.dio.nodo_de(0, 0).voltage() > 1.65f; };
        const sc_time t_lim = sc_time_stamp() + sc_time(3, SC_SEC);
        while (alto() && sc_time_stamp() < t_lim)
            wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
        while (!alto() && sc_time_stamp() < t_lim)
            wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(0, 0).value_changed_event());
        const double t = alto() ? (sc_time_stamp() - t0).to_seconds() * 1e3 : 0.0;
        return Medida{t, dut.core.cpu.inst_count, dut.rom.instrucciones() - r0,
                      rd(p4::CS_B + 0x04) & 0x1FFFu};
    };
    const std::pair<const char*, const char*> fws[] = {
        {"systick_interrupt_gpio_blink", FW_SDK_BLINK},
        {"cs_dco_frequency_tune", FW_SDK_DCO},
        {"freertos_blinky", FW_RTOS},
    };
    Medida r1[3], r2[3];
    dut.rom.pon_r1();
    for (unsigned k = 0; k < 3; ++k) r1[k] = mide(fws[k].second);
    const std::string e = mcu.opcion("rom", ruta);
    if (!e.empty()) { check(false, e.c_str()); return; }
    std::printf("    R2: %s, huella 0x%08X\n", ruta, dut.rom.huella());
    for (unsigned k = 0; k < 3; ++k) r2[k] = mide(fws[k].second);
    std::printf("\n    %-30s %14s %14s %12s %10s %10s\n", "firmware", "R1 (ms)", "R2 (ms)",
                "R1-R2 (us)", "instr R1", "instr R2");
    for (unsigned k = 0; k < 3; ++k)
        std::printf("    %-30s %14.6f %14.6f %12.3f %10llu %10llu\n", fws[k].first,
                    r1[k].t_ms, r2[k].t_ms, (r1[k].t_ms - r2[k].t_ms) * 1e3,
                    (unsigned long long)r1[k].instr, (unsigned long long)r2[k].instr);
    std::printf("    (DCOTUNE de cs_dco_frequency_tune, R1/R2: %u/%u)\n", r1[1].tune, r2[1].tune);
    std::printf("    (lecturas de instruccion de la ROM, R1/R2:");
    for (unsigned k = 0; k < 3; ++k)
        std::printf(" %llu/%llu", (unsigned long long)r1[k].rom, (unsigned long long)r2[k].rom);
    std::printf(")\n");
    for (unsigned k = 0; k < 3; ++k)
        check(r2[k].t_ms > 0.0, (std::string(fws[k].first) + " funciona con la ROM de TI").c_str());
    dut.rom.pon_r1();
}

#endif // STM32_TOP_SC_MAIN_P4_T4_H
