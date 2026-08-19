// =============================================================================
// sc_main.cpp — Banco de pruebas del modelo (fases F1, F2 y F3)
//
// Cada fase añade sus grupos y conserva los anteriores, de modo que la suite
// es acumulativa y detecta regresiones. Fase F1 (infraestructura):
//
//   T01 Power-up, secuencia de reset y estado inicial del RCC   [IR, §4.1, §4.5]
//   T02 Gating de reloj: acceso a periférico sin ENR -> error   [IR, §4.8]
//   T03 Lectura/escritura en SRAM1, SRAM2 y BKPSRAM (8/16/32/bloque)
//   T04 CCM: inalcanzable desde la matriz, alcanzable por el D-bus [IR, §5.3]
//   T05 Máscara de conectividad maestro-esclavo completa        [IR, §6.2]
//   T06 Rangos reservados y bloques ausentes en el F407         [IR, §6.5]
//   T07 Interfaz Flash: llaves, borrado, programación y errores [IR, §5.5-5.9]
//   T08 Estados de espera y acelerador ART                      [IR, §5.2.2-5.2.3]
//   T09 Bit-banding en SRAM y periféricos                       [IR, §5.4]
//   T10 Árbol de reloj: HSE + PLL -> 168/42/84 MHz              [IR, §4.3, §4.4]
//   T11 Penalización del puente AHB->APB                        [IR, §6.4]
//   T12 Contención en un puerto de esclavo de la matriz         [IR, §6.7]
//   T13 Reset por pin NRST y flags de RCC_CSR                   [IR, §4.1, §4.10]
//   T14 Cargador de imagen y alias de arranque de 0x0000 0000   [IR, §2.3, §5.1]
//
// Fase F2 (núcleo Cortex-M4F):
//   T15 Decodificador: las 254 codificaciones de [II]
//   T16 Firmware bare-metal autocomprobable (103 comprobaciones)
//   T17 CoreMark 1.0 de EEMBC                    [criterio de salida de F2]
//
// Fase F3 (pines, GPIO y RCC eléctrico):
//   T18 Pad: alta impedancia, pulls, Schmitt, rango y corriente [IR, §2.4, §3.5]
//   T19 Puerto GPIO: registros, BSRR atómico y LCKR             [IR, §3.4]
//   T20 Multiplexor de funciones alternativas                   [IR, §3.3.3]
//   T21 HSE: presencia del cristal y modo bypass                [IR, §4.2]
//   T22 Clock Security System: fallo del HSE -> HSI + NMI       [IR, §4.2]
//   T23 Salidas de reloj MCO1/MCO2 medidas en el pin            [IR, §4.5.3]
//   T24 Supervisión POR/PDR/BOR con los option bytes            [IR, §5.7.1]
//   T25 Blinky compilado con CMSIS                [criterio de salida de F3]
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include "stm32f407vg.h"
#include "../verif/bus_test_master.h"
#include "../verif/image_loader.h"
#include "../verif/ext_parts.h"
#include "../verif/decoder_vectors.h"

using namespace sc_core;
using namespace stm32;
using tlm::TLM_OK_RESPONSE;
using tlm::TLM_ADDRESS_ERROR_RESPONSE;
using tlm::TLM_GENERIC_ERROR_RESPONSE;

// ---------------------------------------------------------------------------
// Utilidades de comprobación
// ---------------------------------------------------------------------------
static unsigned g_pass = 0, g_fail = 0;
static std::string g_group;

static void group(const char* g) {
    g_group = g;
    std::printf("\n--- %s ---\n", g);
}
static bool check(bool cond, const char* what) {
    (cond ? g_pass : g_fail)++;
    std::printf("  [%s] %s\n", cond ? "OK  " : "FALLO", what);
    return cond;
}
static bool check_eq(uint64_t got, uint64_t exp, const char* what) {
    const bool ok = (got == exp);
    (ok ? g_pass : g_fail)++;
    if (ok) std::printf("  [OK  ] %s\n", what);
    else    std::printf("  [FALLO] %s (obtenido 0x%llX, esperado 0x%llX)\n",
                        what, (unsigned long long)got, (unsigned long long)exp);
    return ok;
}
static bool check_near(double got, double exp, double tol, const char* what) {
    const bool ok = (exp == 0.0) ? (got == 0.0)
                                 : (got > exp * (1 - tol) && got < exp * (1 + tol));
    (ok ? g_pass : g_fail)++;
    if (ok) std::printf("  [OK  ] %s (%.6g)\n", what, got);
    else    std::printf("  [FALLO] %s (obtenido %.6g, esperado %.6g)\n", what, got, exp);
    return ok;
}

// ---------------------------------------------------------------------------
// Banco de pruebas
// ---------------------------------------------------------------------------
SC_MODULE(F1Tb) {
    Stm32F407VG*  dut;
    BusTestMaster tm{"tm"};

    // --- Circuitería externa de la placa (verif/ext_parts.h) ---------------
    // Cristal de 8 MHz en PH0/PH1 y de 32.768 kHz en PC14/PC15: sin ellos el
    // HSE y el LSE no arrancan, igual que en el sistema real [IR, §4.2].
    Crystal* xtal_hse = nullptr;
    Crystal* xtal_lse = nullptr;
    Led*     led_pd12 = nullptr;   // LED verde de la Discovery (PD12, a VSS)
    Button*  btn_pa0  = nullptr;   // pulsador de usuario en PA0-WKUP
    // Oscilador externo para el modo bypass del HSE. Los sc_module deben
    // construirse durante la elaboración, así que se crea aquí parado.
    ExtClock* osc_ext = nullptr;

    // Drivers externos de los nodos analógicos de alimentación / reset / boot
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1, d_pb2 = -1;

    SC_CTOR(F1Tb) {
        dut = new Stm32F407VG("dut");
        tm.isk.bind(dut->matrix.from_tb);          // puerto de verificación
        xtal_hse = new Crystal(dut->pinmux.analog(7, 0));    // PH0-OSC_IN
        xtal_lse = new Crystal(dut->pinmux.analog(2, 14));   // PC14-OSC32_IN
        led_pd12 = new Led("led_pd12", dut->pinmux.analog(3, 12), true);
        btn_pa0  = new Button(dut->pinmux.analog(0, 0));
        osc_ext  = new ExtClock("osc_ext", dut->pinmux.analog(7, 0), 0.0);
        // La pila por defecto de un SC_THREAD (64 KB) se queda corta con las
        // cadenas de llamadas TLM anidadas al compilar con sanitizers.
        SC_THREAD(stim_proc);        set_stack_size(1024 * 1024);
        SC_THREAD(contention_proc);  set_stack_size(256 * 1024);
    }
    ~F1Tb() {
        delete osc_ext; delete btn_pa0; delete led_pd12;
        delete xtal_lse; delete xtal_hse;
        delete dut;
    }

    // -----------------------------------------------------------------------
    void power_up() {
        d_vdd  = dut->pwr_pads.vdd.register_driver("tb_vdd");
        d_vdda = dut->pwr_pads.vdda.register_driver("tb_vdda");
        d_nrst = dut->pwr_pads.nrst.register_driver("tb_nrst");
        d_bt0  = dut->pwr_pads.boot0.register_driver("tb_boot0");
        d_pb2  = dut->pinmux.analog(1, 2).register_driver("tb_pb2");   // BOOT1

        dut->pwr_pads.vdd.set_drive(d_vdd, 0.0f, 1.0f);
        dut->pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        dut->pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);     // BOOT0 = 0
        dut->pinmux.analog(1, 2).set_drive(d_pb2, 0.0f, 10e3f); // BOOT1 = 0
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(10, SC_US);
        dut->pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);        // VDD = 3.3 V
        dut->pwr_pads.vdda.set_drive(d_vdda, 3.3f, 0.1f);
        wait(100, SC_US);
        dut->pwr_pads.nrst.set_hiz(d_nrst);                    // soltar NRST
        wait(200, SC_US);                                      // arranque de HSI
    }

    // Firmware de aparcamiento: MSP válido y un manejador de reset que se
    // duerme. Evita que el núcleo ejecute basura durante las pruebas de bus.
    void park_cpu() {
        ImageLoader ld(*dut);
        ld.write_reset_vector(addr::SRAM1_BASE + addr::SRAM1_SIZE, 0x08000100u);
        ld.poke32(addr::FLASH_BASE + 0x100, 0xE7FEBF30u);   // wfi ; b .
    }

    // Selección del maestro que impersona el banco de pruebas. La tabla de
    // conectividad [IR, §6.2] obliga a usar el bus adecuado en cada caso:
    //   registros y periféricos (AHB1/AHB2/APB) -> S-bus
    //   array de Flash como dato                -> D-bus
    //   array de Flash como instrucción         -> I-bus
    void as_sbus()  { tm.master = BusMaster::CORE_SBUS; tm.instr = false; }
    void as_dbus()  { tm.master = BusMaster::CORE_DBUS; tm.instr = false; }
    void as_ibus()  { tm.master = BusMaster::CORE_IBUS; tm.instr = true;  }

    // Habilita el reloj de un periférico escribiendo su bit en RCC_xxxENR.
    void rcc_enable(uint32_t enr_off, unsigned bit) {
        uint32_t v = 0;
        tm.read32(addr::RCC_B + enr_off, v);
        tm.write32(addr::RCC_B + enr_off, v | (1u << bit));
    }

    // =======================================================================
    void stim_proc() {
        tm.master = BusMaster::CORE_SBUS;
        park_cpu();
        power_up();

        t01_reset_y_relojes();
        t02_gating();
        t03_memorias();
        t04_ccm();
        t05_conectividad();
        t06_rangos_reservados();
        t07_flash();
        t08_wait_states_art();
        t09_bitband();
        t10_arbol_reloj();
        t11_puente_apb();
        t12_contencion();
        t13_reset_nrst();
        t14_cargador_y_boot();
        const unsigned f1_pass = g_pass, f1_fail = g_fail;

        // ================= Fase F2: núcleo Cortex-M4F =======================
        t15_decodificador();
        t16_firmware();
        t17_coremark();
        const unsigned f2_pass = g_pass, f2_fail = g_fail;

        // ============ Fase F3: pines, GPIO y RCC eléctrico ==================
        t18_pad_electrico();
        t19_gpio_registros();
        t20_mux_af();
        t21_hse_bypass();
        t22_css();
        t23_mco();
        t24_bor();
        t25_blinky_cmsis();

        std::printf("\n=====================================================\n");
        std::printf("Resumen F1: %u comprobaciones OK, %u fallos\n", f1_pass, f1_fail);
        std::printf("Resumen F2: %u comprobaciones OK, %u fallos\n",
                    f2_pass - f1_pass, f2_fail - f1_fail);
        std::printf("Resumen F3: %u comprobaciones OK, %u fallos\n",
                    g_pass - f2_pass, g_fail - f2_fail);
        std::printf("TOTAL     : %u comprobaciones OK, %u fallos\n", g_pass, g_fail);
        std::printf("=====================================================\n");
        sc_stop();
    }

    // -----------------------------------------------------------------------
    // T15 — Cobertura del decodificador con los vectores de
    //       doc/valida_instrucciones.py (codificaciones validadas contra
    //       arm-none-eabi-as, 254/254 correctas).
    // -----------------------------------------------------------------------
    void t15_decodificador() {
        group("T15 Decodificador: 254 codificaciones reales [II, todas las secciones]");
        // Detener el núcleo y prepararlo para la sonda
        dut->core.debug.set_halt(true);
        wait(20, SC_US);
        check(dut->core.debug.is_halted(), "el nucleo se detiene a peticion del depurador");

        // Habilitar la FPU (CP10/CP11) para que las V* no den NOCP
        dut->core.debug.ap_write32(0xE000ED88u, 0xFu << 20);

        const uint32_t code = addr::SRAM1_BASE + 0x4000;   // zona de código
        const uint32_t scratch = addr::SRAM1_BASE + 0x5000;
        unsigned no_reconocidas = 0, tam_incorrecto = 0;
        std::string primeras;

        for (unsigned i = 0; i < N_DECODER_VECTORS; ++i) {
            const DecoderVector& v = DECODER_VECTORS[i];
            // Colocar la codificación (más una NOP de relleno) en la SRAM
            dut->sram1.poke32(code - addr::SRAM1_BASE,
                              uint32_t(v.hw[0]) | (uint32_t(v.hw[1]) << 16));
            dut->sram1.poke32(code - addr::SRAM1_BASE + 4, 0xBF00BF00u);
            dut->core.cpu.probe_setup(scratch);
            const Cpu::Probe p = dut->core.cpu.probe(code);
            const unsigned esperado = 2u * v.n_hw;
            // UDF y UDF.W son indefinidas por definición: deben generar
            // UsageFault UNDEFINSTR [II, §1.8, §3.3].
            const bool debe_ser_undef = (std::string(v.asm_text).compare(0, 3, "udf") == 0);
            if (debe_ser_undef) {
                if (p.ok) {
                    ++no_reconocidas;
                    primeras += std::string("        UDF no genera UNDEFINSTR: ") +
                                v.asm_text + "\n";
                }
            } else if (!p.ok) {
                ++no_reconocidas;
                if (primeras.size() < 200)
                    primeras += std::string("        no reconocida: §") + v.section +
                                "  " + v.asm_text + "\n";
            } else if (p.size != esperado) {
                ++tam_incorrecto;
                if (primeras.size() < 200)
                    primeras += std::string("        tamano ") + std::to_string(p.size) +
                                " != " + std::to_string(esperado) + ": " + v.asm_text + "\n";
            }
        }
        if (!primeras.empty()) std::printf("%s", primeras.c_str());
        std::printf("    %u codificaciones probadas\n", N_DECODER_VECTORS);
        check_eq(no_reconocidas, 0, "todas las codificaciones se reconocen (sin UNDEFINSTR)");
        check_eq(tam_incorrecto, 0, "todas consumen el numero de bytes correcto (16/32 bits)");

        dut->core.debug.set_halt(false);
        wait(20, SC_US);
    }

    // -----------------------------------------------------------------------
    // T16 — Ejecución del firmware autocomprobable compilado con
    //       arm-none-eabi-gcc para Cortex-M4F.
    // -----------------------------------------------------------------------
    void t16_firmware() {
        group("T16 Firmware real sobre el modelo [II; IR, §7, §9, §10]");
        // Reset limpio y BOOT0 = 0 (arranque desde la Flash principal)
        dut->pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(30, SC_US);

        ImageLoader ld(*dut);
        const long n = ld.load_file(fw_path_.c_str(), addr::FLASH_BASE);
        if (!check(n > 0, "imagen de firmware cargada en la Flash")) {
            std::printf("        (no se encontro %s; compilar con "
                        "make -C verif/fw)\n", fw_path_.c_str());
            dut->pwr_pads.nrst.set_hiz(d_nrst);
            return;
        }
        std::printf("    %ld bytes cargados desde %s\n", n, fw_path_.c_str());

        if (const char* t = std::getenv("F2_TRACE")) {
            dut->core.cpu.trace_limit = 200;
            dut->core.cpu.trace_from  = uint64_t(std::atoll(t));
        }
        dut->pwr_pads.nrst.set_hiz(d_nrst);
        wait(400, SC_US);

        // El firmware deja su resultado en un buzón al inicio de la SRAM1
        const unsigned MAGIC = 0x46325445u;
        const sc_time t0 = sc_time_stamp();
        bool done = false;
        while ((sc_time_stamp() - t0) < sc_time(400, SC_MS)) {
            wait(200, SC_US);
            if (dut->sram1.peek32(0) == MAGIC && dut->sram1.peek32(16) == 1u) {
                done = true;
                break;
            }
        }
        const uint32_t passed = dut->sram1.peek32(4);
        const uint32_t failed = dut->sram1.peek32(8);
        const uint32_t first  = dut->sram1.peek32(12);
        std::printf("    instrucciones ejecutadas: %llu | excepciones: %llu\n",
                    (unsigned long long)dut->core.cpu.inst_count,
                    (unsigned long long)dut->core.cpu.exc_count);
        check(done, "el firmware llega a su fin y publica el buzon de resultados");
        std::printf("    autocomprobaciones del firmware: %u OK, %u fallos%s\n",
                    passed, failed, failed ? "" : "");
        if (failed) {
            std::printf("        ultimo test alcanzado: #%u\n", dut->sram1.peek32(88));
            std::printf("        tests fallidos:");
            for (unsigned i = 0; i < failed && i < 16; ++i)
                std::printf(" #%u", dut->sram1.peek32(24 + 4 * i));
            std::printf("\n");
        }
        (void)first;
        check(passed > 100u, "el firmware ejecuta mas de 100 autocomprobaciones");
        check_eq(failed, 0, "el firmware no reporta ninguna discrepancia");
        check(dut->core.cpu.exc_count > 0, "el nucleo ha tomado excepciones (SVC/PendSV/IRQ)");
        check(!dut->core.cpu.halted_on_lockup, "el nucleo no ha entrado en lockup");
    }

    // -----------------------------------------------------------------------
    void t01_reset_y_relojes() {
        group("T01 Power-up, reset y estado inicial del RCC [IR, 4.1/4.5]");
        check(dut->s_sysrst_n.read(), "reset de sistema liberado tras el power-up");
        check(dut->s_por_ok.read(), "POR/PDR indica alimentacion valida");
        check_near(dut->rcc.sysclk_hz(), 16e6, 1e-6, "SYSCLK = HSI 16 MHz tras reset");
        check_near(dut->rcc.hclk_freq(), 16e6, 1e-6, "HCLK = 16 MHz (HPRE = /1)");
        check_near(dut->rcc.pclk1_freq(), 16e6, 1e-6, "PCLK1 = 16 MHz (PPRE1 = /1)");
        check_near(dut->rcc.pclk2_freq(), 16e6, 1e-6, "PCLK2 = 16 MHz (PPRE2 = /1)");

        uint32_t cr = 0, cfgr = 0, csr = 0, ahb1enr = 0;
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        tm.read32(addr::RCC_B + Rcc::R_CFGR, cfgr);
        tm.read32(addr::RCC_B + Rcc::R_CSR, csr);
        tm.read32(addr::RCC_B + Rcc::R_AHB1ENR, ahb1enr);
        check((cr & 1u) != 0, "RCC_CR.HSION = 1 tras reset");
        check((cr & 2u) != 0, "RCC_CR.HSIRDY = 1 (HSI estabilizado)");
        check_eq(cfgr & 0xFu, 0x0, "RCC_CFGR.SW = SWS = HSI");
        check_eq(ahb1enr, 0x00100000u, "RCC_AHB1ENR reset = 0x00100000 (CCMDATARAMEN)");
        check((csr & (1u << 27)) != 0, "RCC_CSR.PORRSTF activo tras power-up");
        check_eq(dut->rcc.peek_reg(Rcc::R_PLLCFGR), 0x24003010u,
                 "RCC_PLLCFGR reset = 0x24003010");
        check_eq(dut->rcc.peek_reg(Rcc::R_APB1LPENR), 0x36FEC9FFu,
                 "RCC_APB1LPENR reset = 0x36FEC9FF");
    }

    // -----------------------------------------------------------------------
    void t02_gating() {
        group("T02 Gating de reloj por RCC_xxxENR [IR, 4.8]");
        uint32_t v = 0;
        check(tm.read32(addr::GPIOA_B, v) == TLM_GENERIC_ERROR_RESPONSE,
              "GPIOA sin GPIOAEN -> error de bus");
        rcc_enable(Rcc::R_AHB1ENR, 0);                     // GPIOAEN
        check(dut->s_pcen[P_GPIOA].read(), "senal de gating de GPIOA activa");
        check(tm.read32(addr::GPIOA_B, v) == TLM_OK_RESPONSE,
              "GPIOA con GPIOAEN -> acceso correcto");

        check(tm.read32(addr::PWR_B, v) == TLM_GENERIC_ERROR_RESPONSE,
              "PWR (APB1) sin PWREN -> error de bus");
        rcc_enable(Rcc::R_APB1ENR, 28);                    // PWREN
        check(tm.read32(addr::PWR_B, v) == TLM_OK_RESPONSE,
              "PWR con PWREN -> acceso correcto");

        // Reset de periférico: RCC_AHB1RSTR.GPIOARST
        tm.write32(addr::RCC_B + Rcc::R_AHB1RSTR, 1u << 0);
        check(!dut->s_prst[P_GPIOA].read(), "GPIOARST mantiene el reset de GPIOA");
        tm.write32(addr::RCC_B + Rcc::R_AHB1RSTR, 0);
        check(dut->s_prst[P_GPIOA].read(), "GPIOARST liberado");

        rcc_enable(Rcc::R_APB2ENR, 14);                    // SYSCFGEN (T14)
        rcc_enable(Rcc::R_AHB1ENR, 18);                    // BKPSRAMEN (T03)
    }

    // -----------------------------------------------------------------------
    void t03_memorias() {
        group("T03 Lectura/escritura de memoria desde el maestro de prueba");
        uint32_t v = 0;
        // --- SRAM1 ---
        check(tm.write32(addr::SRAM1_BASE + 0x100, 0xA5A5F00Du) == TLM_OK_RESPONSE,
              "escritura de 32 bits en SRAM1");
        check(tm.read32(addr::SRAM1_BASE + 0x100, v) == TLM_OK_RESPONSE,
              "lectura de 32 bits en SRAM1");
        check_eq(v, 0xA5A5F00Du, "dato leido de SRAM1 coincide");
        check_eq(dut->sram1.peek32(0x100), 0xA5A5F00Du, "contenido fisico de SRAM1");

        // --- accesos de 8 y 16 bits ---
        uint8_t b8 = 0; uint16_t b16 = 0;
        tm.write8(addr::SRAM1_BASE + 0x100, 0x11);
        tm.read8(addr::SRAM1_BASE + 0x100, b8);
        check_eq(b8, 0x11, "escritura/lectura de byte en SRAM1");
        tm.read32(addr::SRAM1_BASE + 0x100, v);
        check_eq(v, 0xA5A5F011u, "el byte solo modifica su posicion en la palabra");
        tm.write16(addr::SRAM1_BASE + 0x102, 0xBEEF);
        tm.read16(addr::SRAM1_BASE + 0x102, b16);
        check_eq(b16, 0xBEEF, "escritura/lectura de media palabra en SRAM1");

        // --- bloque (rafaga INCR) ---
        unsigned char wr[64], rd[64];
        for (unsigned i = 0; i < 64; ++i) { wr[i] = uint8_t(i * 3 + 1); rd[i] = 0; }
        check(tm.write_block(addr::SRAM1_BASE + 0x200, wr, 64) == TLM_OK_RESPONSE,
              "escritura de bloque de 64 bytes en SRAM1");
        tm.read_block(addr::SRAM1_BASE + 0x200, rd, 64);
        check(std::memcmp(wr, rd, 64) == 0, "el bloque leido coincide con el escrito");

        // --- SRAM2 ---
        tm.write32(addr::SRAM2_BASE + 0x40, 0x12345678u);
        tm.read32(addr::SRAM2_BASE + 0x40, v);
        check_eq(v, 0x12345678u, "escritura/lectura en SRAM2 (16 KB)");

        // --- BKPSRAM (AHB1, 4 KB) ---
        tm.write32(addr::BKPSRAM_BASE + 0x10, 0xCAFEBABEu);
        tm.read32(addr::BKPSRAM_BASE + 0x10, v);
        check_eq(v, 0xCAFEBABEu, "escritura/lectura en BKPSRAM");

        // --- límites: último byte válido y primer byte fuera ---
        check(tm.write32(addr::SRAM1_BASE + addr::SRAM1_SIZE - 4, 0x1u) == TLM_OK_RESPONSE,
              "ultimo word de SRAM1 accesible");
        check(tm.read32(addr::SRAM2_BASE + addr::SRAM2_SIZE, v) != TLM_OK_RESPONSE,
              "primer word por encima de SRAM2 -> error");
    }

    // -----------------------------------------------------------------------
    void t04_ccm() {
        group("T04 CCM RAM: solo accesible por el D-bus del nucleo [IR, 5.3]");
        uint32_t v = 0;
        tm.master = BusMaster::DMA1_MEM;
        check(tm.write32(addr::CCM_BASE + 0x20, 0xDEADC0DEu) == TLM_ADDRESS_ERROR_RESPONSE,
              "DMA1 hacia la CCM -> error de bus");
        tm.master = BusMaster::CORE_SBUS;
        check(tm.read32(addr::CCM_BASE + 0x20, v) == TLM_ADDRESS_ERROR_RESPONSE,
              "S-bus hacia la CCM a traves de la matriz -> error de bus");
        check(dut->matrix.n_err_ccm >= 2, "la matriz contabiliza los intentos a la CCM");

        // Camino correcto: router del nucleo (AHB-AP de depuracion)
        check(dut->core.debug.ap_write32(addr::CCM_BASE + 0x20, 0xDEADC0DEu)
                  == TLM_OK_RESPONSE, "AHB-AP (D-bus del nucleo) escribe en la CCM");
        uint32_t r = 0;
        dut->core.debug.ap_read32(addr::CCM_BASE + 0x20, r);
        check_eq(r, 0xDEADC0DEu, "dato leido de la CCM por el D-bus");
        check_eq(dut->ccm.peek32(0x20), 0xDEADC0DEu, "contenido fisico de la CCM");
    }

    // -----------------------------------------------------------------------
    void t05_conectividad() {
        group("T05 Mascara de conectividad maestro-esclavo [IR, 6.2]");
        struct Probe { BusSlaveId s; uint64_t addr; };
        // Una dirección representativa por esclavo de la matriz
        const Probe pr[] = {
            {BusSlaveId::FLASH_ICODE, addr::FLASH_BASE + 0x10},
            {BusSlaveId::FLASH_DCODE, addr::FLASH_BASE + 0x10},
            {BusSlaveId::SRAM1,       addr::SRAM1_BASE + 0x300},
            {BusSlaveId::SRAM2,       addr::SRAM2_BASE + 0x300},
            {BusSlaveId::AHB1_SEG,    addr::GPIOA_B},
            {BusSlaveId::AHB2_SEG,    addr::RNG_B},
            {BusSlaveId::FSMC_EXT,    addr::FSMC_MEM + 0x10}
        };
        unsigned errores = 0, casos = 0;
        for (unsigned m = 0; m < unsigned(BusMaster::N_MASTERS); ++m) {
            for (const Probe& p : pr) {
                // Flash-I solo se alcanza declarando búsqueda de instrucción
                if (p.s == BusSlaveId::FLASH_ICODE && BusMaster(m) != BusMaster::CORE_IBUS)
                    continue;
                if (p.s == BusSlaveId::FLASH_DCODE && BusMaster(m) == BusMaster::CORE_IBUS)
                    continue;
                tm.master = BusMaster(m);
                tm.instr  = (p.s == BusSlaveId::FLASH_ICODE);
                uint32_t v = 0;
                const auto r = tm.read32(p.addr, v);
                const bool esperado_ok = dut->matrix.connected(BusMaster(m), p.s);
                const bool obtenido_ok = (r != TLM_ADDRESS_ERROR_RESPONSE);
                ++casos;
                if (esperado_ok != obtenido_ok) {
                    ++errores;
                    std::printf("    discrepancia: %s -> %s (esperado %s)\n",
                                master_name(BusMaster(m)), slave_name(p.s),
                                esperado_ok ? "permitido" : "prohibido");
                }
            }
        }
        tm.master = BusMaster::CORE_SBUS; tm.instr = false;
        std::printf("    %u pares maestro-esclavo comprobados\n", casos);
        check(errores == 0, "la matriz respeta la tabla de conectividad completa");
        check(dut->matrix.n_err_conn > 0, "se han rechazado caminos inexistentes");
    }

    // -----------------------------------------------------------------------
    void t06_rangos_reservados() {
        group("T06 Rangos reservados y bloques ausentes en el F407 [IR, 6.5]");
        uint32_t v = 0;
        struct R { uint64_t a; const char* d; };
        const R res[] = {
            {0x20020000ull, "0x2002 0000 (por encima de SRAM2)"},
            {0x30000000ull, "0x3000 0000 (region SRAM no implementada)"},
            {0xC0000000ull, "0xC000 0000 (dispositivo externo no implementado)"},
            {0x40016800ull, "0x4001 6800 (LTDC: no existe en el F407)"},
            {0x40015800ull, "0x4001 5800 (SAI1: no existe en el F407)"},
            {0x4002B000ull, "0x4002 B000 (DMA2D: no existe en el F407)"},
            {0x50060000ull, "0x5006 0000 (CRYP: no existe en el F407)"}
        };
        for (const R& r : res)
            check(tm.read32(r.a, v) == TLM_ADDRESS_ERROR_RESPONSE, r.d);
    }

    // -----------------------------------------------------------------------
    void t07_flash() {
        group("T07 Interfaz Flash: llaves, borrado y programacion [IR, 5.5-5.9]");
        uint32_t v = 0;
        // Valores de reset de los registros
        tm.read32(addr::FLASHIF_B + FlashIf::CR, v);
        check_eq(v, 0x80000000u, "FLASH_CR reset = 0x80000000 (LOCK = 1)");
        tm.read32(addr::FLASHIF_B + FlashIf::OPTCR, v);
        check_eq(v, 0x0FFFAAEDu, "FLASH_OPTCR reset = 0x0FFFAAED");

        // El array de Flash solo es alcanzable por los buses I y D del núcleo
        // [IR, §6.2]; los registros FLASH_* están en AHB1 (S-bus).
        as_dbus();
        check_eq(tm.rd32(addr::FLASH_BASE + 0x1000), 0xFFFFFFFFu,
                 "Flash sin programar lee 0xFFFFFFFF");

        // Programación con LOCK = 1 -> error
        check(tm.write32(addr::FLASH_BASE + 0x1000, 0x11223344u) != TLM_OK_RESPONSE,
              "escritura en Flash con FLASH_CR.LOCK = 1 -> error");
        as_sbus();

        // Secuencia de llave incorrecta: bloquea hasta el siguiente reset
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, FlashIf::KEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, 0x00000000u);
        check(dut->flash.cr_locked(), "secuencia de llave incorrecta deja FLASH_CR bloqueado");
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, FlashIf::KEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, FlashIf::KEY2);
        check(dut->flash.cr_locked(), "tras la secuencia rota, la llave correcta no desbloquea");

        // Reset del sistema para levantar el bloqueo de llave
        pulse_nrst();
        check(dut->flash.cr_locked(), "FLASH_CR vuelve a estar bloqueado tras reset");
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, FlashIf::KEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::KEYR, FlashIf::KEY2);
        check(!dut->flash.cr_locked(), "secuencia KEY1/KEY2 correcta desbloquea FLASH_CR");

        // PSIZE = x32, PG = 1 y programación de una palabra virgen
        tm.write32(addr::FLASHIF_B + FlashIf::CR, (2u << 8) | 1u);
        as_dbus();
        check(tm.write32(addr::FLASH_BASE + 0x1000, 0x11223344u) == TLM_OK_RESPONSE,
              "programacion de una palabra virgen");
        check_eq(tm.rd32(addr::FLASH_BASE + 0x1000), 0x11223344u,
                 "la palabra programada se lee correctamente");
        as_sbus();
        tm.read32(addr::FLASHIF_B + FlashIf::SR, v);
        check((v & 1u) != 0, "FLASH_SR.EOP tras la programacion");
        tm.write32(addr::FLASHIF_B + FlashIf::SR, 0xF3u);    // limpiar rc_w1
        tm.read32(addr::FLASHIF_B + FlashIf::SR, v);
        check_eq(v, 0u, "los flags rc_w1 de FLASH_SR se limpian escribiendo 1");

        // Reprogramar sin borrar -> PGSERR
        as_dbus();
        tm.write32(addr::FLASH_BASE + 0x1000, 0x55667788u);
        as_sbus();
        tm.read32(addr::FLASHIF_B + FlashIf::SR, v);
        check((v & (1u << 7)) != 0, "reprogramar sin borrar activa PGSERR");
        as_dbus();
        check_eq(tm.rd32(addr::FLASH_BASE + 0x1000), 0x11223344u,
                 "la palabra no cambia tras el error de secuencia");
        as_sbus();
        tm.write32(addr::FLASHIF_B + FlashIf::SR, 0xF3u);

        // Error de paralelismo: PSIZE x32 pero acceso de 16 bits -> PGPERR
        as_dbus();
        tm.write16(addr::FLASH_BASE + 0x1010, 0xAAAA);
        as_sbus();
        tm.read32(addr::FLASHIF_B + FlashIf::SR, v);
        check((v & (1u << 6)) != 0, "acceso de 16 bits con PSIZE=x32 activa PGPERR");
        tm.write32(addr::FLASHIF_B + FlashIf::SR, 0xF3u);

        // Borrado del sector 0 (16 KB desde 0x0800 0000)
        tm.write32(addr::FLASHIF_B + FlashIf::CR, (2u << 8) | (0u << 3) | (1u << 1));
        tm.write32(addr::FLASHIF_B + FlashIf::CR,
                   (2u << 8) | (0u << 3) | (1u << 1) | (1u << 16));   // STRT
        as_dbus();
        check_eq(tm.rd32(addr::FLASH_BASE + 0x1000), 0xFFFFFFFFu,
                 "el sector 0 queda borrado a 0xFFFFFFFF");
        as_sbus();
        tm.read32(addr::FLASHIF_B + FlashIf::CR, v);
        check((v & (1u << 16)) == 0, "FLASH_CR.STRT se limpia al terminar el borrado");
        tm.write32(addr::FLASHIF_B + FlashIf::SR, 0xF3u);

        // Protección de escritura: nWRP del sector 1 a 0 -> WRPERR
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY2);
        check(!dut->flash.opt_locked(), "secuencia OPTKEY desbloquea FLASH_OPTCR");
        tm.read32(addr::FLASHIF_B + FlashIf::OPTCR, v);
        tm.write32(addr::FLASHIF_B + FlashIf::OPTCR, v & ~(1u << (16 + 1)));
        tm.write32(addr::FLASHIF_B + FlashIf::CR, (2u << 8) | 1u);
        as_dbus();
        tm.write32(FLASH_SECTORS[1].base, 0x0u);
        as_sbus();
        tm.read32(addr::FLASHIF_B + FlashIf::SR, v);
        check((v & (1u << 4)) != 0, "escritura en sector protegido activa WRPERR");
        tm.write32(addr::FLASHIF_B + FlashIf::SR, 0xF3u);

        // Bloqueo software de FLASH_CR
        tm.write32(addr::FLASHIF_B + FlashIf::CR, 0x80000000u);
        check(dut->flash.cr_locked(), "escribir LOCK = 1 vuelve a bloquear FLASH_CR");
    }

    // -----------------------------------------------------------------------
    void t08_wait_states_art() {
        group("T08 Estados de espera y acelerador ART [IR, 5.2.2-5.2.3]");
        // LATENCY = 5 WS, cachés e instrucción-prefetch deshabilitados
        tm.write32(addr::FLASHIF_B + FlashIf::ACR, 5u);
        uint32_t v = 0;
        tm.read32(addr::FLASHIF_B + FlashIf::ACR, v);
        check_eq(v & 0xFu, 5u, "FLASH_ACR.LATENCY programada a 5 WS");
        check_eq(flash_min_latency(168e6), 5u, "tabla de WS: 168 MHz requiere 5 WS");
        check_eq(flash_min_latency(16e6), 0u, "tabla de WS: 16 MHz requiere 0 WS");

        tm.instr = true; tm.master = BusMaster::CORE_IBUS;
        sc_time t_sin_cache;
        tm.access(false, addr::FLASH_BASE + 0x8000, buf4_, 4, &t_sin_cache);
        // Con caché de instrucciones y prefetch activos, la relectura es acierto
        tm.master = BusMaster::CORE_SBUS; tm.instr = false;
        tm.write32(addr::FLASHIF_B + FlashIf::ACR, 5u | (1u << 8) | (1u << 9));
        tm.master = BusMaster::CORE_IBUS; tm.instr = true;
        sc_time t_fallo, t_acierto;
        tm.access(false, addr::FLASH_BASE + 0x9000, buf4_, 4, &t_fallo);
        tm.access(false, addr::FLASH_BASE + 0x9000, buf4_, 4, &t_acierto);
        tm.master = BusMaster::CORE_SBUS; tm.instr = false;

        check(t_fallo > t_acierto, "el acierto en la cache I del ART es mas rapido que el fallo");
        check(dut->flash.art_hits() > 0, "el ART contabiliza aciertos");
        // Lectura secuencial: la línea siguiente ya fue traida por el prefetch
        tm.master = BusMaster::CORE_IBUS; tm.instr = true;
        const uint64_t h0 = dut->flash.art_hits();
        tm.access(false, addr::FLASH_BASE + 0x9010, buf4_, 4, nullptr);
        tm.master = BusMaster::CORE_SBUS; tm.instr = false;
        check(dut->flash.art_hits() > h0, "el prefetch convierte el acceso secuencial en acierto");

        // Aviso de latencia insuficiente
        tm.write32(addr::FLASHIF_B + FlashIf::ACR, 0u);
        check_eq(dut->flash.latency(), 0u, "LATENCY vuelve a 0 WS");
    }

    // -----------------------------------------------------------------------
    void t09_bitband() {
        group("T09 Bit-banding en SRAM y perifericos [IR, 5.4]");
        // Palabra base en SRAM1 y su alias
        const uint32_t base = addr::SRAM1_BASE + 0x400;
        dut->core.debug.ap_write32(base, 0x00000000u);
        // bit 5 del byte 0 -> alias = 0x2200 0000 + (0x400 * 32) + (5 * 4)
        const uint32_t alias = addr::BB_SRAM_ALIAS + (0x400u * 32u) + (5u * 4u);
        check(dut->core.debug.ap_write32(alias, 1u) == TLM_OK_RESPONSE,
              "escritura de un bit por el alias de bit-banding");
        uint32_t v = 0;
        dut->core.debug.ap_read32(base, v);
        check_eq(v, 0x00000020u, "la palabra base refleja el bit puesto a 1");
        dut->core.debug.ap_read32(alias, v);
        check_eq(v, 1u, "la lectura del alias devuelve el valor del bit");
        dut->core.debug.ap_write32(alias, 0u);
        dut->core.debug.ap_read32(base, v);
        check_eq(v, 0u, "la escritura de 0 por el alias limpia el bit");

        // La escritura por el alias no altera los bits vecinos
        dut->core.debug.ap_write32(base, 0xFFFFFFFFu);
        dut->core.debug.ap_write32(alias, 0u);
        dut->core.debug.ap_read32(base, v);
        check_eq(v, 0xFFFFFFDFu, "el resto de bits de la palabra no se altera");

        // Región de periféricos: la BKPSRAM (0x4002 4000) cae dentro del primer
        // MB del espacio de periféricos, así que sirve para verificar el alias
        // 0x4200 0000 sobre un bloque con estado observable.
        rcc_enable(Rcc::R_AHB1ENR, 18);                     // BKPSRAMEN
        const uint32_t pw = addr::BKPSRAM_BASE + 0x30;
        dut->core.debug.ap_write32(pw, 0x00000000u);
        const uint32_t alias_p = addr::BB_PERIPH_ALIAS +
                                 ((pw - addr::BB_PERIPH_BASE) * 32u) + (3u * 4u);
        check(dut->core.debug.ap_write32(alias_p, 1u) == TLM_OK_RESPONSE,
              "escritura por el alias de bit-banding de perifericos");
        dut->core.debug.ap_read32(pw, v);
        check_eq(v & 0xFFu, 0x8u, "bit-banding sobre la region de perifericos (BKPSRAM)");

        // Los maestros DMA no ven los alias: caen en rango reservado
        tm.master = BusMaster::DMA1_MEM;
        check(tm.read32(alias, v) == TLM_ADDRESS_ERROR_RESPONSE,
              "un maestro DMA hacia el alias de bit-banding -> error");
        tm.master = BusMaster::CORE_SBUS;
    }

    // -----------------------------------------------------------------------
    void t10_arbol_reloj() {
        group("T10 Arbol de reloj: HSE 8 MHz + PLL -> 168 MHz [IR, 4.3/4.4]");
        // El cristal externo se declara al modelo del oscilador (F3 anadira la
        // comprobacion electrica del pad).
        dut->rcc.hse.nominal_hz = 8e6;

        // 1) HSEON y espera de HSERDY
        uint32_t cr = 0;
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        tm.write32(addr::RCC_B + Rcc::R_CR, cr | (1u << 16));
        wait(3, SC_MS);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check((cr & (1u << 17)) != 0, "RCC_CR.HSERDY tras el arranque del HSE");

        // 2) Prescalers antes de subir la frecuencia: HPRE=/1, PPRE1=/4, PPRE2=/2
        tm.write32(addr::RCC_B + Rcc::R_CFGR, (5u << 10) | (4u << 13));

        // 3) PLL: M=8, N=336, P=2 (00), Q=7, PLLSRC=HSE
        const uint32_t pllcfgr = 8u | (336u << 6) | (0u << 16) | (1u << 22) | (7u << 24);
        tm.write32(addr::RCC_B + Rcc::R_PLLCFGR, pllcfgr);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        tm.write32(addr::RCC_B + Rcc::R_CR, cr | (1u << 24));       // PLLON
        wait(1, SC_MS);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check((cr & (1u << 25)) != 0, "RCC_CR.PLLRDY tras el enganche del PLL");
        check_near(dut->rcc.pll.vco_in_hz(), 1e6, 1e-6, "VCO de entrada = 1 MHz (rango 1-2)");
        check_near(dut->rcc.pll.vco_out_hz(), 336e6, 1e-6, "VCO de salida = 336 MHz");

        // 4) Estados de espera antes de conmutar (5 WS a 168 MHz)
        tm.write32(addr::FLASHIF_B + FlashIf::ACR, 5u | (1u << 8) | (1u << 9) | (1u << 10));

        // 5) SW = PLL
        uint32_t cfgr = 0;
        tm.read32(addr::RCC_B + Rcc::R_CFGR, cfgr);
        tm.write32(addr::RCC_B + Rcc::R_CFGR, (cfgr & ~3u) | 2u);
        wait(1, SC_US);
        tm.read32(addr::RCC_B + Rcc::R_CFGR, cfgr);
        check_eq((cfgr >> 2) & 3u, 2u, "RCC_CFGR.SWS indica que SYSCLK viene del PLL");
        check_near(dut->rcc.sysclk_hz(), 168e6, 1e-9, "SYSCLK = 168 MHz");
        check_near(dut->rcc.hclk_freq(),  168e6, 1e-9, "HCLK = 168 MHz");
        check_near(dut->rcc.pclk1_freq(),  42e6, 1e-9, "PCLK1 = 42 MHz (limite del APB1)");
        check_near(dut->rcc.pclk2_freq(),  84e6, 1e-9, "PCLK2 = 84 MHz (limite del APB2)");
        check_near(dut->rcc.pll48_freq(),  48e6, 1e-9, "PLL48CK = 48 MHz (PLLQ = 7)");
        check_near(dut->s_hclk_hz.read(),  168e6, 1e-9, "la senal hclk_hz publica 168 MHz");
        check_near(dut->s_timclk1_hz.read(), 84e6, 1e-9,
                   "TIMCLK1 = 2xPCLK1 con prescaler APB1 distinto de 1");

        // 6) Comprobacion del periodo real de la onda de HCLK
        wait(dut->s_hclk.posedge_event());
        const sc_time t0 = sc_time_stamp();
        for (unsigned i = 0; i < 168; ++i) wait(dut->s_hclk.posedge_event());
        const double medido = 168.0 / (sc_time_stamp() - t0).to_seconds();
        check_near(medido, 168e6, 1e-3, "frecuencia medida sobre los flancos de HCLK");

        // 7) No se puede apagar la fuente que alimenta SYSCLK [IR, 4.5.1]
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        tm.write32(addr::RCC_B + Rcc::R_CR, cr & ~(1u << 24));
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check((cr & (1u << 24)) != 0, "PLLON no se puede borrar mientras alimenta SYSCLK");

        // 8) RTCCLK desde el LSI
        tm.write32(addr::RCC_B + Rcc::R_CSR, 1u);            // LSION
        wait(200, SC_US);
        tm.write32(addr::RCC_B + Rcc::R_BDCR, (2u << 8) | (1u << 15));  // RTCSEL=LSI, RTCEN
        wait(1, SC_US);
        check_near(dut->rcc.rtc_freq(), 32e3, 1e-6, "RTCCLK = LSI 32 kHz");
    }

    // -----------------------------------------------------------------------
    void t11_puente_apb() {
        group("T11 Penalizacion del puente AHB->APB [IR, 6.4]");
        sc_time t_ahb, t_apb1, t_apb2;
        uint32_t v = 0;
        rcc_enable(Rcc::R_AHB1ENR, 0);                              // GPIOAEN
        rcc_enable(Rcc::R_APB1ENR, 28);                             // PWREN
        tm.access(false, addr::GPIOA_B, buf4_, 4, &t_ahb);          // AHB1 directo
        rcc_enable(Rcc::R_APB2ENR, 14);                             // SYSCFGEN
        tm.access(false, addr::SYSCFG_B, buf4_, 4, &t_apb2);        // APB2 (84 MHz)
        tm.access(false, addr::PWR_B, buf4_, 4, &t_apb1);           // APB1 (42 MHz)
        (void)v;
        std::printf("    AHB1 %s | APB2 %s | APB1 %s\n",
                    t_ahb.to_string().c_str(), t_apb2.to_string().c_str(),
                    t_apb1.to_string().c_str());
        check(t_apb1 > t_apb2 && t_apb2 > t_ahb,
              "el coste crece AHB1 < APB2 < APB1 (2 ciclos del PCLK correspondiente)");
        // Desde F3 cada esclavo anota su propio acceso en ciclos de SU dominio
        // (BusSlave::clk_hz), de modo que la diferencia observada entre un
        // periférico de APB1 y uno de AHB1 son los 2 ciclos del puente más la
        // diferencia entre un ciclo de PCLK1 y uno de HCLK [IR, §6.4, §4.4].
        const double dt = (t_apb1 - t_ahb).to_seconds();
        check_near(dt, 2.0 / 42e6 + (1.0 / 42e6 - 1.0 / 168e6), 1e-3,
                   "la penalizacion del puente APB1 son 2 ciclos PCLK1");
    }

    // -----------------------------------------------------------------------
    // Proceso paralelo que satura el puerto de SRAM1 para provocar contencion
    sc_event start_contention_, done_contention_;
    void contention_proc() {
        for (;;) {
            wait(start_contention_);
            for (unsigned i = 0; i < 200; ++i) {
                unsigned char d[4] = {1, 2, 3, 4};
                tm.access_as(BusMaster::DMA2_MEM, false, true,
                             addr::SRAM1_BASE + 0x800 + (i % 16) * 4, d, 4);
            }
            done_contention_.notify(SC_ZERO_TIME);
        }
    }

    void t12_contencion() {
        group("T12 Arbitraje y contencion en un puerto de esclavo [IR, 6.7]");
        const uint64_t antes = dut->matrix.n_contention;
        start_contention_.notify(SC_ZERO_TIME);
        for (unsigned i = 0; i < 200; ++i) tm.write32(addr::SRAM1_BASE + 0x900, i);
        wait(done_contention_);
        const uint64_t despues = dut->matrix.n_contention;
        std::printf("    transacciones con espera: %llu\n",
                    (unsigned long long)(despues - antes));
        check(despues > antes,
              "dos maestros sobre SRAM1 se serializan y el perdedor espera");
        check(dut->matrix.n_xfer[unsigned(BusMaster::DMA2_MEM)][unsigned(BusSlaveId::SRAM1)] > 0,
              "la matriz contabiliza las transferencias de DMA2 hacia SRAM1");
    }

    // -----------------------------------------------------------------------
    void pulse_nrst() {
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(50, SC_US);
        dut->pwr_pads.nrst.set_hiz(d_nrst);
        wait(300, SC_US);
    }

    void t13_reset_nrst() {
        group("T13 Reset por el pin NRST y flags de RCC_CSR [IR, 4.1/4.10]");
        // Estado antes del reset: SYSCLK del PLL y GPIOAEN activo
        rcc_enable(Rcc::R_AHB1ENR, 0);                      // GPIOAEN
        check(dut->s_pcen[P_GPIOA].read(), "GPIOAEN activo antes del reset");
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(5, SC_US);
        check(!dut->s_sysrst_n.read(), "NRST a nivel bajo activa el reset de sistema");
        dut->pwr_pads.nrst.set_hiz(d_nrst);
        wait(300, SC_US);
        check(dut->s_sysrst_n.read(), "el reset se libera al soltar NRST");
        check(!dut->s_pcen[P_GPIOA].read(), "RCC_AHB1ENR vuelve a su valor de reset");
        check_near(dut->rcc.sysclk_hz(), 16e6, 1e-6, "SYSCLK vuelve al HSI de 16 MHz");
        uint32_t csr = 0;
        tm.read32(addr::RCC_B + Rcc::R_CSR, csr);
        check((csr & (1u << 26)) != 0, "RCC_CSR.PINRSTF marca el reset por pin");
        tm.write32(addr::RCC_B + Rcc::R_CSR, (1u << 24));      // RMVF
        tm.read32(addr::RCC_B + Rcc::R_CSR, csr);
        check_eq(csr & 0xFE000000u, 0u, "RMVF limpia los flags de fuente de reset");
    }

    // -----------------------------------------------------------------------
    void t14_cargador_y_boot() {
        group("T14 Cargador de imagen y alias de arranque de 0x0 [IR, 2.3/5.1]");
        ImageLoader ld(*dut);
        check(ld.write_reset_vector(0x20020000u, 0x08000101u),
              "tabla de vectores minima escrita en la Flash");
        const uint8_t codigo[8] = {0xEF, 0xBE, 0xAD, 0xDE, 0x0D, 0xF0, 0xFE, 0xCA};
        check(ld.load_bytes(addr::FLASH_BASE + 0x100, codigo, 8),
              "imagen de firmware cargada en la Flash");
        check(ld.load_bytes(addr::SRAM1_BASE + 0x2000, codigo, 8),
              "imagen cargada tambien en SRAM1");

        tm.master = BusMaster::CORE_IBUS; tm.instr = true;
        check_eq(tm.rd32(addr::FLASH_BASE + 0x0), 0x20020000u,
                 "MSP inicial leido en 0x0800 0000");
        check_eq(tm.rd32(addr::FLASH_BASE + 0x4), 0x08000101u,
                 "vector de reset leido en 0x0800 0004");
        check_eq(tm.rd32(addr::FLASH_BASE + 0x100), 0xDEADBEEFu,
                 "primera palabra de la imagen");

        // Alias de 0x0000 0000 con BOOT0 = 0 (Flash principal)
        check_eq(dut->s_boot.read(), 0u, "BOOT[1:0] muestreado = 00 (Flash principal)");
        check_eq(dut->s_memmode.read(), uint8_t(MEM_MODE_FLASH),
                 "SYSCFG_MEMRMP.MEM_MODE = Flash principal");
        uint32_t v = 0;
        dut->core.debug.ap_read32(0x00000000u, v);
        check_eq(v, 0x20020000u, "0x0000 0000 refleja la Flash a traves del router");
        dut->core.debug.ap_read32(0x00000004u, v);
        check_eq(v, 0x08000101u, "0x0000 0004 refleja el vector de reset");

        // Remapeo a SRAM1 por software (SYSCFG_MEMRMP = 11)
        tm.master = BusMaster::CORE_SBUS; tm.instr = false;
        rcc_enable(Rcc::R_APB2ENR, 14);                     // SYSCFGEN
        tm.write32(addr::SYSCFG_B + Syscfg::MEMRMP, MEM_MODE_SRAM1);
        wait(SC_ZERO_TIME);
        check_eq(dut->s_memmode.read(), uint8_t(MEM_MODE_SRAM1),
                 "SYSCFG_MEMRMP conmuta el espejo a SRAM1");
        dut->core.debug.ap_read32(0x00002000u, v);
        check_eq(v, 0xDEADBEEFu, "0x0000 2000 refleja ahora la SRAM1");

        // Arranque desde la memoria de sistema con BOOT0 = 1
        tm.write32(addr::SYSCFG_B + Syscfg::MEMRMP, MEM_MODE_FLASH);
        dut->flash.poke_byte(addr::SYSMEM_BASE + 0, 0x5A);
        dut->pwr_pads.boot0.set_drive(d_bt0, 3.3f, 100.0f);   // BOOT0 = 1
        pulse_nrst();
        check_eq(dut->s_boot.read(), 1u, "BOOT[1:0] muestreado = 01 tras el reset");
        check_eq(dut->s_memmode.read(), uint8_t(MEM_MODE_SYSTEM),
                 "MEM_MODE arranca en System memory con BOOT0 = 1");
        dut->core.debug.ap_read32(0x00000000u, v);
        check_eq(v & 0xFFu, 0x5Au, "0x0000 0000 refleja la System memory (bootloader)");
    }

    // -----------------------------------------------------------------------
    // T17 — CoreMark (EEMBC) compilado para Cortex-M4F y ejecutado sobre el
    //       modelo. Es el criterio de salida de F2 del plan (§7).
    // -----------------------------------------------------------------------
    void t17_coremark() {
        group("T17 CoreMark sobre el modelo [criterio de salida de F2]");
        if (std::getenv("F2_SKIP_COREMARK")) {
            std::printf("    omitido (F2_SKIP_COREMARK)\n");
            return;
        }
        // Presupuesto de tiempo simulado y ruta de la imagen ajustables para
        // poder lanzar la ejecucion oficial de 10 s (ITERATIONS grande).
        if (const char* b = std::getenv("F2_CM_BUDGET_MS")) cm_budget_ms_ = std::atof(b);
        if (const char* p = std::getenv("F2_CM_BIN"))       cm_path_ = p;
        // Las ondas cuadradas de HCLK/PCLK no son observables en esta carga
        // (no hay pines ni temporizadores en juego) y su generación domina el
        // tiempo de simulación: se apagan y se deja solo la frecuencia.
        dut->rcc.set_internal_waveforms(false);
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(30, SC_US);
        ImageLoader ld(*dut);
        const long n = ld.load_file(cm_path_.c_str(), addr::FLASH_BASE);
        if (!check(n > 0, "imagen de CoreMark cargada en la Flash")) {
            std::printf("        (compilar con make -C verif/fw/coremark)\n");
            dut->pwr_pads.nrst.set_hiz(d_nrst);
            return;
        }
        std::printf("    %ld bytes cargados desde %s\n", n, cm_path_.c_str());
        const uint64_t i0 = dut->core.cpu.inst_count;
        if (std::getenv("F2_CM_FAULTS")) dut->core.cpu.fault_trace = 8;
        if (const char* t = std::getenv("F2_CM_TRACE")) {
            dut->core.cpu.trace_from  = i0 + uint64_t(std::atoll(t));
            dut->core.cpu.trace_limit = 120;
        }
        dut->pwr_pads.nrst.set_hiz(d_nrst);

        const sc_time t0 = sc_time_stamp();
        const std::clock_t w0 = std::clock();
        bool done = false;
        uint32_t last_len = 0;
        while ((sc_time_stamp() - t0) < sc_time(cm_budget_ms_, SC_MS)) {
            wait(2, SC_MS);
            if (std::getenv("F2_CM_PC"))
                std::printf("    t=%s PC=0x%08X inst=%llu\n",
                            sc_time_stamp().to_string().c_str(), dut->core.cpu.pc(),
                            (unsigned long long)(dut->core.cpu.inst_count - i0));
            const uint32_t len = dut->sram1.peek32(8);
            if (len != last_len) {                 // progreso de la consola
                last_len = len;
                std::printf("    ... %u caracteres de salida, %llu instrucciones\n",
                            len, (unsigned long long)(dut->core.cpu.inst_count - i0));
                std::fflush(stdout);
            }
            if (dut->sram1.peek32(0) == 1u) { done = true; break; }
        }
        const double wall = double(std::clock() - w0) / CLOCKS_PER_SEC;
        const uint64_t ninst = dut->core.cpu.inst_count - i0;
        const double sim_s = (sc_time_stamp() - t0).to_seconds();

        if (!done)
            std::printf("        sin terminar: PC = 0x%08X, LR = 0x%08X, SP = 0x%08X\n",
                        dut->core.cpu.pc(), dut->core.cpu.reg.r[14],
                        dut->core.cpu.reg.r[13]);
        check(done, "CoreMark termina y publica su informe");
        // Volcado de la consola virtual del firmware
        const uint32_t magic = dut->sram1.peek32(4);
        const uint32_t len   = dut->sram1.peek32(8);
        if (magic == 0x434F4E53u && len > 0 && len < 4096) {
            std::printf("    --- salida de CoreMark ---\n");
            std::string s;
            for (uint32_t i = 0; i < len; ++i) s += char(dut->sram1.peek8(12 + i));
            std::printf("%s", s.c_str());
            if (s.empty() || s.back() != '\n') std::printf("\n");
            std::printf("    --------------------------\n");
            // CoreMark valida su propia ejecución comparando cuatro CRC con
            // los valores canónicos de la ejecución «2K performance run»
            // (seedcrc 0xe9f5).  Si cualquiera de ellos no coincide, la carga
            // de trabajo se ha ejecutado mal en algún punto.
            // crclist/crcmatrix/crcstate son los CRC de cada carga de trabajo y
            // no dependen del numero de iteraciones; crcfinal si (acumula), por
            // lo que solo se compara en la ejecucion de una iteracion.
            const bool crc_ok =
                s.find("seedcrc          : 0xe9f5") != std::string::npos &&
                s.find("[0]crclist       : 0xe714") != std::string::npos &&
                s.find("[0]crcmatrix     : 0x1fd7") != std::string::npos &&
                s.find("[0]crcstate      : 0x8e3a") != std::string::npos &&
                (s.find("Iterations       : 1\n") == std::string::npos ||
                 s.find("[0]crcfinal      : 0xe714") != std::string::npos);
            check(crc_ok, "CoreMark valida su propio resultado (los CRC de las "
                          "cargas coinciden con los canonicos del 2K performance run)");
            // La regla de «al menos 10 s» de EEMBC es un requisito para poder
            // publicar la puntuacion, no un criterio de correccion: con
            // ITERATIONS=1 el mensaje de error es el esperado.
            if (s.find("Correct operation validated") != std::string::npos)
                std::printf("    (ejecucion oficial: CoreMark valida y publica puntuacion)\n");
        } else {
            check(false, "la consola del firmware contiene la salida de CoreMark");
        }
        std::printf("    %llu instrucciones en %.3f s simulados (%.2f MIPS simuladas)\n",
                    (unsigned long long)ninst, sim_s,
                    sim_s > 0 ? double(ninst) / sim_s / 1e6 : 0.0);
        std::printf("    %.1f s de CPU del anfitrion -> %.0f instrucciones/s de modelo\n",
                    wall, wall > 0 ? double(ninst) / wall : 0.0);
        dut->rcc.set_internal_waveforms(true);
    }


    // =======================================================================
    // FASE F3 — Pines, GPIO y RCC eléctrico
    // =======================================================================

    // Reset limpio del MCU dejando el núcleo aparcado en un bucle wfi.
    void reset_dut() {
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(30, SC_US);
        park_cpu();
        dut->pwr_pads.nrst.set_hiz(d_nrst);
        wait(300, SC_US);
    }

    // Acceso a un registro de un puerto GPIO desde el maestro de prueba.
    uint32_t gpio_rd(unsigned port, uint32_t off) {
        uint32_t v = 0;
        tm.read32(addr::GPIOA_B + 0x400u * port + off, v);
        return v;
    }
    void gpio_wr(unsigned port, uint32_t off, uint32_t v) {
        tm.write32(addr::GPIOA_B + 0x400u * port + off, v);
    }
    // Configura un pin: mode 0=in 1=out 2=af 3=analog; pupd 0/1/2; od; speed.
    void pin_cfg(unsigned port, unsigned pin, unsigned mode, unsigned pupd = 0,
                 bool od = false, unsigned speed = 0, unsigned af = 0) {
        const unsigned sh2 = 2 * pin;
        gpio_wr(port, 0x00, (gpio_rd(port, 0x00) & ~(3u << sh2)) | (mode << sh2));
        gpio_wr(port, 0x0C, (gpio_rd(port, 0x0C) & ~(3u << sh2)) | (pupd << sh2));
        gpio_wr(port, 0x08, (gpio_rd(port, 0x08) & ~(3u << sh2)) | (speed << sh2));
        gpio_wr(port, 0x04, (gpio_rd(port, 0x04) & ~(1u << pin)) | (od ? (1u << pin) : 0u));
        if (pin < 8) gpio_wr(port, 0x20,
                             (gpio_rd(port, 0x20) & ~(0xFu << (4 * pin))) | (af << (4 * pin)));
        else         gpio_wr(port, 0x24,
                             (gpio_rd(port, 0x24) & ~(0xFu << (4 * (pin - 8)))) | (af << (4 * (pin - 8))));
        wait(1, SC_US);
    }

    // -----------------------------------------------------------------------
    // T18 — El pad como frontera eléctrica: alta impedancia, pull internos,
    //       divisores con circuitería externa, push-pull / open-drain,
    //       trigger Schmitt con histéresis, rango absoluto y corriente.
    // -----------------------------------------------------------------------
    void t18_pad_electrico() {
        group("T18 Pad: modelo electrico del pin [IR, 2.4, 3.3, 3.5]");
        reset_dut();
        rcc_enable(Rcc::R_AHB1ENR, 4);              // GPIOEEN
        const unsigned PE = 4, PIN = 2;             // PE2: sin funcion especial
        Pad& pad = *dut->pinmux.pad[PE][PIN];
        const unsigned k = PE * N_PORT_PINS + PIN;

        // --- Entrada sin pull: el pin queda flotante ------------------------
        pin_cfg(PE, PIN, 0, 0);
        check(pad.is_floating(), "entrada sin pull: el nodo queda en alta impedancia");
        check(!dut->pinmux.pad_din_ok[k].read(),
              "un pin flotante no entrega un nivel logico valido");

        // --- Pull-up y pull-down internos de 40 kohm ------------------------
        pin_cfg(PE, PIN, 0, 1);
        check_near(pad.voltage(), 3.3, 0.02, "pull-up interno lleva el pin a VDD");
        check(dut->pinmux.pad_din[k].read(), "con pull-up el Schmitt entrega 1");
        pin_cfg(PE, PIN, 0, 2);
        check(pad.voltage() < 0.1, "pull-down interno lleva el pin a VSS");
        check(!dut->pinmux.pad_din[k].read(), "con pull-down el Schmitt entrega 0");

        // --- Divisor con una resistencia externa: zona no garantizada -------
        // 40 kohm externos a VDD contra el pull-down interno de 40 kohm dan
        // VDD/2, que cae entre VIL y VIH: el nivel no esta garantizado.
        {
            Resistor r_ext(dut->pinmux.analog(PE, PIN), 3.3, 40e3);
            wait(1, SC_US);
            check_near(pad.voltage(), 1.65, 0.05,
                       "divisor 40k/40k resuelto por el nodo analogico");
            check(!dut->pinmux.pad_din_ok[k].read(),
                  "tension entre VIL y VIH: nivel no garantizado");
            // Histéresis: el Schmitt conserva el ultimo nivel dentro de la banda
            check(!dut->pinmux.pad_din[k].read(),
                  "el trigger Schmitt mantiene el nivel anterior (histeresis)");
        }
        wait(1, SC_US);

        // --- Salida push-pull contra una carga ------------------------------
        {
            Resistor carga(dut->pinmux.analog(PE, PIN), 0.0, 1000.0);  // 1k a VSS
            pin_cfg(PE, PIN, 1, 0, false, 0);
            gpio_wr(PE, 0x18, 1u << PIN);                              // BSRR set
            wait(1, SC_US);
            check_near(pad.voltage(), 3.3 * 1000.0 / (1000.0 + 55.0), 0.02,
                       "push-pull a 1: divisor Ron/carga");
            check_near(std::fabs(double(pad.current())), 3.3 / 1055.0, 0.05,
                       "corriente entregada por el pad [A]");
            gpio_wr(PE, 0x18, 1u << (PIN + 16));                       // BSRR reset
            wait(1, SC_US);
            check(pad.voltage() < 0.1, "push-pull a 0 absorbe la carga");

            // --- Open-drain: el '1' es alta impedancia ---------------------
            pin_cfg(PE, PIN, 1, 0, true, 0);
            gpio_wr(PE, 0x18, 1u << PIN);
            wait(1, SC_US);
            check(pad.voltage() < 0.1,
                  "open-drain a 1 con carga a VSS: el pin lo fija la carga");
        }
        // Open-drain a 1 con pull-up externo: sube a VDD
        {
            Resistor pu(dut->pinmux.analog(PE, PIN), 3.3, 4700.0);
            wait(1, SC_US);
            check_near(pad.voltage(), 3.3, 0.02,
                       "open-drain a 1 con pull-up externo: el pin sube a VDD");
            gpio_wr(PE, 0x18, 1u << (PIN + 16));
            wait(1, SC_US);
            check(pad.voltage() < 0.1, "open-drain a 0 conduce contra el pull-up");
        }
        wait(1, SC_US);

        // --- Vigilancia de corriente máxima por pin [IR, 3.2] ---------------
        {
            pin_cfg(PE, PIN, 1, 0, false, 3);
            gpio_wr(PE, 0x18, 1u << PIN);
            Resistor corto(dut->pinmux.analog(PE, PIN), 0.0, 0.5);
            wait(1, SC_US);
            check(pad.overcurrent(), "un cortocircuito a VSS supera los 25 mA");
        }
        wait(1, SC_US);

        // --- Rango absoluto: tolerancia a 5 V en pines FT -------------------
        {
            pin_cfg(PE, PIN, 0, 0);
            Driver ext(dut->pinmux.analog(PE, PIN));
            ext.set_volts(5.0, 50.0);
            wait(1, SC_US);
            check(!dut->pinmux.pad_oor[k].read(),
                  "5 V en un pin FT esta dentro del rango absoluto");
            ext.set_volts(6.0, 50.0);
            wait(1, SC_US);
            check(dut->pinmux.pad_oor[k].read(),
                  "6 V supera el rango absoluto incluso en un pin FT");
            ext.set_volts(1.65, 50.0);

            // --- Modo analógico: Schmitt y pulls desconectados --------------
            pin_cfg(PE, PIN, 3, 1);            // analogico con PUPDR = pull-up
            wait(1, SC_US);
            check_near(pad.voltage(), 1.65, 0.02,
                       "en modo analogico el pull-up interno queda desconectado");
            check(!dut->pinmux.pad_din[k].read(),
                  "en modo analogico la entrada digital lee 0 [IR, 3.3.4]");
        }
        wait(1, SC_US);
        pin_cfg(PE, PIN, 0, 0);
    }

    // -----------------------------------------------------------------------
    // T19 — Banco de registros del puerto GPIO.
    // -----------------------------------------------------------------------
    void t19_gpio_registros() {
        group("T19 Puerto GPIO: registros, BSRR y LCKR [IR, 3.4]");
        reset_dut();
        rcc_enable(Rcc::R_AHB1ENR, 0);              // GPIOAEN
        rcc_enable(Rcc::R_AHB1ENR, 1);              // GPIOBEN
        rcc_enable(Rcc::R_AHB1ENR, 4);              // GPIOEEN

        // --- Valores de reset especiales de los pines de depuración ---------
        check_eq(gpio_rd(0, 0x00), 0xA8000000u, "GPIOA_MODER de reset (PA15:13 en AF)");
        check_eq(gpio_rd(0, 0x08), 0x0C000000u, "GPIOA_OSPEEDR de reset (PA13 very high)");
        check_eq(gpio_rd(0, 0x0C), 0x64000000u, "GPIOA_PUPDR de reset (PA15 PU, PA14 PD)");
        check_eq(gpio_rd(1, 0x00), 0x00000280u, "GPIOB_MODER de reset (PB4:3 en AF)");
        check_eq(gpio_rd(1, 0x0C), 0x00000100u, "GPIOB_PUPDR de reset (PB4 PU)");
        check_eq(gpio_rd(4, 0x00), 0x00000000u, "GPIOE_MODER de reset");

        // --- ODR y BSRR: escritura atómica, BS gana a BR --------------------
        gpio_wr(4, 0x14, 0x00005A5Au);
        check_eq(gpio_rd(4, 0x14), 0x00005A5Au, "ODR conserva los 16 bits bajos");
        check_eq(gpio_rd(4, 0x14) >> 16, 0u, "ODR[31:16] esta reservado a 0");
        gpio_wr(4, 0x18, 0x00000005u);                        // BS0, BS2
        check_eq(gpio_rd(4, 0x14), 0x00005A5Fu, "BSRR pone bits sin leer-modificar-escribir");
        gpio_wr(4, 0x18, 0x00050000u);                        // BR0, BR2
        check_eq(gpio_rd(4, 0x14), 0x00005A5Au, "BSRR borra bits");
        gpio_wr(4, 0x18, 0x00010001u);                        // BS0 y BR0 a la vez
        check_eq(gpio_rd(4, 0x14) & 1u, 1u, "con BS y BR simultaneos gana BS [IR, 3.4.7]");
        check_eq(gpio_rd(4, 0x18), 0u, "BSRR es de solo escritura (lee 0)");

        // --- Secuencia de bloqueo del LCKR ---------------------------------
        gpio_wr(4, 0x00, 0x00000001u);                        // PE0 salida
        const uint32_t lck = 0x00000001u;                     // bloquear PE0
        gpio_wr(4, 0x1C, lck | (1u << 16));
        gpio_wr(4, 0x1C, lck);
        gpio_wr(4, 0x1C, lck | (1u << 16));
        check_eq(gpio_rd(4, 0x1C), lck | (1u << 16), "LCKK activo tras la secuencia 1-0-1");
        gpio_wr(4, 0x00, 0x00000002u);                        // intenta PE0 entrada
        check_eq(gpio_rd(4, 0x00) & 3u, 1u, "MODER del pin bloqueado no cambia");
        gpio_wr(4, 0x00, (gpio_rd(4, 0x00) & ~0xCu) | 0x4u);  // PE1 sí puede
        check_eq((gpio_rd(4, 0x00) >> 2) & 3u, 1u, "un pin no bloqueado sigue siendo configurable");
        // Una secuencia incorrecta no bloquea (se comprueba en otro puerto)
        gpio_wr(1, 0x1C, 0x00000100u | (1u << 16));
        gpio_wr(1, 0x1C, 0x00000200u);                        // LCK distinto
        gpio_wr(1, 0x1C, 0x00000100u | (1u << 16));
        check_eq((gpio_rd(1, 0x1C) >> 16) & 1u, 0u,
                 "una secuencia LCKR incorrecta no activa el bloqueo");

        // --- IDR: muestreo del pin -----------------------------------------
        {
            const unsigned PE = 4, PIN = 5;
            pin_cfg(PE, PIN, 0, 0);
            Driver ext(dut->pinmux.analog(PE, PIN));
            ext.set(true);
            wait(2, SC_US);
            check_eq((gpio_rd(PE, 0x10) >> PIN) & 1u, 1u, "IDR refleja el nivel alto del pin");
            ext.set(false);
            wait(2, SC_US);
            check_eq((gpio_rd(PE, 0x10) >> PIN) & 1u, 0u, "IDR refleja el nivel bajo del pin");
            ext.set(true);
            wait(2, SC_US);
            pin_cfg(PE, PIN, 3, 0);                            // modo analogico
            wait(2, SC_US);
            check_eq((gpio_rd(PE, 0x10) >> PIN) & 1u, 0u,
                     "en modo analogico el IDR lee 0 [IR, 3.3.4]");
        }
        check_eq(gpio_rd(4, 0x10) >> 16, 0u, "IDR[31:16] esta reservado a 0");
    }

    // -----------------------------------------------------------------------
    // T20 — Multiplexor de funciones alternativas.
    // -----------------------------------------------------------------------
    void t20_mux_af() {
        group("T20 Multiplexor de funciones alternativas [IR, 3.3.3, 2.1]");
        reset_dut();
        rcc_enable(Rcc::R_AHB1ENR, 0);              // GPIOAEN
        rcc_enable(Rcc::R_AHB1ENR, 4);              // GPIOEEN

        // PA13 arranca en AF0 (SWDIO) con pull-up: el pad es entrada y el
        // pull-up interno lo lleva a VDD.
        Pad& pa13 = *dut->pinmux.pad[0][13];
        check_near(pa13.voltage(), 3.3, 0.02, "PA13 (SWDIO) arranca en AF0 con pull-up");
        check_eq(dut->pinmux.af_of(0, 13), 0u, "el mux ve PA13 en AF0 tras el reset");

        // Al reconfigurar PA13 como GPIO de salida, el puerto de depuración
        // deja de gobernar el pin, igual que en el silicio.
        pin_cfg(0, 13, 1, 0);
        gpio_wr(0, 0x18, 1u << (13 + 16));          // ODR13 = 0
        wait(2, SC_US);
        check(pa13.voltage() < 0.1, "PA13 como salida GPIO deja de ser SWDIO");
        check_eq(dut->pinmux.af_of(0, 13), 0xFFu, "el mux marca PA13 fuera de modo AF");

        // Una AF no modelada deja el pin en alta impedancia: el periférico no
        // existe todavía, pero el GPIO ya no gobierna el pad.
        pin_cfg(4, 3, 2, 0, false, 0, 9);           // PE3 en AF9 (no registrada)
        wait(2, SC_US);
        check(dut->pinmux.pad[4][3]->is_floating(),
              "una AF sin periferico modelado deja el pin en alta impedancia");

        // EVENTOUT (AF15) sí está registrada en todos los pines: el pad pasa a
        // estar gobernado por la salida de evento del núcleo.
        pin_cfg(4, 3, 2, 0, false, 0, 15);
        wait(2, SC_US);
        check(!dut->pinmux.pad[4][3]->is_floating(),
              "EVENTOUT (AF15) gobierna el pin en cualquier puerto");
        pin_cfg(4, 3, 0, 0);
    }

    // -----------------------------------------------------------------------
    // T21 — HSE en modo bypass: reloj externo inyectado por OSC_IN.
    // -----------------------------------------------------------------------
    void t21_hse_bypass() {
        group("T21 HSE: presencia del cristal y modo bypass [IR, 4.2]");
        reset_dut();

        // Sin cristal, HSEON no llega nunca a HSERDY
        xtal_hse->detach();
        wait(1, SC_US);
        tm.write32(addr::RCC_B + Rcc::R_CR, 0x00010001u);      // HSEON | HSION
        wait(4, SC_MS);
        uint32_t cr = 0; tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check_eq((cr >> 17) & 1u, 0u, "sin cristal en OSC_IN, HSERDY no se activa");

        // Reloj externo de 12 MHz en modo bypass (HSEBYP)
        osc_ext->set_freq(12e6);
        tm.write32(addr::RCC_B + Rcc::R_CR, 0x00050001u);      // HSEBYP | HSEON
        wait(4, SC_MS);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check_eq((cr >> 17) & 1u, 1u, "con reloj externo en bypass, HSERDY se activa");
        check_near(dut->rcc.hse.out_hz(), 12e6, 0.02,
                   "el HSE mide la frecuencia del reloj externo inyectado");
        osc_ext->stop();
        wait(10, SC_US);
        // Al retirar el reloj el nodo vuelve a alta impedancia y el HSE cae
        wait(20, SC_US);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check_eq((cr >> 17) & 1u, 0u, "al retirar el reloj externo, HSERDY se apaga");
        xtal_hse->attach();
    }

    // -----------------------------------------------------------------------
    // T22 — Clock Security System: fallo del HSE -> HSI + NMI.
    // -----------------------------------------------------------------------
    void t22_css() {
        group("T22 Clock Security System [IR, 4.2]");
        reset_dut();
        xtal_hse->attach();
        wait(1, SC_US);

        // HSE + CSSON y SYSCLK conmutado al HSE
        tm.write32(addr::RCC_B + Rcc::R_CR, 0x00090001u);      // CSSON|HSEON|HSION
        wait(3, SC_MS);
        uint32_t cr = 0; tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        check_eq((cr >> 17) & 1u, 1u, "HSERDY con el cristal presente");
        tm.write32(addr::RCC_B + Rcc::R_CFGR, 0x00000001u);    // SW = HSE
        wait(10, SC_US);
        uint32_t cfgr = 0; tm.read32(addr::RCC_B + Rcc::R_CFGR, cfgr);
        check_eq((cfgr >> 2) & 3u, 1u, "SWS indica que SYSCLK viene del HSE");

        // Se rompe el cristal
        const uint64_t exc0 = dut->core.cpu.exc_count;
        xtal_hse->detach();
        wait(50, SC_US);
        tm.read32(addr::RCC_B + Rcc::R_CR, cr);
        tm.read32(addr::RCC_B + Rcc::R_CFGR, cfgr);
        uint32_t cir = 0; tm.read32(addr::RCC_B + Rcc::R_CIR, cir);
        check_eq((cir >> 7) & 1u, 1u, "CSSF se activa al fallar el HSE");
        check_eq((cr >> 16) & 1u, 0u, "el CSS apaga HSEON");
        check_eq((cfgr >> 2) & 3u, 0u, "SYSCLK conmuta automaticamente al HSI");
        check_near(dut->s_hclk_hz.read(), 16e6, 0.01, "HCLK vuelve a 16 MHz (HSI)");
        check(dut->core.cpu.exc_count > exc0, "el CSS genera una NMI en el nucleo");

        // CSSC limpia el flag
        tm.write32(addr::RCC_B + Rcc::R_CIR, 1u << 23);
        wait(2, SC_US);
        tm.read32(addr::RCC_B + Rcc::R_CIR, cir);
        check_eq((cir >> 7) & 1u, 0u, "CSSC borra CSSF");
        xtal_hse->attach();
    }

    // -----------------------------------------------------------------------
    // T23 — Salidas de reloj MCO1 (PA8) y MCO2 (PC9).
    // -----------------------------------------------------------------------
    void t23_mco() {
        group("T23 Salidas de reloj MCO1/MCO2 en sus pines [IR, 4.5.3, 2.1]");
        reset_dut();
        rcc_enable(Rcc::R_AHB1ENR, 0);              // GPIOAEN
        // MCO1 = HSI con prescaler /4 -> 4 MHz en PA8 (AF0)
        tm.write32(addr::RCC_B + Rcc::R_CFGR, (0u << 21) | (6u << 24));
        pin_cfg(0, 8, 2, 0, false, 3, 0);           // PA8 en AF0, very high speed
        wait(5, SC_US);

        // Cuenta de flancos de subida en el pad durante una ventana conocida
        const unsigned k = 0 * N_PORT_PINS + 8;
        unsigned edges = 0;
        const sc_time t0 = sc_time_stamp();
        const sc_time win(20, SC_US);
        while (sc_time_stamp() - t0 < win) {
            wait(win, dut->pinmux.pad_din[k].posedge_event());
            if (dut->pinmux.pad_din[k].read()) ++edges;
        }
        const double f = double(edges) / (sc_time_stamp() - t0).to_seconds();
        check_near(f, 4e6, 0.10, "MCO1 = HSI/4 medido en el pin PA8 [Hz]");
        check(!dut->pinmux.pad[0][8]->is_floating(),
              "PA8 en AF0 lo gobierna el generador de MCO1");
        pin_cfg(0, 8, 0, 0);
    }

    // -----------------------------------------------------------------------
    // T24 — Supervisión de alimentación: POR/PDR y BOR programable.
    // -----------------------------------------------------------------------
    void t24_bor() {
        group("T24 Supervision de alimentacion POR/PDR/BOR [IR, 2.2, 5.7.1, 4.10]");
        reset_dut();
        // Tras el reset, RCC_CSR conserva los flags de arranque
        tm.write32(addr::RCC_B + Rcc::R_CSR, 1u << 24);        // RMVF
        wait(2, SC_US);

        // Programar BOR_LEV = 10 (nivel 1, ~2.1 V) en los option bytes
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY2);
        uint32_t optcr = 0; tm.read32(addr::FLASHIF_B + FlashIf::OPTCR, optcr);
        // OPTSTRT lanza la programación: los option bytes son no volátiles y
        // el nivel programado sobrevive al reset que él mismo va a provocar.
        tm.write32(addr::FLASHIF_B + FlashIf::OPTCR, (optcr & ~0xCu) | 0x8u | 0x2u);
        wait(5, SC_US);
        check_near(dut->pwr_pads.trip_level(), 2.10, 0.01,
                   "el umbral de caida sigue a OPTCR.BOR_LEV [V]");

        // Bajar VDD por debajo del nivel de BOR provoca reset con BORRSTF
        dut->pwr_pads.vdd.set_drive(d_vdd, 2.0f, 0.1f);
        wait(50, SC_US);
        check(!dut->s_por_ok.read(), "VDD por debajo del nivel de BOR: reset de alimentacion");
        check(dut->s_bor_trip.read(), "la caida la atribuye el modelo al BOR, no al POR");
        dut->pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);
        wait(500, SC_US);
        check(dut->s_por_ok.read(), "al restablecer VDD el supervisor libera el reset");
        uint32_t csr = 0; tm.read32(addr::RCC_B + Rcc::R_CSR, csr);
        check_eq((csr >> 25) & 1u, 1u, "RCC_CSR.BORRSTF marca la causa del reset");
        check_eq((csr >> 27) & 1u, 0u, "PORRSTF no se activa cuando actua el BOR");

        // Con el BOR desactivado (BOR_LEV = 11) manda el umbral POR/PDR
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY1);
        tm.write32(addr::FLASHIF_B + FlashIf::OPTKEYR, FlashIf::OPTKEY2);
        tm.read32(addr::FLASHIF_B + FlashIf::OPTCR, optcr);
        tm.write32(addr::FLASHIF_B + FlashIf::OPTCR, optcr | 0xCu | 0x2u);
        wait(5, SC_US);
        check_near(dut->pwr_pads.trip_level(), 1.68, 0.01,
                   "con BOR desactivado el umbral es el del PDR [V]");
        dut->pwr_pads.vdd.set_drive(d_vdd, 2.0f, 0.1f);
        wait(50, SC_US);
        check(dut->s_por_ok.read(), "con el BOR apagado, 2.0 V no provoca reset");
        dut->pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);
        wait(50, SC_US);
    }

    // -----------------------------------------------------------------------
    // T25 — Blinky compilado con CMSIS: criterio de salida de la fase F3.
    // -----------------------------------------------------------------------
    void t25_blinky_cmsis() {
        group("T25 Blinky con CMSIS sobre el modelo [criterio de salida de F3]");
        // A 168 MHz la generación de la onda cuadrada de HCLK domina el coste
        // de simulación y aquí no la observa nadie: el LED se mueve por el
        // camino GPIO -> pad -> nodo analógico, y el IDR se muestrea con la
        // frecuencia del dominio (véase clock_gen.h y gpio_port.h).
        dut->rcc.set_internal_waveforms(false);
        xtal_hse->attach();
        dut->pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(30, SC_US);

        ImageLoader ld(*dut);
        const long n = ld.load_file(blinky_path_.c_str(), addr::FLASH_BASE);
        if (!check(n > 0, "imagen del blinky cargada en la Flash")) {
            std::printf("        (compilar con make -C verif/fw/blinky)\n");
            dut->pwr_pads.nrst.set_hiz(d_nrst);
            return;
        }
        std::printf("    %ld bytes cargados desde %s\n", n, blinky_path_.c_str());
        // El buzón está en una sección NOLOAD: se limpia para no leer restos de
        // la carga anterior (CoreMark deja su propia estructura ahí).
        for (unsigned i = 0; i < 32; i += 4) ld.poke32(addr::SRAM1_BASE + i, 0);
        const uint64_t i0 = dut->core.cpu.inst_count;
        if (std::getenv("F3_BLINKY_FAULTS")) dut->core.cpu.fault_trace = 8;
        dut->pwr_pads.nrst.set_hiz(d_nrst);

        // Observación del LED conectado a PD12 mientras corre el firmware
        unsigned led_on_count = 0;
        bool prev_on = false, done = false;
        const sc_time t0 = sc_time_stamp();
        while ((sc_time_stamp() - t0) < sc_time(2000, SC_MS)) {
            wait(200, SC_US);
            if (std::getenv("F3_BLINKY_PC"))
                std::printf("    t=%s PC=0x%08X inst=%llu led=%d\n",
                            sc_time_stamp().to_string().c_str(), dut->core.cpu.pc(),
                            (unsigned long long)(dut->core.cpu.inst_count - i0),
                            led_pd12->on() ? 1 : 0);
            if (led_pd12->on() != prev_on) { prev_on = !prev_on; if (prev_on) ++led_on_count; }
            if (dut->sram1.peek32(0) == 1u) { done = true; break; }
        }
        const uint64_t ninst = dut->core.cpu.inst_count - i0;
        if (!done)
            std::printf("        sin terminar: PC = 0x%08X, SP = 0x%08X, inst = %llu\n",
                        dut->core.cpu.pc(), dut->core.cpu.reg.r[13],
                        (unsigned long long)ninst);
        check(done, "el blinky llega a su fin y publica el buzon");

        const uint32_t sysclk  = dut->sram1.peek32(4);
        const uint32_t toggles = dut->sram1.peek32(8);
        const uint32_t button  = dut->sram1.peek32(12);
        const uint32_t ticks   = dut->sram1.peek32(16);
        std::printf("    SystemCoreClock = %u Hz | conmutaciones = %u | ticks = %u\n",
                    sysclk, toggles, ticks);
        std::printf("    %llu instrucciones | encendidos del LED observados = %u\n",
                    (unsigned long long)ninst, led_on_count);
        check_eq(sysclk, 168000000u,
                 "el firmware calcula SystemCoreClock = 168 MHz con CMSIS");
        check_near(dut->s_hclk_hz.read(), 168e6, 0.001, "HCLK del modelo = 168 MHz");
        check_near(dut->s_pclk1_hz.read(), 42e6, 0.001, "PCLK1 = 42 MHz");
        check_near(dut->s_pclk2_hz.read(), 84e6, 0.001, "PCLK2 = 84 MHz");
        check_eq(toggles, 6u, "el firmware ejecuta las 6 conmutaciones previstas");
        check(ticks >= 500u, "el SysTick de CMSIS entrega al menos 500 interrupciones");
        check(led_on_count >= 3u, "el LED de PD12 se enciende y se apaga en el pin");
        check(!led_pd12->on(), "el LED queda apagado al terminar el firmware");
        check_eq(button, 0u, "PA0 con pull-down interno se lee a 0 con el pulsador libre");

        // Con el pulsador cerrado a VSS el nivel sigue siendo 0; se comprueba el
        // camino de entrada forzando el pin desde fuera.
        {
            Driver ext(dut->pinmux.analog(0, 0));
            ext.set(true);
            wait(50, SC_US);
            uint32_t idr = 0;
            tm.read32(addr::GPIOA_B + 0x10, idr);
            check_eq(idr & 1u, 1u, "un nivel alto externo en PA0 llega al IDR");
        }
        dut->rcc.set_internal_waveforms(true);
    }

    std::string blinky_path_ = "verif/fw/blinky/blinky.bin";
    std::string fw_path_ = "verif/fw/test_isa.bin";
    std::string cm_path_ = "verif/fw/coremark/coremark.bin";
    double      cm_budget_ms_ = 20000.0;

private:
    unsigned char buf4_[4] = {0, 0, 0, 0};
};

// ---------------------------------------------------------------------------
int sc_main(int argc, char** argv) {
    // Los avisos del modelo (limites de frecuencia, latencia de Flash, rangos
    // del VCO) se silencian porque la propia suite provoca esas situaciones a
    // proposito durante la reconfiguracion del arbol de reloj.
    sc_report_handler::set_actions("rcc", SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("flash", SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("pll", SC_WARNING, SC_DO_NOTHING);

    F1Tb tb("tb");
    // Argumento opcional: imagen de firmware alternativa (.bin o .hex)
    if (argc > 1) tb.fw_path_ = argv[1];
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    return (g_fail == 0) ? 0 : 1;
}
