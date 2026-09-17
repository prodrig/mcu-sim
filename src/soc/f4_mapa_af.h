// =============================================================================
// f4_mapa_af.h — EL MAPA DE FUNCIONES ALTERNATIVAS de la familia F4
//
// Qué periférico sale por qué pin con qué número de AF. Es una de las tres
// cosas que definen a una familia de MCU —las otras dos son el juego de
// periféricos y el árbol de reloj— y hasta la fase 1 estaba escrito dentro de
// `Stm32F407VG::bind_analog()`, es decir, **en una función llamada «analógico»,
// que es el último sitio donde alguien lo buscaría**.
//
// POR QUÉ ESTÁ AQUÍ Y NO EN EL TOP. Porque es lo que hay que mirar, copiar o
// adaptar cuando se modela otro chip, y un mapa que no se encuentra se
// reescribe desde cero. Sacarlo era el punto de la fase 1 que más paga y el
// menos vistoso [doc/stm32f407vg_vs_446re.md, §13].
//
// LO QUE SE SABE DE SU RELACIÓN CON EL F446, y que hace que esto valga la pena:
// se compararon las dos tablas de ST par a par, por máquina —260 pares (pin,
// señal) comunes— y **el número de AF no difiere en NINGUNO**. Los periféricos
// nuevos del F446 entran en ranuras que en esos mismos pines estaban libres, y
// el único número que se vacía es el **AF11**, el del Ethernet. O sea: esta
// tabla no hay que rehacerla para el F446, hay que **extenderla**. Ese es el
// motivo de que sea un fichero y no doscientas llamadas sueltas.
//
// POR QUÉ SIGUE SIENDO UN MIEMBRO DE `Stm32F407VG`. El cuerpo se movió TAL
// CUAL, sin tocar una sola referencia, porque una función miembro definida
// fuera de la clase ve los miembros igual que dentro. Es el mismo patrón que ya
// usaba `stm32f407vg_bind2.h`, y es lo que hace que este movimiento sea
// mecánico y comprobable: el tiempo simulado no se movió ni un picosegundo.
//
// Lo que NO está aquí, y conviene saberlo: la tabla del SISTEMA (AF0 con
// SWD/JTAG y MCO1/2, AF15 con EVENTOUT) se registra en `bind_gpio_pins()`
// porque son de la arquitectura y no de la familia; y el Ethernet (AF11) y el
// ULPI del OTG HS (AF10) siguen en `bind_analog()`, enredados con el enganche
// eléctrico de los PHY. Separar esos dos es trabajo de la fase 2, cuando el
// Ethernet tenga que poder no existir.
// =============================================================================
#ifndef STM32_SOC_F4_MAPA_AF_H
#define STM32_SOC_F4_MAPA_AF_H

#include "../top/stm32f407vg.h"

namespace stm32 {

inline void Stm32F407VG::bind_mapa_af() {
    // ---- Ethernet MAC (AF11) ----------------------------------------------
    // Los dieciocho pines de MII y los nueve de RMII son LOS MISMOS pines: la
    // interfaz se elige con SYSCFG_PMC y el mux no cambia, cambia quien mira
    // cada hilo. RMII usa TXD0/1, RXD0/1, TX_EN y CRS_DV; MII anade TXD2/3,
    // RXD2/3, RX_ER, CRS, COL y su propio TX_CLK [IR, cap. 2].
    {
        auto af_e = [&](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                        sc_core::sc_signal<bool>* i) {
            AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = false;
            return ep;
        };
        pinmux.connect_af(2,  1, 11, af_e(&eth.mdc_out, &s_true, nullptr));   // PC1
        pinmux.connect_af(0,  2, 11, af_e(&eth.mdio_out, &eth.mdio_oe,
                                          &eth.mdio_in));                     // PA2
        pinmux.connect_af(2,  3, 11, af_e(nullptr, nullptr, &eth.tx_clk_in));  // PC3
        pinmux.connect_af(0,  1, 11, af_e(nullptr, nullptr, &eth.ref_clk_in)); // PA1
        pinmux.connect_af(1, 11, 11, af_e(&eth.tx_en_out, &s_true, nullptr));  // PB11
        pinmux.connect_af(1, 12, 11, af_e(&eth.txd_out[0], &s_true, nullptr)); // PB12
        pinmux.connect_af(1, 13, 11, af_e(&eth.txd_out[1], &s_true, nullptr)); // PB13
        pinmux.connect_af(2,  2, 11, af_e(&eth.txd_out[2], &s_true, nullptr)); // PC2
        pinmux.connect_af(1,  8, 11, af_e(&eth.txd_out[3], &s_true, nullptr)); // PB8
        pinmux.connect_af(2,  4, 11, af_e(nullptr, nullptr, &eth.rxd_in[0]));  // PC4
        pinmux.connect_af(2,  5, 11, af_e(nullptr, nullptr, &eth.rxd_in[1]));  // PC5
        pinmux.connect_af(1,  0, 11, af_e(nullptr, nullptr, &eth.rxd_in[2]));  // PB0
        pinmux.connect_af(1,  1, 11, af_e(nullptr, nullptr, &eth.rxd_in[3]));  // PB1
        pinmux.connect_af(0,  7, 11, af_e(nullptr, nullptr, &eth.rx_dv_in));   // PA7
        pinmux.connect_af(1, 10, 11, af_e(nullptr, nullptr, &eth.rx_er_in));   // PB10
        pinmux.connect_af(0,  0, 11, af_e(nullptr, nullptr, &eth.crs_in));     // PA0
        pinmux.connect_af(0,  3, 11, af_e(nullptr, nullptr, &eth.col_in));     // PA3
        pinmux.connect_af(1,  5, 11, af_e(&eth.pps_out, &s_true, nullptr));    // PB5
    }
    // ---- ULPI del OTG HS (AF10) ------------------------------------------
    // Los ocho hilos de datos mas CK, STP, DIR y NXT. El enganche ELECTRICO de
    // los dos PHY -bind_phy, bind_vbus, bind_id- se queda en `bind_analog`,
    // que es donde le toca: eso es fisica de pad, no tabla de mux.
    {
        // Los ocho hilos de datos del ULPI son bidireccionales; DIR y NXT los
        // gobierna el PHY, STP el controlador y CK es el reloj de 60 MHz que
        // entra desde fuera. Todos por AF10 [IR, §12.23.1].
        auto af_u = [&](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                        sc_core::sc_signal<bool>* i) {
            AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = false;
            return ep;
        };
        static const unsigned ulpi_d[8][2] = {
            {0, 3}, {1, 0}, {1, 1}, {1, 10}, {1, 11}, {1, 12}, {1, 13}, {1, 5}
        };
        for (unsigned i = 0; i < 8; ++i)
            pinmux.connect_af(ulpi_d[i][0], ulpi_d[i][1], 10,
                              af_u(&otg_hs.ulpi_d_out[i], &otg_hs.ulpi_d_oe[i],
                                   &otg_hs.ulpi_d_in[i]));
        pinmux.connect_af(0, 5, 10, af_u(nullptr, nullptr, &otg_hs.ulpi_ck_in));
        pinmux.connect_af(2, 0, 10, af_u(&otg_hs.ulpi_stp_out, &s_true, nullptr));
        pinmux.connect_af(2, 2, 10, af_u(nullptr, nullptr, &otg_hs.ulpi_dir_in));
        pinmux.connect_af(2, 3, 10, af_u(nullptr, nullptr, &otg_hs.ulpi_nxt_in));
    }
    // Funciones alternativas — EJEMPLOS de registro de periféricos de F4/F5; la
    // tabla del sistema (AF0: SWD/JTAG y MCO1/2; AF15: EVENTOUT) se registra en
    // bind_gpio_pins. El resto se completa al implementar cada periférico.
    // ---- USART y UART [IR, §12.4.3-G] ------------------------------------
    // Los pines de TX se registran con su habilitación de salida (en medio
    // dúplex el periférico suelta la línea); los de RX solo con su entrada.
    auto af_tx = [](UsartBase& u) {
        return AfEndpoint{&u.tx_out, &u.tx_oe, nullptr, true};
    };
    auto af_rx = [](UsartBase& u) {
        return AfEndpoint{nullptr, nullptr, &u.rx_in, true};   // reposo a 1
    };
    auto af_ck = [](UsartBase& u) {
        return AfEndpoint{&u.ck_out, &u.ck_oe, nullptr, true};
    };
    auto af_rts = [](UsartBase& u) {
        return AfEndpoint{&u.rts_out, &u.rts_oe, nullptr, true};
    };
    auto af_cts = [](UsartBase& u) {
        return AfEndpoint{nullptr, nullptr, &u.cts_in, true};  // reposo inactivo
    };
    // USART1 (AF7): PA9/PA10 o PB6/PB7; CK PA8, CTS PA11, RTS PA12
    pinmux.connect_af(0,  9, 7, af_tx(usart1));   pinmux.connect_af(0, 10, 7, af_rx(usart1));
    pinmux.connect_af(1,  6, 7, af_tx(usart1));   pinmux.connect_af(1,  7, 7, af_rx(usart1));
    pinmux.connect_af(0,  8, 7, af_ck(usart1));
    pinmux.connect_af(0, 11, 7, af_cts(usart1));  pinmux.connect_af(0, 12, 7, af_rts(usart1));
    // USART2 (AF7): PA2/PA3 o PD5/PD6; CK PA4, CTS PA0, RTS PA1
    pinmux.connect_af(0,  2, 7, af_tx(usart2));   pinmux.connect_af(0,  3, 7, af_rx(usart2));
    pinmux.connect_af(3,  5, 7, af_tx(usart2));   pinmux.connect_af(3,  6, 7, af_rx(usart2));
    pinmux.connect_af(0,  4, 7, af_ck(usart2));
    pinmux.connect_af(0,  0, 7, af_cts(usart2));  pinmux.connect_af(0,  1, 7, af_rts(usart2));
    // USART3 (AF7): PB10/PB11, PC10/PC11 o PD8/PD9; CK PB12, CTS PB13, RTS PB14
    pinmux.connect_af(1, 10, 7, af_tx(usart3));   pinmux.connect_af(1, 11, 7, af_rx(usart3));
    pinmux.connect_af(2, 10, 7, af_tx(usart3));   pinmux.connect_af(2, 11, 7, af_rx(usart3));
    pinmux.connect_af(3,  8, 7, af_tx(usart3));   pinmux.connect_af(3,  9, 7, af_rx(usart3));
    pinmux.connect_af(1, 12, 7, af_ck(usart3));
    pinmux.connect_af(1, 13, 7, af_cts(usart3));  pinmux.connect_af(1, 14, 7, af_rts(usart3));
    // UART4 (AF8): PA0/PA1 o PC10/PC11 — sin CK ni control de flujo
    pinmux.connect_af(0,  0, 8, af_tx(uart4));    pinmux.connect_af(0,  1, 8, af_rx(uart4));
    pinmux.connect_af(2, 10, 8, af_tx(uart4));    pinmux.connect_af(2, 11, 8, af_rx(uart4));
    // UART5 (AF8): PC12 (TX) y PD2 (RX)
    pinmux.connect_af(2, 12, 8, af_tx(uart5));    pinmux.connect_af(3,  2, 8, af_rx(uart5));
    // USART6 (AF8): PC6/PC7; CTS PG13/PG15 y RTS PG8/PG12 no existen en LQFP100
    pinmux.connect_af(2,  6, 8, af_tx(usart6));   pinmux.connect_af(2,  7, 8, af_rx(usart6));
    // ---- Temporizadores [IR, §12.1-12.3, §12.8; tabla AF de §2.1] ---------
    // Un canal de temporizador es BIDIRECCIONAL: el mismo pin es salida de
    // comparación (OCx) o entrada de captura (ICx) según CCxS, así que se
    // registra con las tres señales. En reposo (pin no asignado a esta AF) la
    // entrada se fuerza a nivel bajo.
    auto af_ch = [](TimerBase& t, unsigned c) {
        return AfEndpoint{&t.ch_out[c], &t.ch_oe[c], &t.ch_in[c], false};
    };
    auto af_chn = [](TimerBase& t, unsigned c) {           // salida complementaria
        return AfEndpoint{&t.chn_out[c], &t.chn_oe[c], nullptr, false};
    };
    auto af_etr = [](TimerBase& t) {
        return AfEndpoint{nullptr, nullptr, &t.etr_in, false};
    };
    // BKIN reposa a nivel ALTO: con la polaridad por defecto (BKP = 0, freno
    // activo en bajo) un pin no asignado a esta AF no debe frenar el puente.
    auto af_bkin = [](TimerBase& t) {
        return AfEndpoint{nullptr, nullptr, &t.bkin_in, true};
    };
    // TIM1 (AF1) y TIM2 (AF1)
    pinmux.connect_af(0,  8, 1, af_ch(tim1, 0));   // PA8  TIM1_CH1
    pinmux.connect_af(0,  9, 1, af_ch(tim1, 1));   // PA9  TIM1_CH2
    pinmux.connect_af(0, 10, 1, af_ch(tim1, 2));   // PA10 TIM1_CH3
    pinmux.connect_af(0, 11, 1, af_ch(tim1, 3));   // PA11 TIM1_CH4
    pinmux.connect_af(0,  7, 1, af_chn(tim1, 0));  // PA7  TIM1_CH1N
    pinmux.connect_af(1, 13, 1, af_chn(tim1, 0));  // PB13 TIM1_CH1N
    pinmux.connect_af(1,  0, 1, af_chn(tim1, 1));  // PB0  TIM1_CH2N
    pinmux.connect_af(1, 14, 1, af_chn(tim1, 1));  // PB14 TIM1_CH2N
    pinmux.connect_af(1,  1, 1, af_chn(tim1, 2));  // PB1  TIM1_CH3N
    pinmux.connect_af(1, 15, 1, af_chn(tim1, 2));  // PB15 TIM1_CH3N
    pinmux.connect_af(0,  6, 1, af_bkin(tim1));    // PA6  TIM1_BKIN
    pinmux.connect_af(1, 12, 1, af_bkin(tim1));    // PB12 TIM1_BKIN
    pinmux.connect_af(0, 12, 1, af_etr(tim1));     // PA12 TIM1_ETR
    pinmux.connect_af(4,  9, 1, af_ch(tim1, 0));   // PE9  TIM1_CH1
    pinmux.connect_af(4, 11, 1, af_ch(tim1, 1));   // PE11 TIM1_CH2
    pinmux.connect_af(4, 13, 1, af_ch(tim1, 2));   // PE13 TIM1_CH3
    pinmux.connect_af(4, 14, 1, af_ch(tim1, 3));   // PE14 TIM1_CH4
    pinmux.connect_af(0,  0, 1, af_ch(tim2, 0));   // PA0  TIM2_CH1/ETR
    pinmux.connect_af(0,  5, 1, af_ch(tim2, 0));   // PA5  TIM2_CH1
    pinmux.connect_af(0, 15, 1, af_ch(tim2, 0));   // PA15 TIM2_CH1
    pinmux.connect_af(0,  1, 1, af_ch(tim2, 1));   // PA1  TIM2_CH2
    pinmux.connect_af(1,  3, 1, af_ch(tim2, 1));   // PB3  TIM2_CH2
    pinmux.connect_af(0,  2, 1, af_ch(tim2, 2));   // PA2  TIM2_CH3
    pinmux.connect_af(1, 10, 1, af_ch(tim2, 2));   // PB10 TIM2_CH3
    pinmux.connect_af(0,  3, 1, af_ch(tim2, 3));   // PA3  TIM2_CH4
    pinmux.connect_af(1, 11, 1, af_ch(tim2, 3));   // PB11 TIM2_CH4
    // TIM3, TIM4 y TIM5 (AF2)
    pinmux.connect_af(0,  6, 2, af_ch(tim3, 0));   // PA6  TIM3_CH1
    pinmux.connect_af(1,  4, 2, af_ch(tim3, 0));   // PB4  TIM3_CH1
    pinmux.connect_af(2,  6, 2, af_ch(tim3, 0));   // PC6  TIM3_CH1
    pinmux.connect_af(0,  7, 2, af_ch(tim3, 1));   // PA7  TIM3_CH2
    pinmux.connect_af(1,  5, 2, af_ch(tim3, 1));   // PB5  TIM3_CH2
    pinmux.connect_af(2,  7, 2, af_ch(tim3, 1));   // PC7  TIM3_CH2
    pinmux.connect_af(1,  0, 2, af_ch(tim3, 2));   // PB0  TIM3_CH3
    pinmux.connect_af(2,  8, 2, af_ch(tim3, 2));   // PC8  TIM3_CH3
    pinmux.connect_af(1,  1, 2, af_ch(tim3, 3));   // PB1  TIM3_CH4
    pinmux.connect_af(2,  9, 2, af_ch(tim3, 3));   // PC9  TIM3_CH4
    pinmux.connect_af(3,  2, 2, af_etr(tim3));     // PD2  TIM3_ETR
    pinmux.connect_af(1,  6, 2, af_ch(tim4, 0));   // PB6  TIM4_CH1
    pinmux.connect_af(3, 12, 2, af_ch(tim4, 0));   // PD12 TIM4_CH1 (LED de la placa)
    pinmux.connect_af(1,  7, 2, af_ch(tim4, 1));   // PB7  TIM4_CH2
    pinmux.connect_af(3, 13, 2, af_ch(tim4, 1));   // PD13 TIM4_CH2
    pinmux.connect_af(1,  8, 2, af_ch(tim4, 2));   // PB8  TIM4_CH3
    pinmux.connect_af(3, 14, 2, af_ch(tim4, 2));   // PD14 TIM4_CH3
    pinmux.connect_af(1,  9, 2, af_ch(tim4, 3));   // PB9  TIM4_CH4
    pinmux.connect_af(3, 15, 2, af_ch(tim4, 3));   // PD15 TIM4_CH4
    pinmux.connect_af(4,  0, 2, af_etr(tim4));     // PE0  TIM4_ETR
    pinmux.connect_af(0,  0, 2, af_ch(tim5, 0));   // PA0  TIM5_CH1
    pinmux.connect_af(0,  1, 2, af_ch(tim5, 1));   // PA1  TIM5_CH2
    pinmux.connect_af(0,  2, 2, af_ch(tim5, 2));   // PA2  TIM5_CH3
    pinmux.connect_af(0,  3, 2, af_ch(tim5, 3));   // PA3  TIM5_CH4
    // TIM8 (AF3) y TIM9/10/11 (AF3)
    pinmux.connect_af(2,  6, 3, af_ch(tim8, 0));   // PC6  TIM8_CH1
    pinmux.connect_af(2,  7, 3, af_ch(tim8, 1));   // PC7  TIM8_CH2
    pinmux.connect_af(2,  8, 3, af_ch(tim8, 2));   // PC8  TIM8_CH3
    pinmux.connect_af(2,  9, 3, af_ch(tim8, 3));   // PC9  TIM8_CH4
    pinmux.connect_af(0,  5, 3, af_chn(tim8, 0));  // PA5  TIM8_CH1N
    pinmux.connect_af(1,  0, 3, af_chn(tim8, 1));  // PB0  TIM8_CH2N
    pinmux.connect_af(1,  1, 3, af_chn(tim8, 2));  // PB1  TIM8_CH3N
    pinmux.connect_af(0,  6, 3, af_bkin(tim8));    // PA6  TIM8_BKIN
    pinmux.connect_af(0,  0, 3, af_etr(tim8));     // PA0  TIM8_ETR
    pinmux.connect_af(0,  2, 3, af_ch(tim9, 0));   // PA2  TIM9_CH1
    pinmux.connect_af(0,  3, 3, af_ch(tim9, 1));   // PA3  TIM9_CH2
    pinmux.connect_af(4,  5, 3, af_ch(tim9, 0));   // PE5  TIM9_CH1
    pinmux.connect_af(4,  6, 3, af_ch(tim9, 1));   // PE6  TIM9_CH2
    pinmux.connect_af(1,  8, 3, af_ch(tim10, 0));  // PB8  TIM10_CH1
    pinmux.connect_af(1,  9, 3, af_ch(tim11, 0));  // PB9  TIM11_CH1
    // TIM12, TIM13 y TIM14 (AF9)
    pinmux.connect_af(1, 14, 9, af_ch(tim12, 0));  // PB14 TIM12_CH1
    pinmux.connect_af(1, 15, 9, af_ch(tim12, 1));  // PB15 TIM12_CH2
    pinmux.connect_af(0,  6, 9, af_ch(tim13, 0));  // PA6  TIM13_CH1
    pinmux.connect_af(0,  7, 9, af_ch(tim14, 0));  // PA7  TIM14_CH1

    // ---- bxCAN (AF9) [IR, §12.12-integracion; tabla AF de §2.1] ----------
    // CAN_TX es una salida push-pull normal y CAN_RX una entrada. El bus en si
    // -el cable en Y, dominante contra recesivo- vive FUERA del MCU, del otro
    // lado del transceptor. En reposo la entrada se fuerza a UNO: sin nadie
    // conectado, el hilo esta recesivo.
    auto af_can_tx = [](BxCanBase& c) {
        return AfEndpoint{&c.tx_out, &c.tx_oe, nullptr, true};
    };
    auto af_can_rx = [](BxCanBase& c) {
        return AfEndpoint{nullptr, nullptr, &c.rx_in, true};
    };
    // CAN1: PA11/PA12, PB8/PB9 o PD0/PD1
    pinmux.connect_af(0, 11, 9, af_can_rx(can1));  // PA11 CAN1_RX
    pinmux.connect_af(0, 12, 9, af_can_tx(can1));  // PA12 CAN1_TX
    pinmux.connect_af(1,  8, 9, af_can_rx(can1));  // PB8  CAN1_RX
    pinmux.connect_af(1,  9, 9, af_can_tx(can1));  // PB9  CAN1_TX
    pinmux.connect_af(3,  0, 9, af_can_rx(can1));  // PD0  CAN1_RX
    pinmux.connect_af(3,  1, 9, af_can_tx(can1));  // PD1  CAN1_TX
    // CAN2: PB5/PB6 o PB12/PB13
    pinmux.connect_af(1,  5, 9, af_can_rx(can2));  // PB5  CAN2_RX
    pinmux.connect_af(1,  6, 9, af_can_tx(can2));  // PB6  CAN2_TX
    pinmux.connect_af(1, 12, 9, af_can_rx(can2));  // PB12 CAN2_RX
    pinmux.connect_af(1, 13, 9, af_can_tx(can2));  // PB13 CAN2_TX

    // ---- I2C (AF4) [IR, §12.6.2; tabla AF de §2.1] -----------------------
    // SCL y SDA son bidireccionales y de colector abierto: el periférico solo
    // tira de la línea a cero y el pad, con OTYPER = open-drain, la deja en
    // alta impedancia cuando escribe un uno. El nivel alto lo da el pull-up de
    // la placa, no el MCU. En reposo la entrada se fuerza a uno (línea libre).
    auto af_i2c = [](sc_core::sc_signal<bool>& o, sc_core::sc_signal<bool>& e,
                     sc_core::sc_signal<bool>& i) {
        return AfEndpoint{&o, &e, &i, true};
    };
    // I2C1: SCL en PB6 o PB8, SDA en PB7 o PB9, SMBA en PB5
    pinmux.connect_af(1, 6, 4, af_i2c(i2c1.scl_out, i2c1.scl_oe, i2c1.scl_in));
    pinmux.connect_af(1, 8, 4, af_i2c(i2c1.scl_out, i2c1.scl_oe, i2c1.scl_in));
    pinmux.connect_af(1, 7, 4, af_i2c(i2c1.sda_out, i2c1.sda_oe, i2c1.sda_in));
    pinmux.connect_af(1, 9, 4, af_i2c(i2c1.sda_out, i2c1.sda_oe, i2c1.sda_in));
    pinmux.connect_af(1, 5, 4, af_i2c(i2c1.smba_out, i2c1.smba_oe, i2c1.smba_in));
    // I2C2: SCL en PB10, SDA en PB11, SMBA en PB12 (PF0/PF1 no existen en LQFP100)
    pinmux.connect_af(1, 10, 4, af_i2c(i2c2.scl_out, i2c2.scl_oe, i2c2.scl_in));
    pinmux.connect_af(1, 11, 4, af_i2c(i2c2.sda_out, i2c2.sda_oe, i2c2.sda_in));
    pinmux.connect_af(1, 12, 4, af_i2c(i2c2.smba_out, i2c2.smba_oe, i2c2.smba_in));
    // I2C3: SCL en PA8, SDA en PC9, SMBA en PA9
    pinmux.connect_af(0, 8, 4, af_i2c(i2c3.scl_out, i2c3.scl_oe, i2c3.scl_in));
    pinmux.connect_af(2, 9, 4, af_i2c(i2c3.sda_out, i2c3.sda_oe, i2c3.sda_in));
    pinmux.connect_af(0, 9, 4, af_i2c(i2c3.smba_out, i2c3.smba_oe, i2c3.smba_in));
    // ---- SDIO (AF12) [IR, §12.17-integración; tabla AF de §2.1] ----------
    // CK es una salida del host; CMD y D0-D7 son BIDIRECCIONALES: el mismo hilo
    // lo gobierna el MCU mientras manda y la tarjeta mientras contesta, y quien
    // decide es el bit de habilitación de salida. En reposo la entrada se fuerza
    // a uno, que es lo que dan los pull-up del zócalo.
    auto af_sd = [](sc_core::sc_signal<bool>& o, sc_core::sc_signal<bool>& e,
                    sc_core::sc_signal<bool>& i) {
        return AfEndpoint{&o, &e, &i, true};
    };
    pinmux.connect_af(2, 12, 12, af_sd(sdio.ck_out,  sdio.ck_oe,  sdio.ck_in));   // PC12 CK
    pinmux.connect_af(3,  2, 12, af_sd(sdio.cmd_out, sdio.cmd_oe, sdio.cmd_in));  // PD2  CMD
    pinmux.connect_af(2,  8, 12, af_sd(sdio.d_out[0], sdio.d_oe[0], sdio.d_in[0]));
    pinmux.connect_af(2,  9, 12, af_sd(sdio.d_out[1], sdio.d_oe[1], sdio.d_in[1]));
    pinmux.connect_af(2, 10, 12, af_sd(sdio.d_out[2], sdio.d_oe[2], sdio.d_in[2]));
    pinmux.connect_af(2, 11, 12, af_sd(sdio.d_out[3], sdio.d_oe[3], sdio.d_in[3]));
    pinmux.connect_af(1,  8, 12, af_sd(sdio.d_out[4], sdio.d_oe[4], sdio.d_in[4]));
    pinmux.connect_af(1,  9, 12, af_sd(sdio.d_out[5], sdio.d_oe[5], sdio.d_in[5]));
    pinmux.connect_af(2,  6, 12, af_sd(sdio.d_out[6], sdio.d_oe[6], sdio.d_in[6]));
    pinmux.connect_af(2,  7, 12, af_sd(sdio.d_out[7], sdio.d_oe[7], sdio.d_in[7]));

    // ---- SPI e I2S [IR, §12.5.3-D: pines tipicos; tabla AF de §2.1] ------
    // Cada pin de un SPI es bidireccional: el mismo hilo es salida en un
    // extremo y entrada en el otro segun quien sea maestro, asi que se registra
    // con las tres senales. En modo I2S los mismos pines son CK (SCK), WS (NSS)
    // y SD (MOSI; MISO en los bloques de extension), y MCK tiene pin propio.
    auto af_spi = [](sc_core::sc_signal<bool>& o, sc_core::sc_signal<bool>& e,
                     sc_core::sc_signal<bool>& i) {
        return AfEndpoint{&o, &e, &i, false};
    };
    // SPI1 (AF5): PA4/PA5/PA6/PA7 y la alternativa PA15/PB3/PB4/PB5
    pinmux.connect_af(0,  4, 5, af_spi(spi1.nss_out,  spi1.nss_oe,  spi1.nss_in));
    pinmux.connect_af(0,  5, 5, af_spi(spi1.sck_out,  spi1.sck_oe,  spi1.sck_in));
    pinmux.connect_af(0,  6, 5, af_spi(spi1.miso_out, spi1.miso_oe, spi1.miso_in));
    pinmux.connect_af(0,  7, 5, af_spi(spi1.mosi_out, spi1.mosi_oe, spi1.mosi_in));
    pinmux.connect_af(0, 15, 5, af_spi(spi1.nss_out,  spi1.nss_oe,  spi1.nss_in));
    pinmux.connect_af(1,  3, 5, af_spi(spi1.sck_out,  spi1.sck_oe,  spi1.sck_in));
    pinmux.connect_af(1,  4, 5, af_spi(spi1.miso_out, spi1.miso_oe, spi1.miso_in));
    pinmux.connect_af(1,  5, 5, af_spi(spi1.mosi_out, spi1.mosi_oe, spi1.mosi_in));
    // SPI2 / I2S2 (AF5): PB12 NSS/WS, PB13 SCK/CK, PB14 MISO, PB15 MOSI/SD
    pinmux.connect_af(1, 12, 5, af_spi(spi2.nss_out,  spi2.nss_oe,  spi2.nss_in));
    pinmux.connect_af(1, 13, 5, af_spi(spi2.sck_out,  spi2.sck_oe,  spi2.sck_in));
    pinmux.connect_af(1, 14, 5, af_spi(spi2.miso_out, spi2.miso_oe, spi2.miso_in));
    pinmux.connect_af(1, 15, 5, af_spi(spi2.mosi_out, spi2.mosi_oe, spi2.mosi_in));
    pinmux.connect_af(1, 10, 5, af_spi(spi2.sck_out,  spi2.sck_oe,  spi2.sck_in));
    pinmux.connect_af(2,  2, 5, af_spi(spi2.miso_out, spi2.miso_oe, spi2.miso_in));
    pinmux.connect_af(2,  3, 5, af_spi(spi2.mosi_out, spi2.mosi_oe, spi2.mosi_in));
    pinmux.connect_af(2,  6, 5, AfEndpoint{&spi2.mck_out, &spi2.mck_oe, nullptr, false});
    // SPI3 / I2S3 (AF6): PA4/PA15 NSS/WS, PB3/PC10 SCK/CK, PB4/PC11 MISO,
    //                    PB5/PC12 MOSI/SD, PC7 MCK
    pinmux.connect_af(0,  4, 6, af_spi(spi3.nss_out,  spi3.nss_oe,  spi3.nss_in));
    pinmux.connect_af(0, 15, 6, af_spi(spi3.nss_out,  spi3.nss_oe,  spi3.nss_in));
    pinmux.connect_af(1,  3, 6, af_spi(spi3.sck_out,  spi3.sck_oe,  spi3.sck_in));
    pinmux.connect_af(2, 10, 6, af_spi(spi3.sck_out,  spi3.sck_oe,  spi3.sck_in));
    pinmux.connect_af(1,  4, 6, af_spi(spi3.miso_out, spi3.miso_oe, spi3.miso_in));
    pinmux.connect_af(2, 11, 6, af_spi(spi3.miso_out, spi3.miso_oe, spi3.miso_in));
    pinmux.connect_af(1,  5, 6, af_spi(spi3.mosi_out, spi3.mosi_oe, spi3.mosi_in));
    pinmux.connect_af(2, 12, 6, af_spi(spi3.mosi_out, spi3.mosi_oe, spi3.mosi_in));
    pinmux.connect_af(2,  7, 6, AfEndpoint{&spi3.mck_out, &spi3.mck_oe, nullptr, false});
    // Bloques de extension: su dato va por el pin MISO del SPI padre, y el CK y
    // el WS los toman de los mismos pines que el bloque principal.
    // I2S2ext (AF6): SD en PB14 o PC2 [IR, §2.1]
    pinmux.connect_af(1, 14, 6, af_spi(i2s2ext.miso_out, i2s2ext.miso_oe, i2s2ext.miso_in));
    pinmux.connect_af(2,  2, 6, af_spi(i2s2ext.miso_out, i2s2ext.miso_oe, i2s2ext.miso_in));

    // I2S3ext (AF7): SD en PB4 o PC11 [IR, tabla AF: AF7 incluye I2S3ext]
    pinmux.connect_af(1,  4, 7, af_spi(i2s3ext.miso_out, i2s3ext.miso_oe, i2s3ext.miso_in));
    pinmux.connect_af(2, 11, 7, af_spi(i2s3ext.miso_out, i2s3ext.miso_oe, i2s3ext.miso_in));

    // TODO(F4/F5): resto de la tabla AF (TIM CHx, CAN, SDIO, FSMC, ETH, ULPI,
    //           DCMI, RTC_AF1...) conforme se implemente cada periférico.
}

} // namespace stm32
#endif // STM32_SOC_F4_MAPA_AF_H
