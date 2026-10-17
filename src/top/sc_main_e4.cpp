// =============================================================================
// sc_main_e4.cpp — LA SUITE DEL MSP432E401Y (fase T6)
//
// Un banco aparte, como el del P4: un chip más dentro de otra simulación
// movería su invariante. [doc/analisis_msp432.md, §8 T6]
//
//   E0      las imágenes de firmware son las versionadas
//   E1-E3   sin tiempo simulado: nombres de pin, catálogo, la integración
//   E4-E13  el chip con e4_demo (cargado como ELF): PIOSC, 120 MHz por las
//           dos fuentes del PLL, los avisos de MEMTIM0 y del PLL, un
//           periférico sin reloj, el pulsador, el perro, SYSRESETREQ, el dato
//           enmascarado, LOCK/CR y DEN
//   E14     CoreMark
//   E19-E24 (entrega 3) los periféricos con DriverLib (e4_t3): UART0 por el
//           backchannel, GPTM (periódico, PWM, captura de tiempo y de
//           flancos), I2C0 con una 24C02, SSI2 en bucle por los pines, ADC0
//           (por el procesador, el sensor de temperatura y disparado por un
//           GPTM), la uDMA y la hibernación con el RTC
//   E25-E29 (entrega 4) la ROM R1: su imagen y sus tablas, el cargador serie
//           con la Flash en blanco (el banco hace de sflash por el
//           backchannel), un firmware con MAP_ (e4_rom) y ROM_UpdateUART, sin
//           ROM (--rom ninguna) y el cargador pedido por un pin (BOOTCFG)
//   E15-E18 (entrega 2) la Flash y la EEPROM desde el firmware, con DriverLib,
//           y GDB de verdad por la sonda de los pines (G2, por PC0/PC1) y por
//           el DAP (G1), con `load` (D2)
//
// EL TIEMPO SIMULADO DE E18 NO ES REPRODUCIBLE entre máquinas, como el de P21
// en testp4: el banco imprime «el resto», el total sin él, y eso es lo que
// contrasta `verif/invariantes.txt`.
//
//   make teste4   (o: make -f Makefile.mcu-sim teste4-build && ./build/teste4)
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include "../common/asan_opciones.h"
#include "../common/huella_fw.h"
#include "../common/imagen_fw.h"
#include "../soc/stm32f4_mcu.h"
#include "../soc/msp432e4/msp432e4_mcu.h"
#include "../parts/netlist_xml.h"
#include "../parts/ext_parts.h"
#include "../verif/bus_test_master.h"
#include "../verif/swd_port.h"
#include "../parts/puente_serie.h"
#include "../verif/gdb_stub.h"
#include "../verif/gdb_client.h"
#include "../core/gdb_stub_dap.h"
#include <fstream>
#include <vector>

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
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

static const char* const FW_DEMO = "verif/fw/msp432e4/e4_demo/e4_demo.out";
static const char* const FW_CM   = "verif/fw/msp432e4/coremark/coremark_e4.bin";
static const char* const FW_DEMO_BIN = "verif/fw/msp432e4/e4_demo/e4_demo.bin";
static const char* const FW_NV   = "verif/fw/msp432e4/e4_nv/e4_nv.out";
static const char* const FW_T3   = "verif/fw/msp432e4/e4_t3/e4_t3.out";
static const char* const FW_ROM  = "verif/fw/msp432e4/e4_rom/e4_rom.out";

// El buzón de e4_demo [verif/fw/msp432e4/e4_demo/main.c, buzon_t]
enum : uint32_t {
    B_MODO = 0x00, B_ARRANQUES = 0x04, B_ESTADO = 0x08, B_RESC = 0x0C,
    B_DID0 = 0x10, B_DID1 = 0x14, B_FLASHPP = 0x18, B_SSIZE = 0x1C,
    B_RSCLKCFG = 0x20, B_MEMTIM0 = 0x24, B_PLLSTAT = 0x28, B_FAULTS = 0x2C,
    B_BFAR = 0x30, B_CFSR = 0x34, B_PULSACIONES = 0x38, B_RIS = 0x3C,
    B_M7_DATO = 0x40, B_M7_CR = 0x44, B_M7_AFSEL = 0x48, B_M7_LOCK = 0x4C,
    B_PR_TRAS_RCGC = 0x50, B_VUELTAS = 0x58, B_TAM = 0x60
};

SC_MODULE(TbE4) {
    McuE4 mcu;
    Msp432E4& dut;
    BusTestMaster tm{"tm"};
    Crystal* y1 = nullptr;     // 25 MHz en OSC0
    Crystal* y3 = nullptr;     // 32 768 Hz en XOSC0
    Button*  sw1 = nullptr;    // USR_SW1, PJ0 a masa
    Button*  sw2 = nullptr;    // USR_SW2, PJ1 a masa
    // Los dos servidores de GDB: el de la sonda soldada a PC0/PC1 (como el
    // XDS110 de la placa) y el que habla con el DAP por dentro
    GdbStub*    gdb_p = nullptr;
    GdbStubDap* gdb_d = nullptr;
    GdbClient   cli;
    static constexpr unsigned PUERTO_PINES = 3441, PUERTO_DAP = 3442;
    // Entrega 3: el backchannel (UART0 en PA0/PA1), relojes de prueba en PA6
    // (captura de tiempo) y PM4 (de flancos), una 24C02 en I2C0 (PB2/PB3) con
    // sus pull-ups, una pista de XDAT0 a XDAT1 del SSI2 (PD1 -> PD0), y dos
    // tensiones en AIN0 (PE3) y AIN1 (PE2). Todo desoldado hasta su grupo
    PuenteSerie* bcl = nullptr;
    ExtClock* reloj_cap = nullptr;
    ExtClock* reloj_cnt = nullptr;
    I2cEeprom* eeprom = nullptr;
    I2cWire *pu_scl = nullptr, *pu_sda = nullptr;
    SignalLink* pista_ssi = nullptr;
    Driver *v_ain0 = nullptr, *v_ain1 = nullptr;

    SC_CTOR(TbE4)
        : mcu("dut", DBG_PINES, Cableado(), *CatalogoMcu::busca("MSP432E401Y")),
          dut(mcu.chip()) {
        tm.isk.bind(dut.from_tb);
        y1 = new Crystal(*dut.osc0);
        y3 = new Crystal(*dut.xosc0);
        sw1 = new Button(dut.gpio.nodo_de(e4::PJ, 0));
        sw2 = new Button(dut.gpio.nodo_de(e4::PJ, 1));
        sw1->release(); sw2->release();
        gdb_p = new GdbStub("gdb_p", *mcu.nodo_swclk(), *mcu.nodo_swdio(), PUERTO_PINES, 2e6);
        gdb_p->set_enabled(false);
        gdb_p->pon_programador(&ProgramadorE4::el());
        gdb_d = new GdbStubDap("gdb_d", dut.core.debug, PUERTO_DAP);
        gdb_d->set_enabled(false);
        gdb_d->pon_programador(&ProgramadorE4::el());
        PuenteSerie::Config cb;
        cb.destino = serie::parsea("memoria");
        bcl = new PuenteSerie("bcl", &dut.gpio.nodo_de(e4::PA, 1), &dut.gpio.nodo_de(e4::PA, 0),
                              nullptr, nullptr, nullptr, cb);
        bcl->set_enabled(false);
        reloj_cap = new ExtClock("reloj_cap", dut.gpio.nodo_de(e4::PA, 6), 0.0);
        reloj_cnt = new ExtClock("reloj_cnt", dut.gpio.nodo_de(e4::PM, 4), 0.0);
        eeprom = new I2cEeprom("eeprom", dut.gpio.nodo_de(e4::PB, 2), dut.gpio.nodo_de(e4::PB, 3), 0x50);
        pu_scl = new I2cWire("pu_scl", {&dut.gpio.nodo_de(e4::PB, 2)});
        pu_sda = new I2cWire("pu_sda", {&dut.gpio.nodo_de(e4::PB, 3)});
        pista_ssi = new SignalLink("pista_ssi", dut.gpio.nodo_de(e4::PD, 1), dut.gpio.nodo_de(e4::PD, 0));
        for (ExtPartBase* q : std::initializer_list<ExtPartBase*>{eeprom, pu_scl, pu_sda, pista_ssi})
            q->set_enabled(false);
        v_ain0 = new Driver(dut.gpio.nodo_de(e4::PE, 3), 3.3, 10.0);
        v_ain1 = new Driver(dut.gpio.nodo_de(e4::PE, 2), 3.3, 10.0);
        v_ain0->release(); v_ain1->release();
        SC_THREAD(run);
    }
    ~TbE4() override {
        delete y1; delete y3; delete sw1; delete sw2; delete gdb_p; delete gdb_d;
        delete bcl; delete reloj_cap; delete reloj_cnt; delete eeprom; delete pu_scl;
        delete pu_sda; delete pista_ssi; delete v_ain0; delete v_ain1;
    }

    // --- Utilidades -----------------------------------------------------------
    uint32_t buzon(uint32_t off) { return dut.sram.peek32(off); }
    void arranca(uint32_t modo, const char* fw = FW_DEMO) {
        mcu.reset_pin(true);
        wait(20, SC_US);
        if (fw) mcu.carga_firmware(fw);
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        dut.sram.poke32(B_MODO, modo);
        mcu.reset_pin(false);
    }
    bool espera_estado(uint32_t e, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (sc_time_stamp() < lim) {
            if (buzon(B_ESTADO) == e) return true;
            wait(50, SC_US);
        }
        return buzon(B_ESTADO) == e;
    }
    double periodo(unsigned p, unsigned i, unsigned n = 4, double max_ms = 50) {
        const sc_time t_lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
        sc_time primero = SC_ZERO_TIME, ultimo = SC_ZERO_TIME;
        unsigned flancos = 0;
        bool ant = dut.gpio.nodo_de(p, i).voltage() > 1.65f;
        while (sc_time_stamp() < t_lim && flancos <= n) {
            wait(t_lim - sc_time_stamp(), dut.gpio.nodo_de(p, i).value_changed_event());
            const bool v = dut.gpio.nodo_de(p, i).voltage() > 1.65f;
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
        grupo("E0 Imagenes de firmware [verif/fw/huellas.txt]");
        const huella_fw::Veredicto v =
            huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "e4");
        std::printf("         %d imagenes: %s\n", v.comprobadas, v.detalle.c_str());
        check(v.ok(), "las imagenes de la suite del MSP432E4 son las versionadas");
    }

    // Sin tiempo simulado
    void nombres() {
        grupo("E1 Nombres de pin del E4: PA0..PQ7, sin I ni O");
        unsigned p = 99, i = 99;
        check(e4::pad_e4("PA0", p, i) && p == 0 && i == 0, "PA0 es el primer pin");
        check(e4::pad_e4("PJ1", p, i) && p == e4::PJ && i == 1,
              "PJ1 es el puerto J, el noveno: no hay I");
        check(e4::pad_e4("PN0", p, i) && p == e4::PN && i == 0 &&
              e4::pad_e4("PQ4", p, i) && p == e4::PQ && i == 4,
              "PN0 (el LED D2) y PQ4: no hay O");
        check(e4::pad_e4("PN.1", p, i) && p == e4::PN && i == 1 &&
              e4::pad_e4("PF_4", p, i) && p == e4::PF && i == 4,
              "PN.1 y PF_4 son alias de PN1 y PF4");
        check(!e4::pad_e4("PI0", p, i) && !e4::pad_e4("PO0", p, i) &&
              !e4::pad_e4("PA8", p, i) && !e4::pad_e4("PR0", p, i) &&
              !e4::pad_e4("P1.0", p, i),
              "y PI0, PO0, PA8, PR0 (del E411Y) y P1.0 no son pines del E401Y");
        check(e4::canonico_e4(e4::PQ, 3) == "PQ3" && e4::canonico_e4(e4::PJ, 0) == "PJ0",
              "la forma canonica es la del datasheet");
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        c.limpia();
        std::string m;
        check(pad_desde_nombre("PJ0", m, p, i) == false || p != e4::PJ,
              "SIN chip de TI en la placa, PJ0 no es del E4: la regla de ST manda");
        c.fija("u0", &NOM_MSP432E4);
        check(pad_desde_nombre("u0.PN1", m, p, i) && m == "u0" && p == e4::PN && i == 1,
              "u0.PN1, con un E4 en u0: el decimotercer puerto");
        check(pad_desde_nombre("u1.PD12", m, p, i) && m == "u1" && p == 3 && i == 12,
              "y en la misma placa u1.PD12 sigue siendo de un STM32");
        c.limpia();
        Netlist nl;
        const std::string e = netlist_desde_texto(nl,
            "<placa nombre='mixta'>"
            "<mcu tipo='MSP432E401Y' id='u0'/><mcu tipo='STM32F407VG' id='u1'/>"
            "<componente tipo='Led' id='L1' a_vss='si'><pin nombre='anodo' nodo='u0.PN.0'/></componente>"
            "<componente tipo='Led' id='L2' a_vss='si'><pin nombre='anodo' nodo='u1.P3.12'/></componente>"
            "</placa>");
        bool l1 = false, l2 = false;
        for (const Instancia& in : nl.instancias())
            for (const Conexion& cx : in.pines) {
                if (cx.nodo == "u0.PN0") l1 = true;
                if (cx.nodo == "u1.PD12") l2 = true;
            }
        check(e.empty() && l1 && l2,
              "un XML con un MSP432E401Y y un STM32F407VG: u0.PN.0 queda PN0 y u1.P3.12 "
              "queda PD12");
        ContextoNomenclatura::g().limpia();
    }

    void catalogo() {
        grupo("E2 El catalogo, la factoria y el encapsulado");
        const McuDesc* d = CatalogoMcu::busca("msp432e401y");
        check(d && std::string(d->familia) == "MSP432E4" && d->enc->n_gpio == 90,
              "MSP432E401Y esta en el catalogo, de la familia MSP432E4, con 90 E/S");
        check(d && d->enc->coherente(), "y su TQFP128 cuadra: 90 pads marcados");
        check(FabricaMcu::conoce("MSP432E4") && FabricaMcu::conoce("STM32F4"),
              "la factoria sabe construir las dos familias");
        check(!ENC_E4_TQFP128.bonded(e4::PB, 6) && ENC_E4_TQFP128.bonded(e4::PA, 6) &&
              !ENC_E4_TQFP128.bonded(e4::PJ, 2) && ENC_E4_TQFP128.bonded(e4::PQ, 4),
              "PB6 y PJ2 no salen; PA6 y PQ4 si");
        check(mcu.nodo_swclk() == &dut.gpio.nodo_de(e4::PC, 0) &&
              mcu.nodo_swdio() == &dut.gpio.nodo_de(e4::PC, 1),
              "SWCLK y SWDIO son PC0 y PC1: pines de puerto, como PA14/PA13 en un STM32");
    }

    void integracion() {
        grupo("E3 La integracion del nucleo");
        check(!INTEGRACION_MSP432E4.alias_arranque &&
              !INTEGRACION_MSP432E4.en_ppb_fab(0xE0042000u),
              "sin alias de fabricante en 0x0 y sin ventana en el PPB: lo de TI esta "
              "en el bus de perifericos");
        check(dut.core.nucleo.n_irq == 111 && dut.core.nucleo.prio_bits == 3,
              "111 lineas (GPIOA..I2C9) y 3 bits de prioridad");
        check(e4::fws_minimo(16e6) == 0 && e4::fws_minimo(40e6) == 1 &&
              e4::fws_minimo(120e6) == 5,
              "la tabla de MEMTIM0 es la de DriverLib: 0 hasta 16 MHz, 5 a 120");
    }

    // =======================================================================
    double t16_ = 0;
    void parpadeo() {
        grupo("E4 Arranca desde 0x0 con PIOSC y parpadea (modo 0)");
        mcu.alimenta(true);
        arranca(0);
        check(espera_estado(100, 5), "el firmware llega al bucle de parpadeo");
        check_eq(buzon(B_ARRANQUES), 1, "una sola vez: los perros del E4 arrancan apagados");
        check(std::fabs(dut.f_sys() - 16e6) < 1, "SYSCLK = 16 MHz, de PIOSC");
        check_eq(buzon(B_RESC), SysctlE4::RESC_POR, "RESC = POR al encender");
        check_eq(buzon(B_DID0), e4::DID0_E401Y, "DID0 = 0x180C0002, el que espera OpenOCD");
        check_eq(buzon(B_DID1), e4::DID1_E401Y, "DID1 = 0x102DC06E");
        check_eq(buzon(B_FLASHPP) & 0xFFFF, 0x1FF, "FLASHPP: 1 MB");
        check_eq((buzon(B_FLASHPP) >> 16) & 7, 4, "y sectores de 16 KB");
        check_eq(buzon(B_SSIZE), 0x3FF, "SSIZE: 256 KB de SRAM");
        t16_ = periodo(e4::PN, 0);
        std::printf("    periodo de PN0 a 16 MHz: %.3f ms\n", t16_ * 1e3);
        check(t16_ > 0, "PN0 (D2) conmuta");
        check(periodo(e4::PN, 1) > 0 && periodo(e4::PF, 4) > 0 && periodo(e4::PF, 0) > 0,
              "y PN1 (D1), PF4 (D3) y PF0 (D4) tambien: los cuatro LED, por el dato "
              "enmascarado");
        check(dut.avisos_flash() == 0 && dut.avisos_sin_reloj() == 0,
              "sin avisos: el firmware da reloj a N y F y espera a PRGPIO");
    }

    void a120() {
        grupo("E5 120 MHz desde el cristal de 25 MHz, en el orden de TI (modo 1)");
        const unsigned af0 = dut.avisos_flash(), ap0 = dut.sysctl.avisos_pll();
        arranca(1);
        check(espera_estado(100, 10), "llega a parpadear");
        check(std::fabs(dut.f_sys() - 120e6) < 1, "SYSCLK = 120 MHz: 25 · 96 / (2 · 5) / 2");
        check(dut.sysctl.mosc.is_ready() && dut.sysctl.pll_enganchado(),
              "con el MOSC en marcha y el PLL enganchado");
        check_eq(dut.sysctl.fws(), 5, "MEMTIM0.FWS = 5 en vigor");
        check(buzon(B_RSCLKCFG) & (1u << 28), "RSCLKCFG.USEPLL");
        check(dut.avisos_flash() == af0 && dut.sysctl.avisos_pll() == ap0,
              "en el orden de TI no hay ningun aviso");
        const double t120 = periodo(e4::PN, 0);
        std::printf("    periodo de PN0 a 120 MHz: %.3f ms (%.2f veces mas rapido)\n",
                    t120 * 1e3, t120 > 0 ? t16_ / t120 : 0.0);
        check(t120 > 0 && t16_ / t120 > 3.0 && t16_ / t120 < 7.5 + 1e-6,
              "el mismo bucle va hasta 7,5 veces mas deprisa: lo que da el reloj, menos "
              "los cinco estados de espera de las lineas que no estan en la prebusqueda");
        check(dut.flash.esperas_pagadas() > 0, "y los estados de espera se pagan");
    }

    void a120_mal() {
        grupo("E6 La secuencia MALA (modo 2): USEPLL sin LOCK y MEMTIM0 sin MEMTIMU");
        const unsigned af0 = dut.avisos_flash(), ap0 = dut.sysctl.avisos_pll();
        arranca(2);
        check(espera_estado(100, 10), "y al corregirla, llega a parpadear");
        check(dut.sysctl.avisos_pll() == ap0 + 1,
              "USEPLL con el PLL sin enganchar: el modelo avisa, una vez");
        check(dut.avisos_flash() == af0 + 1,
              "120 MHz con FWS = 0 en vigor (MEMTIM0 escrito, sin MEMTIMU): tambien");
        check_eq(dut.sysctl.fws(), 5, "y al final, FWS = 5");
    }

    void a120_piosc() {
        grupo("E7 120 MHz desde PIOSC, sin cristal (modo 8)");
        y1->set_enabled(false);
        arranca(8);
        check(espera_estado(100, 10), "llega a parpadear sin el cristal de la placa");
        check(std::fabs(dut.f_sys() - 120e6) < 1, "SYSCLK = 120 MHz: 16 · 30 / (2 · 1) / 2");
        check(!dut.sysctl.mosc.is_ready(), "con el MOSC parado");
        y1->set_enabled(true);
    }

    void sin_reloj() {
        grupo("E8 Un periferico sin reloj (modo 3): error de bus, y dicho");
        const unsigned n0 = dut.avisos_sin_reloj();
        arranca(3);
        check(espera_estado(100, 10), "el firmware se recupera del fallo y parpadea");
        check(buzon(B_FAULTS) >= 1, "leer GPION sin RCGCGPIO da un BusFault");
        check_eq(buzon(B_BFAR), e4::base_gpio(e4::PN) + 0x400, "BFAR = GPION->DIR");
        check(buzon(B_CFSR) & (1u << 15), "con BFARVALID");
        check(dut.avisos_sin_reloj() > n0, "y el modelo lo cuenta y lo dice");
        check_eq(buzon(B_PR_TRAS_RCGC) & (1u << 12), 0,
                 "PRGPIO.R12 no sube en el mismo instante que RCGCGPIO.R12");
    }

    void pulsador() {
        grupo("E9 USR_SW1 (PJ0) por interrupcion de flanco de bajada (modo 4)");
        arranca(4);
        check(espera_estado(40, 5), "el firmware espera en WFI");
        check(dut.gpio.nodo_de(e4::PJ, 0).voltage() > 3.0f, "PJ0 en alto por su pull-up");
        for (int k = 0; k < 3; ++k) {
            sw1->press(); wait(200, SC_US); sw1->release(); wait(200, SC_US);
        }
        check_eq(buzon(B_PULSACIONES), 3, "tres pulsaciones, tres interrupciones GPIOJ");
        check_eq(buzon(B_RIS) & 1u, 1, "con RIS.0 puesto al entrar, y borrado con ICR");
        sw2->press(); wait(200, SC_US); sw2->release(); wait(200, SC_US);
        check_eq(buzon(B_PULSACIONES), 3, "USR_SW2 (PJ1) no esta en IM: no interrumpe");
    }

    void perro() {
        grupo("E10 El perro WDT0 con RESEN, sin alimentar (modo 5)");
        const unsigned r0 = dut.resets();
        arranca(5);
        check(espera_estado(100, 10), "tras el reset del perro, el firmware parpadea");
        check_eq(buzon(B_ARRANQUES), 2, "dos arranques: el segundo vencimiento resetea");
        check(buzon(B_RESC) & SysctlE4::RESC_WDT0, "y RESC dice por que: WDT0");
        check(dut.resets() == r0 + 2, "un reset del pin y otro del perro");
        check(!dut.wdt0.en_marcha(), "y el reset lo deja apagado");
    }

    void sysresetreq() {
        grupo("E11 SYSRESETREQ (modo 6)");
        arranca(6);
        check(espera_estado(61, 5), "el firmware se resetea a si mismo y vuelve");
        check(buzon(B_RESC) & SysctlE4::RESC_SW, "RESC dice SW");
        check(!(buzon(B_RESC) & SysctlE4::RESC_POR), "y no POR: no se ha cortado la corriente");
    }

    void gpio_tiva() {
        grupo("E12 El GPIO de Tiva (modo 7): dato enmascarado, LOCK/CR y DEN");
        const unsigned d0 = dut.gpio.avisos_den();
        arranca(7);
        check(espera_estado(70, 5), "el firmware llega al final");
        check_eq(buzon(B_M7_DATO) & 0xFF, 0x02,
                 "escribir 0xFF en base + (0x02 << 2) solo cambia PN1");
        check_eq((buzon(B_M7_DATO) >> 8) & 0xFF, 0x00,
                 "y leer en base + (0x01 << 2) solo ve PN0");
        check_eq(buzon(B_M7_CR) & 0xFF, 0x7F, "GPIOD->CR = 0x7F: PD7 protegido");
        check_eq(buzon(B_M7_AFSEL) & 0xFF, 0x00, "sin abrir, AFSEL.7 no cambia");
        check_eq((buzon(B_M7_AFSEL) >> 8) & 0xFF, 0x80, "con LOCK y CR abiertos, si");
        check_eq(buzon(B_M7_LOCK), 0x010001,
                 "LOCK se lee 1 cerrado, 0 abierto, y 1 al volver a cerrarlo");
        check((buzon(B_M7_CR) >> 8) == 0x7F, "y CR vuelve a 0x7F");
        check(dut.gpio.avisos_den() == d0 + 1,
              "PF0 de salida sin DEN: el modelo avisa una vez");
        check(dut.gpio.nodo_de(e4::PF, 0).voltage() < 0.5f, "y el pin no se mueve");
    }

    void coremark() {
        grupo("E13 CoreMark sobre el MSP432E401Y");
        mcu.reset_pin(true);
        wait(20, SC_US);
        check(mcu.carga_firmware(FW_CM), "coremark_e4.bin cargado en 0x0");
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
        check(fin, "CoreMark termina");
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

    // =======================================================================
    // Entrega 2: la Flash, la EEPROM y GDB
    // =======================================================================
    void flash_firmware() {
        grupo("E15 Un firmware que reprograma su propia Flash con DriverLib (e4_nv, modo 0)");
        arranca(0, FW_NV);
        check(espera_estado(100, 60), "el firmware llega al final");
        const uint32_t r = buzon(0x0C);
        std::printf("    resultado 0x%02X; %u vueltas mientras borra; FCRIS = 0x%X\n", r,
                    buzon(0x10), buzon(0x14));
        check(r & 1u, "FMC.ERASE borra un sector de 16 KB y lo lee todo a 0xFF");
        check(buzon(0x10) <= 2,
              "y mientras borra la CPU no da ni una vuelta: el bucle esta en la Flash");
        check(r & 2u, "FlashProgram de 64 palabras, con el bufer de escritura (FWBn, FMC2)");
        check(r & 4u, "FlashProgram de una palabra suelta, fuera de alineacion de fila");
        check(r & 8u, "un 0 no vuelve a 1 sin borrar: INVDRIS, y queda el AND");
        check(r & 16u, "FlashProtectSet(solo lectura) y FlashErase da -1, con ARIS");
        check(r & 32u, "y FlashProgram tampoco programa");
        check(r & 64u, "escribir la Flash como si fuera RAM es un error de bus");
        check(dut.flash.peek32(0x80000u) == 0xA5000000u && dut.flash.peek32(0x800FCu) == 0xA500003Fu,
              "y lo programado esta en la Flash, visto desde fuera");
    }

    void eeprom_firmware() {
        grupo("E16 La EEPROM con DriverLib, y despues de un reset (e4_nv, modo 1)");
        arranca(1, FW_NV);
        check(espera_estado(100, 60), "el firmware se resetea y llega al final");
        check_eq(buzon(B_ARRANQUES), 2, "dos arranques: el segundo, tras SYSRESETREQ");
        const uint32_t r = buzon(0x0C);
        check(r & 1u, "EEPROMInit da EEPROM_INIT_OK, despues de SREEPROM");
        check(r & 2u, "EESIZE: 6144 bytes en 96 bloques");
        check(r & 8u, "lo escrito en el primer arranque se lee en el segundo: no es volatil");
        check(dut.eeprom.palabra(0x40 / 4) == 0xEE000000u && dut.eeprom.palabra(0x40 / 4 + 7) == 0xEE000707u,
              "y esta en la EEPROM, visto desde fuera");
        check(dut.eeprom.palabra(5 * 16) == 0xFFFFFFFFu,
              "un bloque de solo lectura (EEPROM_PROT_RO_LNA_URO) no se escribe: NOPERM");
    }

    // --- GDB --------------------------------------------------------------
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
    std::string espera_parada(double ms = 100) { return cli.recibir(sc_time(ms, SC_MS)); }

    struct ResGdb {
        bool conecta = false, carga = false, lectura = false, wp = false, bp = false,
             paso = false, corre = false;
        uint32_t pc_reset = 0, pc_bp = 0, pc_paso = 0;
    };

    ResGdb sesion_gdb(unsigned puerto, uint32_t parpadea, uint32_t dir_arranques) {
        ResGdb r;
        // En la Flash hay OTRA cosa: CoreMark. El `load` tiene que borrar.
        arranca(0, FW_CM);
        wait(2, SC_MS);
        if (!cli.conectar(puerto)) return r;
        wait(2, SC_MS);
        cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=arm");
        r.conecta = cli.pedir("QStartNoAckMode") == "OK" &&
                    cli.pedir("?").rfind("T05", 0) == 0;
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
        cli.pedir("M20000000,8:0000000000000000");          // el buzon: modo 0
        cli.pedir("qRcmd," + hex("reset halt"), sc_time(50, SC_MS));
        r.pc_reset = pc();
        char z[64];
        std::snprintf(z, sizeof z, "Z2,%x,4", dir_arranques);
        const bool zw = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pw = espera_parada();
        r.wp = zw && pw.find("watch") != std::string::npos && buzon(B_ARRANQUES) == 1;
        std::snprintf(z, sizeof z, "z2,%x,4", dir_arranques);
        cli.pedir(z);
        std::snprintf(z, sizeof z, "Z0,%x,2", parpadea);
        const bool zb = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pb = espera_parada();
        r.pc_bp = pc();
        r.bp = zb && pb.find("swbreak") != std::string::npos && r.pc_bp == parpadea;
        std::snprintf(z, sizeof z, "z0,%x,2", parpadea);
        cli.pedir(z);
        cli.enviar("vCont;s");
        const std::string ps = espera_parada();
        r.pc_paso = pc();
        r.paso = ps.rfind("T05", 0) == 0 && r.pc_paso > parpadea && r.pc_paso < parpadea + 8;
        // Y que siga: continuar, mirar los LED, interrumpir y soltar
        cli.enviar("vCont;c");
        wait(1, SC_MS);
        r.corre = buzon(B_ESTADO) == 100 && periodo(e4::PN, 0) > 0;
        cli.crudo("\x03");
        espera_parada();
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        return r;
    }

    void informa(const char* via, const ResGdb& r, uint32_t rh) {
        auto c = [&](bool v, const std::string& t) { check(v, (std::string(via) + ": " + t).c_str()); };
        c(r.conecta, "GDB se conecta, negocia y encuentra el nucleo parado");
        c(r.carga, "`load` de e4_demo.bin con paquetes M sobre CoreMark: el stub borra y "
                   "programa la Flash por FLASH_CTRL (D2)");
        c(r.lectura, "y lo leido de vuelta es el .bin, byte a byte");
        c(r.pc_reset == rh, "`monitor reset halt` para en la primera instruccion, Reset_Handler");
        c(r.wp, "un watchpoint en buzon.arranques para el nucleo en la primera escritura");
        c(r.bp, "un punto de ruptura en parpadea() para el nucleo alli");
        c(r.paso, "un paso avanza una instruccion");
        c(r.corre, "y al soltarlo, el firmware cargado parpadea");
    }

    void gdb() {
        uint32_t rh = 0, pp = 0, arr = 0;
        const bool sim = simbolo_elf(FW_DEMO, "Reset_Handler", rh) &&
                         simbolo_elf(FW_DEMO, "parpadea", pp) &&
                         simbolo_elf(FW_DEMO, "buzon", arr);
        grupo("E17 Los simbolos de e4_demo, del ELF");
        check(sim && arr == 0x20000000u && rh >= 4u * 127u && rh < 0x1000,
              "Reset_Handler detras de la tabla de 127 vectores, y buzon en 0x2000_0000");
        arr += B_ARRANQUES;
        const sc_time t0 = sc_time_stamp();
        grupo("E18 GDB por la sonda de los pines (G2, PC0/PC1) y por el DAP (G1)");
        gdb_p->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rp = sesion_gdb(PUERTO_PINES, pp, arr);
        gdb_p->set_enabled(false);
        gdb_p->soltar_pines();
        informa("G2 (pines)", rp, rh);
        gdb_d->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rd_ = sesion_gdb(PUERTO_DAP, pp, arr);
        gdb_d->set_enabled(false);
        informa("G1 (DAP)", rd_, rh);
        check(rp.pc_reset == rd_.pc_reset && rp.pc_bp == rd_.pc_bp && rp.pc_paso == rd_.pc_paso,
              "los dos caminos dan lo mismo: el mismo depurador, otro cable");
        check(ProgramadorE4::el().fallos() == 0, "y el programador no ha fallado ni una vez");
        g_t_gdb += sc_time_stamp() - t0;
    }

    // =======================================================================
    // Entrega 3: los periféricos, con DriverLib (e4_t3)
    // =======================================================================
    uint32_t r3(unsigned i) { return buzon(0x10 + 4 * i); }
    // Mide en un pin el periodo y la fraccion en alto, sobre n ciclos
    bool pwm(unsigned p, unsigned i, double& periodo_s, double& alto, unsigned n = 4, double max_ms = 20) {
        analog_net_if& nd = dut.gpio.nodo_de(p, i);
        const sc_time lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
        bool ant = nd.voltage() > 1.65f;
        sc_time sube0 = SC_ZERO_TIME, ult_sube = SC_ZERO_TIME, baja = SC_ZERO_TIME;
        unsigned subidas = 0;
        double t_alto = 0;
        while (sc_time_stamp() < lim && subidas <= n) {
            wait(lim - sc_time_stamp(), nd.value_changed_event());
            const bool v = nd.voltage() > 1.65f;
            if (v && !ant) {
                if (subidas == 0) sube0 = sc_time_stamp();
                ult_sube = sc_time_stamp();
                ++subidas;
            } else if (!v && ant && subidas > 0 && subidas <= n) {
                baja = sc_time_stamp();
                t_alto += (baja - ult_sube).to_seconds();
            }
            ant = v;
        }
        if (subidas < 2) return false;
        periodo_s = (ult_sube - sube0).to_seconds() / double(subidas - 1);
        alto = t_alto / double(subidas - 1) / periodo_s;
        return true;
    }

    void t3_uart() {
        grupo("E19 UART0 por el backchannel (PA0/PA1) a 115200, con DriverLib (e4_t3, modo 0)");
        bcl->set_baudios(115200);
        bcl->set_formato(FormatoUart{});
        bcl->borra_recibido();
        bcl->set_enabled(true);
        arranca(0, FW_T3);
        check(espera_estado(100, 20), "SysCtlClockFreqSet, GPIOPinConfigure y UARTConfigSetExpClk");
        check_eq(buzon(0x0C), 120000000, "SysCtlClockFreqSet devuelve 120 MHz");
        wait(3, SC_MS);
        check(bcl->recibido() == "Hola desde el MSP432E4\r\n",
              "el PuenteSerie en PA1 recibe el saludo entero, a 115200 8N1");
        const double b = dut.uart0.baudios();
        std::printf("    baudios: %.1f (IBRD = 65, FBRD = 7 a 120 MHz)\n", b);
        check(std::fabs(b - 115200.0) / 115200.0 < 0.001, "IBRD/FBRD dan 115200 a menos del 0,1 %");
        bcl->envia("eco");
        wait(2, SC_MS);
        check(bcl->recibido() == "Hola desde el MSP432E4\r\nECO",
              "el eco por interrupcion vuelve en mayusculas");
        check(r3(2) == 3 && r3(0) >= 1 && r3(1) >= 1,
              "tres caracteres, por la interrupcion de tiempo de espera (RT): la FIFO no llega "
              "al nivel de RX");
        check(bcl->errores_trama() == 0, "sin errores en el puente");
        bcl->set_enabled(false);
    }

    void t3_timers() {
        grupo("E20 GPTM: periodico, PWM, captura de tiempo y de flancos (e4_t3, modo 1)");
        reloj_cap->set_freq(1000.0);
        reloj_cnt->set_freq(10000.0);
        arranca(1, FW_T3);
        check(espera_estado(10, 20), "los cuatro temporizadores en marcha");
        const sc_time t0 = sc_time_stamp();
        check(espera_estado(100, 30), "TIMER0 de 32 bits vence 20 veces");
        const double ms = (sc_time_stamp() - t0).to_seconds() * 1e3;
        std::printf("    20 vencimientos de TIMER0 en %.3f ms\n", ms);
        check(ms > 18.5 && ms < 20.5, "uno por milisegundo: TimerLoadSet(f/1000 - 1)");
        double per = 0, alto = 0;
        const bool hay = pwm(e4::PA, 4, per, alto);
        std::printf("    PWM en PA4: %.3f us, %.1f %% en alto\n", per * 1e6, alto * 100);
        check(hay && std::fabs(per - 100e-6) < 0.2e-6, "TIMER2A en PWM: 10 kHz en PA4 (T2CCP0)");
        check(hay && std::fabs(alto - 0.25) < 0.01, "y el 25 % en alto: de ILR a MATCHR");
        check(r3(2) >= 3, "TIMER3A captura los flancos de subida de PA6");
        const uint32_t d = (r3(3) - r3(4)) & 0xFFFFFFu;
        std::printf("    capturas: 0x%06X, 0x%06X: %u ciclos entre flancos\n", r3(3), r3(4), d);
        check(d >= 119990 && d <= 120010, "entre dos flancos de 1 kHz caben 120 000 ciclos de 120 MHz "
                                         "(24 bits, contando hacia abajo)");
        check(r3(6) >= 1, "TIMER4A cuenta 10 flancos de PM4 y casa: CAMRIS");
        check_eq(r3(7), 0, "y al casar se para (TAEN a 0)");
        reloj_cap->set_freq(0.0);
        reloj_cnt->set_freq(0.0);
    }

    void t3_i2c_spi() {
        grupo("E21 I2C0 con una 24C02, y SSI2 en bucle por los pines (e4_t3, modo 2)");
        for (ExtPartBase* q : std::initializer_list<ExtPartBase*>{eeprom, pu_scl, pu_sda, pista_ssi})
            q->set_enabled(true);
        arranca(2, FW_T3);
        check(espera_estado(100, 30), "el firmware llega al final");
        check_eq(r3(0), 0, "4 bytes a la 24C02 sin error (I2CMasterErr = 0)");
        check(eeprom->peek(0x10) == 0x11 && eeprom->peek(0x13) == 0x44,
              "y estan en la EEPROM, desde 0x10");
        check_eq(r3(1), 0x44332211u, "se leen de vuelta con START repetido y rafaga");
        check(r3(2) & 0x4u, "un esclavo que no esta: I2C_MASTER_ERR_ADDR_ACK");
        check_eq(r3(3), 0xFF003CA5u, "el SSI2 a 1 MHz en modo 0 recibe lo que manda, por la pista");
        check(std::fabs(dut.ssi2.hz() - 1e6) < 1, "y SSInClk va a 1 MHz: 120 / (2 · 60)");
        for (ExtPartBase* q : std::initializer_list<ExtPartBase*>{eeprom, pu_scl, pu_sda, pista_ssi})
            q->set_enabled(false);
    }

    void t3_adc() {
        grupo("E22 ADC0: AIN0, el sensor de temperatura y AIN1 disparado por TIMER1 (e4_t3, modo 3)");
        v_ain0->set_volts(1.0, 10.0);
        v_ain1->set_volts(2.0, 10.0);
        arranca(3, FW_T3);
        check(espera_estado(100, 30), "el firmware llega al final");
        std::printf("    AIN0 = %u, TS = %u (%.1f C), AIN1 = %u %u %u %u\n", r3(0), r3(1),
                    r3(2) / 10.0, r3(4), r3(5), r3(6), r3(7));
        check(r3(0) >= 1239 && r3(0) <= 1241, "1,0 V en AIN0 (PE3) con 3,3 V de referencia: 1241");
        check(r3(2) >= 245 && r3(2) <= 255, "el sensor de temperatura da 25 C con la formula del datasheet");
        check(r3(4) >= 2481 && r3(4) <= 2483 && r3(7) >= 2481 && r3(7) <= 2483,
              "2,0 V en AIN1 (PE2), cuatro veces, disparado por el vencimiento de TIMER1");
        v_ain0->release(); v_ain1->release();
    }

    void t3_udma() {
        grupo("E23 La uDMA: memoria a memoria y el TX de la UART0 (e4_t3, modo 4)");
        bcl->set_baudios(115200);
        bcl->borra_recibido();
        bcl->set_enabled(true);
        arranca(4, FW_T3);
        check(espera_estado(100, 30), "el firmware llega al final");
        check(r3(0) >= 1, "el canal 30 (software) en modo automatico acaba con la linea UDMA");
        check_eq(r3(1), 256, "y copia las 256 palabras");
        check_eq(r3(2), 1, "el canal 9 (UART0 TX) se apaga al acabar");
        wait(3, SC_MS);
        check(bcl->recibido() == "uDMA a la UART0\r\n", "y la UART0 lo saca entero: lo ha escrito la uDMA");
        check(dut.udma.transferencias() >= 256 + 17, "la uDMA ha hecho las 273 transferencias");
        bcl->set_enabled(false);
    }

    void t3_hib() {
        grupo("E24 La hibernacion: el RTC, su alarma, y despertar de hibernar (e4_t3, modo 5)");
        const unsigned h0 = dut.hibernaciones();
        arranca(5, FW_T3);
        check(espera_estado(100, 4000), "el firmware hiberna, despierta y llega al final");
        std::printf("    estado %u; r = %u %u %u %u %u %u %u; RTC %u s\n", buzon(B_ESTADO), r3(0), r3(1),
                    r3(2), r3(3), r3(4), r3(5), r3(6), dut.hib.segundos());
        check(r3(0) >= 1 || r3(6) == 1, "la alarma del RTC (RTCALT0) interrumpe a los 2 s");
        check(dut.hibernaciones() == h0 + 1, "HibernateRequest apaga el chip");
        check_eq(r3(6), 1, "y despierta con la alarma, con RESC = HIB");
        check_eq(r3(3), 0x5EED, "lo guardado en la memoria del modulo sobrevive");
        check(r3(4) >= 103, "y el RTC ha seguido contando mientras");
        check(r3(5) & 1u, "con RTCALT0 en RIS: la causa de despertar");
    }


    // =======================================================================
    // Entrega 4: la ROM R1 y su cargador serie. El banco hace de sflash: habla
    // el protocolo de las herramientas de TI por el backchannel (bcl, en
    // memoria), byte a byte, con esperas en tiempo simulado.
    size_t pos_ = 0;                              // lo ya leido de bcl->recibido()
    bool lee_byte(uint8_t& b, double ms = 50) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (bcl->recibido().size() <= pos_) {
            if (sc_time_stamp() >= lim) return false;
            wait(20, SC_US);
        }
        b = uint8_t(bcl->recibido()[pos_++]);
        return true;
    }
    // ACK (0xCC) o NAK (0x33) del cargador, saltando los ceros; 0 si no llega
    uint8_t respuesta(double ms = 50) {
        uint8_t b = 0;
        do { if (!lee_byte(b, ms)) return 0; } while (b == 0);
        return b;
    }
    uint8_t paquete(const std::string& d, bool suma_mal = false) {
        uint8_t s = 0;
        for (char c : d) s = uint8_t(s + uint8_t(c));
        if (suma_mal) s = uint8_t(s + 1);
        std::string p;
        p += char(d.size() + 2); p += char(s); p += d;
        bcl->envia(p);
        return respuesta();
    }
    static std::string be32(uint32_t v) {
        std::string r;
        for (int i = 3; i >= 0; --i) r += char((v >> (8 * i)) & 0xFF);
        return r;
    }
    // GET_STATUS: el estado, o -1 si algo del paquete de vuelta no cuadra
    int estado_cargador() {
        if (paquete(std::string(1, '\x23')) != 0xCC) return -1;
        uint8_t n = 0, s = 0, e = 0;
        do { if (!lee_byte(n)) return -1; } while (n == 0);
        if (n != 3 || !lee_byte(s) || !lee_byte(e) || s != e) return -1;
        bcl->envia(std::string("\x00\xCC", 2));
        return e;
    }
    // Lo que hace `sflash -c ... fichero.bin`: DOWNLOAD, SEND_DATA a trozos con
    // GET_STATUS detras de cada uno. Devuelve "" o lo que fallo.
    std::string descarga(const std::vector<uint8_t>& img, uint32_t dir, unsigned trozo = 64) {
        if (paquete(std::string(1, '\x21') + be32(dir) + be32(uint32_t(img.size()))) != 0xCC)
            return "DOWNLOAD sin ACK";
        int e = estado_cargador();
        if (e != 0x40) return "DOWNLOAD: estado " + std::to_string(e);
        for (size_t i = 0; i < img.size(); i += trozo) {
            const size_t n = std::min<size_t>(trozo, img.size() - i);
            if (paquete(std::string(1, '\x24') +
                        std::string(reinterpret_cast<const char*>(img.data() + i), n)) != 0xCC)
                return "SEND_DATA sin ACK en " + std::to_string(i);
            e = estado_cargador();
            if (e != 0x40) return "SEND_DATA: estado " + std::to_string(e) + " en " + std::to_string(i);
        }
        return "";
    }
    static std::vector<uint8_t> lee_bin(const char* ruta) {
        std::vector<uint8_t> d;
        std::ifstream f(ruta, std::ios::binary);
        d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return d;
    }
    bool flash_es(const std::vector<uint8_t>& img, uint32_t dir) {
        for (size_t i = 0; i < img.size(); ++i)
            if (uint8_t(dut.flash.peek32(uint32_t(dir + i) & ~3u) >> (8 * ((dir + i) & 3u))) != img[i])
                return false;
        return true;
    }
    void abre_bcl() {
        bcl->set_baudios(115200);
        bcl->set_formato(FormatoUart{});
        bcl->borra_recibido();
        pos_ = 0;
        bcl->set_enabled(true);
    }

    void rom_r1() {
        grupo("E25 La ROM R1: DriverLib en las casillas de rom.h y el cargador");
        check(dut.rom.origen() == RomE4::Origen::R1, "el chip sale con la ROM R1, sin --rom");
        check_eq(dut.rom.huella(), rom_r1_e4::FNV,
                 "la imagen es la de rom_r1.inc (y su huella, la de verif/fw/huellas.txt)");
        check_eq(dut.rom.peek32(e4::ROM_B), 0x20004000u, "vector 0 de la ROM: la pila del cargador");
        const uint32_t reset = dut.rom.peek32(e4::ROM_B + 4);
        check((reset & 1u) && reset > e4::ROM_B && reset < e4::ROM_B + e4::ROM_SIZE,
              "vector 1: el cargador, en la ROM y en Thumb");
        check_eq(dut.rom.peek32(e4::ROM_APITABLE), 1, "ROM_APITABLE[0], la version de la R1: 1");
        const uint32_t uart_t = dut.rom.peek32(e4::ROM_APITABLE + 4);
        const uint32_t upd = dut.rom.peek32(uart_t + 4 * 21);
        check(uart_t > e4::ROM_B && (upd & 1u) && upd > e4::ROM_B && upd < e4::ROM_B + e4::ROM_SIZE &&
              upd != dut.rom.peek32(uart_t + 4 * 20),
              "ROM_UARTTABLE[21] es UpdateUART, el cargador sin autobaudios");
        bool desc = false;
        for (const std::string& l : dut.limitaciones()) if (l.find("R1") != std::string::npos) desc = true;
        check(desc, "y las limitaciones de la R1 se dicen (--limitaciones)");
    }

    void cargador() {
        grupo("E26 Flash en blanco: el chip arranca en el cargador de la ROM (sflash por el banco)");
        mcu.reset_pin(true);
        wait(20, SC_US);
        dut.flash.borra_todo();
        const uint64_t i0 = dut.rom.instrucciones();
        mcu.reset_pin(false);
        wait(1, SC_MS);
        check(dut.arranco_en_rom(), "con la palabra de 0x4 a 0xFFFFFFFF, RMCTL.BA = 1: la ROM en 0x0");
        check(dut.rom.instrucciones() > i0 && dut.core.cpu.pc() >= e4::ROM_B &&
              dut.core.cpu.pc() < e4::ROM_B + e4::ROM_SIZE,
              "y el nucleo ejecuta el cargador, en la ROM");
        abre_bcl();
        bcl->envia(std::string("\x55\x55", 2));
        check_eq(respuesta(), 0xCC, "dos 0x55 a 115200: el cargador mide los baudios y contesta ACK");
        check_eq(paquete(std::string(1, '\x20')), 0xCC, "PING: ACK");
        check_eq(estado_cargador(), 0x40, "GET_STATUS: SUCCESS (0x40)");
        check_eq(paquete(std::string(1, '\x20'), true), 0x33, "un paquete con la suma mal: NAK (0x33)");
        check_eq(paquete(std::string(1, '\x21') + be32(0x00100000u) + be32(4)), 0xCC,
                 "DOWNLOAD fuera de la Flash: se acepta el paquete...");
        check_eq(estado_cargador(), 0x43, "...pero el estado es INVALID_ADR (0x43)");
        check_eq(paquete(std::string(1, '\x7E')), 0xCC, "una orden que no existe...");
        check_eq(estado_cargador(), 0x41, "...da UNKNOWN_CMD (0x41)");
        const std::vector<uint8_t> img = lee_bin(FW_DEMO_BIN);
        const sc_time t0 = sc_time_stamp();
        const std::string e = descarga(img, 0);
        std::printf("    %zu bytes de e4_demo.bin en %.1f ms simulados\n", img.size(),
                    (sc_time_stamp() - t0).to_seconds() * 1e3);
        check(img.size() > 1000 && e.empty(), e.empty() ? "e4_demo.bin entero, a trozos de 64 bytes, "
              "todos con SUCCESS" : e.c_str());
        check(flash_es(img, 0), "y la Flash tiene lo descargado, byte a byte");
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);                    // modo 0 en el buzon
        check_eq(paquete(std::string(1, '\x22') + be32(0)), 0xCC, "RUN 0x0: ACK");
        check(espera_estado(100, 5), "y salta a la aplicacion: e4_demo llega a parpadear");
        check(!dut.arranco_en_rom(), "con RMCTL.BA borrado: la Flash vuelve a 0x0");
        check(periodo(e4::PN, 0) > 0, "PN0 (D2) parpadea");
        check(bcl->errores_trama() == 0, "sin errores de trama en el puente");
        bcl->set_enabled(false);
        // Otra vez en el cargador, por un reset con la Flash buena: ya no
        mcu.reset_pin(true);
        wait(20, SC_US);
        dut.sram.load(ceros, sizeof ceros, 0);
        mcu.reset_pin(false);
        check(espera_estado(100, 5) && !dut.arranco_en_rom(),
              "tras un reset, con la Flash programada, arranca la aplicacion directamente");
    }

    void rom_map() {
        grupo("E27 Un firmware con MAP_ (e4_rom): DriverLib desde la ROM, y ROM_UpdateUART");
        abre_bcl();
        arranca(0, FW_ROM);
        check(espera_estado(100, 20), "e4_rom llega al final: todo lo de DriverLib, por la ROM");
        wait(1, SC_MS);                                           // lo que queda en la FIFO
        check_eq(buzon(0x0C), 1, "ROM_VERSION, la de la R1");
        check_eq(buzon(0x10), 120000000u, "SysCtlClockFreqSet (de DriverLib) devuelve 120 MHz");
        check(std::fabs(dut.f_sys() - 120e6) < 1, "y SYSCLK es 120 MHz");
        check(bcl->recibido() == "ROM R1\r\n",
              "el saludo sale por la UART0, configurada con ROM_UARTConfigSetExpClk");
        const double p = periodo(e4::PN, 0, 4, 20);
        std::printf("    periodo de PN0: %.3f ms (ROM_SysCtlDelay(f/3000) por medio periodo)\n", p * 1e3);
        // TI cuenta 3 ciclos por vuelta de SysCtlDelay; el nucleo del modelo
        // cobra el salto tomado con 2 de rellenado (1 + P, P = 2): 4 ciclos
        check(p > 1.9e-3 && p < 2.8e-3,
              "PN0 parpadea a 2-2,7 ms: ROM_GPIOPinWrite y ROM_SysCtlDelay (3 o 4 ciclos por vuelta)");
        const uint64_t i0 = dut.rom.instrucciones();
        wait(1, SC_MS);
        check(dut.rom.instrucciones() > i0, "y el nucleo ejecuta en la ROM (SysCtlDelay)");
        // Modo 1: ROM_UpdateUART con la UART0 ya a 115200: sin autobaudios
        abre_bcl();
        arranca(1, FW_ROM);
        check(espera_estado(10, 20), "modo 1: la aplicacion llama a ROM_UpdateUART");
        wait(1, SC_MS);
        while (pos_ < bcl->recibido().size()) ++pos_;              // el saludo
        check_eq(paquete(std::string(1, '\x20')), 0xCC, "y el cargador contesta al PING sin autobaudios");
        const std::vector<uint8_t> img = lee_bin(FW_DEMO_BIN);
        const std::string e = descarga(img, 0, 252);
        check(e.empty(), e.empty() ? "reprograma la Flash con e4_demo.bin, a 120 MHz, a trozos de 252" : e.c_str());
        check(flash_es(img, 0), "y la Flash tiene e4_demo");
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        const unsigned arr = buzon(B_ARRANQUES);
        check_eq(paquete(std::string(1, '\x25')), 0xCC, "RESET: ACK...");
        check(espera_estado(100, 10) && arr == 0, "...y reset del sistema: arranca e4_demo");
        check(buzon(B_RESC) & SysctlE4::RESC_SW, "con RESC = SW (SYSRESETREQ)");
        bcl->set_enabled(false);
    }

    void sin_rom() {
        grupo("E28 Sin ROM (--rom ninguna): ni cargador ni DriverLib en ROM");
        check(mcu.opcion("rom", "ninguna").empty(), "--rom ninguna");
        const uint64_t i0 = dut.rom.instrucciones();
        mcu.reset_pin(true);
        wait(20, SC_US);
        dut.flash.borra_todo();
        mcu.reset_pin(false);
        wait(1, SC_MS);
        check(!dut.arranco_en_rom(), "con la Flash en blanco no hay alias: BA = 0");
        check(dut.rom.instrucciones() == i0 && dut.core.cpu.pc() >= 0xF0000000u,
              "y el nucleo no ejecuta nada: su vector de reset es 0xFFFFFFFF");
        const unsigned f0 = dut.rom.fallos();
        arranca(0, FW_ROM);
        wait(2, SC_MS);
        check(dut.rom.fallos() > f0 && buzon(B_ESTADO) == 0 && buzon(B_ARRANQUES) == 1,
              "e4_rom: el primer ROM_ es un error de bus, y no pasa de ahi");
        check(mcu.opcion("rom", "r1").empty() && dut.rom.origen() == RomE4::Origen::R1,
              "--rom r1 la devuelve");
    }

    void bootcfg() {
        grupo("E29 BOOTCFG: el cargador por un pin (PE3 a nivel bajo)");
        arranca(0, FW_DEMO);
        check(espera_estado(100, 5), "e4_demo arranca de la Flash");
        // BOOTCFG: EN, PORT = E (4), PIN = 3, POL = 0, KEY; y COMT para fijarlo
        const uint32_t v = 0x7FFF0000u | (4u << 13) | (3u << 10) | (1u << 8) | (1u << 4) | 0xEu;
        tm.write32(0x400FE1D0u, v);
        tm.write32(0x400FD000u, 0x75100000u);                     // FMA
        tm.write32(0x400FD008u, (0xA442u << 16) | (1u << 3));    // FMC: WRKEY | COMT
        wait(1, SC_MS);
        check_eq(dut.flashc.bootcfg(), v, "BOOTCFG fijado con COMT (NW = 0)");
        v_ain0->set(false);
        abre_bcl();
        mcu.reset_pin(true);
        wait(20, SC_US);
        mcu.reset_pin(false);
        wait(1, SC_MS);
        check(dut.arranco_en_rom(), "con PE3 a 0 y la Flash buena, arranca en la ROM");
        bcl->envia(std::string("\x55\x55", 2));
        check_eq(respuesta(), 0xCC, "y el cargador contesta");
        bcl->set_enabled(false);
        v_ain0->set(true);
        uint8_t ceros[B_TAM] = {};
        mcu.reset_pin(true);
        wait(20, SC_US);
        dut.sram.load(ceros, sizeof ceros, 0);
        mcu.reset_pin(false);
        check(espera_estado(100, 5) && !dut.arranco_en_rom(),
              "con PE3 a 1, arranca la aplicacion");
        v_ain0->release();
    }

    void run() {
        // TESTE4_SOLO=t3: solo los grupos de la entrega 3, para trabajar en ellos.
        // No vale para el invariante (la cifra es otra).
        if (const char* solo = std::getenv("TESTE4_SOLO")) {
            std::printf("  [TESTE4_SOLO=%s] el tiempo NO es el del invariante\n", solo);
            mcu.alimenta(true);
            const std::string q = solo;
            if (q == "t3" || q == "uart") t3_uart();
            if (q == "t3" || q == "timers") t3_timers();
            if (q == "t3" || q == "i2c") t3_i2c_spi();
            if (q == "t3" || q == "adc") t3_adc();
            if (q == "t3" || q == "udma") t3_udma();
            if (q == "t3" || q == "hib") t3_hib();
            if (q == "rom") { rom_r1(); cargador(); rom_map(); sin_rom(); bootcfg(); }
            std::printf("\nTOTAL E4 (solo %s): %u comprobaciones OK, %u fallos\n", solo, g_ok, g_fallos);
            sc_stop();
            return;
        }
        firmwares();
        nombres();
        catalogo();
        integracion();
        parpadeo();
        a120();
        a120_mal();
        a120_piosc();
        sin_reloj();
        pulsador();
        perro();
        sysresetreq();
        gpio_tiva();
        coremark();
        flash_firmware();
        eeprom_firmware();
        gdb();
        t3_uart();
        t3_timers();
        t3_i2c_spi();
        t3_adc();
        t3_udma();
        t3_hib();
        rom_r1();
        cargador();
        rom_map();
        sin_rom();
        bootcfg();
        std::printf("\nTOTAL E4: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING,
        std::getenv("TESTE4_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    TbE4 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    if (g_t_gdb != SC_ZERO_TIME)
        std::printf("  de los cuales E (socket de GDB)      : %s\n"
                    "  el resto                             : %s\n",
                    g_t_gdb.to_string().c_str(),
                    (sc_time_stamp() - g_t_gdb).to_string().c_str());
    return g_fallos ? 1 : 0;
}
