// =============================================================================
// soc_f4.h — TOP: instancia y conexiona el DIE de la familia STM32F4
//
// Este fichero es el CONTRATO DE INTEGRACIÓN del modelo: aquí se materializan
// las interconexiones del plan (doc/stm32f4xx/smt32f407vg_diseño.md §4): la matriz,
// decodificadores AHB1/AHB2, puentes APB, relojes, resets, las líneas de
// interrupción, las líneas EXTI, las peticiones DMA (tablas [IR, §11.4]) y los
// pines (pads + pin_mux). La frontera externa del MCU son los AnalogNet de
// pin_mux/power_pads.
//
// SE LLAMABA `Stm32F407VG` Y AHORA SE LLAMA `SocF4`. El cambio es de nombre, no
// de contenido, y lo pide la fase 2 del plan del F446: este netlist nunca fue
// «del F407VG». Ya servía, sin tocar una línea, para los once miembros de la
// familia F405/407 —cambiando el descriptor—, y sirve para el F446 por la misma
// razón: lo que aquí se conecta es **el die de la familia F4**, y lo que un
// miembro concreto tiene o no tiene lo dice su `McuCaps`.
//
// Que la clase se llamara como una de las piezas que construye era exactamente
// la clase de mentira que este proyecto persigue: invitaba a pensar que montar
// otro chip obligaba a copiar el fichero. `using Stm32F407VG = SocF4;`, al
// final, deja valer todo lo escrito hasta hoy.
//
// UN DIE SUPERCONJUNTO, y conviene decirlo claro: aquí se CONSTRUYEN todos los
// bloques que lleva cualquier miembro de la familia —Ethernet, RNG, CCM, los
// bloques de extensión del I2S— y el descriptor decide cuáles son ALCANZABLES.
// Un bloque que este chip no lleva se queda sin entrada en el decodificador y
// su ventana es espacio reservado, que es lo que pasa en el silicio; el módulo
// sigue construido porque la elaboración de SystemC es estática, y eso no lo
// hace visible desde el bus. Véase `tapa()`, más abajo.
// =============================================================================
#ifndef STM32_TOP_SOC_F4_H
#define STM32_TOP_SOC_F4_H

#include "../common/ahb_types.h"
#include "../common/clock_gen.h"
#include "../pins/pin_mux.h"
#include "../pins/power_pads.h"
#include "../bus/ahb_matrix.h"
#include "../bus/ahb_decoder.h"
#include "../mem/flash_if.h"
#include "../mem/sram.h"
#include "../rcc/rcc.h"
#include "../core/cortex_m4f.h"
#include "../periph/gpio_port.h"
#include "../periph/timers.h"
#include "../periph/usart.h"
#include "../periph/spi.h"
#include "../periph/i2c.h"
#include "../periph/can.h"
#include "../periph/adc.h"
#include "../periph/dac.h"
#include "../periph/watchdog.h"
#include "../periph/rtc.h"
#include "../periph/exti.h"
#include "../periph/syscfg.h"
#include "../periph/pwr.h"
#include "../periph/crc.h"
#include "../periph/rng.h"   // el F446 no lo lleva [vs_446re, 8.2]
#include "../periph/sdio.h"
#include "../periph/fsmc.h"
#include "../periph/dcmi.h"
#include "../periph/eth_mac.h"
#include "../periph/otg.h"
#include "../periph/dma.h"
#include <memory>
#include <string>
#include <vector>
#include "mcu_caps.h"

namespace stm32 {

// Puerta OR de 2 entradas para IRQs compartidas (TIM1/TIM9..., TIM6/DAC)
SC_MODULE(Or2) {
    sc_core::sc_in<bool> a{"a"}, b{"b"};
    sc_core::sc_out<bool> y{"y"};
    SC_CTOR(Or2) { SC_METHOD(run); sensitive << a << b; }
    void run() { y.write(a.read() || b.read()); }
};

SC_MODULE(SocF4) {
    // El DESCRIPTOR del chip: rasgos del núcleo, mapa de memoria y límites de
    // reloj. Va LO PRIMERO porque de él salen los tamaños con los que se
    // construyen las memorias y el núcleo, y por omisión es el del F407VG, de
    // modo que un modelo construido como siempre es el de siempre.
    // [top/mcu_caps.h]
    const McuCaps mcu;

    // =========================== Subcomponentes =============================
    PinMux    pinmux;
    PowerPads pwr_pads{"pwr_pads"};
    Rcc       rcc;
    // El núcleo, con sus rasgos de depuración: pines expuestos (por omisión) o
    // reservados con el stub interno enganchado al DAP. Véase core/cortex_m4f.h
    // y doc/stm32f4xx/stm32f407vg_fase6_gdb2.md.
    CortexM4F core;
    AhbMatrix matrix;

    // Las memorias, dimensionadas por el descriptor. Antes leían las constantes
    // globales del F407; ahora leen las de ESTE chip, que con el descriptor por
    // omisión son las mismas.
    FlashIf flash;
    Sram    sram1, sram2;
    BkpSram bkpsram;
    Ccm     ccm;

    AhbDecoder   ahb1_dec{"ahb1_dec"}, ahb2_dec{"ahb2_dec"};
    AhbDecoder   apb1_dec{"apb1_dec"}, apb2_dec{"apb2_dec"};
    AhbApbBridge br_apb1{"br_apb1"}, br_apb2{"br_apb2"};

    sc_core::sc_vector<GpioPort> gpio;   // A..I

    DmaCtrl dma1{"dma1", addr::DMA1_B, false};
    DmaCtrl dma2{"dma2", addr::DMA2_B, true};
    EthMac  eth{"eth"};
    OtgHs   otg_hs{"otg_hs"};
    OtgFs   otg_fs{"otg_fs"};
    Dcmi    dcmi{"dcmi"};
    Rng     rng{"rng"};
    CrcUnit crc{"crc"};
    Fsmc    fsmc{"fsmc"};

    // Los catorce temporizadores salen del MISMO modelo; el tipo de cada uno lo
    // fija el parámetro de plantilla con los rasgos de su familia (periph/timers.h).
    TimAdvanced tim1{"tim1", addr::TIM1_B},   tim8{"tim8", addr::TIM8_B};
    TimGp32     tim2{"tim2", addr::TIM2_B},   tim5{"tim5", addr::TIM5_B};
    TimGp16     tim3{"tim3", addr::TIM3_B},   tim4{"tim4", addr::TIM4_B};
    TimGp2Ch    tim9{"tim9", addr::TIM9_B},   tim12{"tim12", addr::TIM12_B};
    TimGp1Ch    tim10{"tim10", addr::TIM10_B}, tim11{"tim11", addr::TIM11_B};
    TimGp1Ch    tim13{"tim13", addr::TIM13_B}, tim14{"tim14", addr::TIM14_B};
    TimBasic    tim6{"tim6", addr::TIM6_B},   tim7{"tim7", addr::TIM7_B};

    Usart usart1{"usart1", addr::USART1_B}, usart2{"usart2", addr::USART2_B};
    Usart usart3{"usart3", addr::USART3_B};
    // UART4/5 son la variante reducida: el tipo lo dice (véase periph/usart.h)
    Uart  uart4{"uart4", addr::UART4_B}, uart5{"uart5", addr::UART5_B};
    Usart usart6{"usart6", addr::USART6_B};
    // Las cinco instancias del bloque SPI/I2S salen del MISMO modelo; el tipo de
    // cada una lo fija el parámetro de plantilla con sus rasgos (periph/spi.h).
    // EL SPI1 YA NO ES UN TIPO FIJO. En el F407 es un SPI puro; en el F446 el
    // mismo bloque trae la mitad de audio conectada y se llama I2S1. Como es el
    // MISMO IP con un rasgo distinto, lo que cambia es el rasgo —que llega por
    // el constructor— y no la clase. `caps().kind` dice cuál de los dos es.
    SpiBase spi1;                                        // SPI1 / I2S1
    SpiI2s spi2{"spi2", addr::SPI2_B}, spi3{"spi3", addr::SPI3_B};   // SPI + I2S
    I2sExt i2s2ext{"i2s2ext", addr::I2S2EXT_B};           // solo audio, esclavo
    I2sExt i2s3ext{"i2s3ext", addr::I2S3EXT_B};
    I2c   i2c1{"i2c1", addr::I2C1_B}, i2c2{"i2c2", addr::I2C2_B}, i2c3{"i2c3", addr::I2C3_B};
    // CAN1 es el MAESTRO de los 28 bancos de filtros; CAN2 no tiene ventana de
    // filtros propia y usa la de CAN1 [IR, §12.12]. La diferencia va en el tipo.
    Can1 can1{"can1", addr::CAN1_B};
    Can2 can2{"can2", addr::CAN2_B};

    AdcBlock adc{"adc"};
    Dac      dac{"dac"};
    Wwdg     wwdg{"wwdg"};
    Iwdg     iwdg{"iwdg"};
    Rtc      rtc{"rtc"};
    Exti     exti{"exti"};
    Syscfg   syscfg{"syscfg"};
    // El PWR, con o sin over-drive. Es el único rasgo suyo que cambia entre
    // las dos familias, y es el que hace que el F446 llegue a 180 MHz.
    Pwr      pwr;
    Sdio     sdio{"sdio"};

    // ============================== Señales =================================
    // Relojes
    sc_core::sc_signal<bool>   s_hclk{"s_hclk"}, s_pclk1{"s_pclk1"}, s_pclk2{"s_pclk2"};
    sc_core::sc_signal<bool>   s_timclk1{"s_timclk1"}, s_timclk2{"s_timclk2"};
    sc_core::sc_signal<bool>   s_pll48{"s_pll48"}, s_rtcclk{"s_rtcclk"};
    sc_core::sc_signal<bool>   s_lsiclk{"s_lsiclk"}, s_stk_ext{"s_stk_ext"};
    sc_core::sc_signal<double> s_lsi_hz{"s_lsi_hz"};
    sc_core::sc_signal<double> s_hclk_hz{"s_hclk_hz"}, s_pclk1_hz{"s_pclk1_hz"};
    sc_core::sc_signal<double> s_pclk2_hz{"s_pclk2_hz"}, s_timclk1_hz{"s_timclk1_hz"};
    sc_core::sc_signal<double> s_timclk2_hz{"s_timclk2_hz"}, s_pll48_hz{"s_pll48_hz"};
    sc_core::sc_signal<double> s_rtcclk_hz{"s_rtcclk_hz"};
    // Los relojes de núcleo de los periféricos dedicados del F446. Existen en
    // los dos chips porque el puerto del RCC existe en los dos; en un F407
    // valen cero y no los lee nadie.
    sc_core::sc_signal<double> s_sai1_hz{"s_sai1_hz"}, s_sai2_hz{"s_sai2_hz"};
    sc_core::sc_signal<double> s_fmpi2c1_hz{"s_fmpi2c1_hz"};
    sc_core::sc_signal<double> s_i2s1_hz{"s_i2s1_hz"};
    // Resets y gating
    sc_core::sc_signal<bool> s_sysrst_n{"s_sysrst_n"}, s_bkprst_n{"s_bkprst_n"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_prst{"s_prst", P_COUNT};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_pcen{"s_pcen", P_COUNT};
    // IRQ / NMI / eventos. El TAMAÑO sale del descriptor y no de una constante
    // global: el F407 tiene 82 posiciones y el F446 tiene 97, y las quince de
    // más se quedan sin nadie que las gobierne, que es exactamente lo que es una
    // posición reservada de la tabla de vectores. [core/core_caps.h]
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq;
    sc_core::sc_signal<bool> s_nmi{"s_nmi"}, s_evt_in{"s_evt_in"}, s_evt_out{"s_evt_out"};
    // Núcleo / sistema
    sc_core::sc_signal<bool> s_sysresetreq{"s_sysresetreq"};
    sc_core::sc_signal<bool> s_sleeping{"s_sleeping"}, s_sleepdeep{"s_sleepdeep"};
    sc_core::sc_signal<uint8_t> s_boot{"s_boot"}, s_memmode{"s_memmode"};
    // PowerPads / PWR
    sc_core::sc_signal<bool>   s_por_ok{"s_por_ok"}, s_nrst_n{"s_nrst_n"};
    sc_core::sc_signal<bool>   s_boot0{"s_boot0"}, s_nrst_drv{"s_nrst_drv"};
    sc_core::sc_signal<double> s_vdd{"s_vdd"}, s_vdda{"s_vdda"}, s_vbat{"s_vbat"};
    sc_core::sc_signal<bool>    s_bor_trip{"s_bor_trip"};
    sc_core::sc_signal<uint8_t> s_bor_lev{"s_bor_lev"};   // option bytes -> BOR
    // El over-drive del PWR hacia el RCC (solo se mueve en los chips que lo
    // tienen). Va con las señales de energía porque de ahí viene.
    sc_core::sc_signal<bool>   s_over_drive{"s_over_drive"};
    sc_core::sc_signal<bool>   s_dbp{"s_dbp"}, s_pvd_line{"s_pvd_line"};
    sc_core::sc_signal<bool>   s_wwdg_rr{"s_wwdg_rr"}, s_iwdg_rr{"s_iwdg_rr"};
    sc_core::sc_signal<bool>   s_iwdg_lsi{"s_iwdg_lsi"};
    // EXTI
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_exti_gpio{"s_exti_gpio",
                                                             N_GPIO_PORTS * N_PORT_PINS};
    sc_core::sc_vector<sc_core::sc_signal<uint8_t>> s_exticr{"s_exticr", 16};
    sc_core::sc_signal<bool> s_rtc_l17{"s_rtc_l17"}, s_rtc_l21{"s_rtc_l21"},
                             s_rtc_l22{"s_rtc_l22"}, s_fswk_l18{"s_fswk_l18"},
                             s_ethwk_l19{"s_ethwk_l19"}, s_hswk_l20{"s_hswk_l20"};
    sc_core::sc_signal<bool> s_exti_wakeup{"s_exti_wakeup"};
    // ---- Bajo consumo [IR, §14] -------------------------------------------
    sc_core::sc_signal<uint8_t> s_lp_mode{"s_lp_mode"};      // LpMode (PWR -> todos)
    sc_core::sc_signal<bool>    s_stop_req{"s_stop_req"}, s_standby{"s_standby"};
    sc_core::sc_signal<bool>    s_ewup{"s_ewup"}, s_bre{"s_bre"};
    sc_core::sc_signal<unsigned> s_periph_on{"s_periph_on"};
    sc_core::sc_signal<uint8_t> s_dbg_lp{"s_dbg_lp"};        // DBGMCU_CR[2:0]
    sc_core::sc_signal<bool>    s_dbg_stby{"s_dbg_stby"};    // DBG_STANDBY suelto
    sc_core::sc_signal<double>  s_idd{"s_idd"}, s_ibat{"s_ibat"};
    // Debug / freeze
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_freeze{"s_freeze", FZ_COUNT};
    sc_core::sc_signal<bool> s_swdio_o{"s_swdio_o"}, s_swdio_oe{"s_swdio_oe"},
                             s_jtdo{"s_jtdo"};
    // Entradas de depuración: las entrega el mux de AF0 (PA13/14/15, PB4), no
    // el pad directamente, para que reconfigurar el pin como GPIO desconecte el
    // puerto de depuración igual que en el silicio [IR, §13.1; §3.3.3].
    sc_core::sc_signal<bool> s_dbg_swclk{"s_dbg_swclk"}, s_dbg_swdio_i{"s_dbg_swdio_i"},
                             s_dbg_jtdi{"s_dbg_jtdi"}, s_dbg_njtrst{"s_dbg_njtrst"};
    // DMA req/ack
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_dma1_ack{"s_dma1_ack", 64};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_dma2_ack{"s_dma2_ack", 64};
    // Peticiones con nombre (una señal por fuente; varias celdas pueden leerla)
    sc_core::sc_signal<bool>
        q_spi2_rx{"q_spi2_rx"}, q_spi2_tx{"q_spi2_tx"},
        q_spi3_rx{"q_spi3_rx"}, q_spi3_tx{"q_spi3_tx"},
        q_spi1_rx{"q_spi1_rx"}, q_spi1_tx{"q_spi1_tx"},
        q_i2c1_rx{"q_i2c1_rx"}, q_i2c1_tx{"q_i2c1_tx"},
        q_i2c2_rx{"q_i2c2_rx"}, q_i2c2_tx{"q_i2c2_tx"},
        q_i2c3_rx{"q_i2c3_rx"}, q_i2c3_tx{"q_i2c3_tx"},
        q_u1_rx{"q_u1_rx"}, q_u1_tx{"q_u1_tx"}, q_u2_rx{"q_u2_rx"}, q_u2_tx{"q_u2_tx"},
        q_u3_rx{"q_u3_rx"}, q_u3_tx{"q_u3_tx"}, q_u4_rx{"q_u4_rx"}, q_u4_tx{"q_u4_tx"},
        q_u5_rx{"q_u5_rx"}, q_u5_tx{"q_u5_tx"}, q_u6_rx{"q_u6_rx"}, q_u6_tx{"q_u6_tx"},
        q_adc1{"q_adc1"}, q_adc2{"q_adc2"}, q_adc3{"q_adc3"},
        q_dac1{"q_dac1"}, q_dac2{"q_dac2"}, q_sdio{"q_sdio"}, q_dcmi{"q_dcmi"},
        q_tim6_up{"q_tim6_up"}, q_tim7_up{"q_tim7_up"},
        // Los bloques de extension del I2S. Sus celdas de DMA existen -la base
        // de datos de ST las tiene, y el RM0090 tambien-; lo que pasaba es que
        // el informe interno de este proyecto no las recogia, y por eso
        // estuvieron al aire hasta la fase 6.
        q_i2s2ext_rx{"q_i2s2ext_rx"}, q_i2s2ext_tx{"q_i2s2ext_tx"},
        q_i2s3ext_rx{"q_i2s3ext_rx"}, q_i2s3ext_tx{"q_i2s3ext_tx"},
        // LAS DEL F446. Viven aqui y no en `Stm32F446` por una razon de
        // elaboracion: `bind_dma_requests()` corre en el constructor de ESTA
        // clase, cuando los perifericos de la derivada todavia no existen, y un
        // `sc_in` no se reata. Asi que el DMA se ata a la senal, y la derivada
        // engancha su periferico a la MISMA senal cuando le toca construirse.
        q_fmpi2c1_rx{"q_fmpi2c1_rx"}, q_fmpi2c1_tx{"q_fmpi2c1_tx"},
        q_sai1_a{"q_sai1_a"}, q_sai1_b{"q_sai1_b"},
        q_sai2_a{"q_sai2_a"}, q_sai2_b{"q_sai2_b"},
        q_spi4_rx{"q_spi4_rx"}, q_spi4_tx{"q_spi4_tx"},
        q_qspi{"q_qspi"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> q_tim_cc{"q_tim_cc", 6 * 4}; // tim1/2/3/4/5/8 x ch
    sc_core::sc_signal<bool> q_tim_up[6];   // tim1/2/3/4/5/8 UP
    sc_core::sc_signal<bool> q_tim1_trig{"q_tim1_trig"}, q_tim8_trig{"q_tim8_trig"};
    // Triggers / trgo
    sc_core::sc_signal<bool> s_trgo[8];     // tim1,2,3,4,5,6,7,8
    sc_core::sc_signal<bool> s_mii{"s_mii"};
    // Constantes y sumidero de no-conectados
    sc_core::sc_signal<bool> s_false{"s_false"}, s_true{"s_true"};
    sc_core::sc_signal<double> s_zero_hz{"s_zero_hz"};   // dominio sin reloj
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_nc{"s_nc", 256};
    unsigned nc_i_ = 0;
    // OR de IRQs compartidas
    Or2 or_irq24{"or_irq24"}, or_irq25{"or_irq25"}, or_irq26{"or_irq26"};
    Or2 or_irq43{"or_irq43"}, or_irq44{"or_irq44"}, or_irq45{"or_irq45"};
    Or2 or_irq54{"or_irq54"};
    // Los bloques de extension del I2S comparten el vector de su SPI padre
    Or2 or_spi2{"or_spi2"}, or_spi3{"or_spi3"};
    sc_core::sc_signal<bool> s_or_in[20];

    // ============================ Construcción ==============================
    // `cab` es el cableado de la PLACA: los pines cuyo nodo eléctrico no es
    // suyo sino compartido con otro pad —un puente de placa entre dos pines de
    // este mismo chip, o un hilo que comparte con otro MCU—. Tiene que llegar
    // por el constructor porque `Pad::net` es un `sc_port` y un `sc_port` no se
    // reata; vacío, el MCU se construye exactamente igual que siempre.
    // [doc/multi_mcu.md, §4.3]
    explicit SocF4(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
                         const Cableado& cab = Cableado(),
                         McuCaps caps = MCU_STM32F407VG)
        : sc_core::sc_module(nm), mcu(caps), pinmux("pinmux", cab, caps.enc),
          rcc("rcc", caps.reloj, caps.arbol, caps.perif.alguno_f446()),
          core("core", dbg, caps.nucleo, caps.memoria.ram),
          matrix("matrix", caps.memoria.ram, caps.perif.fsmc, caps.conn,
                 caps.perif.quadspi),
          flash("flash", caps.memoria.flash),
          sram1("sram1", caps.memoria.ram.sram1_base, caps.memoria.ram.sram1_size),
          sram2("sram2", caps.memoria.ram.sram2_base, caps.memoria.ram.sram2_size),
          bkpsram("bkpsram", caps.memoria.ram.bkp_base, caps.memoria.ram.bkp_size),
          ccm("ccm", caps.memoria.ram.ccm_base, caps.memoria.ram.ccm_size),
          gpio("gpio", N_GPIO_PORTS, [](const char* n, size_t i) {
                   return new GpioPort(n, unsigned(i)); }),
          spi1("spi1", addr::SPI1_B,
               caps.perif.i2s1 ? CAPS_SPI_APB2_I2S : CAPS_SPI_APB2),
          pwr("pwr", caps.arbol.over_drive),
          s_irq("s_irq", caps.nucleo.n_irq) {
        SC_HAS_PROCESS(SocF4);
        bind_clocks_resets();
        bind_bus();
        bind_core();
        bind_gpio_pins();
        bind_periph_common();
        bind_irqs();
        bind_dma_requests();
        bind_analog();
        SC_THREAD(init_proc);
        // El apagado del dominio de 1,2 V y el bit suelto que necesita el mux
        SC_METHOD(standby_proc); sensitive << s_standby; dont_initialize();
        SC_METHOD(dbg_stby_proc); sensitive << s_dbg_lp;
    }

    // -----------------------------------------------------------------------
    // Entrada en Standby: se APAGA el dominio de 1,2 V y con él se va todo lo
    // que vivía allí. Es el efecto más brutal de los cuatro modos y el que más
    // sorprende al firmware: al volver, la SRAM no vale nada [IR, §14.5.2].
    //
    // Sobreviven, y por eso NO se tocan aquí: el dominio de backup (RTC y sus
    // registros), el PWR, y la BKPSRAM si -y solo si- el regulador de backup
    // está encendido, que es exactamente para lo que existe BRE.
    // -----------------------------------------------------------------------
    void standby_proc() {
        if (!s_standby.read()) return;
        sram1.pierde_contenido();
        sram2.pierde_contenido();
        ccm.pierde_contenido();
        if (!s_bre.read()) bkpsram.pierde_contenido();
    }
    void dbg_stby_proc() { s_dbg_stby.write((s_dbg_lp.read() & 4u) != 0); }

    // -----------------------------------------------------------------------
    // LO QUE ESTE MODELO TODAVIA NO HACE
    //
    // Para los once del F405/407 la lista esta vacia, y no por optimismo: lo
    // que falta en ellos -las revisiones del silicio, el acelerador
    // criptografico del F415- son chips que este catalogo no nombra, no
    // agujeros de los que si nombra. Una clase derivada que modele un chip a
    // medias tiene que decirlo aqui, y `sim` lo imprime al montar la placa.
    // -----------------------------------------------------------------------
    virtual std::vector<std::string> limitaciones() const { return {}; }

    // Muestreo de los pines de arranque: BOOT0 (pin dedicado) y BOOT1 (PB2) se
    // capturan en el 4º flanco ascendente de SYSCLK tras la salida de reset y
    // conservan su valor hasta el siguiente reset [IR, §2.3].
    //
    // Mientras el reset está activo el valor sigue a los pines, de modo que
    // SYSCFG_MEMRMP ya es correcto en el instante en que el núcleo sale de
    // reset y lee la tabla de vectores; el latch del 4º flanco lo congela.
    uint8_t sample_boot() const {
        const uint8_t b0 = s_boot0.read() ? 1u : 0u;
        const uint8_t b1 = pinmux.pad_din[1 * N_PORT_PINS + 2].read() ? 2u : 0u;
        return uint8_t(b1 | b0);
    }
    void init_proc() {
        s_true.write(true); s_false.write(false);
        s_boot.write(0);
        for (;;) {
            // Reset activo: seguimiento continuo de los pines
            while (!s_sysrst_n.read()) {
                s_boot.write(sample_boot());
                wait(s_sysrst_n.value_changed_event() |
                     s_boot0.value_changed_event() |
                     pinmux.pad_din[1 * N_PORT_PINS + 2].value_changed_event());
            }
            // Reset liberado: el 4º flanco de SYSCLK congela el valor
            for (unsigned i = 0; i < 4; ++i) wait(s_hclk.posedge_event());
            s_boot.write(sample_boot());
            wait(s_sysrst_n.negedge_event());
        }
    }

// A PARTIR DE AQUI, PROTEGIDO Y NO PRIVADO. La diferencia importa desde la
// fase 4: `Stm32F446` es una clase DERIVADA que añade sus propios periféricos
// -el FMPI2C1, los dos SAI, el QUADSPI...- y para engancharlos necesita lo
// mismo que usa el die: `bind_bus_slave()` para el reloj y el gating, `tapa()`
// para lo que no lleva camino, y `nc()` para las salidas que nadie escucha.
// Dejarlo privado habría obligado a duplicar esas tres cosas en la derivada.
protected:
    unsigned nc() { return nc_i_++; }   // siguiente señal de no-conectado

    void bind_clocks_resets();
    void bind_bus();
    // El mapa de funciones alternativas de los periféricos de comunicación.
    // Vive en `soc/f4_mapa_af.h`: es el mapa DE LA FAMILIA, no del top.
    void bind_mapa_af();

    // ---- «Tapar» el socket de un periférico que este chip NO lleva ---------
    //
    // La elaboración de SystemC es estática: el módulo del Ethernet existe
    // aunque el chip no lo tenga, y un socket de destino con su puerto sin atar
    // es un error de elaboración. Así que se le ata un iniciador que NO MANDA
    // NADA NUNCA.
    //
    // No es un apaño: es exactamente lo que se quiere decir. El periférico
    // queda sin camino desde el bus —su ventana no está en ningún
    // decodificador, y tocarla da error, igual que en el silicio—, y el
    // iniciador mudo solo existe para que la elaboración cierre. Alternativa
    // descartada: decodificar la ventana hacia un destino que devuelva error.
    // Se vería igual desde el firmware, pero sería mentira en el netlist —el
    // volcado enseñaría un periférico donde no lo hay—.
    std::vector<std::unique_ptr<tlm_utils::simple_initiator_socket<SocF4>>> tapones_;
    template <class Socket>
    void tapa(Socket& tsk, const char* nombre) {
        tapones_.emplace_back(
            new tlm_utils::simple_initiator_socket<SocF4>(nombre));
        tapones_.back()->bind(tsk);
    }
    void bind_core();
    void bind_gpio_pins();
    void bind_periph_common();
    void bind_irqs();
    void bind_dma_requests();
    void bind_analog();

    // Ayudas de binding uniforme
    // La frecuencia del dominio se deduce del reloj enlazado, de modo que cada
    // esclavo anota sus accesos en ciclos de SU bus [IR, §4.4, §6.4].
    void bind_bus_slave(BusSlave& p, sc_core::sc_signal<bool>& clk, PeriphId id) {
        p.clk(clk); p.clk_hz(domain_hz_of(clk));
        p.rst_n(s_prst[id]); p.clk_en(s_pcen[id]);
        p.clk_en_live = rcc.clk_en_ptr(id);
    }
    sc_core::sc_signal<double>& domain_hz_of(sc_core::sc_signal<bool>& clk) {
        if (&clk == &s_pclk1) return s_pclk1_hz;
        if (&clk == &s_pclk2) return s_pclk2_hz;
        return s_hclk_hz;
    }
};

// ===========================================================================
// Relojes, resets y RCC
// ===========================================================================
inline void SocF4::bind_clocks_resets() {
    rcc.hclk(s_hclk);       rcc.hclk_hz(s_hclk_hz);
    rcc.pclk1(s_pclk1);     rcc.pclk1_hz(s_pclk1_hz);
    rcc.pclk2(s_pclk2);     rcc.pclk2_hz(s_pclk2_hz);
    rcc.timclk1(s_timclk1); rcc.timclk1_hz(s_timclk1_hz);
    rcc.timclk2(s_timclk2); rcc.timclk2_hz(s_timclk2_hz);
    rcc.pll48ck(s_pll48);   rcc.pll48ck_hz(s_pll48_hz);
    rcc.rtcclk(s_rtcclk);   rcc.rtcclk_hz(s_rtcclk_hz);
    rcc.lsi_clk(s_lsiclk);  rcc.lsi_hz(s_lsi_hz);
    rcc.systick_ext(s_stk_ext);
    rcc.sai1_hz(s_sai1_hz); rcc.sai2_hz(s_sai2_hz); rcc.fmpi2c1_hz(s_fmpi2c1_hz);
    rcc.i2s1_hz(s_i2s1_hz);
    for (unsigned i = 0; i < P_COUNT; ++i) {
        rcc.periph_clk_en[i](s_pcen[i]);
        rcc.periph_rst_n[i](s_prst[i]);
    }
    rcc.sys_rst_n(s_sysrst_n);
    rcc.bkp_rst_n(s_bkprst_n);
    rcc.drive_nrst_low(s_nrst_drv);
    rcc.por_ok(s_por_ok);
    rcc.nrst_in_n(s_nrst_n);
    rcc.wwdg_rst_req(s_wwdg_rr);
    rcc.iwdg_rst_req(s_iwdg_rr);
    rcc.iwdg_lsi_req(s_iwdg_lsi);
    rcc.sysresetreq(s_sysresetreq);
    // Bajo consumo: el PWR manda y el RCC obedece [IR, §14]
    rcc.lp_mode(s_lp_mode);
    rcc.stop_req(s_stop_req);
    rcc.standby_req(s_standby);
    rcc.periph_on(s_periph_on);
    rcc.nmi_css(s_nmi);
    rcc.irq(s_irq[5]);
    // RCC como esclavo de su propio dominio AHB1 (gating siempre activo)
    rcc.clk(s_hclk); rcc.clk_hz(s_hclk_hz); rcc.rst_n(s_sysrst_n); rcc.clk_en(s_true);

    pwr_pads.por_ok(s_por_ok);
    pwr_pads.bor_trip(s_bor_trip);
    pwr_pads.bor_lev(s_bor_lev);
    pwr_pads.vbat_lvl(s_vbat);
    rcc.bor_rst(s_bor_trip);
    pwr_pads.nrst_in_n(s_nrst_n);
    pwr_pads.boot0_lvl(s_boot0);
    pwr_pads.vdd_lvl(s_vdd);
    pwr_pads.vdda_lvl(s_vdda);
    pwr_pads.drive_nrst_low(s_nrst_drv);
    // El consumo del MCU, como carga real sobre los pines de alimentación
    pwr_pads.idd_req(s_idd);
    pwr_pads.ibat_req(s_ibat);
    // Standby: los pines de puerto quedan en alta impedancia salvo WKUP y,
    // si DBGMCU lo pide, los de depuración [IR, §14.5.2]
    pinmux.standby(s_standby);
    pinmux.wkup_en(s_ewup);
    pinmux.dbg_pins(s_dbg_stby);
    // Cristales externos hacia HSE/LSE por ruta analógica [plan P4]
    rcc.hse.xtal_in = &pinmux.analog(7, 0);    // PH0
    rcc.lse.xtal_in = &pinmux.analog(2, 14);   // PC14
}

// ===========================================================================
// Matriz, decodificadores y puentes
// ===========================================================================
inline void SocF4::bind_bus() {
    matrix.hclk(s_hclk); matrix.hclk_hz(s_hclk_hz); matrix.rst_n(s_sysrst_n);
    // Maestros [IR, §6.1.1]
    core.icode.bind(matrix.from_master[unsigned(BusMaster::CORE_IBUS)]);
    core.dcode.bind(matrix.from_master[unsigned(BusMaster::CORE_DBUS)]);
    core.sbus.bind(matrix.from_master[unsigned(BusMaster::CORE_SBUS)]);
    dma1.mem_m.bind(matrix.from_master[unsigned(BusMaster::DMA1_MEM)]);
    dma2.mem_m.bind(matrix.from_master[unsigned(BusMaster::DMA2_MEM)]);
    dma2.periph_m.bind(matrix.from_master[unsigned(BusMaster::DMA2_PERIPH)]);
    eth.dma_m.bind(matrix.from_master[unsigned(BusMaster::ETH_DMA)]);
    otg_hs.dma_m.bind(matrix.from_master[unsigned(BusMaster::OTG_HS_DMA)]);
    // DMA1 puerto de periféricos: directo a APB1 (sin matriz) [IR, §11.1.1]
    dma1.periph_m.bind(apb1_dec.tsk2);
    // Esclavos [IR, §6.1.2]
    matrix.to_slave[unsigned(BusSlaveId::FLASH_ICODE)].bind(flash.icode);
    matrix.to_slave[unsigned(BusSlaveId::FLASH_DCODE)].bind(flash.dcode);
    matrix.to_slave[unsigned(BusSlaveId::SRAM1)].bind(sram1.tsk);
    matrix.to_slave[unsigned(BusSlaveId::SRAM2)].bind(sram2.tsk);
    matrix.to_slave[unsigned(BusSlaveId::AHB1_SEG)].bind(ahb1_dec.tsk);
    matrix.to_slave[unsigned(BusSlaveId::AHB2_SEG)].bind(ahb2_dec.tsk);
    // EL PUERTO DE MEMORIA EXTERNA. En el F407 es del FSMC y punto. En el F446
    // ese mismo puerto es «FMC / QUADSPI», así que cuando el chip lleva QUADSPI
    // lo ata la clase derivada -a un decodificador suyo, porque ahí caben dos
    // ventanas- y el FSMC se queda tapado. La elaboración de SystemC no permite
    // reatar un socket, de modo que la decisión tiene que tomarse aquí.
    if (mcu.perif.quadspi) {
        tapa(fsmc.mem, "nc_fsmc_mem");
    } else {
        matrix.to_slave[unsigned(BusSlaveId::FSMC_EXT)].bind(fsmc.mem);
    }

    // Segmento AHB1 [IR, §6.5]
    for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
        ahb1_dec.add_slave(("to_gpio" + std::string(1, 'a' + p)).c_str(),
                           addr::GPIOA_B + 0x400 * p, 0x400)->bind(gpio[p].tsk);
    ahb1_dec.add_slave("to_crc", addr::CRC_B, 0x400)->bind(crc.tsk);
    ahb1_dec.add_slave("to_rcc", addr::RCC_B, 0x400)->bind(rcc.tsk);
    ahb1_dec.add_slave("to_flashif", addr::FLASHIF_B, 0x400)->bind(flash.regs);
    ahb1_dec.add_slave("to_bkpsram", addr::BKPSRAM_BASE, addr::BKPSRAM_SIZE)
        ->bind(bkpsram.tsk);
    ahb1_dec.add_slave("to_dma1", addr::DMA1_B, 0x400)->bind(dma1.tsk);
    ahb1_dec.add_slave("to_dma2", addr::DMA2_B, 0x400)->bind(dma2.tsk);
    // Un periferico que este miembro de la familia NO lleva no se decodifica,
    // y eso es exactamente lo que pasa en el silicio: en un F405 la ventana
    // del Ethernet es espacio RESERVADO, y tocarla da error de bus. Que el
    // modulo siga construido -la elaboracion de SystemC es estatica- no lo
    // hace visible: sin entrada en el decodificador no hay camino hasta el.
    if (mcu.perif.eth)
        ahb1_dec.add_slave("to_eth", addr::ETH_B, 0x1400)->bind(eth.tsk);
    else
        tapa(eth.tsk, "nc_eth");
    ahb1_dec.add_slave("to_otghs", addr::OTG_HS_B, 0x40000)->bind(otg_hs.tsk);
    ahb1_dec.add_slave("to_apb1", 0x40000000, 0x8000)->bind(br_apb1.ahb);
    // EL SEGMENTO APB2 NO MIDE LO MISMO EN LAS DOS PIEZAS. En el F407 termina
    // en 0x4001_57FF, y ahi se acaba; en el F446 sigue 2 KB mas porque detras
    // del ultimo temporizador estan los dos SAI (0x4001_5800 y 0x4001_5C00).
    // Es la clase de detalle que no da error sino silencio: con la ventana
    // corta, el puente APB2 no reclama esas direcciones, el decodificador de
    // AHB1 no encuentra a nadie, y el SAI -que esta perfectamente construido y
    // dado de alta en el decodificador de APB2- no recibe un solo acceso.
    ahb1_dec.add_slave("to_apb2", 0x40010000,
                       mcu.perif.sai ? 0x6000 : 0x5800)->bind(br_apb2.ahb);
    // Segmento AHB2
    ahb2_dec.add_slave("to_otgfs", addr::OTG_FS_B, 0x40000)->bind(otg_fs.tsk);
    if (mcu.perif.dcmi)
        ahb2_dec.add_slave("to_dcmi", addr::DCMI_B, 0x400)->bind(dcmi.tsk);
    else
        tapa(dcmi.tsk, "nc_dcmi");
    if (mcu.perif.rng)
        ahb2_dec.add_slave("to_rng", addr::RNG_B, 0x400)->bind(rng.tsk);
    else
        tapa(rng.tsk, "nc_rng");
    // Puentes y decodificadores APB
    br_apb1.pclk(s_pclk1); br_apb1.pclk_hz(s_pclk1_hz); br_apb1.apb.bind(apb1_dec.tsk);
    br_apb2.pclk(s_pclk2); br_apb2.pclk_hz(s_pclk2_hz); br_apb2.apb.bind(apb2_dec.tsk);
    // APB1 [IR, §6.5]
    apb1_dec.add_slave("to_tim2", addr::TIM2_B, 0x400)->bind(tim2.tsk);
    apb1_dec.add_slave("to_tim3", addr::TIM3_B, 0x400)->bind(tim3.tsk);
    apb1_dec.add_slave("to_tim4", addr::TIM4_B, 0x400)->bind(tim4.tsk);
    apb1_dec.add_slave("to_tim5", addr::TIM5_B, 0x400)->bind(tim5.tsk);
    apb1_dec.add_slave("to_tim6", addr::TIM6_B, 0x400)->bind(tim6.tsk);
    apb1_dec.add_slave("to_tim7", addr::TIM7_B, 0x400)->bind(tim7.tsk);
    apb1_dec.add_slave("to_tim12", addr::TIM12_B, 0x400)->bind(tim12.tsk);
    apb1_dec.add_slave("to_tim13", addr::TIM13_B, 0x400)->bind(tim13.tsk);
    apb1_dec.add_slave("to_tim14", addr::TIM14_B, 0x400)->bind(tim14.tsk);
    apb1_dec.add_slave("to_rtc", addr::RTC_B, 0x400)->bind(rtc.tsk);
    apb1_dec.add_slave("to_wwdg", addr::WWDG_B, 0x400)->bind(wwdg.tsk);
    apb1_dec.add_slave("to_iwdg", addr::IWDG_B, 0x400)->bind(iwdg.tsk);
    apb1_dec.add_slave("to_spi2", addr::SPI2_B, 0x400)->bind(spi2.tsk);
    apb1_dec.add_slave("to_spi3", addr::SPI3_B, 0x400)->bind(spi3.tsk);
    // Los bloques de extension del I2S, que dan el full-duplex en el F407 y
    // DESAPARECEN en el F446 [AN4658]. Y ojo con la ventana del I2S3ext,
    // 0x4000_4000: en el F446 no esta vacia, la ocupa el SPDIF-RX, que es otro
    // periferico. Mientras ese no este modelado, ahi no hay nada, y es mejor
    // que haya un error de bus que un I2S3ext contestando donde el silicio
    // tiene otra cosa. [vs_446re, 5.3 -- la trampa de direcciones]
    if (mcu.perif.i2sext) {
        apb1_dec.add_slave("to_i2s2ext", addr::I2S2EXT_B, 0x400)->bind(i2s2ext.tsk);
        apb1_dec.add_slave("to_i2s3ext", addr::I2S3EXT_B, 0x400)->bind(i2s3ext.tsk);
    } else {
        tapa(i2s2ext.tsk, "nc_i2s2ext");
        tapa(i2s3ext.tsk, "nc_i2s3ext");
    }
    apb1_dec.add_slave("to_usart2", addr::USART2_B, 0x400)->bind(usart2.tsk);
    apb1_dec.add_slave("to_usart3", addr::USART3_B, 0x400)->bind(usart3.tsk);
    apb1_dec.add_slave("to_uart4", addr::UART4_B, 0x400)->bind(uart4.tsk);
    apb1_dec.add_slave("to_uart5", addr::UART5_B, 0x400)->bind(uart5.tsk);
    apb1_dec.add_slave("to_i2c1", addr::I2C1_B, 0x400)->bind(i2c1.tsk);
    apb1_dec.add_slave("to_i2c2", addr::I2C2_B, 0x400)->bind(i2c2.tsk);
    apb1_dec.add_slave("to_i2c3", addr::I2C3_B, 0x400)->bind(i2c3.tsk);
    apb1_dec.add_slave("to_can1", addr::CAN1_B, 0x400)->bind(can1.tsk);
    apb1_dec.add_slave("to_can2", addr::CAN2_B, 0x400)->bind(can2.tsk);
    apb1_dec.add_slave("to_pwr", addr::PWR_B, 0x400)->bind(pwr.tsk);
    apb1_dec.add_slave("to_dac", addr::DAC_B, 0x400)->bind(dac.tsk);
    // APB2
    apb2_dec.add_slave("to_tim1", addr::TIM1_B, 0x400)->bind(tim1.tsk);
    apb2_dec.add_slave("to_tim8", addr::TIM8_B, 0x400)->bind(tim8.tsk);
    apb2_dec.add_slave("to_usart1", addr::USART1_B, 0x400)->bind(usart1.tsk);
    apb2_dec.add_slave("to_usart6", addr::USART6_B, 0x400)->bind(usart6.tsk);
    apb2_dec.add_slave("to_adc", addr::ADC_B, 0x400)->bind(adc.tsk);
    apb2_dec.add_slave("to_sdio", addr::SDIO_B, 0x400)->bind(sdio.tsk);
    apb2_dec.add_slave("to_spi1", addr::SPI1_B, 0x400)->bind(spi1.tsk);
    apb2_dec.add_slave("to_syscfg", addr::SYSCFG_B, 0x400)->bind(syscfg.tsk);
    apb2_dec.add_slave("to_exti", addr::EXTI_B, 0x400)->bind(exti.tsk);
    apb2_dec.add_slave("to_tim9", addr::TIM9_B, 0x400)->bind(tim9.tsk);
    apb2_dec.add_slave("to_tim10", addr::TIM10_B, 0x400)->bind(tim10.tsk);
    apb2_dec.add_slave("to_tim11", addr::TIM11_B, 0x400)->bind(tim11.tsk);
    // FSMC (los registros 0xA000_0000 se decodifican dentro de fsmc.mem).
    // Ya NO hay memoria TLM detrás: lo que conteste al bus externo tiene que
    // estar SOLDADO A LOS PINES, como en la placa. Véase parts/ext_parts.h.
    fsmc.hclk(s_hclk); fsmc.hclk_hz(s_hclk_hz);
    fsmc.rst_n(s_prst[P_FSMC]); fsmc.clk_en(s_pcen[P_FSMC]);
}

// ===========================================================================
// Núcleo
// ===========================================================================
inline void SocF4::bind_core() {
    core.fclk(s_hclk); core.fclk_hz(s_hclk_hz);
    core.systick_ext(s_stk_ext); core.rst_n(s_sysrst_n);
    for (unsigned i = 0; i < mcu.nucleo.n_irq; ++i) core.irq_in[i](s_irq[i]);
    core.nmi_in(s_nmi);
    core.sysresetreq(s_sysresetreq);
    core.sleeping(s_sleeping); core.sleepdeep(s_sleepdeep);
    core.event_in(s_evt_in);   core.event_out(s_evt_out);
    core.boot_mode(s_memmode);
    core.ccm.bind(ccm.tsk);
    // Quien es este chip, para el depurador. Es el UNICO valor del subsistema
    // de depuracion que cambia de una pieza a otra, y va aqui porque el que lo
    // sabe es el descriptor. [core/debug.h; vs_446re, 10]
    core.debug.set_idcode(mcu.idcode);
    // Debug: pines AF0 (PA13/14/15, PB3/PB4) [IR, §13.1]. Las señales llegan
    // por el mux de funciones alternativas (véase bind_gpio_pins).
    core.debug.swclk_tck(s_dbg_swclk);                   // PA14
    core.debug.swdio_in(s_dbg_swdio_i);                  // PA13
    core.debug.swdio_out(s_swdio_o);
    core.debug.swdio_oe(s_swdio_oe);
    core.debug.jtdi(s_dbg_jtdi);                         // PA15
    core.debug.jtdo_swo(s_jtdo);                         // PB3
    core.debug.njtrst(s_dbg_njtrst);                     // PB4
    core.debug.dbg_lp(s_dbg_lp);                         // DBGMCU_CR[2:0]
    for (unsigned i = 0; i < FZ_COUNT; ++i) core.debug.freeze[i](s_freeze[i]);
    // (debug.ahb_ap queda enlazado al router dentro de CortexM4F)

    ccm.hclk(s_hclk); ccm.rst_n(s_sysrst_n);
    sram1.hclk(s_hclk); sram1.rst_n(s_sysrst_n);
    sram2.hclk(s_hclk); sram2.rst_n(s_sysrst_n);
    bkpsram.hclk(s_hclk); bkpsram.rst_n(s_bkprst_n);
    flash.hclk(s_hclk); flash.hclk_hz(s_hclk_hz); flash.rst_n(s_sysrst_n);
    flash.irq(s_irq[4]);
    flash.bor_lev(s_bor_lev);          // OPTCR.BOR_LEV -> supervisor de VDD
}

// ===========================================================================
// GPIO <-> pads y líneas EXTI
// ===========================================================================
inline void SocF4::bind_gpio_pins() {
    for (unsigned p = 0; p < N_GPIO_PORTS; ++p) {
        bind_bus_slave(gpio[p], s_hclk, PeriphId(P_GPIOA + p));
        gpio[p].mux = &pinmux;                    // publicación de MODER/AFRx
        for (unsigned i = 0; i < N_PORT_PINS; ++i) {
            const unsigned k = p * N_PORT_PINS + i;
            gpio[p].pad_drive[i](pinmux.gpio_drive[k]);
            gpio[p].pad_din[i](pinmux.pad_din[k]);
            gpio[p].exti_line[i](s_exti_gpio[k]);
            exti.gpio_line[k](s_exti_gpio[k]);
        }
    }

    // ---------------------------------------------------------------------
    // Funciones alternativas del sistema (AF0) y EVENTOUT (AF15) [IR, §2.1].
    // El resto de la tabla AF (TIM, USART, SPI, I2C, CAN, SDIO, FSMC, ETH...)
    // se registra al implementar cada periférico, en F4 y F5.
    // ---------------------------------------------------------------------
    auto af = [](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                 sc_core::sc_signal<bool>* i, bool idle = true) {
        AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = idle; return ep;
    };
    // Depuración SWD/JTAG: PA13 SWDIO (bidireccional), PA14 SWCLK, PA15 JTDI,
    // PB3 JTDO/SWO, PB4 NJTRST [IR, §13.1].
    pinmux.connect_af(0, 13, 0, af(&s_swdio_o, &s_swdio_oe, &s_dbg_swdio_i, false));
    pinmux.connect_af(0, 14, 0, af(nullptr, nullptr, &s_dbg_swclk, false));
    pinmux.connect_af(0, 15, 0, af(nullptr, nullptr, &s_dbg_jtdi, false));
    pinmux.connect_af(1,  3, 0, af(&s_jtdo, &s_true, nullptr));
    pinmux.connect_af(1,  4, 0, af(nullptr, nullptr, &s_dbg_njtrst, true));
    // Salidas de reloj: MCO1 en PA8 y MCO2 en PC9 [IR, §2.1, §4.5.3].
    pinmux.connect_af(0,  8, 0, af(&rcc.mco1_sig, &s_true, nullptr));
    pinmux.connect_af(2,  9, 0, af(&rcc.mco2_sig, &s_true, nullptr));
    // -----------------------------------------------------------------------
    // Interfaz de cámara (AF13) [IR, §12.22.1 y cap. 2]
    //
    // Diecisiete señales, todas ENTRADAS: el DCMI no conduce ninguna. Y una
    // ausencia que dice mucho: en la tabla de pines del LQFP100 aparecen
    // DCMI_D0 a DCMI_D11 y NO aparecen D12 ni D13, cuyas unicas salidas
    // (PF11/PG6 y PG7/PI0) estan en puertos que este encapsulado no tiene. El
    // bus del DCMI del F407VG es, fisicamente, de doce hilos.
    // -----------------------------------------------------------------------
    auto af_in = [&](sc_core::sc_signal<bool>* i) {
        return af(nullptr, nullptr, i, false);
    };
    // Cada linea de datos sale en varios pines; el firmware elige uno. Todos
    // se registran, porque todos son validos.
    pinmux.connect_af(0,  9, 13, af_in(&dcmi.d_in[0]));    // PA9
    pinmux.connect_af(2,  6, 13, af_in(&dcmi.d_in[0]));    // PC6
    pinmux.connect_af(0, 10, 13, af_in(&dcmi.d_in[1]));    // PA10
    pinmux.connect_af(2,  7, 13, af_in(&dcmi.d_in[1]));    // PC7
    pinmux.connect_af(2,  8, 13, af_in(&dcmi.d_in[2]));    // PC8
    pinmux.connect_af(4,  0, 13, af_in(&dcmi.d_in[2]));    // PE0
    pinmux.connect_af(2,  9, 13, af_in(&dcmi.d_in[3]));    // PC9
    pinmux.connect_af(4,  1, 13, af_in(&dcmi.d_in[3]));    // PE1
    pinmux.connect_af(2, 11, 13, af_in(&dcmi.d_in[4]));    // PC11
    pinmux.connect_af(4,  4, 13, af_in(&dcmi.d_in[4]));    // PE4
    pinmux.connect_af(1,  6, 13, af_in(&dcmi.d_in[5]));    // PB6
    pinmux.connect_af(1,  8, 13, af_in(&dcmi.d_in[6]));    // PB8
    pinmux.connect_af(4,  5, 13, af_in(&dcmi.d_in[6]));    // PE5
    pinmux.connect_af(1,  9, 13, af_in(&dcmi.d_in[7]));    // PB9
    pinmux.connect_af(4,  6, 13, af_in(&dcmi.d_in[7]));    // PE6
    pinmux.connect_af(2, 10, 13, af_in(&dcmi.d_in[8]));    // PC10
    pinmux.connect_af(2, 12, 13, af_in(&dcmi.d_in[9]));    // PC12
    pinmux.connect_af(1,  5, 13, af_in(&dcmi.d_in[10]));   // PB5
    pinmux.connect_af(3,  2, 13, af_in(&dcmi.d_in[11]));   // PD2
    // D12 y D13: sin pin en este encapsulado. Sus señales existen en el
    // periférico y nadie las conduce nunca.
    pinmux.connect_af(0,  4, 13, af_in(&dcmi.hsync_in));   // PA4
    pinmux.connect_af(1,  7, 13, af_in(&dcmi.vsync_in));   // PB7
    pinmux.connect_af(0,  6, 13, af_in(&dcmi.pixclk_in));  // PA6

    // -----------------------------------------------------------------------
    // El bus de memoria externa (AF12) [IR, §12.18, cap. 2]
    //
    // Lo que este encapsulado tiene, y solo esto: dieciséis hilos de datos,
    // OCHO de dirección (A16-A23), NOE, NWE, NWAIT, NBL0/1, NL y UN chip
    // select. No hay A0-A15 -viven en PF0-PF15- ni NE2/NE3/NE4 ni ninguna
    // señal del banco 4. Por eso el bus de este chip solo sirve multiplexado.
    // -----------------------------------------------------------------------
    auto af_io = [&](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                     sc_core::sc_signal<bool>* i) {
        AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = true;
        return ep;
    };
    auto af_out = [&](sc_core::sc_signal<bool>* o) {
        return af(o, &s_true, nullptr);
    };
    // Datos D0-D15: bidireccionales, que es lo que hace de esto un bus.
    static const unsigned d_pin[16][2] = {
        {3, 14}, {3, 15}, {3, 0}, {3, 1}, {4, 7}, {4, 8}, {4, 9}, {4, 10},
        {4, 11}, {4, 12}, {4, 13}, {4, 14}, {4, 15}, {3, 8}, {3, 9}, {3, 10}
    };
    for (unsigned i = 0; i < 16; ++i)
        pinmux.connect_af(d_pin[i][0], d_pin[i][1], 12,
                          af_io(&fsmc.d_out[i], &fsmc.d_oe[i], &fsmc.d_in[i]));
    // Direcciones A16-A23: las únicas que salen al encapsulado.
    static const unsigned a_pin[8][2] = {
        {3, 11}, {3, 12}, {3, 13}, {4, 3}, {4, 4}, {4, 5}, {4, 6}, {4, 2}
    };
    for (unsigned i = 0; i < 8; ++i)
        pinmux.connect_af(a_pin[i][0], a_pin[i][1], 12, af_out(&fsmc.a_out[i]));
    pinmux.connect_af(3,  4, 12, af_out(&fsmc.noe));      // PD4  NOE
    pinmux.connect_af(3,  5, 12, af_out(&fsmc.nwe));      // PD5  NWE
    pinmux.connect_af(1,  7, 12, af_out(&fsmc.nl));       // PB7  NL (NADV)
    pinmux.connect_af(4,  0, 12, af_out(&fsmc.nbl[0]));   // PE0  NBL0
    pinmux.connect_af(4,  1, 12, af_out(&fsmc.nbl[1]));   // PE1  NBL1
    // PD7 es NE1 para el banco 1 y NCE2 para el banco 2: el MISMO pin con dos
    // nombres, según qué banco lo use. El informe lo lista como NCE2.
    pinmux.connect_af(3,  7, 12, af_out(&fsmc.ne[0]));
    pinmux.connect_af(3,  6, 12, af(nullptr, nullptr, &fsmc.nwait_in, true));

    // EVENTOUT (AF15) está disponible en todos los pines: es la salida de
    // evento del núcleo (instrucción SEV) [IR, §2.1].
    pinmux.connect_af_all(15, af(&s_evt_out, &s_true, nullptr));
    for (unsigned i = 0; i < 16; ++i) {
        syscfg.exticr_sel[i](s_exticr[i]);
        exti.exticr_sel[i](s_exticr[i]);
    }
    syscfg.boot_pins(s_boot);
    syscfg.mem_mode(s_memmode);
}

// ---------------------------------------------------------------------------
// EL NOMBRE DE ANTES, que sigue valiendo.
//
// Los once miembros de la familia F405/407 son este mismo die con distintos
// descriptores, así que `Stm32F407VG` nunca fue una clase distinta: era ESTA.
// El alias no es compatibilidad hacia atrás por cortesía —no hay código ajeno
// que mantener— sino la forma de decir que el renombrado no cambió nada.
// ---------------------------------------------------------------------------
using Stm32F407VG = SocF4;

} // namespace stm32

#include "soc_f4_bind2.h"   // continuación del netlist (periph/irq/dma)

#endif // STM32_TOP_SOC_F4_H
