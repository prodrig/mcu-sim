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
#include <chrono>
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
#include "../parts/puente_usb.h"
#include "../verif/gdb_stub.h"
#include "../verif/lee_vec.h"
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
static const char* const FW_T7   = "verif/fw/msp432e4/e4_t7/e4_t7.out";
static const char* const FW_ETH  = "verif/fw/msp432e4/e4_eth/e4_eth.out";
static const char* const FW_BOOTP = "verif/fw/msp432e4/e4_bootp/e4_bootp.out";
static const char* const FW_USB  = "verif/fw/msp432e4/e4_usb/e4_usb.out";
static const char* const BIN_ETH = "verif/fw/msp432e4/e4_eth/e4_eth.bin";

// El otro extremo del cable del PHY interno, en el banco: lo que manda el MCU
// se encola, y lo que se le manda entra por EphyE4::de_cable
struct CableBanco : public eth_cable_if {
    std::deque<std::vector<uint8_t>> rx;
    sc_core::sc_event llega;
    void trama_del_cable(const std::vector<uint8_t>& t) override { rx.push_back(t); llega.notify(SC_ZERO_TIME); }
};
// Las sumas de una trama IPv4 que manda el MCU, comprobadas aqui
static uint32_t suma16(const uint8_t* d, size_t n, uint32_t s = 0) {
    for (size_t i = 0; i + 1 < n; i += 2) s += (uint32_t(d[i]) << 8) | d[i + 1];
    if (n & 1) s += uint32_t(d[n - 1]) << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return s;
}
static bool sumas_bien(const std::vector<uint8_t>& t) {
    if (t.size() < 34) return false;
    const size_t ihl = size_t(t[14] & 0xF) * 4, total = (size_t(t[16]) << 8) | t[17];
    if (14 + total > t.size()) return false;
    if (suma16(&t[14], ihl) != 0xFFFF) return false;
    const uint8_t proto = t[23];
    const uint8_t* c = &t[14 + ihl];
    const size_t n = total - ihl;
    if (proto == 1) return suma16(c, n) == 0xFFFF;
    uint32_t ps = suma16(&t[26], 8);
    ps += proto; ps += uint32_t(n);
    return suma16(c, n, ps) == 0xFFFF;
}

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
    // T7, entrega 2: un bus CAN con su terminador, un transceptor en CAN0
    // (PA1/PA0) y otro en CAN1 (PB1/PB0), y un nodo externo que escucha
    CanWire* bus_can = nullptr;
    CanTransceiver *xcvr0 = nullptr, *xcvr1 = nullptr;
    CanNode* espia = nullptr;
    // E32: OTRO MSP432E401Y en la misma placa, con su cristal y su
    // transceptor en su CAN0 (PA1/PA0). Sin alimentar hasta su grupo
    McuE4 mcu2;
    Msp432E4& dut2;
    Crystal* y2 = nullptr;
    CanTransceiver* xcvr_b = nullptr;
    CableBanco cable;
    // T7, entrega 5: un PC en el USB0 (D+ PL6, D- PL7). VBUS e ID van a sus
    // propios nodos: en la LaunchPad son PB1 y PB0, que aqui son del CAN1
    AnalogNet usb_vbus{"usb_vbus"}, usb_id{"usb_id"};
    UsbHostRig* pc_usb = nullptr;
    PuenteUsbCdc* puente_usb = nullptr;   // E36: se monta en su grupo, como en mcu-sim

    SC_CTOR(TbE4)
        : mcu("dut", DBG_PINES, Cableado(), *CatalogoMcu::busca("MSP432E401Y")),
          dut(mcu.chip()),
          mcu2("dut2", DBG_PINES, Cableado(), *CatalogoMcu::busca("MSP432E401Y")),
          dut2(mcu2.chip()) {
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
        bus_can = new CanWire(3.3, 60.0, "bus_can");
        xcvr0 = new CanTransceiver("xcvr0", dut.gpio.nodo_de(e4::PA, 1), dut.gpio.nodo_de(e4::PA, 0), *bus_can);
        xcvr1 = new CanTransceiver("xcvr1", dut.gpio.nodo_de(e4::PB, 1), dut.gpio.nodo_de(e4::PB, 0), *bus_can);
        espia = new CanNode("espia", *bus_can, 500e3);
        espia->set_enabled(false);
        y2 = new Crystal(*dut2.osc0);
        xcvr_b = new CanTransceiver("xcvr_b", dut2.gpio.nodo_de(e4::PA, 1), dut2.gpio.nodo_de(e4::PA, 0), *bus_can);
        pc_usb = new UsbHostRig("pc_usb", dut.gpio.nodo_de(e4::PL, 7), dut.gpio.nodo_de(e4::PL, 6), usb_vbus, usb_id);
        pc_usb->conectar_dispositivo(&dut.usb);
        SC_THREAD(run);
    }
    ~TbE4() override {
        delete y1; delete y3; delete sw1; delete sw2; delete gdb_p; delete gdb_d;
        delete bcl; delete reloj_cap; delete reloj_cnt; delete eeprom; delete pu_scl;
        delete pu_sda; delete pista_ssi; delete v_ain0; delete v_ain1;
        delete xcvr0; delete xcvr1; delete espia; delete y2; delete xcvr_b; delete bus_can;
        delete pc_usb;
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
    // Dos cifras hexadecimales; si no lo son -una respuesta que no es la que
    // se esperaba, un "OK" o un "E01"-, 0xFF y no una excepcion que tumbe la
    // suite: la comprobacion que lo use fallara, y dira cual
    static unsigned hex2(const std::string& s, size_t i) {
        auto d = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int a = d(s[i]), b = d(s[i + 1]);
        return a < 0 || b < 0 ? 0xFFu : unsigned(a * 16 + b);
    }
    static uint32_t de_hex_le(const std::string& s) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4 && 2 * i + 1 < s.size(); ++i)
            v |= uint32_t(hex2(s, 2 * i)) << (8 * i);
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
        // `vCont;c` no tiene respuesta: el núcleo arranca cuando el stub lee
        // el paquete del socket, y eso pasa en tiempo de PARED. Un milisegundo
        // simulado fijo bastaba en Linux y no en los runners de macOS, donde
        // el paquete llegaba después. Se espera a que el núcleo deje de estar
        // parado, con un plazo de reloj (5 s), y entonces se mira el LED.
        {
            const auto t_lim = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (dut.core.debug.is_halted() && std::chrono::steady_clock::now() < t_lim)
                wait(100, SC_US);
        }
        wait(1, SC_MS);
        const bool parado = dut.core.debug.is_halted();
        const uint32_t estado = buzon(B_ESTADO);
        const double t_led = periodo(e4::PN, 0);
        r.corre = !parado && estado == 100 && t_led > 0;
        if (!r.corre)
            std::printf("    al soltarlo: nucleo %s, estado %u, periodo de PN0 %.3g s\n",
                        parado ? "PARADO" : "en marcha", unsigned(estado), t_led);
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


    // =======================================================================
    // T7, entrega 1: el CCM con e4_t7, un servidor de vectores
    // [verif/fw/msp432e4/e4_t7/main.c, buzon_t]
    enum : uint32_t {
        C_ORDEN = 0x10, C_HECHO = 0x14, C_OP = 0x18, C_CFG = 0x1C, C_NCLAVE = 0x20,
        C_NENT = 0x24, C_NAAD = 0x28, C_RES = 0x2C, C_R0 = 0x30, C_CLAVE = 0x40, C_IV = 0x140,
        C_TAG = 0x150, C_IVSAL = 0x160, C_AAD = 0x1B0, C_ENT = 0x230, C_SAL = 0x630, C_FIN = 0xA30
    };
    uint32_t orden_ = 0;
    // Un caso: los datos al buzón, la orden, y esperar a que la haga
    bool ccm_op(uint32_t op, uint32_t cfg, const std::vector<uint8_t>& clave,
                const std::vector<uint8_t>& iv, const std::vector<uint8_t>& ent,
                const std::vector<uint8_t>& aad = {}) {
        std::vector<uint8_t> ceros(C_FIN - C_CLAVE, 0);
        dut.sram.load(ceros.data(), ceros.size(), C_CLAVE);
        if (!clave.empty()) dut.sram.load(clave.data(), clave.size(), C_CLAVE);
        if (!iv.empty()) dut.sram.load(iv.data(), iv.size(), C_IV);
        if (!aad.empty()) dut.sram.load(aad.data(), aad.size(), C_AAD);
        if (!ent.empty()) dut.sram.load(ent.data(), ent.size(), C_ENT);
        dut.sram.poke32(C_OP, op); dut.sram.poke32(C_CFG, cfg);
        dut.sram.poke32(C_NCLAVE, uint32_t(clave.size()));
        dut.sram.poke32(C_NENT, uint32_t(ent.size()));
        dut.sram.poke32(C_NAAD, uint32_t(aad.size()));
        dut.sram.poke32(C_ORDEN, ++orden_);
        const sc_time lim = sc_time_stamp() + sc_time(20, SC_MS);
        while (sc_time_stamp() < lim) {
            wait(5, SC_US);
            if (buzon(C_HECHO) == orden_) return true;
        }
        return false;
    }
    std::vector<uint8_t> sram_bytes(uint32_t off, size_t n) {
        std::vector<uint8_t> v(n);
        for (size_t i = 0; i < n; ++i) v[i] = uint8_t(dut.sram.peek32(uint32_t(off + (i & ~size_t(3)))) >> (8 * (i & 3)));
        return v;
    }
    static uint32_t cfg_sha(const std::string& a) {
        const std::string b = a.rfind("hmac-", 0) == 0 ? a.substr(5) : a;
        const uint32_t algo = b == "md5" ? 0u : b == "sha1" ? 2u : b == "sha224" ? 4u : 6u;
        return a == b ? (algo | 0x18u) : algo;
    }
    static unsigned tam_sha(const std::string& a) {
        return a.find("md5") != std::string::npos ? 16 : a.find("sha1") != std::string::npos ? 20
             : a.find("sha224") != std::string::npos ? 28 : 32;
    }

    void ccm_hash(unsigned& ok, unsigned& total, unsigned& ok_pp, unsigned& tot_pp,
                  unsigned& ok_dos, unsigned& tot_dos) {
        for (const char* f : {"verif/vectores/hash.vec", "verif/vectores/sha2.vec"}) {
            for (const vec::Caso& c : vec::lee(f)) {
                if (vec::campo(c, "lento") == "si") continue;
                const std::string a = vec::campo(c, "algoritmo"), id = vec::campo(c, "id");
                std::vector<uint8_t> m;
                const auto u = vec::hex(vec::campo(c, "mensaje"));
                const std::string rep = vec::campo(c, "repite");
                for (unsigned r = 0; r < (rep.empty() ? 1u : unsigned(std::stoul(rep))); ++r)
                    m.insert(m.end(), u.begin(), u.end());
                const std::string esp = vec::campo(c, "salida");
                const unsigned n = tam_sha(a);
                const bool hm = a.rfind("hmac-", 0) == 0;
                const auto k = vec::hex(vec::campo(c, "clave"));
                auto prueba = [&](uint32_t op, unsigned& ok_, unsigned& tot_, const char* que) {
                    ++tot_;
                    const bool hecho = ccm_op(op, cfg_sha(a), k, {}, m);
                    const std::string got = vec::a_hex(sram_bytes(C_SAL, n).data(), n);
                    if (hecho && got == esp) { ++ok_; return; }
                    std::printf("    FALLO %s (%s): %s\n      esperado %s\n", id.c_str(), que,
                                hecho ? got.c_str() : "(no acaba)", esp.c_str());
                };
                if (!hm) {
                    prueba(1, ok, total, "SHAMD5DataProcess");
                    if (m.size() > 64) prueba(4, ok_dos, tot_dos, "en dos trozos");
                } else {
                    prueba(2, ok, total, "SHAMD5HMACKeySet");
                    prueba(3, ok_pp, tot_pp, "SHAMD5HMACPPKeySet");
                }
            }
        }
    }

    void ccm_cifra(unsigned& ok, unsigned& total, unsigned& ok_gcm, unsigned& tot_gcm, unsigned& iv_ok) {
        for (const char* f : {"verif/vectores/cryp.vec", "verif/vectores/cryp2.vec"}) {
            for (const vec::Caso& c : vec::lee(f)) {
                const std::string a = vec::campo(c, "algoritmo"), id = vec::campo(c, "id");
                const auto k = vec::hex(vec::campo(c, "clave"));
                auto iv = vec::hex(vec::campo(c, "iv"));
                const auto ent = vec::hex(vec::campo(c, "entrada")), sal = vec::hex(vec::campo(c, "salida"));
                const auto aad = vec::hex(vec::campo(c, "aad"));
                const bool aes = a.rfind("aes", 0) == 0, gcm = a == "aes-gcm";
                uint32_t op = 7, modo = 0;
                if (aes) {
                    op = gcm ? 6 : 5;
                    modo = (k.size() == 16 ? 0x8u : k.size() == 24 ? 0x10u : 0x18u);
                    if (a == "aes-cbc") modo |= 0x20u;
                    else if (a == "aes-ctr") modo |= 0x40u | 0x180u;
                    else if (a == "aes-cfb") modo |= 0x400u;
                    else if (gcm) { modo |= 0x20030040u; iv.insert(iv.end(), {0, 0, 0, 1}); }
                } else {
                    if (a.rfind("tdes", 0) == 0) modo |= 0x8u;
                    if (a.find("cbc") != std::string::npos) modo |= 0x10u;
                    else if (a.find("cfb") != std::string::npos) modo |= 0x20u;
                }
                const std::string tag = vec::campo(c, "tag");
                for (int dir = 1; dir >= 0; --dir) {
                    const auto& in = dir ? ent : sal;
                    const auto& out = dir ? sal : ent;
                    unsigned& o = gcm ? ok_gcm : ok;
                    ++(gcm ? tot_gcm : total);
                    const bool hecho = ccm_op(op, modo | (dir ? 0x4u : 0u), k, iv, in, aad);
                    const auto got = sram_bytes(C_SAL, out.size());
                    bool bien = hecho && got == out;
                    if (gcm) bien = bien && vec::a_hex(sram_bytes(C_TAG, 16).data(), 16) == tag;
                    if (bien) ++o;
                    else std::printf("    FALLO %s (%s): %s\n", id.c_str(), dir ? "cifrar" : "descifrar",
                                     hecho ? vec::a_hex(got.data(), got.size()).c_str() : "(no acaba)");
                    // En CBC, el IV que queda es el último bloque cifrado
                    if (hecho && dir && a == "aes-cbc" &&
                        sram_bytes(C_IVSAL, 16) == std::vector<uint8_t>(sal.end() - 16, sal.end()))
                        ++iv_ok;
                }
            }
        }
    }

    void ccm_crc(unsigned& ok, unsigned& total) {
        for (const vec::Caso& c : vec::lee("verif/vectores/crc.vec")) {
            const std::string a = vec::campo(c, "algoritmo"), id = vec::campo(c, "id");
            uint32_t cfg = 0x30u;                         // ENDIAN: B0 primero, el orden de la memoria
            if (a == "crc32") cfg |= 0x2u | 0x6000u | 0x80u | 0x100u | 0x200u;
            else if (a == "crc32c") cfg |= 0x3u | 0x6000u | 0x80u | 0x100u | 0x200u;
            else if (a == "crc16-ccitt") cfg |= 0x1u | 0x6000u;
            else if (a == "crc16-xmodem") cfg |= 0x1u | 0x4000u;
            else if (a == "crc16-arc") cfg |= 0x0u | 0x4000u | 0x80u | 0x100u;
            else if (a == "crc16-umts") cfg |= 0x0u | 0x4000u;
            else if (a == "inet") cfg |= 0x8u | 0x4000u | 0x200u;
            ++total;
            const bool hecho = ccm_op(8, cfg, {}, {}, vec::hex(vec::campo(c, "mensaje")));
            const std::string esp = vec::campo(c, "salida");
            char got[12];
            std::snprintf(got, sizeof got, "%0*x", int(esp.size()), buzon(C_RES));
            if (hecho && esp == got) ++ok;
            else std::printf("    FALLO %s: %s, esperado %s\n", id.c_str(), got, esp.c_str());
        }
    }

    void t7_ccm() {
        grupo("E30 El CCM con DriverLib (e4_t7): todos los vectores de verif/vectores por el chip");
        arranca(0, FW_T7);
        check(espera_estado(100, 20), "SysCtlPeripheralEnable(SYSCTL_PERIPH_CCM0) y PRCCM");
        const unsigned av0 = dut.ccm.avisos();
        unsigned ok = 0, tot = 0, ok_pp = 0, tot_pp = 0, ok_dos = 0, tot_dos = 0;
        ccm_hash(ok, tot, ok_pp, tot_pp, ok_dos, tot_dos);
        std::printf("    resumenes y HMAC: %u/%u; HMAC con la clave procesada: %u/%u; "
                    "en dos trozos: %u/%u\n", ok, tot, ok_pp, tot_pp, ok_dos, tot_dos);
        check(tot >= 40 && ok == tot, "MD5, SHA-1, SHA-224 y SHA-256 y sus HMAC (hash.vec y sha2.vec), "
                                     "con SHAMD5DataProcess y SHAMD5HMACProcess");
        check(tot_pp >= 20 && ok_pp == tot_pp,
              "HMAC con SHAMD5HMACPPKeyGenerate y SHAMD5HMACPPKeySet: ODIGEST e IDIGEST de ida y vuelta");
        check(tot_dos >= 4 && ok_dos == tot_dos,
              "un resumen en dos trozos, con el contexto (IDIGEST y DIGEST_COUNT) guardado y repuesto");
        unsigned c_ok = 0, c_tot = 0, g_ok = 0, g_tot = 0, iv_ok = 0;
        ccm_cifra(c_ok, c_tot, g_ok, g_tot, iv_ok);
        std::printf("    AES/DES: %u/%u; GCM: %u/%u\n", c_ok, c_tot, g_ok, g_tot);
        check(c_tot >= 40 && c_ok == c_tot,
              "AES ECB/CBC/CTR/CFB de 128, 192 y 256 bits, DES y TDES ECB/CBC/CFB, en las dos "
              "direcciones (cryp.vec y cryp2.vec)");
        check(g_tot >= 14 && g_ok == g_tot,
              "AES-GCM con AESDataProcessAuth: texto y etiqueta, con AAD y bloques a medias");
        check_eq(iv_ok, 3, "AESIVRead tras CBC devuelve el ultimo bloque cifrado (para encadenar)");
        unsigned r_ok = 0, r_tot = 0;
        ccm_crc(r_ok, r_tot);
        std::printf("    CRC: %u/%u\n", r_ok, r_tot);
        check(r_tot >= 13 && r_ok == r_tot,
              "CRC-32, CRC-32C, cuatro CRC-16 y la suma de Internet (crc.vec), por palabras y bytes");
        check_eq(dut.ccm.avisos() - av0, 0, "ni un aviso del CCM: todo lo que se usa esta modelado");
        // Sin RCGCCCM, el CCM da error de bus, como cualquier periferico
        mcu.reset_pin(true);
        wait(20, SC_US);
        uint32_t v = 0;
        check(tm.read32(0x44036050u, v) != tlm::TLM_OK_RESPONSE,
              "con RCGCCCM a 0 (tras el reset), AES_CTRL da error de bus");
        mcu.reset_pin(false);
    }

    // =======================================================================
    // T7, entrega 2: los dos C_CAN en el mismo bus (e4_t7, modo 1)
    uint32_t sal(unsigned i) { return buzon(C_SAL + 4 * i); }
    // e4_t7 con su buzon entero a cero: los resultados de un grupo no se
    // mezclan con los del anterior
    void arranca_t7(uint32_t modo) {
        arranca(modo, FW_T7);
        std::vector<uint8_t> ceros(C_FIN - B_TAM, 0);
        dut.sram.load(ceros.data(), ceros.size(), B_TAM);
    }
    void t7_can() {
        grupo("E31 CAN0 y CAN1 en el mismo hilo, con DriverLib: filtros, FIFO, extendido y remota (e4_t7, modo 1)");
        xcvr0->set_enabled(true); xcvr1->set_enabled(true);
        espia->flush(); espia->set_enabled(true);
        const uint64_t tx1 = dut.can1.frames_tx();
        arranca_t7(1);
        const bool fin = espera_estado(100, 40);
        std::printf("    estado %u, %u marcos guardados; el espia ha visto %u\n", buzon(B_ESTADO), sal(7),
                    espia->received());
        check(fin, "las cinco pruebas acaban (estado 100)");
        check(sal(0) == 500000 && sal(1) == 500000, "CANBitRateSet da 500 kbit/s con 120 MHz en los dos");
        std::printf("    tiempo de bit: %.3f us\n", dut.can0.bit_time() * 1e6);
        check(std::fabs(dut.can0.bit_time() - 2e-6) < 1e-12 && std::fabs(dut.can1.bit_time() - 2e-6) < 1e-12,
              "y el tiempo de bit es 2 us: CANBIT y CANBRPE de verdad");
        auto marco = [&](unsigned k, uint32_t id, unsigned obj, unsigned len, uint32_t d0, uint32_t d1) {
            const uint32_t f = sal(9 + 4 * k);
            return sal(8 + 4 * k) == id && (f >> 24) == obj && ((f >> 16) & 0xFFu) == len &&
                   sal(10 + 4 * k) == d0 && sal(11 + 4 * k) == d1;
        };
        check(sal(7) == 6, "seis marcos guardados en objetos");
        check(marco(0, 0x123, 1, 8, 0x44332211u, 0x88776655u), "0x123 llega al objeto 1 de CAN1 con sus ocho bytes");
        check(marco(1, 0x201, 2, 1, 0xA1, 0) && marco(2, 0x202, 3, 2, 0xB2B1, 0) && marco(3, 0x203, 4, 3, 0xC3C2C1, 0),
              "0x201, 0x202 y 0x203 llenan la FIFO 2-3-4 en orden (mascara 0x7F0, EOB en el 4)");
        check(marco(4, 0x1ABCDE05, 5, 4, 0xEFBEADDEu, 0) && (sal(9 + 16) & 0x4u),
              "0x1ABCDE05, extendido, al objeto 5 por su filtro de 29 bits");
        check(marco(5, 0x300, 10, 8, 0x4F4D4552u, 0x21214154u),
              "una remota de 0x300: CAN1 contesta solo (RMTEN) y el dato vuelve al objeto 10 de CAN0");
        check_eq(dut.can1.frames_tx() - tx1, 1, "CAN1 transmite una vez: la respuesta a la remota");
        check_eq(sal(2), 5, "CAN0: una interrupcion por cada objeto de transmision con TXIE");
        check(sal(4) >= 6 && (sal(5) & 0x10u), "CAN1: interrupciones de estado con RXOK (CANStatusGet las borra)");
        check_eq(buzon(0x30), 0, "0x7FF no casa con ningun objeto: ningun NEWDAT en CAN1");
        check_eq(espia->received(), 8, "el nodo externo ve los ocho marcos del hilo, la remota y su respuesta incluidas");
        check_eq(sal(6), 0, "CANERR de CAN0 a cero: ningun error en el hilo");
        check_eq(dut.can0.avisos() + dut.can1.avisos(), 0, "sin avisos de los C_CAN");
        xcvr0->set_enabled(false); xcvr1->set_enabled(false); espia->set_enabled(false);
    }

    // E32: dos chips en la misma placa, cada uno con su firmware y su CAN0
    void t7_dos_e4() {
        grupo("E32 Dos MSP432E401Y en la misma placa, hablando por CAN (e4_t7, modos 2 y 3)");
        xcvr0->set_enabled(true); xcvr_b->set_enabled(true);
        espia->flush(); espia->set_enabled(true);
        mcu2.alimenta(true);
        mcu2.reset_pin(true);
        wait(20, SC_US);
        check(mcu2.carga_firmware(FW_T7), "el segundo chip carga e4_t7 en su Flash");
        uint8_t ceros[B_TAM] = {};
        dut2.sram.load(ceros, sizeof ceros, 0);
        dut2.sram.poke32(B_MODO, 3);
        mcu2.reset_pin(false);
        wait(2, SC_MS);
        arranca_t7(2);
        const bool fin = espera_estado(100, 40);
        std::printf("    nodo A: %u de 5 respuestas buenas; nodo B: %u marcos devueltos; el espia ve %u\n",
                    buzon(0x30), dut2.sram.peek32(0x30), espia->received());
        check(fin, "el nodo A acaba sus cinco idas y vueltas");
        check_eq(buzon(0x30), 5, "las cinco respuestas llegan con el primer byte mas uno");
        check_eq(dut2.sram.peek32(0x30), 5, "el nodo B ha devuelto cinco marcos");
        check_eq(espia->received(), 10, "en el hilo, diez marcos: cinco de cada chip");
        check(sal(8) == 0x180 && (sal(9) >> 24) == 2, "el nodo A los recibe en su objeto 2, con identificador 0x180");
        check(dut.can0.tec() == 0 && dut2.can0.tec() == 0, "sin errores de transmision en ninguno");
        mcu2.alimenta(false);
        xcvr0->set_enabled(false); xcvr_b->set_enabled(false); espia->set_enabled(false);
    }

    // =======================================================================
    // T7, entrega 3: el EMAC con su PHY interno y lwIP (e4_eth)
    std::vector<uint8_t> eth(const std::vector<uint8_t>& dst, uint16_t tipo, const std::vector<uint8_t>& c) {
        static const uint8_t YO[6] = {0x02, 0, 0, 0, 0, 0xFE};
        std::vector<uint8_t> t(dst);
        t.insert(t.end(), YO, YO + 6);
        t.push_back(uint8_t(tipo >> 8)); t.push_back(uint8_t(tipo));
        t.insert(t.end(), c.begin(), c.end());
        return t;
    }
    std::vector<uint8_t> ipv4(const std::vector<uint8_t>& dst, uint8_t proto, std::vector<uint8_t> c) {
        std::vector<uint8_t> h = {0x45, 0, uint8_t((20 + c.size()) >> 8), uint8_t(20 + c.size()), 0x12, 0x34,
                                  0x40, 0, 64, proto, 0, 0, 192, 168, 127, 1, 192, 168, 127, 2};
        const uint16_t k = uint16_t(~suma16(h.data(), 20));
        h[10] = uint8_t(k >> 8); h[11] = uint8_t(k);
        h.insert(h.end(), c.begin(), c.end());
        return eth(dst, 0x0800, h);
    }
    // Pone la suma de UDP o TCP (pseudo-cabecera de 192.168.127.1 a .2)
    static void suma_l4(std::vector<uint8_t>& c, uint8_t proto, size_t pos) {
        static const uint8_t P[8] = {192, 168, 127, 1, 192, 168, 127, 2};
        uint32_t ps = suma16(P, 8) + proto + uint32_t(c.size());
        c[pos] = c[pos + 1] = 0;
        const uint16_t k = uint16_t(~suma16(c.data(), c.size(), ps));
        c[pos] = uint8_t(k >> 8); c[pos + 1] = uint8_t(k);
    }
    std::vector<uint8_t> tcp_seg(const std::vector<uint8_t>& dst, uint32_t seq, uint32_t ack, uint8_t fl,
                                 const std::string& datos = "") {
        std::vector<uint8_t> c = {0x9C, 0x42, 0, 7, uint8_t(seq >> 24), uint8_t(seq >> 16), uint8_t(seq >> 8),
                                  uint8_t(seq), uint8_t(ack >> 24), uint8_t(ack >> 16), uint8_t(ack >> 8),
                                  uint8_t(ack), 0x50, fl, 0x20, 0x00, 0, 0, 0, 0};
        c.insert(c.end(), datos.begin(), datos.end());
        suma_l4(c, 6, 16);
        return ipv4(dst, 6, c);
    }
    // Espera una trama IPv4 del protocolo dado; contesta sola a los ARP del MCU
    bool espera_eth(std::vector<uint8_t>& t, int proto, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (sc_time_stamp() < lim) {
            if (cable.rx.empty()) { wait(lim - sc_time_stamp(), cable.llega); continue; }
            t = cable.rx.front(); cable.rx.pop_front();
            if (t.size() >= 42 && t[12] == 8 && t[13] == 6 && t[21] == 1) {      // ARP que pregunta
                std::vector<uint8_t> r = {0, 1, 8, 0, 6, 4, 0, 2, 0x02, 0, 0, 0, 0, 0xFE, 192, 168, 127, 1};
                r.insert(r.end(), t.begin() + 22, t.begin() + 32);
                dut.emac.phy.de_cable(eth(std::vector<uint8_t>(t.begin() + 6, t.begin() + 12), 0x0806, r));
                continue;
            }
            if (proto < 0) return true;
            if (t.size() >= 34 && t[12] == 8 && t[13] == 0 && t[23] == proto) return true;
        }
        return false;
    }
    void t7_eth() {
        grupo("E33 El EMAC con su PHY interno: lwIP, ARP, ping y eco UDP y TCP (e4_eth)");
        cable.rx.clear();
        dut.emac.phy.conecta_cable(&cable);
        arranca(0, FW_ETH);
        check(espera_estado(100, 400), "lwIP arriba y el enlace del PHY interno, subido");
        check_eq(buzon(0x28), 0x2000A221u, "EMACPHYRead por el MDIO interno: EPHY_ID1/ID2");
        check(dut.emac.phy.cien() && dut.emac.phy.duplex(), "autonegociado a 100 Mbit/s en duplex");
        // ARP
        const std::vector<uint8_t> DIF(6, 0xFF);
        std::vector<uint8_t> arp = {0, 1, 8, 0, 6, 4, 0, 1, 0x02, 0, 0, 0, 0, 0xFE, 192, 168, 127, 1,
                                    0, 0, 0, 0, 0, 0, 192, 168, 127, 2};
        dut.emac.phy.de_cable(eth(DIF, 0x0806, arp));
        std::vector<uint8_t> t;
        bool hay = false;
        const sc_time lim = sc_time_stamp() + sc_time(20, SC_MS);
        while (!hay && sc_time_stamp() < lim) {
            if (!espera_eth(t, -1, 20)) break;
            hay = t.size() >= 42 && t[12] == 8 && t[13] == 6 && t[21] == 2;
        }
        check(hay, "ARP: 192.168.127.2 contesta");
        const std::vector<uint8_t> mac = hay ? std::vector<uint8_t>(t.begin() + 22, t.begin() + 28)
                                             : std::vector<uint8_t>(6, 0);
        check(mac == std::vector<uint8_t>({0x02, 0, 0, 0xE4, 0, 1}), "con su MAC, 02:00:00:e4:00:01");
        // Ping
        std::vector<uint8_t> ic = {8, 0, 0, 0, 0x45, 0x34, 0, 1};
        for (unsigned i = 0; i < 32; ++i) ic.push_back(uint8_t(i));
        const uint16_t k = uint16_t(~suma16(ic.data(), ic.size()));
        ic[2] = uint8_t(k >> 8); ic[3] = uint8_t(k);
        dut.emac.phy.de_cable(ipv4(mac, 1, ic));
        const bool ping = espera_eth(t, 1, 20) && t[34] == 0 && std::equal(ic.begin() + 8, ic.end(), t.begin() + 42);
        check(ping, "ping: vuelve con sus 32 bytes");
        check(ping && sumas_bien(t), "y lwIP deja las sumas a cero: las de IP e ICMP las pone el EMAC (COE)");
        // UDP: diez seguidos, mas que descriptores de recepcion tiene el firmware
        unsigned udp_ok = 0, udp_sumas = 0;
        for (unsigned i = 0; i < 10; ++i) {
            std::string d = "eco UDP numero " + std::to_string(i);
            std::vector<uint8_t> u = {0x9C, 0x41, 0, 7, 0, uint8_t(8 + d.size()), 0, 0};
            u.insert(u.end(), d.begin(), d.end());
            suma_l4(u, 17, 6);
            dut.emac.phy.de_cable(ipv4(mac, 17, u));
            if (espera_eth(t, 17, 20) && std::string(t.begin() + 42, t.begin() + 42 + long(d.size())) == d) {
                ++udp_ok;
                if (sumas_bien(t)) ++udp_sumas;
            }
        }
        check_eq(udp_ok, 10, "UDP 7: diez datagramas, diez ecos (el anillo de 8 descriptores da la vuelta)");
        check_eq(udp_sumas, 10, "con la suma de UDP del EMAC");
        // TCP
        dut.emac.phy.de_cable(tcp_seg(mac, 1000, 0, 0x02));
        const bool sa = espera_eth(t, 6, 20) && (t[47] & 0x12) == 0x12;
        check(sa, "TCP 7: SYN, y vuelve SYN+ACK");
        check(sa && sumas_bien(t), "con la suma de TCP del EMAC");
        uint32_t su = sa ? ((uint32_t(t[38]) << 24) | (uint32_t(t[39]) << 16) | (uint32_t(t[40]) << 8) | t[41]) + 1 : 0;
        const std::string msg = "Hola, eco TCP por el PHY interno del MSP432E401Y\r\n";
        dut.emac.phy.de_cable(tcp_seg(mac, 1001, su, 0x10));
        dut.emac.phy.de_cable(tcp_seg(mac, 1001, su, 0x18, msg));
        std::string eco;
        const sc_time lim2 = sc_time_stamp() + sc_time(30, SC_MS);
        while (eco.size() < msg.size() && sc_time_stamp() < lim2) {
            if (!espera_eth(t, 6, 30)) break;
            const size_t doff = size_t(t[46] >> 4) * 4, total = (size_t(t[16]) << 8) | t[17];
            if (14 + total > 34 + doff) {
                eco.append(t.begin() + long(34 + doff), t.begin() + long(14 + total));
                su += uint32_t(total - 20 - doff);
                dut.emac.phy.de_cable(tcp_seg(mac, 1001 + uint32_t(msg.size()), su, 0x10));
            }
        }
        check(eco == msg, "los datos vuelven por el eco (RFC 862)");
        dut.emac.phy.de_cable(tcp_seg(mac, 1001 + uint32_t(msg.size()), su, 0x11));
        bool fin = false;
        const sc_time lim3 = sc_time_stamp() + sc_time(30, SC_MS);
        while (!fin && sc_time_stamp() < lim3 && espera_eth(t, 6, 30)) fin = (t[47] & 0x01) != 0;
        check(fin, "y el cierre: el MCU manda su FIN");
        // Una trama a otra MAC: el filtro del EMAC no la deja pasar
        const unsigned desc0 = dut.emac.descartadas();
        dut.emac.phy.de_cable(ipv4({0x02, 0, 0, 0xE4, 0, 2}, 1, ic));
        check(!espera_eth(t, 1, 5) && dut.emac.descartadas() == desc0 + 1,
              "un ping a otra MAC no llega a lwIP: lo tira el filtro de direcciones");
        check_eq(buzon(0x24), 0, "ningun LWIP_ASSERT");
        std::printf("    tramas: %u del MCU, %u al MCU; conexiones TCP %u, bytes de eco %u, UDP %u\n",
                    dut.emac.tramas_tx(), dut.emac.tramas_rx(), buzon(0x10), buzon(0x14), buzon(0x18));
        dut.emac.phy.conecta_cable(nullptr);
    }

    // =======================================================================
    // E34: el cargador por Ethernet de la ROM R1 (ROM_UpdateEMAC): BOOTP y
    // TFTP contra un servidor en el banco, que sirve e4_eth.bin
    std::vector<uint8_t> udp_a(const std::vector<uint8_t>& mac, const uint8_t ip[4], uint16_t sp, uint16_t dp,
                               const std::vector<uint8_t>& d) {
        std::vector<uint8_t> u = {uint8_t(sp >> 8), uint8_t(sp), uint8_t(dp >> 8), uint8_t(dp),
                                  uint8_t((8 + d.size()) >> 8), uint8_t(8 + d.size()), 0, 0};
        u.insert(u.end(), d.begin(), d.end());
        std::vector<uint8_t> h = {0x45, 0, uint8_t((20 + u.size()) >> 8), uint8_t(20 + u.size()), 0, 1, 0, 0,
                                  64, 17, 0, 0, 192, 168, 127, 1, ip[0], ip[1], ip[2], ip[3]};
        const uint16_t k = uint16_t(~suma16(h.data(), 20));
        h[10] = uint8_t(k >> 8); h[11] = uint8_t(k);
        // La suma de UDP, con la pseudocabecera
        uint32_t ps = suma16(&h[12], 8) + 17 + uint32_t(u.size());
        const uint16_t ku = uint16_t(~suma16(u.data(), u.size(), ps));
        u[6] = uint8_t(ku >> 8); u[7] = uint8_t(ku);
        h.insert(h.end(), u.begin(), u.end());
        return eth(mac, 0x0800, h);
    }
    void t7_bootp() {
        grupo("E34 El cargador por Ethernet de la ROM R1: ROM_UpdateEMAC, BOOTP y TFTP (e4_bootp)");
        std::vector<uint8_t> img;
        if (FILE* f = std::fopen(BIN_ETH, "rb")) {
            int c; while ((c = std::fgetc(f)) != EOF) img.push_back(uint8_t(c));
            std::fclose(f);
        }
        cable.rx.clear();
        dut.emac.phy.conecta_cable(&cable);
        arranca(0, FW_BOOTP);
        check(espera_estado(50, 20), "la aplicacion llama a ROM_UpdateEMAC(120 MHz)");
        static const uint8_t IP[4] = {192, 168, 127, 2};
        unsigned peticiones = 0, bloques = 0, mal = 0;
        std::vector<uint8_t> mac, t;
        std::string fichero;
        uint16_t puerto = 0;
        bool hecho = false;
        const sc_time lim = sc_time_stamp() + sc_time(2000, SC_MS);
        while (!hecho && sc_time_stamp() < lim) {
            if (!espera_eth(t, 17, double((lim - sc_time_stamp()).to_seconds() * 1e3))) break;
            const uint16_t dp = uint16_t((t[36] << 8) | t[37]), sp = uint16_t((t[34] << 8) | t[35]);
            const uint8_t* d = &t[42];
            if (dp == 67 && d[0] == 1) {                         // BOOTREQUEST
                ++peticiones;
                mac.assign(t.begin() + 6, t.begin() + 12);
                std::vector<uint8_t> r(300, 0);
                r[0] = 2; r[1] = 1; r[2] = 6;
                std::copy(d + 4, d + 8, r.begin() + 4);          // xid
                std::copy(IP, IP + 4, r.begin() + 16);           // yiaddr
                r[20] = 192; r[21] = 168; r[22] = 127; r[23] = 1;   // siaddr
                std::copy(d + 28, d + 34, r.begin() + 28);       // chaddr
                const std::string f = "e4_eth.bin";
                std::copy(f.begin(), f.end(), r.begin() + 108);
                r[236] = 99; r[237] = 130; r[238] = 83; r[239] = 99; r[240] = 255;
                dut.emac.phy.de_cable(udp_a(mac, IP, 67, 68, r));
            } else if (dp == 69 && d[1] == 1) {                  // RRQ
                fichero = std::string(reinterpret_cast<const char*>(d + 2));
                puerto = sp;
                bloques = 0;
                std::vector<uint8_t> b = {0, 3, 0, 1};
                b.insert(b.end(), img.begin(), img.begin() + long(std::min<size_t>(512, img.size())));
                dut.emac.phy.de_cable(udp_a(mac, IP, 40069, puerto, b));
            } else if (dp == 40069 && d[1] == 4) {               // ACK
                const unsigned n = unsigned((d[2] << 8) | d[3]);
                ++bloques;
                if (size_t(n) * 512 > img.size()) { hecho = true; break; }
                const size_t a = size_t(n) * 512, z = std::min(img.size(), a + 512);
                std::vector<uint8_t> b = {0, 3, uint8_t((n + 1) >> 8), uint8_t(n + 1)};
                b.insert(b.end(), img.begin() + long(a), img.begin() + long(z));
                dut.emac.phy.de_cable(udp_a(mac, IP, 40069, puerto, b));
            } else {
                ++mal;
            }
        }
        std::printf("    %u peticiones BOOTP, %u bloques asentidos de %zu bytes, %u paquetes raros\n",
                    peticiones, bloques, img.size(), mal);
        check(peticiones >= 1 && mac == std::vector<uint8_t>({0x02, 0, 0, 0xE4, 0, 0}),
              "BOOTREQUEST por difusion, con la MAC por omision (USER0/USER1 en blanco)");
        check(fichero == "e4_eth.bin", "TFTP: pide por RRQ el fichero que dijo el BOOTP, en modo octet");
        check(hecho && bloques == img.size() / 512 + 1, "y asiente cada bloque de 512, el ultimo corto incluido");
        bool igual = !img.empty();
        for (size_t i = 0; igual && i + 4 <= img.size(); i += 4) {
            const uint32_t w = uint32_t(img[i]) | (uint32_t(img[i + 1]) << 8) | (uint32_t(img[i + 2]) << 16) |
                               (uint32_t(img[i + 3]) << 24);
            if (dut.flash.peek32(uint32_t(i)) != w) igual = false;
        }
        check(igual, "la Flash desde 0x0 es e4_eth.bin, byte a byte");
        // Reset del sistema, y lo grabado arranca
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        check(espera_estado(100, 600), "tras el reset arranca e4_eth: lwIP con el enlace arriba");
        std::vector<uint8_t> arp = {0, 1, 8, 0, 6, 4, 0, 1, 0x02, 0, 0, 0, 0, 0xFE, 192, 168, 127, 1,
                                    0, 0, 0, 0, 0, 0, 192, 168, 127, 2};
        dut.emac.phy.de_cable(eth(std::vector<uint8_t>(6, 0xFF), 0x0806, arp));
        bool arp_ok = false;
        const sc_time lim2 = sc_time_stamp() + sc_time(20, SC_MS);
        while (!arp_ok && sc_time_stamp() < lim2 && espera_eth(t, -1, 20))
            arp_ok = t.size() >= 42 && t[12] == 8 && t[13] == 6 && t[21] == 2 &&
                     std::vector<uint8_t>(t.begin() + 22, t.begin() + 28) ==
                         std::vector<uint8_t>({0x02, 0, 0, 0xE4, 0, 1});
        check(arp_ok, "y contesta al ARP con la MAC de e4_eth");
        dut.emac.phy.conecta_cable(nullptr);
    }

    // T7, entrega 5: el USB como dispositivo CDC-ACM (e4_usb), con un PC
    // de banco que enumera como lo haria uno de verdad. Los NAK se reintentan
    // cada 20 us, como un anfitrion que vuelve en la siguiente vuelta
    uint8_t usb_reintenta(uint8_t pid, uint8_t dir, uint8_t ep, const std::vector<uint8_t>& s,
                          std::vector<uint8_t>& e, double ms = 5) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        for (;;) {
            const uint8_t r = pid == PID_IN ? pc_usb->in(dir, ep, e) : pc_usb->out(dir, ep, s);
            if (r != PID_NAK || sc_time_stamp() >= lim) return r;
            wait(20, SC_US);
        }
    }
    static std::vector<uint8_t> setup8(uint8_t tipo, uint8_t pet, uint16_t valor, uint16_t indice, uint16_t largo) {
        return {tipo, pet, uint8_t(valor), uint8_t(valor >> 8), uint8_t(indice), uint8_t(indice >> 8),
                uint8_t(largo), uint8_t(largo >> 8)};
    }
    // Una transferencia de control de entrada: SETUP, los IN hasta el corto,
    // y el OUT de cero de la etapa de estado. Devuelve el PID que la acaba
    uint8_t usb_control_in(uint8_t dir, const std::vector<uint8_t>& s, std::vector<uint8_t>& d) {
        d.clear();
        if (pc_usb->setup(dir, s) != PID_ACK) return PID_NADIE;
        const unsigned largo = unsigned(s[6] | (s[7] << 8));
        std::vector<uint8_t> p;
        for (;;) {
            const uint8_t r = usb_reintenta(PID_IN, dir, 0, {}, p);
            if (r != PID_ACK) return r;
            d.insert(d.end(), p.begin(), p.end());
            if (p.size() < 64 || d.size() >= largo) break;
        }
        std::vector<uint8_t> e;
        return usb_reintenta(PID_OUT, dir, 0, {}, e);
    }
    // Y una de salida: SETUP, los datos (si hay) y el IN de cero del estado
    uint8_t usb_control_out(uint8_t dir, const std::vector<uint8_t>& s, const std::vector<uint8_t>& d = {}) {
        if (pc_usb->setup(dir, s) != PID_ACK) return PID_NADIE;
        std::vector<uint8_t> e;
        if (!d.empty()) {
            const uint8_t r = usb_reintenta(PID_OUT, dir, 0, d, e);
            if (r != PID_ACK) return r;
        }
        const uint8_t r = usb_reintenta(PID_IN, dir, 0, {}, e);
        return r == PID_ACK && !e.empty() ? uint8_t(PID_NADIE) : r;
    }
    void t7_usb() {
        grupo("E35 El USB del E4 como dispositivo: enumeracion CDC-ACM y eco por bulk (e4_usb)");
        pc_usb->set_vbus(true);
        pc_usb->conectar(true);
        analog_net_if& dp = dut.gpio.nodo_de(e4::PL, 6);
        analog_net_if& dm = dut.gpio.nodo_de(e4::PL, 7);
        arranca(0, FW_USB);
        check(espera_estado(10, 50), "e4_usb arranca y pone SOFTCONN (USBDevConnect)");
        wait(10, SC_US);
        check(dp.voltage() > 2.7f && dm.voltage() < 0.3f,
              "el PC ve el 1,5 kOhm en D+ contra sus 15 kOhm: velocidad completa");
        std::printf("    D+ %.2f V, D- %.2f V\n", double(dp.voltage()), double(dm.voltage()));
        std::vector<uint8_t> d, e;
        pc_usb->reset_bus();
        pc_usb->sofs(2);
        check(dut.usb.resets() == 1 && buzon(0x10) == 1, "el SE0 de 10 ms es un reset de bus: IS.RESET y su interrupcion");
        check(usb_control_in(0, setup8(0x80, 6, 0x0100, 0, 64), d) == PID_ACK && d.size() == 18,
              "GET_DESCRIPTOR de dispositivo en la direccion 0: los 18 bytes");
        check(d.size() == 18 && d[0] == 18 && d[1] == 1 && d[4] == 2 && d[8] == 0x09 && d[9] == 0x12 &&
              d[10] == 0x01 && d[11] == 0x00, "clase CDC, VID 1209 y PID 0001");
        check(usb_control_out(0, setup8(0x00, 5, 7, 0, 0)) == PID_ACK, "SET_ADDRESS 7, con su etapa de estado");
        pc_usb->sofs(2);                                        // el tiempo de recuperacion
        check(dut.usb.direccion() == 7 && buzon(0x14) == 7 && buzon(B_ESTADO) == 20,
              "FADDR = 7 tras el estado, como manda el protocolo");
        check(pc_usb->in(0, 0, e) == PID_NADIE, "en la direccion 0 ya no contesta nadie");
        check(usb_control_in(7, setup8(0x80, 6, 0x0200, 0, 9), d) == PID_ACK && d.size() == 9 && d[2] == 67,
              "GET_DESCRIPTOR de configuracion (9 bytes): wTotalLength 67");
        check(usb_control_in(7, setup8(0x80, 6, 0x0200, 0, 255), d) == PID_ACK && d.size() == 67 && d[9 + 5] == 2 &&
              d[9 + 6] == 2, "y entera: 67 bytes en dos paquetes (64 + 3), interfaz CDC/ACM");
        check(usb_control_in(7, setup8(0x80, 6, 0x0300, 0, 255), d) == PID_ACK &&
              d == std::vector<uint8_t>({4, 3, 0x09, 0x04}), "la cadena 0: ingles de EE. UU.");
        std::string prod;
        if (usb_control_in(7, setup8(0x80, 6, 0x0302, 0x0409, 255), d) == PID_ACK)
            for (size_t i = 2; i + 1 < d.size(); i += 2) prod += char(d[i]);
        check(prod == "MSP432E401Y CDC de prueba", "la cadena 2, el producto, en UTF-16LE");
        check(usb_control_out(7, setup8(0x00, 9, 1, 0, 0)) == PID_ACK && buzon(B_ESTADO) == 100,
              "SET_CONFIGURATION 1: EP1 de bulk y EP2 de interrupcion configurados");
        check(usb_control_in(7, setup8(0x80, 8, 0, 0, 1), d) == PID_ACK && d == std::vector<uint8_t>({1}),
              "GET_CONFIGURATION: 1");
        // La clase: 230400 8E2 y DTR+RTS
        const std::vector<uint8_t> lc = {0x00, 0x84, 0x03, 0x00, 2, 2, 8};
        check(usb_control_out(7, setup8(0x21, 0x20, 0, 0, 7), lc) == PID_ACK && buzon(0x18) == 230400 &&
              buzon(0x1C) == 0x020802u, "SET_LINE_CODING con su etapa de datos: 230400 8E2");
        check(usb_control_in(7, setup8(0xA1, 0x21, 0, 0, 7), d) == PID_ACK && d == lc, "GET_LINE_CODING devuelve lo mismo");
        check(usb_control_out(7, setup8(0x21, 0x22, 3, 0, 0)) == PID_ACK && buzon(0x20) == 3,
              "SET_CONTROL_LINE_STATE: DTR y RTS");
        // Una peticion que no conoce: STALL, y la siguiente SETUP lo arregla
        const unsigned st0 = pc_usb->stalls();
        check(usb_control_in(7, setup8(0x80, 6, 0x0F00, 0, 5), d) == PID_STALL && buzon(0x28) == 1 &&
              pc_usb->stalls() == st0 + 1, "un descriptor que no tiene (BOS): STALL en el EP0");
        check(usb_control_in(7, setup8(0x80, 0, 0, 0, 2), d) == PID_ACK && d == std::vector<uint8_t>({1, 0}),
              "y la siguiente SETUP se atiende: GET_STATUS, autoalimentado");
        // Los datos: el eco en mayusculas
        check(pc_usb->in(7, 1, e) == PID_NAK, "EP1 IN sin nada que mandar: NAK");
        const std::string m = "hola, puerto serie por usb";
        check(usb_reintenta(PID_OUT, 7, 1, std::vector<uint8_t>(m.begin(), m.end()), e) == PID_ACK,
              "EP1 OUT: 26 bytes");
        check(usb_reintenta(PID_IN, 7, 1, {}, d) == PID_ACK && std::string(d.begin(), d.end()) ==
              "HOLA, PUERTO SERIE POR USB", "EP1 IN: vuelven en mayusculas");
        std::vector<uint8_t> grande(64);
        for (unsigned i = 0; i < 64; ++i) grande[i] = uint8_t('a' + i % 26);
        bool todo = true;
        for (unsigned k = 0; k < 4 && todo; ++k) {
            todo = usb_reintenta(PID_OUT, 7, 1, grande, e) == PID_ACK &&
                   usb_reintenta(PID_IN, 7, 1, {}, d) == PID_ACK && d.size() == 64 &&
                   d[0] == 'A' && d[63] == uint8_t('A' + 63 % 26);
        }
        check(todo, "cuatro paquetes llenos de 64 bytes, ida y vuelta");
        check_eq(buzon(0x24), 26 + 4 * 64, "el firmware cuenta los bytes del eco");
        check(pc_usb->in(7, 3, e) == PID_NAK, "un endpoint sin configurar no manda nada");
        // El reset del chip quita el 1,5 kOhm: el PC ve irse al dispositivo
        mcu.reset_pin(true);
        wait(20, SC_US);
        check(dp.voltage() < 0.3f, "en reset, sin SOFTCONN: D+ cae a masa por los 15 kOhm del PC");
        mcu.reset_pin(false);
        pc_usb->conectar(false);
        pc_usb->set_vbus(false);
        std::printf("    %u transacciones en el USB0; el PC: %u ACK, %u NAK, %u STALL\n", dut.usb.transacciones(),
                    pc_usb->acks(), pc_usb->naks(), pc_usb->stalls());
    }

    // T7, entrega 5: el puente de clase (U2). El mismo e4_usb, pero ahora el
    // PC es el de parts/puente_usb.h, el de `mcu-sim --usb`: enumera solo,
    // y lleva el CDC a un canal serie en memoria
    template <class F> bool espera_que(F f, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (!f() && sc_time_stamp() < lim) wait(100, SC_US);
        return f();
    }
    void t7_puente_usb() {
        grupo("E36 El puente de clase CDC-ACM: enumera e4_usb y lo lleva a un canal serie (U2)");
        arranca(0, FW_USB);
        puente_usb = new PuenteUsbCdc(dut.gpio.nodo_de(e4::PL, 6), dut.gpio.nodo_de(e4::PL, 7), &dut.usb,
                                      serie::parsea("memoria"));
        CanalMemoria& m = *puente_usb->memoria();
        check(puente_usb->error().empty(), "el puente se monta, con su canal en memoria");
        check(espera_que([&] { return puente_usb->listo(); }, 400), "el puente ve llegar el dispositivo y lo enumera");
        check(puente_usb->vid() == 0x1209 && puente_usb->pid() == 0x0001 &&
              puente_usb->producto() == "MSP432E401Y CDC de prueba", "1209:0001, \"MSP432E401Y CDC de prueba\"");
        check(espera_que([&] { return buzon(0x18) == 115200 && buzon(0x20) == 3; }, 10),
              "al abrir el puerto: SET_LINE_CODING 115200 8N1 y SET_CONTROL_LINE_STATE con DTR y RTS");
        check(buzon(B_ESTADO) == 100 && dut.usb.direccion() == 1, "configurado, en la direccion 1");
        m.empuja("eco por el puente");
        check(espera_que([&] { return m.recibido() == "ECO POR EL PUENTE"; }, 20),
              "lo que entra por el canal vuelve en mayusculas");
        // RFC 2217 -> CDC
        puente_usb->pide_baudios(9600);
        puente_usb->pide_paridad(3);
        puente_usb->pide_parada(2);
        check(espera_que([&] { return buzon(0x18) == 9600 && buzon(0x1C) == 0x020802u; }, 10),
              "SET-BAUDRATE 9600, PARITY par y STOPSIZE 2 del terminal: un SET_LINE_CODING 9600 8E2");
        puente_usb->pide_control(9);
        check(espera_que([&] { return buzon(0x20) == 2; }, 10), "SET-CONTROL 9 (DTR fuera): SET_CONTROL_LINE_STATE 2");
        m.borra_recibido();
        m.empuja("?");
        check(espera_que([&] { return m.recibido() == "9600 8E2 2\r\n"; }, 20),
              "y el firmware lo cuenta por el mismo canal: \"9600 8E2 2\"");
        // Un bloque grande: el flujo por NAK, sin perder nada
        m.borra_recibido();
        std::string blq, esp;
        for (unsigned i = 0; i < 1500; ++i) {
            const char c = char('a' + i % 26);
            blq += c; esp += char(c - 32);
        }
        const sc_time t0 = sc_time_stamp();
        m.empuja(blq);
        const bool todo = espera_que([&] { return m.recibido().size() >= esp.size(); }, 200);
        check(todo && m.recibido() == esp, "1500 bytes de ida y vuelta, en orden y sin perder ninguno");
        std::printf("    en %.1f ms simulados; %u paquetes, %u NAK\n", (sc_time_stamp() - t0).to_seconds() * 1e3,
                    puente_usb->controlador().paquetes(), puente_usb->controlador().naks());
        // El chip se reinicia: se va del bus, vuelve y se enumera otra vez
        mcu.reset_pin(true);
        wait(50, SC_US);
        mcu.reset_pin(false);
        check(espera_que([&] { return puente_usb->enumeraciones() == 2 && puente_usb->listo(); }, 400),
              "tras un reset del chip el puente lo ve irse y lo vuelve a enumerar");
        m.borra_recibido();
        m.empuja("otra vez");
        check(espera_que([&] { return m.recibido() == "OTRA VEZ"; }, 20) && buzon(0x18) == 9600,
              "y sigue: con la linea que tenia el terminal (9600)");
        puente_usb->enchufa(false);
        wait(10, SC_US);
        check(!puente_usb->listo(), "desenchufado, el puente suelta el dispositivo");
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
            if (q == "t7" || q == "ccm") t7_ccm();
            if (q == "t7" || q == "can") { t7_can(); t7_dos_e4(); }
            if (q == "t7" || q == "eth") { t7_eth(); t7_bootp(); }
            if (q == "t7" || q == "usb") { t7_usb(); t7_puente_usb(); }
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
        t7_ccm();
        t7_can();
        t7_dos_e4();
        t7_eth();
        t7_bootp();
        t7_usb();
        t7_puente_usb();
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
