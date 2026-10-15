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

#endif // STM32_TOP_SC_MAIN_P4_T3B_H
