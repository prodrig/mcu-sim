// =============================================================================
// sc_main_p4.cpp — LA SUITE DEL MSP432P401R (fases T0, T1 y T2)
//
// Un banco aparte, por la misma razón que los del F446 y el F417: un chip más
// dentro de otra simulación movería su invariante. Y es, además, el sitio de
// las pruebas PROPIAS DE T0 -nombres de pin por chip, catálogo común,
// formatos ELF/TI-TXT/HEX, la integración del núcleo-, que no se podían meter
// en test407 sin cambiar su cifra de comprobaciones sin que cambiase nada del
// F407. [doc/analisis_msp432.md, §8]
//
//   P0   las imágenes de firmware son las versionadas
//   P1-P4  T0, sin tiempo simulado: preguntar no cuesta ni un evento
//   P5-P16 T1: el chip, con el firmware p4_demo compilado con la cabecera de
//        TI y cargado COMO ELF -el `.out` de CCS-
//   P17  CoreMark, sobre el núcleo de siempre con la integración de TI
//   P18-P21 T2: el FLCTL desde el bus, un firmware que reprograma su propia
//        Flash, y GDB de verdad -por la sonda de los pines (G2) y por el DAP
//        (G1)- con `load`, `monitor reset halt`, puntos de ruptura, un
//        watchpoint, un paso y el perro congelado mientras el núcleo está
//        parado
//
// EL TIEMPO SIMULADO DE P21 NO ES REPRODUCIBLE entre máquinas, por lo mismo
// que el de T96/T97 en test407 (T-16): mientras espera la respuesta de un
// socket, el stub gasta tiempo simulado en pasos de 100 µs. Por eso el banco
// imprime también «el resto», el total sin P21, y eso es lo que contrasta
// `verif/invariantes.txt`.
//
//   make testp4   (o: make -f Makefile.mcu-sim testp4-build && ./build/testp4)
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include "../common/asan_opciones.h"
#include "../common/huella_fw.h"
#include "../common/imagen_fw.h"
#include "../soc/stm32f4_mcu.h"
#include "../soc/msp432p4/msp432p4_mcu.h"
#include "../parts/netlist_xml.h"
#include "../parts/ext_parts.h"
#include "../parts/puente_serie.h"
#include "../parts/servo.h"
#include "../parts/tft_st7735.h"
#include "../parts/boosterpack.h"
#include "../verif/bus_test_master.h"
#include "../verif/swd_port.h"
#include "../verif/gdb_stub.h"
#include "../verif/gdb_client.h"
#include "../core/gdb_stub_dap.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
// Lo que gastan los grupos que hablan por un socket con un GDB (P21)
static sc_core::sc_time g_t_gdb = sc_core::SC_ZERO_TIME;
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

static const char* const FW_DEMO = "verif/fw/msp432p4/p4_demo/p4_demo.out";
static const char* const FW_DEMO_BIN = "verif/fw/msp432p4/p4_demo/p4_demo.bin";
static const char* const FW_CM   = "verif/fw/msp432p4/coremark/coremark_p4.bin";
static const char* const FW_T3   = "verif/fw/msp432p4/p4_t3/p4_t3.out";
static const char* const FW_T3B  = "verif/fw/msp432p4/p4_t3b/p4_t3b.out";

// El buzón del firmware [verif/fw/msp432p4/p4_demo/main.c, buzon_t]
enum : uint32_t {
    B_MODO = 0x00, B_ARRANQUES = 0x04, B_ESTADO = 0x08, B_HARD = 0x0C,
    B_SOFT = 0x10, B_PIN = 0x14, B_DEVID = 0x18, B_HWREV = 0x1C,
    B_FLASH = 0x20, B_SRAM = 0x24, B_CS_SIN_LLAVE = 0x28, B_CS_KEY = 0x2C,
    B_PCM_IFG = 0x30, B_PCM_CTL0 = 0x34, B_PULSACIONES = 0x38, B_IV = 0x3C,
    B_WDT = 0x40, B_PMAP = 0x44, B_TLV_FIN = 0x48, B_ALIAS = 0x4C,
    B_P1DIR = 0x50, B_REBOOT = 0x54, B_M9_B1 = 0x58, B_M9_B0 = 0x5C,
    B_M9_RES = 0x60, B_M9_IFG = 0x64, B_TAM = 0x68
};

SC_MODULE(TbP4) {
    McuP4 mcu;
    Msp432P4& dut;
    BusTestMaster tm{"tm"};
    SwdProbe* sonda = nullptr;
    Crystal* y1 = nullptr;     // 32 768 Hz en PJ.0
    Crystal* y2 = nullptr;     // 48 MHz en PJ.3
    Button*  s1 = nullptr;     // P1.1, a masa
    // Los dos servidores de GDB (T2): el de la sonda soldada a SWCLKTCK y
    // SWDIOTMS, como un XDS110, y el que habla con el DAP por dentro. Los
    // construye el banco, como hace test407 con los del F407; los dos con el
    // programador de la Flash del P4.
    GdbStub*    gdb_p = nullptr;
    GdbStubDap* gdb_d = nullptr;
    GdbClient   cli;
    static constexpr unsigned PUERTO_PINES = 3431, PUERTO_DAP = 3432;
    // T3: el backchannel de la LaunchPad (el XDS110 en P1.2/P1.3) y otro
    // puente en P2.2/P2.3 para el PMAP. Desoldados hasta sus grupos.
    PuenteSerie* bcl  = nullptr;
    PuenteSerie* bcl2 = nullptr;
    // Relojes de prueba para la captura (P10.5) y TA1CLK (P7.2), parados
    ExtClock* reloj_cap = nullptr;
    ExtClock* reloj_ta1 = nullptr;
    // Un SG90 en P2.5 (TA0.2), con sus 5 V; desenchufado hasta su grupo
    AnalogNet* servo_vcc = nullptr;
    AnalogNet* servo_gnd = nullptr;
    Fuente* servo_5v = nullptr;
    Gnd*    servo_masa = nullptr;
    Servo*  servo = nullptr;
    // I2C: una 24Cxx en UCB0 (P1.6 SDA, P1.7 SCL) y un maestro externo en
    // UCB1 (P6.4, P6.5), con sus pull-ups; SPI: la ST7735 en UCB0 (P1.5 CLK,
    // P1.6 SIMO, CS P5.0, RS P3.7, RST P5.7, luz P2.6) y tres pistas entre
    // UCA3 (P9.5-P9.7) y UCB3 (P10.1-P10.3). Todo desoldado hasta su grupo
    // Analógicos: una fuente de tensión ajustable en A0..A3 (P5.5-P5.2)
    Driver* v_an[4] = {nullptr, nullptr, nullptr, nullptr};
    Driver* v_ce = nullptr;                     // C0.2 (P7.7), para el COMP_E0
    // Las piezas de la BOOSTXL-EDUMKII: los dos sensores en la UCB1 (con los
    // pull-ups de P37), el zumbador en P2.7 y el eje X del joystick en P6.0
    Opt3001* opt = nullptr;
    Tmp006*  tmp = nullptr;
    Zumbador* buz = nullptr;
    FuenteAjustable* joy_x = nullptr;
    I2cEeprom* eeprom = nullptr;
    I2cWire *pu_sda0 = nullptr, *pu_scl0 = nullptr, *pu_sda1 = nullptr, *pu_scl1 = nullptr;
    I2cExtMaster* i2c_ext = nullptr;
    AnalogNet *tft_vcc = nullptr, *tft_gnd = nullptr;
    Fuente* tft_3v3 = nullptr;
    Gnd*    tft_masa = nullptr;
    Tft128x160* tft = nullptr;
    SignalLink *pista_clk = nullptr, *pista_simo = nullptr, *pista_somi = nullptr;

    SC_CTOR(TbP4)
        : mcu("dut", DBG_PINES, Cableado(), *CatalogoMcu::busca("MSP432P401R")),
          dut(mcu.chip()) {
        tm.isk.bind(dut.from_tb);
        y1 = new Crystal(dut.dio.nodo_de(p4::PUERTO_J, 0));
        y2 = new Crystal(dut.dio.nodo_de(p4::PUERTO_J, 3));
        s1 = new Button(dut.dio.nodo_de(0, 1));
        s1->release();
        sonda = new SwdProbe(*dut.swclk_net, *dut.swdio_net, 2e6);
        gdb_p = new GdbStub("gdb_p", *dut.swclk_net, *dut.swdio_net, PUERTO_PINES, 2e6);
        gdb_p->set_enabled(false);
        gdb_p->pon_programador(&ProgramadorP4::el());
        gdb_d = new GdbStubDap("gdb_d", dut.core.debug, PUERTO_DAP);
        gdb_d->set_enabled(false);
        gdb_d->pon_programador(&ProgramadorP4::el());
        PuenteSerie::Config cb;
        cb.destino = serie::parsea("memoria");
        bcl = new PuenteSerie("bcl", &dut.dio.nodo_de(0, 3), &dut.dio.nodo_de(0, 2),
                              nullptr, nullptr, nullptr, cb);
        bcl->set_enabled(false);
        bcl2 = new PuenteSerie("bcl2", &dut.dio.nodo_de(1, 3), &dut.dio.nodo_de(1, 2),
                               nullptr, nullptr, nullptr, cb);
        bcl2->set_enabled(false);
        reloj_cap = new ExtClock("reloj_cap", dut.dio.nodo_de(9, 5), 0.0);
        reloj_ta1 = new ExtClock("reloj_ta1", dut.dio.nodo_de(6, 2), 0.0);
        servo_vcc = new AnalogNet("servo_vcc");
        servo_gnd = new AnalogNet("servo_gnd");
        servo_5v = new Fuente("servo_5v", *servo_vcc, 5.0);
        servo_masa = new Gnd("servo_masa", *servo_gnd);
        ConfigServo cs;
        cs.inicial_aleatoria = false;
        cs.posicion_inicial = -60.0;
        servo = new Servo("servo", *servo_vcc, *servo_gnd, dut.dio.nodo_de(1, 5), cs);
        servo->set_enabled(false);
        eeprom = new I2cEeprom("eeprom", dut.dio.nodo_de(0, 7), dut.dio.nodo_de(0, 6), 0x50);
        pu_sda0 = new I2cWire("pu_sda0", {&dut.dio.nodo_de(0, 6)});
        pu_scl0 = new I2cWire("pu_scl0", {&dut.dio.nodo_de(0, 7)});
        i2c_ext = new I2cExtMaster("i2c_ext", dut.dio.nodo_de(5, 5), dut.dio.nodo_de(5, 4), 100e3);
        pu_sda1 = new I2cWire("pu_sda1", {&dut.dio.nodo_de(5, 4)});
        pu_scl1 = new I2cWire("pu_scl1", {&dut.dio.nodo_de(5, 5)});
        for (ExtPartBase* q : std::initializer_list<ExtPartBase*>{eeprom, pu_sda0, pu_scl0,
                                                                   i2c_ext, pu_sda1, pu_scl1})
            q->set_enabled(false);
        tft_vcc = new AnalogNet("tft_vcc");
        tft_gnd = new AnalogNet("tft_gnd");
        tft_3v3 = new Fuente("tft_3v3", *tft_vcc, 3.3);
        tft_masa = new Gnd("tft_masa", *tft_gnd);
        tft = new Tft128x160("tft", *tft_vcc, *tft_gnd, dut.dio.nodo_de(4, 0),
                             dut.dio.nodo_de(4, 7), dut.dio.nodo_de(2, 7),
                             dut.dio.nodo_de(0, 6), dut.dio.nodo_de(0, 5),
                             dut.dio.nodo_de(1, 6));
        tft->set_enabled(false);
        pista_clk  = new SignalLink("pista_clk",  dut.dio.nodo_de(8, 5), dut.dio.nodo_de(9, 1));
        pista_simo = new SignalLink("pista_simo", dut.dio.nodo_de(8, 7), dut.dio.nodo_de(9, 2));
        pista_somi = new SignalLink("pista_somi", dut.dio.nodo_de(9, 3), dut.dio.nodo_de(8, 6));
        for (SignalLink* q : {pista_clk, pista_simo, pista_somi}) q->set_enabled(false);
        for (unsigned k = 0; k < 4; ++k) {
            v_an[k] = new Driver(dut.dio.nodo_de(4, 5 - k), 3.3, 10.0);
            v_an[k]->release();
        }
        v_ce = new Driver(dut.dio.nodo_de(6, 7), 3.3, 10.0);
        v_ce->release();
        opt = new Opt3001("opt3001", dut.dio.nodo_de(5, 5), dut.dio.nodo_de(5, 4),
                          &dut.dio.nodo_de(3, 6), 0x44, 250.0);
        tmp = new Tmp006("tmp006", dut.dio.nodo_de(5, 5), dut.dio.nodo_de(5, 4),
                         &dut.dio.nodo_de(2, 6), 0x40, 30.5, 40.0);
        buz = new Zumbador("buz", dut.dio.nodo_de(1, 7));
        joy_x = new FuenteAjustable(dut.dio.nodo_de(5, 0), 2.5, 0.0, 3.3, 100.0, "joy_x");
        for (ExtPartBase* q : std::initializer_list<ExtPartBase*>{opt, tmp, buz, joy_x})
            q->set_enabled(false);
        SC_THREAD(run);
    }
    ~TbP4() override {
        delete sonda; delete y1; delete y2; delete s1; delete gdb_p; delete gdb_d;
        delete bcl; delete bcl2; delete reloj_cap; delete reloj_ta1;
        delete servo; delete servo_5v; delete servo_masa; delete servo_vcc; delete servo_gnd;
        delete eeprom; delete pu_sda0; delete pu_scl0; delete i2c_ext; delete pu_sda1;
        delete pu_scl1; delete tft; delete tft_3v3; delete tft_masa; delete tft_vcc;
        delete tft_gnd; delete pista_clk; delete pista_simo; delete pista_somi;
        for (Driver* d : v_an) delete d;
        delete v_ce; delete opt; delete tmp; delete buz; delete joy_x;
    }

    // --- T3 [top/sc_main_p4_t3.h] ------------------------------------------
    uint32_t r3(unsigned i) { return dut.sram.peek32(0x10 + 4 * i); }
    void arranca3(uint32_t modo, const char* fw = FW_T3);
    bool espera3(uint32_t estado, double ms);
    void t3_uart();
    void t3_timers();
    void t3_i2c_spi();
    void t3_adc();
    void t3_dma();
    void t3_resto();
    void t3_lpm();
    void t3_edumkii();
    // Mide en un pin el periodo y la fraccion en alto, sobre `n` ciclos
    bool pwm(unsigned p, unsigned i, double& periodo_s, double& alto, unsigned n = 4,
             double max_ms = 50);

    // --- Utilidades -----------------------------------------------------------
    uint32_t buzon(uint32_t off) { return dut.sram.peek32(off); }
    // Deja el chip en reset, carga el firmware, escribe el modo y lo suelta.
    void arranca(uint32_t modo, const char* fw = FW_DEMO) {
        mcu.reset_pin(true);
        wait(20, SC_US);
        if (fw) mcu.carga_firmware(fw);
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        dut.sram.poke32(B_MODO, modo);
        mcu.reset_pin(false);
    }
    // Espera a que `n` flancos de subida de un pad y devuelve el periodo medio.
    double periodo(unsigned p, unsigned i, unsigned n = 4, double max_ms = 50) {
        const sc_time t_lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
        sc_time primero = SC_ZERO_TIME, ultimo = SC_ZERO_TIME;
        unsigned flancos = 0;
        bool ant = dut.dio.nivel(p, i);
        while (sc_time_stamp() < t_lim && flancos <= n) {
            wait(t_lim - sc_time_stamp(), dut.dio.nodo_de(p, i).value_changed_event());
            const bool v = dut.dio.nodo_de(p, i).voltage() > 1.65f;
            if (v && !ant) {
                if (flancos == 0) primero = sc_time_stamp();
                ultimo = sc_time_stamp();
                ++flancos;
            }
            ant = v;
        }
        if (flancos < 2) return 0.0;
        return (ultimo - primero).to_seconds() / double(flancos - 1);
    }

    // =======================================================================
    void firmwares() {
        grupo("P0 Imagenes de firmware [verif/fw/huellas.txt]");
        const huella_fw::Veredicto v =
            huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "p4");
        std::printf("         %d imagenes: %s\n", v.comprobadas, v.detalle.c_str());
        check(v.ok(), "las imagenes de la suite del MSP432P4 son las versionadas");
    }

    // =======================================================================
    // T0. Nada de esto gasta tiempo simulado.
    // =======================================================================
    void t0_nombres() {
        grupo("P1 Nombres de pin por chip (T0): P1.0 no es PB0 en un MSP432");
        unsigned p = 99, i = 99;
        check(p4::pad_p4("P1.0", p, i) && p == 0 && i == 0,
              "P1.0 es el primer pin del PRIMER puerto: no hay P0");
        check(p4::pad_p4("P10.5", p, i) && p == 9 && i == 5, "P10.5 existe");
        check(p4::pad_p4("PJ.4", p, i) && p == p4::PUERTO_J && i == 4,
              "PJ.4 es el puerto J, que no choca con P9");
        check(p4::pad_p4("P2_1", p, i) && p == 1 && i == 1,
              "P2_1, la forma que se usa en codigo, es P2.1");
        check(!p4::pad_p4("P0.0", p, i) && !p4::pad_p4("P11.0", p, i) &&
              !p4::pad_p4("PJ.6", p, i) && !p4::pad_p4("P1.8", p, i) &&
              !p4::pad_p4("P01.0", p, i) && !p4::pad_p4("PA5", p, i),
              "y P0.0, P11.0, PJ.6, P1.8, P01.0 y PA5 no son pines del P4");
        check(p4::canonico_p4(9, 3) == "P10.3" && p4::canonico_p4(p4::PUERTO_J, 2) == "PJ.2",
              "la forma canonica es la del datasheet");

        ContextoNomenclatura& c = ContextoNomenclatura::g();
        c.limpia();
        std::string m;
        check(!c.activo() && pad_desde_nombre("P1.0", m, p, i) && p == 1 && i == 0 &&
              nombre_canonico_pad("P1.0") == "PB0",
              "SIN chip de TI en la placa, P1.0 sigue siendo PB0: la regla de ST "
              "(T125) no se toca");
        c.fija_desnudo(&NOM_MSP432P4);
        check(pad_desde_nombre("P1.0", m, p, i) && m.empty() && p == 0 && i == 0 &&
              nombre_canonico_pad("P1_0") == "P1.0",
              "con un MSP432P4 en la placa, P1.0 es el suyo, y P1_0 se canoniza a P1.0");
        check(!pad_desde_nombre("PA5", m, p, i),
              "y PA5 deja de ser un pin: en esa placa es un cable");
        c.limpia();
        c.fija("u0", &NOM_MSP432P4);
        check(pad_desde_nombre("u0.P10.3", m, p, i) && m == "u0" && p == 9 && i == 3,
              "u0.P10.3: el punto del MCU y los del pin no se confunden");
        check(pad_desde_nombre("u1.PD12", m, p, i) && m == "u1" && p == 3 && i == 12,
              "y en la misma placa, u1.PD12 sigue siendo de un STM32");
        check(nombre_canonico_pad("u0.PJ.4") == "u0.PJ.4" &&
              nombre_canonico_pad("u1.P3.12") == "u1.PD12",
              "cada uno se canoniza con SU regla");
        std::string pre, al;
        check(alim_desde_nombre("u0.RSTN", pre, al) && pre == "u0" && al == "RSTN" &&
              alim_desde_nombre("u1.NRST", pre, al) && al == "NRST",
              "RSTN es la patilla de reset del P4, y NRST sigue siendo la de ST");
        c.limpia();
        check(!c.activo(), "y el contexto se deja limpio");

        // Y el netlist entero, leyendo un XML con un P4 y un F407
        Netlist nl;
        const std::string e = netlist_desde_texto(nl,
            "<placa nombre='mixta'>"
            "<mcu tipo='MSP432P401R' id='u0'/><mcu tipo='STM32F407VG' id='u1'/>"
            "<componente tipo='Led' id='L1' a_vss='si'><pin nombre='anodo' nodo='u0.P1.0'/></componente>"
            "<componente tipo='Led' id='L2' a_vss='si'><pin nombre='anodo' nodo='u1.P3.12'/></componente>"
            "</placa>");
        bool l1 = false, l2 = false;
        for (const Instancia& in : nl.instancias())
            for (const Conexion& cx : in.pines) {
                if (cx.nodo == "u0.P1.0") l1 = true;
                if (cx.nodo == "u1.PD12") l2 = true;
            }
        check(e.empty() && l1 && l2,
              "un XML con un MSP432P401R y un STM32F407VG: u0.P1.0 queda P1.0 y "
              "u1.P3.12 queda PD12, cada uno con la regla de su chip");
        ContextoNomenclatura::g().limpia();
    }

    void t0_catalogo() {
        grupo("P2 El catalogo y la factoria ya no exigen un McuCaps (T0)");
        const McuDesc* d = CatalogoMcu::busca("msp432p401r");
        check(d && std::string(d->familia) == "MSP432P4" && d->enc->n_gpio == 84,
              "MSP432P401R esta en el catalogo, de la familia MSP432P4, con 84 E/S");
        check(d && d->enc->coherente(), "y su LQFP100 cuadra: 84 pads marcados");
        check(FabricaMcu::conoce("MSP432P4") && FabricaMcu::conoce("STM32F4"),
              "la factoria sabe construir las dos familias");
        check_eq(CatalogoMcu::todos().size(), N_CATALOGO_MCU + 1,
                 "los 29 STM32 y el MSP432P401R");
        bool orden = true;
        for (unsigned k = 0; k < N_CATALOGO_MCU; ++k)
            orden = orden && CatalogoMcu::todos()[k].propio == CATALOGO_MCU[k];
        check(orden, "los STM32 se dan de alta en el mismo orden que CATALOGO_MCU, "
                     "con su McuCaps como descriptor propio");
        check(std::string(mcu.desc().nombre) == "MSP432P401R",
              "el adaptador dice que chip es por mcu_if::desc()");
        check(mcu.nodo_swclk() == dut.swclk_net && mcu.nodo_swdio() == dut.swdio_net,
              "y SWCLK/SWDIO son las patillas dedicadas, no PA14/PA13");
    }

    void t0_formatos() {
        grupo("P3 ELF, TI-TXT e Intel HEX (T0)");
        std::vector<TrozoFw> elf, hex, txt;
        check(formato_fw(FW_DEMO) == FormatoFw::Elf && formato_fw("x.txt") == FormatoFw::TiTxt &&
              formato_fw("x.hex") == FormatoFw::IntelHex,
              "el formato sale de la extension (y del numero magico para ELF)");
        const std::string e = lee_elf(FW_DEMO, elf);
        check(e.empty() && !elf.empty() && elf[0].dir == 0,
              "el .out se lee como ELF y su primer segmento va a 0x0, donde esta "
              "la Flash del MSP432");
        // Lo que el ELF pone en la Flash es exactamente el .bin de objcopy
        std::vector<uint8_t> bin;
        {
            std::ifstream f(FW_DEMO_BIN, std::ios::binary);
            bin.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        std::vector<uint8_t> img(bin.size(), 0xEE);
        for (const TrozoFw& t : elf)
            for (size_t k = 0; k < t.datos.size(); ++k)
                if (t.dir + k < img.size()) img[t.dir + k] = t.datos[k];
        check(!bin.empty() && img == bin,
              "y lo que el ELF pone en la Flash es, byte a byte, el .bin de objcopy");
        // TI-TXT e Intel HEX, escritos aquí mismo
        {
            std::ofstream f("build/p4_prueba.txt");
            f << "@0000\n20 00 01 20 01 01 00 00\n@0100\n20 BF FD E7\nq\n";
        }
        check(lee_titxt("build/p4_prueba.txt", txt).empty() && txt.size() == 2 &&
              txt[0].dir == 0 && txt[1].dir == 0x100 && txt[1].datos[0] == 0x20 &&
              txt[1].datos[3] == 0xE7,
              "TI-TXT: dos trozos con su @direccion, terminado en q");
        {
            std::ofstream f("build/p4_prueba.hex");
            f << ":0400000020000120BB\n:020000040000FA\n:0401000020BFFDE738\n:00000001FF\n";
        }
        check(lee_ihex("build/p4_prueba.hex", hex).empty() && hex.size() == 2 &&
              hex[1].dir == 0x100 && hex[1].datos[2] == 0xFD,
              "Intel HEX: los mismos dos trozos");
        {
            std::ofstream f("build/p4_malo.txt");
            f << "20 00\nq\n";
        }
        check(!lee_titxt("build/p4_malo.txt", txt).empty(),
              "un TI-TXT con datos antes de la primera @ es un error, con su linea");
    }

    void t0_integracion() {
        grupo("P4 La integracion del nucleo (T0)");
        check(INTEGRACION_ST.alias_arranque && !INTEGRACION_ST.en_ppb_fab(0xE0042000u),
              "la de ST: alias de 0x0 y ninguna ventana del PPB para el chip");
        check(!INTEGRACION_MSP432P4.alias_arranque &&
              INTEGRACION_MSP432P4.en_ppb_fab(0xE0042000u) &&
              INTEGRACION_MSP432P4.en_ppb_fab(0xE0044FFCu) &&
              !INTEGRACION_MSP432P4.en_ppb_fab(0xE0041000u) &&
              !INTEGRACION_MSP432P4.en_ppb_fab(0xE00FF000u),
              "la del P4: sin alias, y la ventana 0xE004_2000-0xE004_5000 para RSTCTL, "
              "SYSCTL y SYSCTL_Boot; ni el ETM ni la ROM table caen en ella");
        check(dut.core.integracion.alias_arranque == false && dut.core.ppb_fab == &dut,
              "el nucleo del P4 se ha construido con ella y sabe quien la atiende");
        check(dut.core.nucleo.n_irq == 41 && dut.core.nucleo.prio_bits == 3 &&
              dut.core.nucleo.prio_mask() == 0xE0,
              "41 lineas y 3 bits de prioridad: NVIC_SetPriority(irq, 5) escribe 0xA0");
    }

    // =======================================================================
    // T1
    // =======================================================================
    void t1_parpadeo() {
        grupo("P5 Arranca desde 0x0 y parpadea a 3 MHz (modo 0)");
        mcu.alimenta(true);
        arranca(0);
        wait(5, SC_MS);
        check_eq(buzon(B_ESTADO), 100, "el firmware llega al bucle de parpadeo");
        check_eq(buzon(B_ARRANQUES), 1, "y ha arrancado una sola vez: ha parado el perro");
        check(std::fabs(dut.f_mclk() - 3e6) < 1, "MCLK sale del DCO a 3 MHz, como del reset");
        t3_ = periodo(0, 0);
        std::printf("    periodo de P1.0 a 3 MHz: %.3f ms\n", t3_ * 1e3);
        check(t3_ > 0, "P1.0 conmuta: el LED1 parpadea");
        bool rgb = false;
        for (int k = 0; k < 20 && !rgb; ++k) {
            wait(dut.dio.nodo_de(1, 0).value_changed_event() | dut.dio.nodo_de(1, 1).value_changed_event());
            rgb = dut.dio.nodo_de(1, 1).voltage() > 3.0f || dut.dio.nodo_de(1, 0).voltage() > 3.0f;
        }
        check(rgb, "y el RGB de P2 enciende, con sus pines de alta corriente");
    }

    void t1_48mhz() {
        grupo("P6 La secuencia buena para 48 MHz (modo 1)");
        const unsigned av0 = dut.avisos_vcore(), af0 = dut.avisos_flash();
        arranca(1);
        wait(3, SC_MS);
        check_eq(buzon(B_ESTADO), 100, "llega a parpadear");
        check(std::fabs(dut.f_mclk() - 48e6) < 1, "MCLK = 48 MHz (DCORSEL = 5)");
        check(dut.pcm.vcore() == 1 && dut.pcm.cpm() == 1, "con el PCM en AM_LDO_VCORE1");
        check(dut.flctl.wait_banco(0) == 1 && dut.flctl.wait_banco(1) == 1,
              "y un estado de espera en los dos bancos de la Flash");
        check(dut.avisos_vcore() == av0 && dut.avisos_flash() == af0,
              "en el orden de TI no hay ningun aviso");
        const double t48 = periodo(0, 0);
        std::printf("    periodo de P1.0 a 48 MHz: %.3f ms (%.1f veces mas rapido)\n",
                    t48 * 1e3, t48 > 0 ? t3_ / t48 : 0.0);
        check(t48 > 0 && t3_ / t48 > 8.0 && t3_ / t48 < 16.5,
              "el mismo bucle va entre 8 y 16 veces mas deprisa: 16 por el reloj, "
              "menos lo que cuesta el estado de espera");
        check(dut.flash.esperas_pagadas() > 0, "y los estados de espera se pagan");
    }

    void t1_48mhz_mal() {
        grupo("P7 La secuencia MALA para 48 MHz (modo 2)");
        const unsigned av0 = dut.avisos_vcore(), af0 = dut.avisos_flash();
        const unsigned rech0 = dut.pcm.rechazadas();
        arranca(2);
        wait(4, SC_MS);
        check(buzon(B_PCM_IFG) & (1u << 2),
              "LDO_VCORE0 -> DCDC_VCORE1 de golpe: el PCM levanta AM_INVALID_TR_IFG");
        check_eq((buzon(B_PCM_CTL0) >> 8) & 0x3F, 0,
                 "y se queda donde estaba (CPM = AM_LDO_VCORE0)");
        check(dut.pcm.rechazadas() == rech0 + 1, "una transicion rechazada");
        check(dut.avisos_vcore() == av0 + 1,
              "48 MHz con VCORE0: el modelo avisa, una vez");
        check(dut.avisos_flash() >= af0 + 1,
              "48 MHz con VCORE1 y sin estado de espera: tambien");
        check_eq(buzon(B_ESTADO), 100, "y al corregirlo, llega a parpadear");
    }

    void t1_perro() {
        grupo("P8 El perro arranca encendido (modo 3)");
        const unsigned r0 = dut.resets();
        arranca(3);
        wait(40, SC_MS);
        const uint32_t n = buzon(B_ARRANQUES);
        std::printf("    %u arranques en 40 ms; HARDRESET_STAT del ultimo = 0x%X\n",
                    n, buzon(B_HARD));
        check(n >= 3 && n <= 5, "sin pararlo, resetea cada 2^15 ciclos de SMCLK "
                                "(10,9 ms a 3 MHz): 3 o 4 arranques en 40 ms");
        check(buzon(B_HARD) & (1u << 1),
              "y RSTCTL dice por que: HARDRESET_STAT.SRC1, el perro vencido");
        check_eq(buzon(B_WDT), 0x6904,
                 "WDTCTL se lee 0x6904: contrasena 0x69, 2^15 ciclos de SMCLK");
        check(dut.resets() >= r0 + 3 && dut.wdt.vencimientos() >= 3,
              "los resets los ha pedido el WDT_A");
    }

    void t1_boton() {
        grupo("P9 Un pulsador con interrupcion y P1IV (modo 4)");
        arranca(4);
        wait(2, SC_MS);
        check_eq(buzon(B_ESTADO), 1, "configurado: pull-up interno, flanco de bajada, IE");
        const bool led0 = dut.dio.nivel(0, 0);
        check(dut.dio.nodo_de(0, 1).voltage() > 3.0f,
              "S1 suelto: P1.1 arriba por el pull-up interno, que es lo unico que "
              "lo sujeta (la placa no lleva resistencia)");
        s1->press();
        wait(1, SC_MS);
        check_eq(buzon(B_PULSACIONES), 1, "pulsar S1 da UNA interrupcion");
        check_eq(buzon(B_IV), 0x04, "y P1IV vale 0x04: el pin 1 (2*(1+1))");
        check(dut.dio.nivel(0, 0) != led0, "el manejador conmuta el LED1");
        s1->release();
        wait(1, SC_MS);
        check_eq(buzon(B_PULSACIONES), 1, "soltar no interrumpe: es flanco de bajada");
        s1->press();
        wait(1, SC_MS);
        s1->release();
        wait(1, SC_MS);
        check_eq(buzon(B_PULSACIONES), 2, "la segunda pulsacion, otra: leer P1IV "
                                          "bajo el flag de la primera");
    }

    void t1_identidad() {
        grupo("P10 Identidad, SYSCTL en el PPB y las llaves (modo 5)");
        const unsigned sl0 = dut.cs.escrituras_sin_llave();
        arranca(5);
        wait(2, SC_MS);
        check_eq(buzon(B_ESTADO), 1, "el firmware termina");
        check_eq(buzon(B_DEVID), 0xA000, "TLV->DEVICE_ID = 0xA000, el P401R");
        check_eq(buzon(B_HWREV), 0x43, "TLV->HWREV = 'C'");
        check_eq(buzon(B_TLV_FIN), 0x0BD0E11D,
                 "TLV->TLV_END = 0x0BD0E11D en su sitio: la estructura del TLV "
                 "cuadra palabra a palabra con la de la cabecera de TI");
        check_eq(buzon(B_FLASH), 0x40000,
                 "SYSCTL->FLASH_SIZE, en 0xE004_3020 (el PPB), = 256 KB");
        check_eq(buzon(B_SRAM), 0x10000, "SYSCTL->SRAM_SIZE = 64 KB");
        check_eq(buzon(B_CS_KEY), 0xA596, "CS->KEY se lee 0xA596");
        check_eq(buzon(B_CS_SIN_LLAVE), 0x00010000,
                 "sin la llave, escribir CS->CTL0 no hace nada: sigue en 3 MHz");
        check(dut.cs.escrituras_sin_llave() > sl0, "y el modelo lo ha contado");
        check_eq(buzon(B_PMAP), 7, "sin la llave del PMAP, P2MAP no cambia: sigue con su valor "
                 "de reset, PM_UCA1STE (7)");
        check_eq(buzon(B_ALIAS), 0xCAFE432F,
                 "lo escrito en 0x2000_8000 se lee en 0x0100_8000: la SRAM tiene su "
                 "alias en la region de codigo");
    }

    void t1_resets() {
        grupo("P11 Violacion de la contrasena del perro (modo 6)");
        arranca(6);
        wait(2, SC_MS);
        check_eq(buzon(B_ARRANQUES), 2, "escribir WDTCTL sin 0x5A resetea el chip");
        check(buzon(B_HARD) & (1u << 2), "HARDRESET_STAT.SRC2: violacion de contrasena");
        check(dut.wdt.violaciones() >= 1, "contada por el WDT_A");

        grupo("P12 Reset blando y reset duro por RSTCTL (modos 7 y 8)");
        arranca(7);
        wait(2, SC_MS);
        check_eq(buzon(B_ARRANQUES), 2, "RESET_REQ con SOFT_REQ y la llave 0x69: un reset");
        check(buzon(B_P1DIR) & 1u,
              "BLANDO: el nucleo vuelve a empezar, pero P1DIR sigue como estaba");
        arranca(8);
        wait(2, SC_MS);
        check_eq(buzon(B_ARRANQUES), 2, "con HARD_REQ, otro");
        check_eq(buzon(B_P1DIR) & 1u, 0, "DURO: los perifericos tambien vuelven a cero");

        grupo("P13 El boton de RESET (RSTN)");
        arranca(5);
        wait(1, SC_MS);
        mcu.reset_pin(true);
        wait(50, SC_US);
        check(dut.en_reset(), "con RSTN abajo el chip esta en reset");
        mcu.reset_pin(false);
        wait(1, SC_MS);
        check_eq(buzon(B_PIN), 1, "al soltarlo arranca, y PINRESET_STAT lo dice");
        check_eq(buzon(B_ARRANQUES), 2, "dos arranques: el de antes y el del boton");
    }

    void t1_cristales() {
        grupo("P14 Los cristales: HFXT arranca solo si hay cristal y pin");
        arranca(99);                     // aparcado: el banco toca los registros
        wait(1, SC_MS);
        auto wr32 = [&](uint32_t a, uint32_t v) { tm.write32(a, v); };
        auto rd32 = [&](uint32_t a) { uint32_t v = 0; tm.read32(a, v); return v; };
        const uint32_t CS_ = p4::CS_B;
        wr32(CS_ + CsP4::R_KEY, CsP4::KEY_VAL);
        wr32(CS_ + CsP4::R_CTL2, rd32(CS_ + CsP4::R_CTL2) | (1u << 24));   // HFXT_EN
        wait(2, SC_MS);
        wr32(CS_ + CsP4::R_CLRIFG, CsP4::IFG_HFXT);
        check(rd32(CS_ + CsP4::R_IFG) & CsP4::IFG_HFXT,
              "con PJ.3 en GPIO el cristal no tiene a que engancharse: HFXTIFG "
              "vuelve a subir aunque se borre");
        tm.write8(p4::DIO_B + 0x120 + 0x0A, 0x08);      // PJSEL0.3: HFXIN
        wait(2, SC_MS);
        wr32(CS_ + CsP4::R_CLRIFG, CsP4::IFG_HFXT);
        check(!(rd32(CS_ + CsP4::R_IFG) & CsP4::IFG_HFXT) &&
              (rd32(CS_ + CsP4::R_STAT) & (1u << 2)),
              "con el pin en su funcion y el cristal Y2 en la placa, HFXT arranca: "
              "HFXTIFG se borra y HFXT_ON = 1");
        wr32(CS_ + CsP4::R_CTL1, (rd32(CS_ + CsP4::R_CTL1) & ~7u) | CsP4::HFXT);
        wait(10, SC_US);
        check(std::fabs(dut.f_mclk() - 48e6) < 1,
              "MCLK de HFXT: los 48 MHz del cristal de la LaunchPad");
        y2->detach();
        wait(100, SC_US);
        check(std::fabs(dut.f_mclk() - 3e6) < 1 && (rd32(CS_ + CsP4::R_IFG) & CsP4::IFG_HFXT),
              "se desuelda el cristal: HFXT cae, HFXTIFG sube y MCLK vuelve al DCO");
        y2->attach();
        wr32(CS_ + CsP4::R_KEY, 0);
    }

    void t1_swd() {
        grupo("P15 La sonda SWD por las patillas dedicadas (G2)");
        arranca(5);
        wait(1, SC_MS);
        const uint32_t id = sonda->conectar_swd();
        check_eq(id, 0x2BA01477u, "SWCLKTCK/SWDIOTMS: el SW-DP del Cortex-M4 contesta, "
                                  "el mismo que en el F407");
        uint32_t v = 0;
        check(sonda->mem_read32(0xE0043020u, v) && v == 0x40000u,
              "y por el AHB-AP se lee SYSCTL_FLASH_SIZE en 0xE004_3020, que es como "
              "OpenOCD averigua cuanta Flash hay");
        check(sonda->mem_read32(0x00201000u + 12u, v) && v == 0xA000u,
              "y el DEVICE_ID del TLV, que es como OpenOCD sabe que es un P401x");
        sonda->desconectar();
    }

    void t1_coremark() {
        grupo("P16 CoreMark sobre el MSP432P401R");
        arranca(0, nullptr);             // el firmware no importa: se cambia ahora
        mcu.reset_pin(true);
        wait(20, SC_US);
        check(mcu.carga_firmware(FW_CM), "coremark_p4.bin cargado en 0x0");
        uint8_t ceros[16] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        const uint64_t i0 = dut.core.cpu.inst_count;
        mcu.reset_pin(false);
        const sc_time t0 = sc_time_stamp();
        bool fin = false;
        while (sc_time_stamp() - t0 < sc_time(4000, SC_MS)) {
            wait(2, SC_MS);
            if (dut.sram.peek32(0) == 1u) { fin = true; break; }
        }
        check(fin, "CoreMark termina, sin que el perro lo interrumpa");
        const uint32_t magic = dut.sram.peek32(4), len = dut.sram.peek32(8);
        std::string s;
        if (magic == 0x434F4E53u && len < 4096)
            for (uint32_t k = 0; k < len; ++k) s += char(dut.sram.peek32(12 + (k & ~3u)) >> (8 * (k & 3u)));
        const bool crc_ok =
            s.find("seedcrc          : 0xe9f5") != std::string::npos &&
            s.find("[0]crclist       : 0xe714") != std::string::npos &&
            s.find("[0]crcmatrix     : 0x1fd7") != std::string::npos &&
            s.find("[0]crcstate      : 0x8e3a") != std::string::npos;
        check(crc_ok, "y valida su resultado: los CRC son los canonicos del 2K run");
        std::printf("    %llu instrucciones en %.3f s simulados\n",
                    (unsigned long long)(dut.core.cpu.inst_count - i0),
                    (sc_time_stamp() - t0).to_seconds());
    }

    void t1_rom() {
        grupo("P17 Lo que no esta, lo dice");
        uint32_t v = 0;
        const unsigned n0 = dut.rom.accesos();
        check(tm.read32(p4::ROM_B + 0x800, v) == tlm::TLM_ADDRESS_ERROR_RESPONSE &&
              dut.rom.accesos() == n0 + 1,
              "la ROM de DriverLib (ROM_APITABLE, 0x0200_0800) da error de bus: no "
              "esta, y no se finge (T4)");
        check(tm.read32(p4::ADC14_B, v) == tlm::TLM_OK_RESPONSE && v == 0,
              "el ADC14 esta DECLARADO: contesta cero (T3)");
        check(mcu.limitaciones().size() >= 6, "y limitaciones() lo enumera al montar");
    }


    // =======================================================================
    // T2
    // =======================================================================
    uint32_t rd(uint32_t a) { uint32_t v = 0; tm.read32(a, v); return v; }
    void     wr(uint32_t a, uint32_t v) { tm.write32(a, v); }
    uint32_t F(uint32_t off) { return p4::FLCTL_B + off; }
    // Lo que tarda un acceso del maestro de pruebas
    sc_time lee_cuanto(uint32_t a) {
        unsigned char b[4];
        sc_time t = SC_ZERO_TIME;
        tm.access(false, a, b, 4, &t);
        return t;
    }

    void t2_flctl() {
        grupo("P18 El FLCTL desde el bus: proteccion, borrado y programacion (T2)");
        arranca(99);                                 // aparcado, el perro parado
        wait(1, SC_MS);
        check(rd(F(FlctlP4::R_BANK0_MAIN_WEPROT)) == 0xFFFFFFFFu &&
              rd(F(FlctlP4::R_BANK1_MAIN_WEPROT)) == 0xFFFFFFFFu &&
              rd(F(FlctlP4::R_BANK0_INFO_WEPROT)) == 3u && rd(F(FlctlP4::R_BANK1_INFO_WEPROT)) == 3u,
              "al salir del reset toda la Flash esta protegida");
        wr(F(FlctlP4::R_BANK0_INFO_WEPROT), 0);
        check_eq(rd(F(FlctlP4::R_BANK0_INFO_WEPROT)), 2u,
                 "el sector 1 de la informacion -el TLV- no se desprotege: es de TI");
        wr(F(FlctlP4::R_BANK0_INFO_WEPROT), 3);
        check_eq(rd(F(FlctlP4::R_POWER_STAT)) & 7u, 4u, "POWER_STAT.PSTAT = 4: activa");

        // Escribir sin ENABLE no programa
        const unsigned ign = dut.flctl.escrituras_ignoradas();
        wr(0x34000u, 0);
        check(rd(0x34000u) == 0xFFFFFFFFu && dut.flctl.escrituras_ignoradas() == ign + 1,
              "sin PRG_CTLSTAT.ENABLE, escribir en la Flash no programa nada");

        // Borrar un sector del banco 1, y lo que tarda
        for (uint32_t a = 0x34000u; a < 0x34010u; ++a) dut.flash.poke_byte(a, 0x5A);
        wr(F(FlctlP4::R_ERASE_SECTADDR), 0x34000u);
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_START);
        check_eq(rd(F(FlctlP4::R_ERASE_CTLSTAT)) & 0x3F0000u, 0x30000u,
                 "un sector PROTEGIDO no se borra, pero la operacion acaba igual (STATUS 3)");
        check_eq(rd(0x34000u), 0x5A5A5A5Au, "y el contenido sigue ahi");
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_CLR_STAT);
        wr(F(FlctlP4::R_BANK1_MAIN_WEPROT), ~(1u << 20));      // 0x34000: banco 1, sector 20
        wr(F(FlctlP4::R_CLRIFG), 0xFFFFFFFFu);
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_START);
        check_eq((rd(F(FlctlP4::R_ERASE_CTLSTAT)) >> 16) & 3u, 2u, "desprotegido: STATUS 2, en curso");
        const sc_time t_otro = lee_cuanto(0x00000100u);
        const sc_time t_mismo = lee_cuanto(0x34000u);
        std::printf("    leer el banco 0 durante el borrado: %s; el banco 1: %s\n",
                    t_otro.to_string().c_str(), t_mismo.to_string().c_str());
        check(t_otro < sc_time(1, SC_US),
              "leer el OTRO banco mientras se borra no espera: read-while-write");
        check(t_mismo > sc_time(4, SC_MS) && t_mismo <= sc_time(5, SC_MS),
              "leer el MISMO banco espera a que acabe el borrado (5 ms de modelo)");
        check(rd(0x34000u) == 0xFFFFFFFFu && ((rd(F(FlctlP4::R_ERASE_CTLSTAT)) >> 16) & 3u) == 3u &&
              (rd(F(FlctlP4::R_IFG)) & FlctlP4::IFG_ERASE),
              "y queda borrado: 0xFF, STATUS 3 e IFG.ERASE");
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_CLR_STAT);
        check_eq(rd(F(FlctlP4::R_ERASE_CTLSTAT)) & 0x70000u, 0u, "CLR_STAT lo deja en reposo");

        // Programacion inmediata: solo baja bits
        wr(F(FlctlP4::R_PRG_CTLSTAT), FlctlP4::PRG_ENABLE | FlctlP4::PRG_VER_PST);
        wr(0x34000u, 0x0F0F00FFu);
        wr(0x34000u, 0xFF00F0F0u);
        check_eq(rd(0x34000u), 0x0F0000F0u,
                 "programar dos veces la misma palabra da el AND: solo se bajan bits");
        check(rd(F(FlctlP4::R_IFG)) & FlctlP4::IFG_AVPST,
              "y con VER_PST, la segunda levanta AVPST: no quedo lo pedido");
        // El sector protegido
        wr(F(FlctlP4::R_CLRIFG), 0xFFFFFFFFu);
        wr(0x35000u, 0);
        check(rd(0x35000u) == 0xFFFFFFFFu && (rd(F(FlctlP4::R_IFG)) & FlctlP4::IFG_PRG_ERR),
              "en un sector protegido no se programa: PRG_ERR");
        // Palabra completa: hasta los 16 bytes no se programa nada
        wr(F(FlctlP4::R_PRG_CTLSTAT), FlctlP4::PRG_ENABLE | FlctlP4::PRG_MODE);
        wr(0x34010u, 0x11111111u); wr(0x34014u, 0x22222222u); wr(0x34018u, 0x33333333u);
        const uint32_t a_medias = rd(0x34010u);
        wr(0x3401Cu, 0x44444444u);
        check(a_medias == 0xFFFFFFFFu && rd(0x34010u) == 0x11111111u && rd(0x3401Cu) == 0x44444444u,
              "en modo de palabra completa se junta la linea de 128 bits y se programa entera");
        wr(F(FlctlP4::R_PRG_CTLSTAT), 0);
        // Rafaga: desalineada, fuera de rango y buena
        for (unsigned i = 0; i < 16; ++i) wr(F(FlctlP4::R_PRGBRST_DATA) + 4 * i, 0xC0DE0000u + i);
        wr(F(FlctlP4::R_PRGBRST_STARTADDR), 0x34028u);
        wr(F(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_START | (1u << 3));
        check(rd(F(FlctlP4::R_PRGBRST_CTLSTAT)) & FlctlP4::BRST_ADDR_ERR,
              "una rafaga que no empieza en 16 bytes da ADDR_ERR");
        wr(F(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_CLR_STAT);
        wr(F(FlctlP4::R_PRGBRST_STARTADDR), 0x34040u);
        wr(F(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_START | (4u << 3));
        unsigned vistos = 0, ultimo = 99;
        for (unsigned k = 0; k < 40; ++k) {
            const unsigned st = (rd(F(FlctlP4::R_PRGBRST_CTLSTAT)) >> 16) & 7u;
            if (st != ultimo) { ++vistos; ultimo = st; }
            if (st == 7) break;
            wait(5, SC_US);
        }
        check(ultimo == 7 && vistos >= 4,
              "una rafaga de cuatro lineas pasa por BURST_STATUS 2..5 y acaba en 7");
        check(rd(0x34040u) == 0xC0DE0000u && rd(0x3407Cu) == 0xC0DE000Fu,
              "y deja las 16 palabras de PRGBRST_DATA en la Flash");
        wr(F(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_CLR_STAT);

        // Borrado masivo: lo no protegido, de las dos memorias, menos el TLV
        for (uint32_t a = 0x36000u; a < 0x36004u; ++a) dut.flash.poke_byte(a, 0);
        for (uint32_t a = 0x37000u; a < 0x37004u; ++a) dut.flash.poke_byte(a, 0);
        wr(F(FlctlP4::R_BANK1_MAIN_WEPROT), ~(1u << 22));      // solo 0x36000
        wr(F(FlctlP4::R_BANK0_MAIN_WEPROT), 0xFFFFFFFFu);
        wr(F(FlctlP4::R_CLRIFG), 0xFFFFFFFFu);
        wr(F(FlctlP4::R_IE), FlctlP4::IFG_ERASE);
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_START | FlctlP4::ERASE_MODE_MASA);
        wait(41, SC_MS);
        check(rd(0x36000u) == 0xFFFFFFFFu && rd(0x37000u) == 0 &&
              rd(p4::TLV_B + 0x148u) == 0x0BD0E11Du && rd(0x100u) != 0xFFFFFFFFu,
              "el borrado masivo borra lo desprotegido y respeta lo protegido, el TLV y "
              "el firmware");
        // Con IE.ERASE, la linea FLCTL_IRQn llega al NVIC: se mira su ISPR por SWD
        sonda->conectar_swd();
        uint32_t ispr = 0;
        sonda->mem_read32(0xE000E200u, ispr);
        sonda->desconectar();
        check(ispr & (1u << p4::IRQ_FLCTL), "y con IE.ERASE, FLCTL_IRQn queda pendiente en el NVIC");
        wr(F(FlctlP4::R_IE), 0);
        wr(F(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_CLR_STAT);
        wr(F(FlctlP4::R_BANK1_MAIN_WEPROT), 0xFFFFFFFFu);
    }

    void t2_autoprograma() {
        grupo("P19 Un firmware que reprograma su propia Flash (modo 9)");
        arranca(9);
        wait(30, SC_MS);
        const uint32_t r = buzon(B_M9_RES);
        std::printf("    resultado 0x%03X; vueltas del bucle de espera: %u borrando el banco 1, "
                    "%u borrando el 0\n", r, buzon(B_M9_B1), buzon(B_M9_B0));
        check_eq(buzon(B_ESTADO), 1, "el firmware llega al final");
        check(r & 0x3u, "borra un sector del banco 1 y lo lee todo a 0xFF");
        check(buzon(B_M9_B1) > 1000 && buzon(B_M9_B0) <= 2,
              "mientras borra el banco 1 la CPU da miles de vueltas desde el 0; borrando "
              "el 0, ninguna: la busqueda de instrucciones espera");
        check(r & (1u << 2), "programa palabra a palabra, con IFG.PRG y sin AVPST");
        check(r & (1u << 3), "programa dos lineas en rafaga, con IFG.PRGB");
        check(r & (1u << 4), "un sector protegido no se programa: PRG_ERR");
        check(r & (1u << 5), "un 0 no vuelve a 1 sin borrar: AVPST, y el dato no cambia");
        check(r & (1u << 6), "el TLV no se borra: ADDR_ERR, y sigue intacto");
        check(r & (1u << 7), "una rafaga desalineada da ADDR_ERR y no programa nada");
        check(r & (1u << 8), "sin ENABLE, escribir en la Flash no programa nada");
        check(dut.flash.peek32(0x30000u) == 0x11111111u && dut.flash.peek32(0x3005Cu) == 0xA5000007u,
              "y lo programado esta en la Flash, visto desde fuera");
    }

    // --- GDB --------------------------------------------------------------
    static std::string hex_le(uint32_t v) {
        char b[9];
        std::snprintf(b, sizeof b, "%02x%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF,
                      (v >> 16) & 0xFF, v >> 24);
        return b;
    }
    static std::string hex(const std::string& s) {
        std::string h; char b[3];
        for (unsigned char c : s) { std::snprintf(b, sizeof b, "%02x", c); h += b; }
        return h;
    }
    static uint32_t de_hex_le(const std::string& s) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4 && 2 * i + 1 < s.size(); ++i)
            v |= uint32_t(std::stoul(s.substr(2 * i, 2), nullptr, 16)) << (8 * i);
        return v;
    }
    uint32_t pc() { return de_hex_le(cli.pedir("p0f")); }
    std::string espera_parada(double ms = 100) {
        return cli.recibir(sc_time(ms, SC_MS));
    }

    struct ResGdb {
        bool conecta = false, carga = false, lectura = false, congelado = false,
             sin_congelar = false, wp = false, bp = false, paso = false;
        uint32_t pc_reset = 0, pc_bp = 0, pc_paso = 0;
    };

    ResGdb sesion_gdb(unsigned puerto, uint32_t reset_handler, uint32_t parpadea,
                      uint32_t dir_arranques) {
        ResGdb r;
        // En la Flash hay OTRA cosa: CoreMark. El `load` tiene que borrar.
        arranca(0, FW_CM);
        wait(2, SC_MS);
        if (!cli.conectar(puerto)) return r;
        wait(2, SC_MS);
        cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=arm");
        r.conecta = cli.pedir("QStartNoAckMode") == "OK" &&
                    cli.pedir("?").rfind("T05", 0) == 0;
        // load, como lo hace GDB sin mapa de memoria: paquetes M
        std::vector<uint8_t> img;
        {
            std::ifstream f(FW_DEMO_BIN, std::ios::binary);
            img.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        bool ok = !img.empty();
        char cab[64];
        for (size_t o = 0; ok && o < img.size(); o += 256) {
            const size_t n = std::min<size_t>(256, img.size() - o);
            std::snprintf(cab, sizeof cab, "M%zx,%zx:", o, n);
            std::string pq = cab;
            for (size_t i = 0; i < n; ++i) { char b[3]; std::snprintf(b, 3, "%02x", img[o + i]); pq += b; }
            ok = cli.pedir(pq, sc_time(200, SC_MS)) == "OK";
        }
        r.carga = ok;
        std::string leido;
        for (size_t o = 0; o < img.size(); o += 256) {
            std::snprintf(cab, sizeof cab, "m%zx,%zx", o, std::min<size_t>(256, img.size() - o));
            leido += cli.pedir(cab, sc_time(100, SC_MS));
        }
        std::string esperado;
        for (uint8_t c : img) { char b[3]; std::snprintf(b, 3, "%02x", c); esperado += b; }
        r.lectura = leido == esperado;
        // El buzon: modo 0 y ningun arranque
        cli.pedir("M20000000,8:0000000000000000");
        // monitor reset halt
        cli.pedir("qRcmd," + hex("reset halt"), sc_time(50, SC_MS));
        r.pc_reset = pc();
        // El perro, congelado con el nucleo parado (SYSCTL_PERIHALT_CTL.HALT_WDT)
        const unsigned r0 = dut.resets();
        wait(30, SC_MS);
        r.congelado = dut.resets() == r0 && buzon(B_ARRANQUES) == 0;
        // ...y sin congelar: el perro resetea el chip aunque este parado
        cli.pedir("Me004300c,4:00000000");
        wait(15, SC_MS);
        r.sin_congelar = dut.resets() > r0;
        cli.pedir("Me004300c,4:00400000");
        cli.pedir("qRcmd," + hex("reset halt"), sc_time(50, SC_MS));
        // Un watchpoint: la primera escritura de main es buzon.arranques++
        char z[64];
        std::snprintf(z, sizeof z, "Z2,%x,4", dir_arranques);
        const bool zw = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pw = espera_parada();
        r.wp = zw && pw.find("watch") != std::string::npos && buzon(B_ARRANQUES) == 1;
        std::snprintf(z, sizeof z, "z2,%x,4", dir_arranques);
        cli.pedir(z);
        // Un punto de ruptura en parpadea
        std::snprintf(z, sizeof z, "Z0,%x,2", parpadea);
        const bool zb = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pb = espera_parada();
        r.pc_bp = pc();
        r.bp = zb && pb.find("swbreak") != std::string::npos && r.pc_bp == parpadea;
        std::snprintf(z, sizeof z, "z0,%x,2", parpadea);
        cli.pedir(z);
        // Un paso
        cli.enviar("vCont;s");
        const std::string ps = espera_parada();
        r.pc_paso = pc();
        r.paso = ps.rfind("T05", 0) == 0 && r.pc_paso != parpadea &&
                 r.pc_paso > parpadea && r.pc_paso < parpadea + 8;
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        return r;
    }

    void informa(const char* via, const ResGdb& r, uint32_t rh, uint32_t pp) {
        std::string q;
        auto c = [&](bool v, const std::string& t) { check(v, (std::string(via) + ": " + t).c_str()); };
        c(r.conecta, "GDB se conecta, negocia y encuentra el nucleo parado");
        c(r.carga, "`load` de p4_demo.bin con paquetes M sobre CoreMark: el stub borra y "
                   "programa la Flash por el FLCTL");
        c(r.lectura, "y lo leido de vuelta es el .bin, byte a byte");
        c(r.pc_reset == rh, "`monitor reset halt` para en la primera instruccion, Reset_Handler");
        c(r.congelado, "con el nucleo parado el perro no corre (PERIHALT_CTL.HALT_WDT)");
        c(r.sin_congelar, "y con HALT_WDT a cero, el perro resetea el chip aunque este parado");
        c(r.wp, "un watchpoint en buzon.arranques para el nucleo en la primera escritura");
        c(r.bp, "un punto de ruptura en parpadea() para el nucleo alli");
        c(r.paso, "y un paso avanza una instruccion");
        (void)pp;
    }

    void t2_gdb() {
        uint32_t rh = 0, pp = 0, arr = 0;
        const bool sim = simbolo_elf(FW_DEMO, "Reset_Handler", rh) &&
                         simbolo_elf(FW_DEMO, "parpadea", pp) &&
                         simbolo_elf(FW_DEMO, "buzon", arr);
        grupo("P20 Los simbolos del firmware, del ELF");
        check(sim && rh == 0xE4 && arr == 0x20000000u,
              "Reset_Handler en 0xE4 y buzon en 0x2000_0000, leidos de la tabla de simbolos");
        arr += B_ARRANQUES;

        const sc_time t0 = sc_time_stamp();
        grupo("P21 GDB por la sonda de los pines (G2) y por el DAP (G1)");
        sonda->desconectar();
        gdb_p->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rp = sesion_gdb(PUERTO_PINES, rh, pp, arr);
        gdb_p->set_enabled(false);
        gdb_p->soltar_pines();
        informa("G2 (pines)", rp, rh, pp);
        gdb_d->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rd_ = sesion_gdb(PUERTO_DAP, rh, pp, arr);
        gdb_d->set_enabled(false);
        informa("G1 (DAP)", rd_, rh, pp);
        check(rp.pc_reset == rd_.pc_reset && rp.pc_bp == rd_.pc_bp && rp.pc_paso == rd_.pc_paso,
              "los dos caminos dan lo mismo: el mismo depurador, otro cable");
        check(ProgramadorP4::el().fallos() == 0, "y el programador no ha fallado ni una vez");
        g_t_gdb += sc_time_stamp() - t0;
    }

    void run() {
        // TESTP4_SOLO=t3: solo los grupos de T3, para trabajar en ellos. No
        // vale para el invariante (la cifra es otra), y lo dice.
        if (const char* solo = std::getenv("TESTP4_SOLO")) {
            if (std::string(solo) == "t3b") {
                std::printf("  [TESTP4_SOLO=t3b] solo los grupos de p4_t3b: el tiempo NO "
                            "es el del invariante\n");
                mcu.alimenta(true);
                t3_adc();
                t3_dma();
                t3_resto();
                t3_lpm();
                t3_edumkii();
                std::printf("\nTOTAL P4 (solo T3b): %u comprobaciones OK, %u fallos\n",
                            g_ok, g_fallos);
                sc_stop();
                return;
            }
            if (std::string(solo) == "t3") {
                std::printf("  [TESTP4_SOLO=t3] solo los grupos de T3: el tiempo NO es el "
                            "del invariante\n");
                mcu.alimenta(true);
                t3_uart();
                t3_timers();
                t3_i2c_spi();
                t3_adc();
                t3_dma();
                t3_resto();
                t3_lpm();
                t3_edumkii();
                std::printf("\nTOTAL P4 (solo T3): %u comprobaciones OK, %u fallos\n",
                            g_ok, g_fallos);
                sc_stop();
                return;
            }
        }
        firmwares();
        t0_nombres();
        t0_catalogo();
        t0_formatos();
        t0_integracion();
        t1_parpadeo();
        t1_48mhz();
        t1_48mhz_mal();
        t1_perro();
        t1_boton();
        t1_identidad();
        t1_resets();
        t1_cristales();
        t1_swd();
        t1_rom();
        t1_coremark();
        t2_flctl();
        t2_autoprograma();
        t2_gdb();
        t3_uart();
        t3_timers();
        t3_i2c_spi();
        t3_adc();
        t3_dma();
        t3_resto();
        t3_lpm();
        t3_edumkii();
        std::printf("\n=====================================================\n");
        std::printf("TOTAL P4 : %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        std::printf("=====================================================\n");
        sc_stop();
    }

    double t3_ = 0.0;
};

#include "sc_main_p4_t3.h"
#include "sc_main_p4_t3b.h"

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbP4 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    // Lo que se puede comparar entre maquinas: el total sin P21 (T-16)
    if (g_t_gdb != SC_ZERO_TIME)
        std::printf("  de los cuales P21 (socket de GDB)     : %s\n"
                    "  el resto                             : %s\n",
                    g_t_gdb.to_string().c_str(),
                    (sc_time_stamp() - g_t_gdb).to_string().c_str());
    return g_fallos ? 1 : 0;
}
