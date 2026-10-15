// =============================================================================
// sc_main_p4_t3b.h — Los grupos de testp4 que usan p4_t3b (fase T3)
//
// ADC14 y REF_A, y lo que venga detrás con ese firmware. Se incluye desde
// top/sc_main_p4.cpp, como sc_main_p4_t3.h.
// =============================================================================
#ifndef STM32_TOP_SC_MAIN_P4_T3B_H
#define STM32_TOP_SC_MAIN_P4_T3B_H

// =============================================================================
// P40-P44  ADC14 y REF_A
// =============================================================================
inline void TbP4::t3_adc() {
    auto pon = [this](unsigned k, double v) { v_an[k]->set_volts(v, 10.0); };
    auto suelta = [this] { for (Driver* d : v_an) d->release(); };
    // Lo que da un convertidor ideal de `n` bits contra AVCC (el float del nodo)
    double avcc = 3.3;
    auto codigo = [](double v, double vr, unsigned n) {
        return uint32_t(std::floor(double(1u << n) * double(float(v)) / vr));
    };

    grupo("P40 ADC14: una conversion de A0 en 14, 12, 10 y 8 bits, con signo y en diferencial");
    pon(0, 1.0); pon(1, 0.5);
    arranca3(30, FW_T3B);
    avcc = double(dut.avcc->voltage()) - double(dut.avss->voltage());
    check(espera3(1, 5), "el firmware hace sus seis conversiones");
    std::printf("    A0 = 1,0 V contra AVCC = %.3f V: %u %u %u %u; con signo %d; A0-A1 %u\n",
                avcc, r3(0), r3(1), r3(2), r3(3), int32_t(r3(4)), r3(5));
    check(r3(0) == codigo(1.0, avcc, 14) && r3(1) == codigo(1.0, avcc, 12) &&
          r3(2) == codigo(1.0, avcc, 10) && r3(3) == codigo(1.0, avcc, 8),
          "N = 2^n * Vin / AVCC, truncado, en las cuatro resoluciones");
    check(int32_t(r3(4)) == (int32_t(codigo(1.0, avcc, 14)) - 8192) * 4,
          "ADC14DF = 1: complemento a 2, alineado a la izquierda");
    check(r3(5) == uint32_t(std::floor(8192.0 * (1.0 + 0.5 / avcc))),
          "en diferencial, A0 - A1 = 0,5 V: 2^13 * (1 + dV / VR)");
    check(r3(6) == 0, "con ENC = 1, MCTLn no se deja escribir");

    grupo("P41 ADC14: secuencia A0..A3 con MSC a SMCLK/32, por interrupcion e IV");
    pon(0, 1.0); pon(1, 0.5); pon(2, 2.0); pon(3, 3.0);
    arranca3(31, FW_T3B);
    check(espera3(1, 10), "la secuencia acaba y la interrupcion de MEM3 llega");
    std::printf("    %u %u %u %u; IV 0x%02X; %u tics del Timer32 (128 ciclos de ADC: 4096)\n",
                r3(1), r3(2), r3(3), r3(4), r3(5), r3(0));
    check(r3(1) == codigo(1.0, avcc, 14) && r3(2) == codigo(0.5, avcc, 14) &&
          r3(3) == codigo(2.0, avcc, 14) && r3(4) == codigo(3.0, avcc, 14),
          "cada memoria con su canal, hasta la de EOS");
    check(r3(5) == 0x12, "ADC14IV = 0x12: IFG3, el unico habilitado");
    check(r3(0) >= 4096 && r3(0) < 4096 + 300,
          "lo que tarda: 4 x (16 de muestreo + 16 de conversion) a 93,75 kHz, mas el "
          "camino del firmware");
    check(r3(6) == 0, "leer MEMn baja su IFG");

    grupo("P42 REF_A a 2,5 V: el sensor de temperatura, AVCC/2, A0 contra 1,2 V y VREF+");
    suelta();
    pon(0, 1.0);
    dut.adc.temperatura_c = 41.0;
    arranca3(32, FW_T3B);
    check(espera3(1, 10), "el firmware hace sus tres medidas");
    std::printf("    %u vueltas hasta GENRDY; REFCTL0 = 0x%04X; sensor %u -> %.2f C; "
                "AVCC/2 %u; A0/1,2 V %u\n", r3(0), r3(1), r3(2), r3(3) / 100.0, r3(4), r3(5));
    check(r3(0) > 3, "la referencia tarda en asentar: GENRDY no sube en el acto");
    // El firmware escribe el registro entero: REFTCOFF, que sale del reset a 1, queda a 0
    check(r3(1) == 0x3333u, "REFCTL0: ON, OUT, 2,5 V, GENACT, BGACT, GENRDY y BGRDY");
    check(int32_t(r3(3)) >= 4090 && int32_t(r3(3)) <= 4110,
          "la cuenta de los ejemplos de TI con la calibracion del TLV da los 41 C del chip");
    check(r3(4) == 8192, "BATMAP: A23 es AVCC/2, la mitad de la escala");
    check(r3(5) == codigo(1.0, 1.2, 14), "VRSEL = 1 con 1,2 V: A0 = 1,0 V es 2^14 / 1,2");
    check(espera3(2, 5), "y vuelve a 2,5 V con REFOUT");
    wait(10, SC_US);
    const double vref_pin = dut.dio.nodo_de(4, 6).voltage();
    std::printf("    VREF+ en P5.6: %.3f V\n", vref_pin);
    check(std::fabs(vref_pin - 2.5) < 0.01, "VREF+ sale por P5.6 con REFOUT = 1");
    dut.adc.temperatura_c = 25.0;

    grupo("P43 ADC14 disparado por TA0.1 a 1 kHz, en canal unico repetido");
    pon(0, 1.0);
    arranca3(33, FW_T3B);
    check(espera3(1, 5), "el firmware arma el ADC y el temporizador");
    wait(5500, SC_US);
    const uint32_t n5 = r3(0);
    pon(0, 2.0);
    wait(5000, SC_US);
    const uint32_t n10 = r3(0);
    std::printf("    conversiones: %u a los 5,5 ms, %u a los 10,5 ms; la 3.a %u, la 8.a %u\n",
                n5, n10, r3(1 + 2), r3(1 + 8));
    check(n5 >= 5 && n5 <= 6 && n10 >= 10 && n10 <= 11,
          "una conversion por cada flanco de subida de TA0.1: una por milisegundo");
    check(r3(1 + 2) == codigo(1.0, avcc, 12) && r3(1 + 8) == codigo(2.0, avcc, 12),
          "y cada una ve la tension del momento");

    grupo("P44 ADC14: comparador de ventana, desbordamiento e IV");
    pon(0, 0.5);
    arranca3(34, FW_T3B);
    const double vs[3] = {0.5, 1.0, 2.0};
    for (uint32_t k = 0; k < 3; ++k) {
        pon(0, vs[k]);
        wait(10, SC_US);
        dut.sram.poke32(0x0C, k + 1);
        espera3(k + 1, 5);
    }
    check(espera3(4, 10), "el firmware llega al final");
    std::printf("    0,5 V: %u IFGR1 0x%02X; 1,0 V: %u 0x%02X; 2,0 V: %u 0x%02X; OV 0x%02X; "
                "IV %02X %02X %02X %02X\n", r3(0), r3(1), r3(2), r3(3), r3(4), r3(5), r3(6),
                r3(7), r3(8), r3(9), r3(10));
    check(r3(1) == Adc14P4::I_LO && r3(3) == Adc14P4::I_IN && r3(5) == Adc14P4::I_HI,
          "LO0 = 4000, HI0 = 6000: por debajo, dentro y por encima");
    check(r3(6) == Adc14P4::I_OV, "una conversion sobre una MEM0 sin leer levanta OVIFG");
    check(r3(7) == 0x02 && r3(8) == 0x06 && r3(9) == 0x0C && r3(10) == 0,
          "el IV: OV, HI y luego IFG0; y leerlo los va bajando");
    suelta();
}

// =============================================================================
// P45-P48  uDMA
// =============================================================================
inline void TbP4::t3_dma() {
    grupo("P45 uDMA: memoria a memoria en automatico, DMA_INT0 y un error de bus");
    const uint64_t n0 = dut.udma.transferencias();
    arranca3(40, FW_T3B);
    check(espera3(1, 5), "el firmware llega al final");
    std::printf("    %u palabras bien; INT0_SRCFLG 0x%02X; control 0x%04X; ENA %u; "
                "errores %u; %llu transferencias\n", r3(0), r3(1), r3(2), r3(3), r3(4),
                (unsigned long long)(dut.udma.transferencias() - n0));
    check(r3(0) == 64, "las 64 palabras copiadas: una peticion basta en modo automatico");
    check(r3(1) == (1u << 3), "al acabar, el canal 3 levanta su bit de INT0_SRCFLG");
    check(r3(2) == 0 && r3(3) == 0,
          "y la DMA deja en la tabla cycle_ctrl = 0 y n_minus_1 = 0, y apaga el canal");
    check(r3(4) == 1 && r3(5) == 0, "con la tabla donde no hay memoria: DMA_ERR, y el canal se apaga");

    grupo("P46 uDMA con la UCA0: TX por el canal 0 y RX por el 1, en UCLISTEN");
    arranca3(41, FW_T3B);
    check(espera3(1, 5), "los 32 caracteres dan la vuelta");
    std::printf("    %u bien; INT0_SRCFLG 0x%02X; ENA %u\n", r3(0), r3(1), r3(2));
    check(r3(0) == 32, "lo recibido por DMA es lo enviado por DMA");
    check(r3(1) == 1u, "el canal 0 acaba por INT0; el 1, mapeado a DMA_INT1, no deja flag");
    check(r3(2) == 0, "y los dos canales acaban apagados");

    grupo("P47 uDMA con el ADC14: 16 resultados de MEM0 al canal 7");
    v_an[0]->set_volts(1.0, 10.0);
    arranca3(42, FW_T3B);
    check(espera3(1, 5), "el canal 7 acaba");
    bool igual = true;
    const uint32_t esperado = uint32_t(std::floor(4096.0 * double(1.0f) / 3.3));
    for (unsigned i = 0; i < 16; ++i) if (r3(i) != esperado) igual = false;
    std::printf("    %u %u ... %u (esperado %u); INT0_SRCFLG 0x%02X\n", r3(0), r3(1), r3(15),
                esperado, r3(16));
    check(igual, "cada lectura de MEM0 por la DMA es una conversion: las 16, el codigo de 1,0 V");
    check(r3(16) == 0x80u, "y el fin del canal 7, en INT0_SRCFLG");

    grupo("P48 uDMA en ping-pong con el ADC14 disparado por TA0.1");
    arranca3(43, FW_T3B);
    check(espera3(1, 5), "el firmware arma la DMA, el ADC y el temporizador");
    wait(9, SC_MS);
    v_an[0]->set_volts(2.0, 10.0);
    wait(8, SC_MS);
    std::printf("    %u mitades; la 1.a %u %u %u %u (de %u); la 4.a %u (de %u)\n", r3(0),
                r3(1), r3(2), r3(3), r3(4), r3(30), r3(13), r3(33));
    const uint32_t c1 = uint32_t(std::floor(4096.0 * double(1.0f) / 3.3));
    const uint32_t c2 = uint32_t(std::floor(4096.0 * double(2.0f) / 3.3));
    check(r3(0) == 4, "16 ms a 1 kHz son cuatro mitades de cuatro muestras");
    check(r3(30) == 0 && r3(31) == 1 && r3(32) == 0 && r3(33) == 1,
          "primaria, alternativa, primaria, alternativa: la DMA cambia sola de estructura");
    check(r3(1) == c1 && r3(4) == c1 && r3(13) == c2 && r3(16) == c2,
          "y cada mitad trae las muestras de su momento");
    v_an[0]->release();
}

// =============================================================================
// P49-P52  CRC32, AES256, RTC_C y COMP_E
// =============================================================================
inline void TbP4::t3_resto() {
    grupo("P49 CRC32: CRC-CCITT y CRC32 de \"123456789\", por DI y por DIRB");
    arranca3(50, FW_T3B);
    check(espera3(1, 5), "el firmware hace sus cinco firmas");
    std::printf("    CCITT 0x%04X; reflejado 0x%04X; CRC32 0x%08X; 8 bytes 0x%08X / 0x%08X\n",
                r3(0), r3(1), r3(2), r3(3), r3(4));
    check(r3(0) == 0x29B1u, "por DIRB16 con semilla 0xFFFF: el CRC-CCITT de la norma, 0x29B1");
    check(r3(1) == 0x6F91u, "por DI16, leido en RESR16: el reflejado (X.25 sin el XOR final)");
    check(r3(2) == 0xCBF43926u, "por DI32, ~RESR32: el CRC32 de Ethernet y zlib, 0xCBF43926");
    check(r3(3) == r3(4), "dos medias palabras dan lo mismo que cuatro bytes, el bajo primero");

    grupo("P50 AES256: los vectores de la FIPS-197, por la secuencia de DriverLib");
    arranca3(51, FW_T3B);
    check(espera3(1, 5), "el firmware cifra, descifra y vuelve a cifrar");
    static const uint8_t c128[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    static const uint8_t c256[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                     0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    bool ok128 = true, okd = true, ok256 = true;
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t w0 = dut.sram.peek32(0x100 + (i & ~3u));
        const uint32_t w1 = dut.sram.peek32(0x110 + (i & ~3u));
        const uint32_t w2 = dut.sram.peek32(0x120 + (i & ~3u));
        if (uint8_t(w0 >> (8 * (i & 3u))) != c128[i]) ok128 = false;
        if (uint8_t(w1 >> (8 * (i & 3u))) != uint8_t(0x11 * i)) okd = false;
        if (uint8_t(w2 >> (8 * (i & 3u))) != c256[i]) ok256 = false;
    }
    std::printf("    vueltas con BUSY: %u cifrando, %u generando la clave, %u con 256 bits; "
                "ERRFG 0x%X; %u interrupciones\n", r3(0), r3(1), r3(3), r3(4), r3(5));
    check(ok128, "AES-128 (FIPS-197 C.1): 69c4e0d8...70b4c55a");
    check(okd, "OP = 2 genera la clave de descifrado y OP = 3 descifra: vuelve el texto");
    check(r3(2) == 0, "y cifrar con OP = 0 sin recargar la clave usa la de descifrado: otra cosa");
    check(ok256, "AES-256 (FIPS-197 C.3): 8ea2b7ca...4b496089");
    check(r3(0) > 0 && r3(3) > r3(0) && r3(1) > 0,
          "BUSY dura: mas con 256 bits que con 128, y tambien al generar la clave");
    check(r3(4) != 0, "escribir DIN con el AES ocupado levanta ERRFG");
    check(r3(5) >= 4, "RDYIFG con RDYIE: una interrupcion por bloque");

    grupo("P51 RTC_C en BCD: de las 23:59:58 a la medianoche, con eventos y alarma");
    arranca3(52, FW_T3B);
    check(espera3(1, 5), "el firmware pone la hora y se duerme");
    std::printf("    clave 0x%02X; HOLD con la clave cerrada %u; BIN2BCD(1234) 0x%04X; "
                "BCD2BIN(0x0987) %u\n", r3(0), r3(1), r3(2), r3(3));
    check(r3(0) == 0x96 && r3(1) == 0, "cerrado (RTCKEY se lee 0x96), CTL13 no cambia");
    check(r3(2) == 0x1234 && r3(3) == 987, "los conversores BIN2BCD y BCD2BIN");
    check(espera3(2, 3200), "tres segundos despues, tres RTCRDY");
    std::printf("    TIM0/TIM1/DATE: %04X %04X %04X | %04X %04X %04X | %04X %04X %04X; "
                "TEV %u, alarma %u, RT0PS %u, ano %04X\n",
                r3(4), r3(5), r3(6), r3(7), r3(8), r3(9), r3(10), r3(11), r3(12),
                r3(16), r3(17), r3(18), r3(19));
    check(r3(4) == 0x5959 && r3(5) == 0x0423 && r3(6) == 0x1015,
          "un segundo: 23:59:59 del jueves 15");
    check(r3(7) == 0x0000 && r3(8) == 0x0500 && r3(9) == 0x1016,
          "dos: medianoche, viernes 16, en BCD");
    check(r3(16) == 1 && r3(17) == 1, "a medianoche, el evento (RTCTEV = 2) y la alarma de las 00:00");
    check(r3(18) >= 380 && r3(18) <= 400, "RT0PS cada 256 tics de BCLK: 128 por segundo");
    check(r3(19) == 0x2026, "y el ano no se mueve");

    grupo("P52 COMP_E0: C0.2 contra la escalera de VCC, con histeresis");
    v_ce->set_volts(1.0, 10.0);
    arranca3(53, FW_T3B);
    check(espera3(1, 5), "el comparador arranca");
    const double vs[5] = {1.0, 1.8, 2.2, 1.8, 1.5};
    bool pin_ok = true;
    for (uint32_t k = 0; k < 5; ++k) {
        v_ce->set_volts(vs[k], 10.0);
        wait(10, SC_US);
        if (dut.dio.nivel(6, 1) != (k == 2 || k == 3)) pin_ok = false;
        dut.sram.poke32(0x0C, k + 1);
        espera3(k + 2, 5);
    }
    std::printf("    OUT: %u %u %u %u %u; subidas %u, bajadas %u; umbral ahora %.3f V\n",
                r3(0), r3(3), r3(6), r3(9), r3(12), r3(13), r3(14), dut.ce0.umbral());
    check(r3(0) == 0 && r3(3) == 0 && r3(6) == 1 && r3(9) == 1 && r3(12) == 0,
          "sube al pasar de 20/32 de VCC y baja al bajar de 16/32: la histeresis de REF1 y REF0");
    check(r3(13) == 1 && r3(14) == 1, "CEIFG en la subida y CEIIFG en la bajada, por CEIV");
    check(pin_ok, "y C0OUT sale por P7.1");
    v_ce->release();
}

// =============================================================================
// P53-P55  Los modos de bajo consumo y el consumo
// =============================================================================
inline void TbP4::t3_lpm() {
    auto ua = [this] { return dut.idd_medida() * 1e6; };

    grupo("P53 Activo, LPM0, LPM3 y LPM4: que se para y cuanto consume");
    arranca3(60, FW_T3B);
    check(espera3(1, 5), "el firmware arranca, activo a 3 MHz");
    wait(100, SC_US);
    const double i_act = ua();
    dut.sram.poke32(0x0C, 1);
    check(espera3(2, 5), "y se duerme en LPM0");
    wait(5, SC_MS);
    const unsigned m0 = dut.modo_lpm();
    const double i_lpm0 = ua();
    check(espera3(3, 40), "tras tres despertares, a LPM3");
    wait(5, SC_MS);
    const unsigned m3 = dut.modo_lpm();
    const double i_lpm3 = ua();
    const double smclk_lpm3 = dut.cs.f_smclk(), aclk_lpm3 = dut.cs.f_aclk_rtc_wdt(),
                 bclk_lpm3 = dut.cs.f_bclk();
    check(espera3(4, 60), "tras otros tres, a LPM4");
    wait(1, SC_MS);
    const unsigned m4 = dut.modo_lpm();
    const double i_lpm4 = ua();
    s1->press();
    check(espera3(5, 5), "S1 lo despierta de LPM4");
    s1->release();
    std::printf("    IDD: activo %.1f uA, LPM0 %.1f uA, LPM3 %.3f uA, LPM4 %.3f uA "
                "(modos %u %u %u); en LPM3 SMCLK %.0f Hz y ACLK (RTC/WDT) %.0f Hz; el Timer32 "
                "avanzo %u tics en 30 ms de LPM3\n", i_act, i_lpm0, i_lpm3, i_lpm4,
                m0, m3, m4, smclk_lpm3, aclk_lpm3, r3(0));
    check(std::fabs(i_act - 340.0) < 5.0, "activo en LDO/VCORE0 a 3 MHz: 40 + 100 uA/MHz");
    check(m0 == 1 && std::fabs(i_lpm0 - 136.0) < 5.0, "WFI sin SLEEPDEEP es LPM0: el 40 %");
    check(m3 == 3 && std::fabs(i_lpm3 - 0.66) < 0.05, "con SLEEPDEEP y LPMR = 0, LPM3: 0,66 uA");
    check(smclk_lpm3 == 0.0 && aclk_lpm3 > 30000.0 && bclk_lpm3 > 30000.0,
          "en LPM3 SMCLK se para, y ACLK y BCLK siguen para el RTC y el perro");
    std::printf("    en LPM3: TA1 (ACLK) avanzo %u tics; %u despertares del RTC\n", r3(3), r3(1));
    check(r3(1) == 3 && r3(0) < 3000 && r3(3) < 10,
          "el RTC despierta al chip; el Timer32 (MCLK) y TA1 (ACLK) apenas avanzan: en "
          "LPM3 solo funcionan el RTC y el perro");
    check(m4 == 4 && std::fabs(i_lpm4 - 0.5) < 0.05,
          "sin RTC ni perro, el mismo LPM3 es LPM4: 0,5 uA");

    grupo("P54 LPM3.5: el RTC despierta al chip con un POR, y las E/S esperan a LOCKLPM5");
    arranca3(61, FW_T3B);
    check(espera3(1, 5), "el firmware deja P1.0 a 1, arma el RTC y se duerme");
    wait(2, SC_MS);
    const unsigned m35 = dut.modo_lpm();
    const double i35 = ua();
    const bool p10_dormido = dut.dio.nivel(0, 0);
    const unsigned resets0 = dut.salidas_lpm5();
    check(espera3(10, 20), "el preescalador del RTC lo despierta");
    const bool p10_cerrojo = dut.dio.nivel(0, 0);
    std::printf("    LPM3.5: modo %u, %.3f uA, P1.0 %d; al salir: PCMRESET_STAT %u, "
                "LOCKLPM5 %u, RT0PSIFG %u, arranques %u, P1.0 %d\n", m35, i35, p10_dormido,
                r3(10), r3(11), r3(13), r3(14), p10_cerrojo);
    check(m35 == 35 && std::fabs(i35 - 0.63) < 0.05 && p10_dormido,
          "en LPM3.5: 0,63 uA y P1.0 sigue a 1 con el nucleo apagado");
    check(dut.salidas_lpm5() == resets0 + 1 && r3(10) == 1 && r3(11) == 1,
          "sale por un POR: RSTCTL_PCMRESET_STAT = LPM35 y LOCKLPM5 = 1");
    check(r3(14) == 2 && r3(13) != 0,
          "la SRAM se conserva (arranques = 2) y el RTC tambien (RT0PSIFG sigue puesto)");
    check(p10_cerrojo, "con LOCKLPM5, P1.0 sigue a 1 aunque los registros del puerto ya son los de reset");
    dut.sram.poke32(0x0C, 10);
    check(espera3(11, 5), "el firmware rehace la configuracion y suelta el cerrojo");
    wait(10, SC_US);
    check(!dut.dio.nivel(0, 0) && r3(15) == 0,
          "y entonces P1.0 baja, la nueva; PCMRESET_CLR borra la causa");

    grupo("P55 LPM4.5: 25 nA, y S1 lo despierta; la SRAM se pierde");
    arranca3(62, FW_T3B);
    check(espera3(1, 5), "el firmware arma P1.1 y se duerme");
    wait(1, SC_MS);
    const unsigned m45 = dut.modo_lpm();
    const double i45 = dut.idd_medida() * 1e9;
    s1->press();
    check(espera3(10, 10), "S1 lo despierta");
    s1->release();
    std::printf("    LPM4.5: modo %u, %.1f nA; al salir: PCMRESET_STAT %u, LOCKLPM5 %u, "
                "P1IFG 0x%02X, arranques %u, modo en el buzon %u\n", m45, i45, r3(10), r3(11),
                r3(12), r3(14), buzon(0x00));
    check(m45 == 45 && std::fabs(i45 - 25.0) < 3.0, "en LPM4.5: 25 nA");
    check(r3(10) == 2 && r3(11) == 1, "RSTCTL_PCMRESET_STAT = LPM45 y LOCKLPM5 = 1");
    check(r3(12) & 0x02u, "el IFG de P1.1 dice quien desperto al chip");
    check(r3(14) == 1 && buzon(0x00) == 0, "la SRAM se ha perdido: el buzon vuelve a empezar");
    dut.sram.poke32(0x0C, 10);
    check(espera3(11, 5), "y el firmware suelta el cerrojo");
}

#endif // STM32_TOP_SC_MAIN_P4_T3B_H
