// =============================================================================
// sc_main_p4_t3.h — Los grupos de la fase T3 de la suite del MSP432P401R
//
// Van aparte para que sc_main_p4.cpp no crezca sin fin: son métodos de TbP4,
// definidos aquí, con el firmware verif/fw/msp432p4/p4_t3. Cada periférico de
// aula tiene su grupo, como en las suites de F4 y F5:
//
//   P22-P27  eUSCI_A en UART: el backchannel con un PuenteSerie, los formatos,
//            los errores, el break, UCLISTEN y el IV, los modos
//            multiprocesador y el PMAP
//   P28-P34  Timer_A (PWM, modo continuo, ascendente/descendente, captura,
//            reloj externo, un servo) y Timer32
// =============================================================================
#ifndef STM32_TOP_SC_MAIN_P4_T3_H
#define STM32_TOP_SC_MAIN_P4_T3_H

// Deja el chip en reset, carga p4_t3, borra el buzón y lo suelta
inline void TbP4::arranca3(uint32_t modo) {
    mcu.reset_pin(true);
    wait(20, SC_US);
    mcu.carga_firmware(FW_T3);
    uint8_t ceros[0x200] = {};
    dut.sram.load(ceros, sizeof ceros, 0);
    dut.sram.poke32(0x00, modo);
    mcu.reset_pin(false);
}
// Espera a que el firmware llegue a `estado`, como mucho `ms`
inline bool TbP4::espera3(uint32_t estado, double ms) {
    const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
    while (sc_time_stamp() < lim) {
        if (dut.sram.peek32(0x08) >= estado) return true;
        wait(20, SC_US);
    }
    return dut.sram.peek32(0x08) >= estado;
}

// =============================================================================
// P22-P27  eUSCI_A en UART
// =============================================================================
inline void TbP4::t3_uart() {
    using E = EusciP4;
    // P21 deja el nucleo PARADO: el `D` de GDB se despide sin reanudar, y
    // C_DEBUGEN y C_HALT sobreviven a un reset que no sea de alimentacion,
    // como en el silicio. Antes de T3 la sonda suelta el nucleo: DHCSR sin
    // C_DEBUGEN, que es lo que hace un depurador que se desconecta bien.
    sonda->conectar_swd();
    sonda->mem_write32(0xE000EDF0u, 0xA05F0000u);
    sonda->desconectar();
    grupo("P22 eUSCI_A0 en UART: el backchannel a 115200, eco por interrupcion");
    bcl->set_baudios(115200);
    bcl->set_formato(FormatoUart{});
    bcl->borra_recibido();
    bcl->set_enabled(true);
    arranca3(1);
    check(espera3(1, 10), "el firmware configura la UCA0 y manda el saludo");
    check(bcl->recibido() == "Hola desde el MSP432\r\n",
          "el PuenteSerie en P1.3 recibe el saludo entero, a 115200 8N1");
    const double b = dut.uca0.baudios();
    std::printf("    baudios del generador: %.1f (UCBR = 6, UCBRF = 8, UCBRS = 0x20, "
                "SMCLK = 12 MHz)\n", b);
    check(std::fabs(b - 115200.0) / 115200.0 < 0.005,
          "UCOS16 con UCBRF y el patron UCBRS dan 115200 a menos del 0,5 %");
    check_eq(r3(2) & 0x0F, E::TXIFG | E::TXCPTIFG,
             "al acabar: TXIFG y TXCPTIFG, y nada mas");
    bcl->envia("eco");
    wait(1, SC_MS);
    check(bcl->recibido() == "Hola desde el MSP432\r\nECO",
          "el eco por RXIFG y UCA0IV vuelve en mayusculas");
    check(r3(0) == 3 && r3(1) == 0,
          "tres interrupciones de RX, y ningun otro vector");
    check(bcl->errores_trama() == 0 && bcl->errores_paridad() == 0,
          "sin errores en el puente");

    grupo("P23 9600 7E2, y los errores de trama y paridad");
    bcl->borra_recibido();
    bcl->set_baudios(9600);
    FormatoUart f7e2; f7e2.bits = 7; f7e2.paridad = Paridad::par; f7e2.parada = Parada::dos;
    bcl->set_formato(f7e2);
    arranca3(2);
    check(espera3(1, 20) && bcl->recibido() == "7E2",
          "a 3 MHz y 9600 7E2 (UCBR = 19, UCBRF = 8, UCBRS = 0xAA) el puente lo lee bien");
    bcl->envia("ab");
    const bool e2 = espera3(2, 20);
    wait(3, SC_MS);                               // el segundo eco, en la linea
    check(e2 && bcl->recibido() == "7E2ab",
          "y lo que manda el puente en 7E2 vuelve en eco");
    bcl->set_formato(FormatoUart{});              // 8N1: el MCU vera basura
    bcl->envia("Q");
    wait(3, SC_MS);
    std::printf("    STATW tras 'Q' en 8N1: 0x%02X; IFG: 0x%02X\n", r3(0), r3(1));
    check((r3(0) & E::ST_PE) && (r3(0) & E::ST_RXERR),
          "'Q' en 8N1 leido como 7E2: error de paridad (UCPE y UCRXERR)");
    check(!(r3(1) & E::RXIFG),
          "con UCRXEIE = 0 el caracter con error NO llega: no hay RXIFG");
    check(!(r3(2) & (E::ST_PE | E::ST_RXERR)), "leer RXBUF borra los errores");
    check(espera3(3, 5), "con UCRXEIE = 1...");
    bcl->envia("Q");
    check(espera3(4, 5) && (r3(3) & E::ST_PE) && r3(4) == 0x51,
          "...el mismo caracter SI llega, con UCPE puesto: 0x51");

    grupo("P24 Desbordamiento y break en los dos sentidos");
    bcl->borra_recibido();
    bcl->set_baudios(115200);
    bcl->set_formato(FormatoUart{});
    arranca3(3);
    check(espera3(1, 5), "la UCA0 lista, con UCBRKIE");
    bcl->envia("123");
    wait(1, SC_MS);
    dut.sram.poke32(0x0C, 1);
    check(espera3(2, 5), "el firmware lee despues de que lleguen los tres");
    check((r3(0) & E::ST_OE) && (r3(0) & E::ST_RXERR),
          "tres caracteres sin leer: UCOE y UCRXERR");
    check_eq(r3(1), '3', "el ultimo machaca a los anteriores en RXBUF");
    check(!(r3(2) & E::ST_OE), "y leer RXBUF borra UCOE");
    const uint64_t brk0 = bcl->breaks_desde_mcu();
    bcl->envia_break();
    check(espera3(3, 5), "el break del puente levanta RXIFG (UCBRKIE)...");
    check((r3(3) & E::ST_BRK) && (r3(3) & E::ST_FE) && r3(4) == 0,
          "...con UCBRK y UCFE, y RXBUF a cero");
    check(bcl->breaks_desde_mcu() == brk0 + 1, "UCTXBRK manda un break que el puente ve");
    check(r3(5) == 0 && bcl->recibido() == "K",
          "UCTXBRK se borra solo, y lo siguiente sale normal");

    grupo("P25 UCLISTEN a 1 Mbaudio, y el orden del IV");
    arranca3(4);
    check(espera3(1, 10), "64 caracteres por dentro, sin pines");
    check_eq(r3(0), 0, "todos vuelven iguales");
    check(r3(1) == 4 && r3(2) == 2 && r3(3) == 4 && r3(4) == 6 && r3(5) == 8,
          "con las cuatro banderas, UCA2IV da 2 (RX), 4 (TX), 6 (STT) y 8 (TXCPT), "
          "y cada lectura borra la suya");
    check_eq(r3(10), 0x5A, "leer el IV de RX no toca RXBUF");

    grupo("P26 Los dos modos multiprocesador, con UCDORM");
    const char* nombres[2] = {"bit de direccion", "linea ociosa"};
    for (unsigned m = 0; m < 2; ++m) {
        arranca3(5 + m);
        char t[160];
        const bool ok = espera3(1, 5);
        std::snprintf(t, sizeof t, "%s: dormido, un dato no llega; una direccion si, "
                      "con UCADDR/UCIDLE", nombres[m]);
        check(ok && r3(0) == 0 && r3(1) == 1 && r3(2) == E::ST_ADDR_IDLE && r3(3) == 0x42, t);
        std::snprintf(t, sizeof t, "%s: UCTXADDR se borra solo", nombres[m]);
        check(r3(4) == 0, t);
        std::snprintf(t, sizeof t, "%s: despierto, la direccion y el dato llegan, el dato "
                      "sin la marca", nombres[m]);
        check(r3(5) == 0x43 && r3(6) == 1 && r3(7) == 0 && r3(8) == 0x55, t);
    }

    grupo("P27 El PMAP: la UCA1 en sus pines de reset y la UCA2 mapeada a P2.3");
    bcl->set_enabled(false);
    bcl2->set_baudios(115200);
    bcl2->set_formato(FormatoUart{});
    bcl2->borra_recibido();
    bcl2->set_enabled(true);
    arranca3(7);
    check(espera3(1, 5) && bcl2->recibido() == "A1",
          "P2.3 lleva por omision UCA1TXD (P2MAP3 = PM_UCA1TXD)");
    check(espera3(2, 5) && bcl2->recibido() == "A1A2",
          "con P2MAP3 = PM_UCA2TXD lo que sale por P2.3 es la UCA2, y la UCA1 "
          "se queda sin pin");
    check(r3(0) == PmapP4::PM_UCA2TXD && r3(1) == 1 && r3(2) == PmapP4::PM_UCA2TXD,
          "cerrado sin PMAPRECFG, el PMAP ya no cambia aunque se ponga la llave");
    bcl2->set_enabled(false);
}

// Periodo y fraccion en alto de lo que hay en un pin, midiendo `n` ciclos
inline bool TbP4::pwm(unsigned p, unsigned i, double& per, double& alto, unsigned n,
                      double max_ms) {
    analog_net_if& nd = dut.dio.nodo_de(p, i);
    const sc_time lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
    auto nivel = [&] { return nd.voltage() > 1.65f; };
    // Sincroniza con un flanco de subida
    bool ant = nivel();
    sc_time t_sube = SC_ZERO_TIME, t_baja = SC_ZERO_TIME, t0 = SC_ZERO_TIME;
    unsigned subidas = 0;
    double sum_alto = 0.0;
    while (sc_time_stamp() < lim && subidas <= n) {
        wait(lim - sc_time_stamp(), nd.value_changed_event());
        const bool v = nivel();
        if (v && !ant) {
            if (subidas == 0) t0 = sc_time_stamp();
            else sum_alto += (t_baja - t_sube).to_seconds();
            t_sube = sc_time_stamp();
            ++subidas;
        } else if (!v && ant) {
            t_baja = sc_time_stamp();
        }
        ant = v;
    }
    if (subidas < 2) { per = 0; alto = 0; return false; }
    per = (t_sube - t0).to_seconds() / double(subidas - 1);
    alto = sum_alto / double(subidas - 1) / per;
    return true;
}

// =============================================================================
// P28-P33  Timer_A y Timer32
// =============================================================================
inline void TbP4::t3_timers() {
    using T = TimerAP4;
    char t[200];
    double per = 0, alto = 0;

    grupo("P28 Timer_A0 en modo ascendente: PWM en TA0.1 y TA0.2, por el PMAP de reset");
    arranca3(10);
    check(espera3(1, 5), "el firmware configura TA0: SMCLK = 3 MHz, CCR0 = 2999, modo 7");
    pwm(1, 4, per, alto);
    std::printf("    P2.4 (TA0.1): periodo %.4f ms, en alto %.3f %%\n", per * 1e3, alto * 100);
    check(std::fabs(per - 1e-3) < 1e-7, "el periodo es (CCR0 + 1) tics: 1,0000 ms");
    check(std::fabs(alto - 0.25) < 1e-4,
          "con CCR1 = 749 en reset/set, el pin esta alto (CCR1 + 1) / (CCR0 + 1): 25 %");
    pwm(1, 5, per, alto);
    std::printf("    P2.5 (TA0.2): periodo %.4f ms, en alto %.3f %%\n", per * 1e3, alto * 100);
    check(std::fabs(alto - 0.50) < 1e-4, "y TA0.2 con CCR2 = 1499, el 50 %");
    check(espera3(2, 20), "diez interrupciones de CCR0 por TA0_0...");
    check(r3(0) == 10 && r3(1) >= 9 && r3(1) <= 10 && r3(2) == 0,
          "...y TAIFG por TA0_N con el IV a 0x0E, una por periodo");
    std::snprintf(t, sizeof t, "TAR sigue dentro del periodo (%u <= 2999)", unsigned(dut.ta0.tar()));
    check(dut.ta0.tar() <= 2999, t);

    grupo("P29 Timer_A1 en modo continuo: dos bases de tiempo con CCRn += paso");
    arranca3(11);
    check(espera3(1, 5), "TA1 en continuo, con CCR1 y CCR2 en modo 4 (toggle)");
    pwm(6, 7, per, alto);
    std::printf("    P7.7 (TA1.1): periodo %.4f ms\n", per * 1e3);
    check(std::fabs(per - 2000.0 / 3e6) < 1e-8,
          "TA1.1 conmuta cada 1000 tics: un periodo de 2000 tics, 0,6667 ms");
    pwm(6, 6, per, alto);
    std::printf("    P7.6 (TA1.2): periodo %.4f ms\n", per * 1e3);
    check(std::fabs(per - 6000.0 / 3e6) < 1e-8, "y TA1.2 cada 3000: 2,0000 ms");
    check(espera3(2, 40) && r3(0) == 60 && (r3(1) == 20 || r3(1) == 19),
          "por el IV, tres de CCR1 por cada una de CCR2");

    grupo("P30 Timer_A2 ascendente/descendente: PWM centrado");
    arranca3(12);
    check(espera3(1, 5), "TA2 con CCR0 = 1000 y CCR1 = 250 en modo 2 (toggle/reset)");
    wait(2, SC_MS);                               // el primer periodo es transitorio
    pwm(4, 6, per, alto);
    std::printf("    P5.6 (TA2.1): periodo %.4f ms, en alto %.3f %%\n", per * 1e3, alto * 100);
    check(std::fabs(per - 2000.0 / 3e6) < 1e-8, "el periodo es 2 x CCR0 tics: 0,6667 ms");
    check(std::fabs(alto - 0.25) < 1e-4, "y el pulso, 2 x CCR1 tics centrados en el cero: 25 %");
    std::printf("    TAR al ver TAIFG: %u\n", r3(0));
    check(r3(0) <= 40, "TAIFG llega al pasar por cero, bajando: TAR acaba de empezar a subir");

    grupo("P31 Timer_A3: captura de los dos flancos, COV y captura por programa");
    reloj_cap->set_freq(1000.0);
    arranca3(13);
    check(espera3(2, 20), "cinco capturas de un reloj de 1 kHz en P10.5 (CCI1A)");
    std::printf("    diferencias: %u %u %u %u tics\n", r3(0), r3(1), r3(2), r3(3));
    check(r3(0) >= 1499 && r3(0) <= 1501 && r3(1) >= 1499 && r3(1) <= 1501 &&
          r3(2) >= 1499 && r3(2) <= 1501,
          "medio periodo entre capturas: 1500 tics de 3 MHz");
    check(r3(4) == T::CC_COV, "una captura sin atender la anterior levanta COV");
    std::printf("    captura por programa: CCIFG=%u, %u tics despues de leer TAR, CCI=%u\n",
                r3(5), r3(6), r3(7));
    check(r3(5) == T::CC_IFG && r3(6) <= 20 && r3(7) == T::CC_CCI,
          "CCIS de GND a VCC captura TAR al momento, y CCI lee VCC");
    reloj_cap->stop();

    grupo("P32 Timer32: periodico, de un disparo, libre, BGLOAD e INTC");
    arranca3(14);
    check(espera3(1, 5) && r3(0) == Timer32P4::IE, "CONTROL sale del reset con IE (0x20)");
    check(espera3(2, 200), "cinco vencimientos del periodico...");
    std::printf("    VALUE leido dos veces: %u y luego %u\n", r3(9), r3(10));
    check(r3(1) == 1, "VALUE cuenta hacia abajo");
    std::printf("    T32_1: %llu vencimientos; T32_2 (un disparo): %u\n",
                (unsigned long long)dut.t32.vencimientos(0), r3(3));
    check(r3(2) == 5 && r3(3) == 1 && r3(4) == 0,
          "...a 10 ms cada uno; el de un disparo vence una vez (20 ms) y se queda en 0");
    check(r3(5) == 59999, "BGLOAD cambia la recarga sin tocar la cuenta en curso");
    check(r3(6) >= 0xFF00 && r3(6) <= 0xFFFF && r3(7) == 0,
          "en libre y 16 bits da la vuelta a 0xFFFF, y sin IE no hay MIS");
    check(r3(8) >= 1, "T32_INTC llega con la de cualquiera de los dos");

    grupo("P33 Timer_A1 con TA1CLK externo en P7.2, y los divisores ID e IDEX");
    reloj_ta1->set_freq(100000.0);
    arranca3(15);
    check(espera3(1, 5), "TA1 con TASSEL = TAxCLK, ID = /2 e IDEX = /5");
    wait(10, SC_MS);
    dut.sram.poke32(0x0C, 1);
    check(espera3(2, 5), "el firmware lee TAR");
    std::printf("    TAR tras ~10 ms de 100 kHz /10: %u\n", r3(0));
    check(r3(0) >= 99 && r3(0) <= 102, "cuenta los flancos del pin entre diez: unas 100");
    reloj_ta1->stop();

    grupo("P34 Un servo SG90 en TA0.2 (P2.5): 50 Hz, y el eje va donde dice el pulso");
    servo->set_enabled(true);
    arranca3(16);
    check(espera3(1, 5), "TA0 a 50 Hz (SMCLK / 4, CCR0 = 14999) con 1,5 ms en CCR2");
    pwm(1, 5, per, alto, 3, 100);
    std::printf("    P2.5: periodo %.3f ms, pulso %.4f ms\n", per * 1e3, alto * per * 1e3);
    check(std::fabs(per - 20e-3) < 1e-6 && std::fabs(alto * per - 1.5e-3) < 2e-6,
          "20 ms de periodo y 1,5 ms de pulso");
    wait(300, SC_MS);
    std::printf("    el eje: %.1f grados\n", servo->angulo());
    check(std::fabs(servo->angulo()) < 1.0, "con 1,5 ms el eje va al centro, desde -60");
    dut.sram.poke32(0x0C, 1);
    check(espera3(2, 50), "el firmware pasa a 2,0 ms (despierta con TA0_0)");
    wait(300, SC_MS);
    std::printf("    el eje: %.1f grados\n", servo->angulo());
    check(std::fabs(servo->angulo() - 90.0) < 1.0, "y con 2,0 ms, a +90");
    servo->set_enabled(false);
}

#endif // STM32_TOP_SC_MAIN_P4_T3_H
