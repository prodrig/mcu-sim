// =============================================================================
// sc_main_rv.cpp — LA SUITE DEL NÚCLEO RISC-V (fase E1 de
// doc/analisis_esp32_riscv.md)
//
// Un banco aparte, como el de cada chip. Prueba el núcleo genérico (src/rv)
// sobre el SoC de laboratorio (rv/lab/soc_lab_rv.h), sin ningún periférico de
// Espressif: si algo falla aquí, es del núcleo.
//
//   V1-V3   sin tiempo simulado: la expansión de las comprimidas contra el
//           ensamblador de GNU (verif/rv/vectores_c.inc, con las reservadas),
//           misa de las dos configuraciones y los ELF versionados
//   V4      riscv-tests (rv32ui/um/ua/uc/mi, entorno -p) en RV32IMAC, cada
//           uno una comprobación; ma_data aparte, en V6
//   V5      los mismos en RV32IMC, menos ua, y ua tiene que fallar
//   V6      los accesos desalineados en hardware: ma_data con
//           RvDesalineado::Hardware y con HardwareCpu863
//   V7      las pruebas propias del núcleo (verif/rv/propias): excepciones,
//           modo U con PMP, mtvec, triggers, interrupciones del CLINT con wfi,
//           la errata CPU-863 y las extensiones del fabricante
//   V8      lockstep con Spike: la cuenta y la huella de la traza de
//           instrucciones retiradas de seis ELF (verif/rv/trazas_spike.inc),
//           uno de ellos con interrupciones del CLINT (irq_spike.S)
//   V9      CoreMark, validado por su propia CRC
//
//   (E2) la depuración genérica, en el laboratorio (rv/debug, rv/gdb):
//   W1      el DTM por los pines, con la sonda de bancada: IDCODE, dtmcs y el
//           DM por el DMI; lo mismo que en directo, y lo que cuesta
//   W2      el DM en directo: comandos abstractos y sus errores, program
//           buffer, abstractauto, SBA y sus errores, la memoria por el program
//           buffer, los triggers descubiertos, ndmreset con resethaltreq
//   W3      dos harts: hartsel, haltsum0, parar uno sin el otro, el disparo
//           cruzado y «unavail»
//   W4-W5   GDB por G1 (el DM en directo) y por G2' (el DTM por los pines),
//           con la misma sesión y los mismos resultados: parar, registros,
//           memoria, ebreak, trigger, watchpoint, paso, monitor reset halt y
//           un programa cargado en la RAM
//   W6      GDB con dos harts: dos hilos
//   W4-W6 van por un socket de verdad: van al final, y su tiempo no cuenta
//   en «el resto» (como F19 en testf3)
//
// Uso: testrv [carpeta con los riscv-tests]   (por omisión, build/rvt; la
// rellena `make testrv` desde verif/rv/elf/riscv-tests.tar.xz)
//      RV_TRAZA=carpeta  deja la traza de cada ELF de V8 en texto, para
//                        compararla con la de gen_trazas.py --volcado
//
//      testrv --servir=bitbang|cmsis-dap|gdb --port=N [--harts=2] [elf]
//                        no es la suite: el laboratorio con el ELF (por
//                        omisión, dbg.elf) en marcha y una sonda en sus pines
//                        JTAG (remote_bitbang o CMSIS-DAP, para OpenOCD), o el
//                        stub G1 de GDB, hasta que lo maten. Lo usa
//                        verif/openocd/riscv_lab.py (G2 de E2).
// =============================================================================
#include <systemc>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include "common/asan_opciones.h"     // sin esto, asanrv revienta (SystemC y ASan)
#include "rv/lab/soc_lab_rv.h"
#include "rv/hart/rv_decod.h"
#include "rv/gdb/gdb_stub_rv.h"
#include "verif/sonda_jtag_rv.h"
#include "verif/gdb_client.h"
#include "verif/sonda_openocd.h"
#include "verif/sonda_cmsis_dap.h"
#include <chrono>

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) {
    std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str());
}
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok;  std::printf("  [OK  ] %s\n", q.c_str()); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static bool check_eq(uint64_t got, uint64_t exp, const std::string& q) {
    const bool c = got == exp;
    if (c) std::printf("  [OK  ] %s\n", q.c_str());
    else   std::printf("  [FALLO] %s (obtenido 0x%llX, esperado 0x%llX)\n", q.c_str(),
                       (unsigned long long)got, (unsigned long long)exp);
    if (c) ++g_ok; else ++g_fallos;
    return c;
}

struct VectorC { uint16_t c; uint32_t e; };
static const VectorC VECTORES_C[] = {
#include "verif/rv/vectores_c.inc"
};
struct TrazaSpike { const char* elf; uint32_t n; uint64_t huella; };
static const TrazaSpike TRAZAS_SPIKE[] = {
#include "verif/rv/trazas_spike.inc"
};

static const char* const ELF_DIR = "verif/rv/elf/";
static const char* const PROPIAS[] = {"traps", "umodo", "vec", "irq", "trig", "cpu863", "ext", "dbg",
                                       "irq_spike"};
static sc_time g_t_gdb = SC_ZERO_TIME;
static unsigned puerto_gdb(unsigned i) {
    const char* e = std::getenv("TESTRV_PUERTO_GDB");
    return (e ? unsigned(std::atoi(e)) : 3451u) + i;
}

// Las configuraciones del laboratorio: las dos de rv_caps.h y dos variantes
// de la de IMAC con los desalineados en hardware
static RvCaps caps_hw() { RvCaps c = RV_LAB_IMAC; c.nombre = "lab-imac-hw";
                          c.desalineado = RvDesalineado::Hardware; return c; }
static RvCaps caps_863() { RvCaps c = RV_LAB_IMAC; c.nombre = "lab-imac-cpu863";
                           c.desalineado = RvDesalineado::HardwareCpu863; return c; }
static const RvCaps CAPS_HW = caps_hw();
static const RvCaps CAPS_863 = caps_863();

// La extensión de ext.S: custom-0, funct3 = funct7 = 0, rd = rs1·2 + rs2; y
// el CSR 0x7C0
struct ExtPrueba : rv_ext_if, rv_csr_ext_if {
    uint32_t c = 0;
    bool ejecuta(RvHart& h, uint32_t i, unsigned& ciclos) override {
        if (rvd::opcode(i) != rvd::OP_CUSTOM0 || rvd::funct3(i) || rvd::funct7(i)) return false;
        h.pon_x(rvd::rd(i), h.x(rvd::rs1(i)) * 2u + h.x(rvd::rs2(i)));
        ciclos = 1;
        return true;
    }
    bool atiende(uint16_t a) const override { return a == 0x7C0; }
    bool lee(RvHart&, uint16_t, uint32_t& v) override { v = c++; return true; }
    bool escribe(RvHart&, uint16_t, uint32_t v) override { c = v; return true; }
};

static std::vector<std::string> lista(const std::string& dir) {
    std::vector<std::string> r;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.compare(0, 5, "rv32u") == 0 || n.compare(0, 5, "rv32m") == 0)
                if (n.find('.') == std::string::npos) r.push_back(n);
        }
        closedir(d);
    }
    std::sort(r.begin(), r.end());
    return r;
}

SC_MODULE(TbRv) {
    SocLabRv imac{"imac", {&RV_LAB_IMAC}};
    SocLabRv imc{"imc", {&RV_LAB_IMC}};
    SocLabRv hw{"hw", {&CAPS_HW}};
    SocLabRv c863{"c863", {&CAPS_863}};
    SocLabRv dos{"dos", {&RV_LAB_IMAC, &RV_LAB_IMAC}};
    std::string rvt;
    ExtPrueba ext;
    // E2: la sonda de bancada en los pines del laboratorio IMAC, el DMI por
    // ella y los stubs de GDB
    JtagSonda sonda{JtagSonda::Pines{&imac.jtag_tck, &imac.jtag_tms, &imac.jtag_tdi,
                                     &imac.jtag_tdo, &imac.jtag_ntrst}, 10e6};
    DmiPorJtag dmi_pines{sonda};
    GdbStubRv g1{"g1", *imac.dm, puerto_gdb(0), "gdb-g1"};
    GdbStubRv g2p{"g2p", dmi_pines, puerto_gdb(1), "gdb-g2p"};
    GdbStubRv g1dos{"g1dos", *dos.dm, puerto_gdb(2), "gdb-dos"};
    GdbClient cli;
    // Los símbolos de dbg.elf
    uint32_t s_main = 0, s_objetivo = 0, s_otro = 0, s_contador = 0, s_contador1 = 0,
             s_vigilada = 0;

    TbRv(sc_module_name nm, const std::string& dir_rvt) : sc_module(nm), rvt(dir_rvt) {
        SC_HAS_PROCESS(TbRv);
        SC_THREAD(run);
        for (GdbStubRv* g : {&g1, &g2p, &g1dos}) g->set_halt_on_attach(true);
    }

    // Carga un ELF, suelta el reset y espera a que acabe. El código de
    // salida, o -1 si no acaba en `limite`, o -2 si no se puede cargar.
    int corre(SocLabRv& lab, const std::string& elf, sc_time limite = sc_time(100, SC_MS)) {
        lab.reset(true);
        wait(2, SC_US);
        const std::string e = lab.carga_elf(elf);
        if (!e.empty()) { std::printf("  (%s)\n", e.c_str()); return -2; }
        lab.reset(false);
        wait(limite, lab.ev_fin());
        const int r = lab.terminado() ? int(lab.codigo()) : -1;
        lab.reset(true);
        wait(2, SC_US);
        return r;
    }

    // --- V1-V3, sin tiempo ---------------------------------------------------
    void sin_tiempo() {
        grupo("V1 la expansión de las comprimidas, contra GNU as (vectores_c.inc)");
        unsigned validas = 0, reservadas = 0, mal_v = 0, mal_r = 0;
        for (const VectorC& v : VECTORES_C) {
            uint32_t o = 0;
            const bool ok = rvd::expande_c(v.c, o);
            if (v.e) { ++validas; if (!ok || o != v.e) { ++mal_v;
                std::printf("    0x%04x -> 0x%08x (%d), GNU as dice 0x%08x\n", v.c, o, ok, v.e); } }
            else     { ++reservadas; if (ok) { ++mal_r;
                std::printf("    0x%04x reservada, expandida a 0x%08x\n", v.c, o); } }
        }
        check(validas > 200 && mal_v == 0,
              "las " + std::to_string(validas) + " válidas expanden como dice el ensamblador");
        check(reservadas >= 20 && mal_r == 0,
              "las " + std::to_string(reservadas) + " reservadas de RV32C se rechazan");
        uint32_t o = 0;
        check(!rvd::expande_c(0x6000, o) && rvd::expande_c(0x6000, o, true),
              "c.flw solo existe con F (expande_c(.., f = true))");

        grupo("V2 misa de las configuraciones del laboratorio");
        check_eq(RV_LAB_IMAC.misa(), 0x40101105u, "RV32IMAC con U: MXL = 1, A C I M U");
        check_eq(RV_LAB_IMC.misa(), 0x40101104u, "RV32IMC con U: MXL = 1, C I M U");
        uint32_t misa = 0;
        imac.hart().hart.csr_lee(0x301, misa);
        check_eq(misa, 0x40101105u, "y el hart lo lee en misa");

        grupo("V3 los ELF versionados");
        unsigned faltan = 0;
        for (const char* p : PROPIAS) {
            std::vector<TrozoFw> t;
            if (!lee_elf(std::string(ELF_DIR) + p + ".elf", t, ELF_RISCV).empty()) ++faltan;
        }
        check(faltan == 0, "las 9 pruebas propias (verif/rv/elf)");
        std::vector<TrozoFw> t;
        check(lee_elf(std::string(ELF_DIR) + "coremark_rv.elf", t, ELF_RISCV).empty(),
              "coremark_rv.elf");
        check(!lee_elf(std::string(ELF_DIR) + "coremark_rv.elf", t, ELF_ARM).empty(),
              "y un ELF de RISC-V no pasa por uno de ARM (lee_elf con ELF_ARM)");
        check_eq(lista(rvt).size(), 77, "los 77 riscv-tests, en " + rvt);
    }

    // --- V4-V6, riscv-tests --------------------------------------------------
    void riscv_tests() {
        const std::vector<std::string> l = lista(rvt);
        grupo("V4 riscv-tests en RV32IMAC (entorno -p, Trap en los desalineados)");
        for (const std::string& n : l) {
            if (n == "rv32ui-p-ma_data") continue;
            check_eq(corre(imac, rvt + "/" + n), 0, n);
        }
        grupo("V5 riscv-tests en RV32IMC");
        for (const std::string& n : l) {
            if (n == "rv32ui-p-ma_data" || n.compare(0, 6, "rv32ua") == 0) continue;
            check_eq(corre(imc, rvt + "/" + n), 0, n);
        }
        check(corre(imc, rvt + "/rv32ua-p-amoadd_w") > 0,
              "rv32ua-p-amoadd_w falla sin la A (la AMO es ilegal)");
        grupo("V6 los desalineados en hardware: rv32ui-p-ma_data");
        check(corre(imac, rvt + "/rv32ui-p-ma_data") > 0, "con Trap, falla (lo espera en hardware)");
        check_eq(corre(hw, rvt + "/rv32ui-p-ma_data"), 0, "con Hardware");
        check_eq(corre(c863, rvt + "/rv32ui-p-ma_data"), 0,
                 "con HardwareCpu863 (sin PMP, la errata no se ve)");
    }

    // --- V7, las propias -----------------------------------------------------
    void propias() {
        grupo("V7 las pruebas propias del núcleo (verif/rv/propias)");
        const std::string d = ELF_DIR;
        check_eq(corre(imac, d + "traps.elf"), 0, "traps: causa, mtval y mepc de 14 excepciones");
        check_eq(corre(imac, d + "umodo.elf"), 0,
                 "umodo: modo U, privilegio de los CSR, TW, mcounteren, MPRV y PMP con L");
        check_eq(corre(imac, d + "vec.elf"), 0, "vec: mtvec y su legalización");
        check_eq(corre(imac, d + "trig.elf"), 0,
                 "trig: triggers mcontrol de ejecución, carga y almacenamiento, desde U");
        check_eq(corre(imac, d + "irq.elf"), 0,
                 "irq: MSI y MTI del CLINT, directo y vectorizado, wfi y prioridad");
        check_eq(corre(imc, d + "irq.elf"), 0, "irq también en RV32IMC");
        check_eq(corre(imac, d + "irq_spike.elf"), 0,
                 "irq_spike: MSI y MTI desenmascaradas por CSR, prioridad, anidada y wfi");
        check_eq(corre(c863, d + "cpu863.elf"), 0,
                 "cpu863: la carga desalineada que cruza y el almacenamiento a otra región PMP");
        check(corre(hw, d + "cpu863.elf") == 2, "y sin la errata no falla (código 2)");
        check_eq(corre(imac, d + "ext.elf"), 2, "ext sin extensión: custom-0 es ilegal (código 2)");
        imac.hart().hart.pon_ext(&ext);
        imac.hart().hart.pon_csr_ext(&ext);
        check_eq(corre(imac, d + "ext.elf"), 0, "ext con rv_ext_if y rv_csr_ext_if (custom-0 y 0x7C0)");
        imac.hart().hart.pon_ext(nullptr);
        imac.hart().hart.pon_csr_ext(nullptr);
    }

    // --- V8, lockstep con Spike ----------------------------------------------
    void lockstep() {
        grupo("V8 lockstep con Spike: la traza de instrucciones retiradas");
        const char* dir = std::getenv("RV_TRAZA");
        for (const TrazaSpike& s : TRAZAS_SPIKE) {
            uint64_t h = 0xcbf29ce484222325ull;
            uint32_t n = 0;
            bool fin = false;
            FILE* f = dir ? std::fopen((std::string(dir) + "/" + s.elf + ".lab.txt").c_str(), "w")
                          : nullptr;
            auto fnv = [&h](uint32_t w) {
                for (unsigned k = 0; k < 4; ++k) { h ^= (w >> (8 * k)) & 0xFFu; h *= 0x100000001b3ull; }
            };
            imac.hart().hart.traza = [&](const RvCommit& c) {
                if (fin || c.pc < 0x80000000u) return;
                const uint32_t rd = c.rd > 0 ? uint32_t(c.rd) : 0xFFFFFFFFu;
                const uint32_t v = c.rd > 0 ? c.valor : 0u;
                fnv(c.pc); fnv(c.insn); fnv(rd); fnv(v);
                ++n;
                if (f) std::fprintf(f, "%08x %08x %08x %08x\n", c.pc, c.insn, rd, v);
                if (imac.terminado()) fin = true;
            };
            const int r = corre(imac, std::string(ELF_DIR) + s.elf);
            imac.hart().hart.traza = nullptr;
            if (f) std::fclose(f);
            check(r == 0 && n == s.n && h == s.huella,
                  std::string(s.elf) + ": " + std::to_string(n) + " instrucciones, como Spike");
            if (n != s.n || h != s.huella)
                std::printf("    Spike: %u instrucciones, huella %016llx; aquí %016llx\n", s.n,
                            (unsigned long long)s.huella, (unsigned long long)h);
        }
    }

    // --- V9, CoreMark --------------------------------------------------------
    void coremark() {
        grupo("V9 CoreMark (1 iteración) en RV32IMAC");
        const std::string elf = std::string(ELF_DIR) + "coremark_rv.elf";
        const sc_time t0 = sc_time_stamp();
        check_eq(corre(imac, elf), 0, "acaba por HTIF con 0");
        uint32_t con = 0;
        simbolo_elf(elf, "cm_console", con);
        std::string sal;
        const uint32_t len = imac.ram32(con + 4);
        for (uint32_t i = 0; i < len && i < 4096; ++i)
            sal.push_back(char(imac.ram32((con + 8 + i) & ~3u) >> (8 * ((con + 8 + i) & 3u))));
        check_eq(imac.ram32(con), 0x434F4E53u, "la consola de CoreMark está en su buzón");
        // Las CRC de cada carga de trabajo, las de cualquier CoreMark de 2K con
        // PERFORMANCE_RUN; crcfinal, la de una iteración (como en test407).
        // «Errors detected» lo dice siempre: pide 10 s de ejecución
        check(sal.find("seedcrc          : 0xe9f5") != std::string::npos &&
              sal.find("[0]crclist       : 0xe714") != std::string::npos &&
              sal.find("[0]crcmatrix     : 0x1fd7") != std::string::npos &&
              sal.find("[0]crcstate      : 0x8e3a") != std::string::npos,
              "seedcrc, crclist, crcmatrix y crcstate, las de referencia");
        check(sal.find("[0]crcfinal      : 0xe714") != std::string::npos,
              "crcfinal = 0xe714 (una iteración)");
        // minstret: rt/inicio.S lo pone a cero antes de main
        std::printf("    %llu instrucciones en %s\n",
                    (unsigned long long)imac.hart().instrucciones(),
                    (sc_time_stamp() - t0).to_string().c_str());
        if (std::getenv("RV_COREMARK")) std::printf("%s\n", sal.c_str());
    }


    // =========================================================================
    // E2: LA DEPURACIÓN
    // =========================================================================
    // Arranca dbg.elf en un laboratorio y lo deja correr un poco
    void arranca_dbg(SocLabRv& lab) {
        lab.reset(true);
        wait(2, SC_US);
        lab.carga_elf(std::string(ELF_DIR) + "dbg.elf");
        lab.reset(false);
        wait(50, SC_US);
    }
    void simbolos() {
        const std::string e = std::string(ELF_DIR) + "dbg.elf";
        simbolo_elf(e, "main", s_main);
        simbolo_elf(e, "objetivo", s_objetivo);
        simbolo_elf(e, "otro_hart", s_otro);
        simbolo_elf(e, "contador", s_contador);
        simbolo_elf(e, "contador1", s_contador1);
        simbolo_elf(e, "vigilada", s_vigilada);
    }
    // ¿Está `pc` dentro del bucle de main (o en objetivo)?
    bool en_bucle(uint32_t pc) const { return pc >= s_main && pc < s_objetivo + 4; }

    struct ResDm {
        bool ok = false, parado = false, regs = false, mem = false, sigue = false;
        uint32_t pc = 0, s0 = 0;
        uint64_t accesos = 0;
    };
    // La misma secuencia por cualquier dmi_if
    ResDm secuencia_dm(dmi_if& d) {
        ResDm r;
        RvDmCliente c(d);
        const uint64_t a0 = 0;
        r.ok = c.activar() && c.n_harts() == 1 && c.tiene_sba() && c.progbuf() == 8;
        r.parado = c.parar();
        uint32_t s0 = 0, a0v = 0, pc = 0, mem = 0;
        c.lee_reg(RvDmCliente::CSR_DPC, pc);
        c.lee_reg(RvDmCliente::GPR(8), s0);
        c.lee_reg(RvDmCliente::GPR(10), a0v);
        c.escribe_reg(RvDmCliente::GPR(10), 0x12345678u);
        uint32_t vuelta = 0;
        c.lee_reg(RvDmCliente::GPR(10), vuelta);
        c.escribe_reg(RvDmCliente::GPR(10), a0v);
        r.regs = vuelta == 0x12345678u && en_bucle(pc);
        c.lee32(s_contador, mem);
        r.mem = mem == imac.ram32(s_contador) && (mem == s0 || mem + 1 == s0);
        r.pc = pc; r.s0 = s0;
        r.sigue = c.seguir();
        wait(20, SC_US);
        uint32_t s0b = 0;
        c.parar();
        c.lee_reg(RvDmCliente::GPR(8), s0b);
        r.sigue = r.sigue && s0b > s0;
        c.seguir();
        r.accesos = c.accesos() - a0;
        return r;
    }

    void w1_pines() {
        grupo("W1 el DTM por los pines (sonda de bancada) y el DM en directo");
        simbolos();
        arranca_dbg(imac);
        const bool eng = dmi_pines.enganchar();
        check(eng, "dtmcs: version = 1 (0.13)");
        check_eq(dmi_pines.idcode(), SocLabRv::IDCODE, "el IDCODE, tras Test-Logic-Reset");
        check_eq(dmi_pines.abits(), 7, "abits = 7");
        const uint64_t c0 = sonda.ciclos();
        const sc_time t0 = sc_time_stamp();
        const auto r0 = std::chrono::steady_clock::now();
        const ResDm rp = secuencia_dm(dmi_pines);
        const double ms_p = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();
        const uint64_t ciclos = sonda.ciclos() - c0;
        const sc_time t_p = sc_time_stamp() - t0;
        check(rp.ok && rp.parado && rp.regs && rp.mem && rp.sigue,
              "por los pines: activar, parar, registros, memoria por SBA y seguir");
        const auto r1 = std::chrono::steady_clock::now();
        const ResDm rd = secuencia_dm(*imac.dm);
        const double ms_d = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r1).count();
        check(rd.ok && rd.parado && rd.regs && rd.mem && rd.sigue, "en directo: lo mismo");
        check(rp.pc != 0 && en_bucle(rd.pc), "y los dos encuentran el hart en el bucle de main");
        std::printf("    %llu accesos al DM; por los pines, %llu ciclos de TCK (%.1f por acceso) y %s "
                    "simulados a 10 MHz\n    (T97) reloj de pared: %.2f ms por los pines, %.2f ms en directo\n",
                    (unsigned long long)rp.accesos, (unsigned long long)ciclos,
                    double(ciclos) / double(rp.accesos ? rp.accesos : 1), t_p.to_string().c_str(),
                    ms_p, ms_d);
    }

    void w2_dm() {
        grupo("W2 el DM en directo: comandos, program buffer, SBA, triggers, reset");
        RvDm& d = *imac.dm;
        RvDmCliente c(d);
        auto rd = [&d](uint32_t a) { uint32_t v = 0; d.dmi_leer(a, v); return v; };
        auto wr = [&d](uint32_t a, uint32_t v) { d.dmi_escribir(a, v); };
        c.activar();
        check_eq(rd(dm::DMSTATUS) & 0xFFu, 0xA2u,
                 "dmstatus: version 2 (0.13.2), authenticated y hasresethaltreq");
        check_eq(rd(dm::ABSTRACTCS), (8u << 24) | 2u, "abstractcs: progbufsize = 8, datacount = 2");
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_TRANSFER | 0x1008u);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 4u, "un comando con el hart corriendo: cmderr = 4");
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_TRANSFER | 0x1008u);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 4u, "y con cmderr puesto, el comando ni se intenta");
        wr(dm::ABSTRACTCS, 7u << 8);
        check(c.parar() && rd(dm::HALTSUM0) == 1u, "parado: haltsum0 = 1");
        wr(dm::COMMAND, (3u << 20) | dm::AR_TRANSFER | 0x1008u);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 2u, "aarsize = 3 (64 bits) en RV32: cmderr = 2");
        wr(dm::ABSTRACTCS, 7u << 8);
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_TRANSFER | 0x105u);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 3u, "un CSR que no hay (stvec): cmderr = 3");
        wr(dm::ABSTRACTCS, 7u << 8);
        wr(dm::COMMAND, 1u << 24);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 2u, "quick access (cmdtype 1): cmderr = 2");
        wr(dm::ABSTRACTCS, 7u << 8);
        uint32_t s0 = 0, s0b = 0;
        c.lee_reg(RvDmCliente::GPR(8), s0);
        wr(dm::PROGBUF0, 0x00540413u);                       // addi s0, s0, 5
        wr(dm::PROGBUF0 + 1, 0x00100073u);                   // ebreak
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_POSTEXEC);
        c.lee_reg(RvDmCliente::GPR(8), s0b);
        check_eq(s0b, s0 + 5, "postexec: el program buffer suma 5 a s0");
        wr(dm::PROGBUF0, 0x00000057u);                       // ilegal
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_POSTEXEC);
        check_eq((rd(dm::ABSTRACTCS) >> 8) & 7u, 3u, "una excepción en el program buffer: cmderr = 3");
        wr(dm::ABSTRACTCS, 7u << 8);
        c.lee_reg(RvDmCliente::GPR(8), s0b);
        uint32_t dpc = 0;
        c.lee_reg(RvDmCliente::CSR_DPC, dpc);
        check(s0b == s0 + 5 && en_bucle(dpc), "y el hart queda como estaba (s0 y dpc)");
        c.escribe_reg(RvDmCliente::GPR(8), s0);
        wr(dm::COMMAND, dm::AR_SIZE32 | dm::AR_TRANSFER | dm::AR_WRITE | 0x1009u);
        wr(dm::ABSTRACTAUTO, 1u);
        wr(dm::DATA0, 0x77u);
        wr(dm::ABSTRACTAUTO, 0u);
        uint32_t s1 = 0;
        c.lee_reg(RvDmCliente::GPR(9), s1);
        check_eq(s1, 0x77u, "abstractauto: escribir data0 repite el comando (s1 = data0)");
        c.escribe_reg(RvDmCliente::GPR(9), s_contador);
        // SBA
        wr(dm::SBCS, (2u << 17) | dm::SB_READONADDR);
        wr(dm::SBADDRESS0, s_contador + 1);
        check_eq((rd(dm::SBCS) >> 12) & 7u, 3u, "SBA desalineado: sberror = 3");
        wr(dm::SBCS, 7u << 12);
        wr(dm::SBCS, (2u << 17) | dm::SB_READONADDR);
        wr(dm::SBADDRESS0, 0x40000000u);
        check_eq((rd(dm::SBCS) >> 12) & 7u, 2u, "SBA donde no hay nada: sberror = 2");
        wr(dm::SBCS, (3u << 17) | (7u << 12));
        wr(dm::SBDATA0, 0);
        check_eq((rd(dm::SBCS) >> 12) & 7u, 4u, "SBA de 64 bits: sberror = 4");
        wr(dm::SBCS, 7u << 12);
        uint32_t blq[4] = {}, ram[4];
        for (unsigned i = 0; i < 4; ++i) ram[i] = imac.ram32(s_main + 4 * i);
        check(c.lee_bloque(s_main, blq, 4) == 4 && std::memcmp(blq, ram, sizeof ram) == 0,
              "SBA con sbreadondata y autoincremento: un bloque");
        c.escribe16(s_vigilada, 0xBEEF);
        check_eq(imac.ram32(s_vigilada) & 0xFFFFu, 0xBEEFu, "SBA de 16 bits");
        c.usar_sba = false;
        uint32_t m = 0;
        const bool pb = c.lee32(s_main, m) && c.escribe32(s_vigilada, 0xCAFEF00Du);
        c.usar_sba = true;
        check(pb && m == ram[0] && imac.ram32(s_vigilada) == 0xCAFEF00Du,
              "la memoria por el program buffer (lw/sw con s0 y s1), sin SBA");
        check_eq(c.cuenta_triggers(), 4u, "los triggers, descubiertos recorriendo tselect: 4");
        check(c.reset(true), "ndmreset con resethaltreq: havereset y parado");
        c.lee_reg(RvDmCliente::CSR_DPC, dpc);
        uint32_t dcsr = 0;
        c.lee_reg(RvDmCliente::CSR_DCSR, dcsr);
        if (!check(dpc == 0x80000000u && ((dcsr >> 6) & 7u) == 5u,
                   "en la primera instrucción, con dcsr.cause = 5 (resethaltreq)"))
            std::printf("    dpc = 0x%08x, dcsr = 0x%08x\n", dpc, dcsr);
        check(!(c.dmstatus() & dm::ANYHAVERESET), "ackhavereset la borra");
        c.paso();
        c.lee_reg(RvDmCliente::CSR_DPC, dpc);
        c.lee_reg(RvDmCliente::CSR_DCSR, dcsr);
        if (!check(dpc == 0x80000004u && ((dcsr >> 6) & 7u) == 4u, "un paso (dcsr.step): cause = 4"))
            std::printf("    dpc = 0x%08x, dcsr = 0x%08x\n", dpc, dcsr);
        check(c.seguir() && !c.parado(), "resumereq: resumeack y en marcha");
    }

    void w3_dos() {
        grupo("W3 dos harts: hartsel, parar uno sin el otro, disparo cruzado, unavail");
        arranca_dbg(dos);
        RvDm& d = *dos.dm;
        RvDmCliente c(d);
        check(c.activar() && c.n_harts() == 2, "dos harts (HARTSELLEN = 1, el 2 no existe)");
        c.selecciona(1);
        c.parar();
        uint32_t pc1 = 0;
        c.lee_reg(RvDmCliente::CSR_DPC, pc1);
        if (!check(pc1 >= s_otro && pc1 < s_otro + 24, "el hart 1 está en otro_hart"))
            std::printf("    pc = 0x%08x, otro_hart = 0x%08x\n", pc1, s_otro);
        const uint32_t a0 = dos.ram32(s_contador), b0 = dos.ram32(s_contador1);
        wait(20, SC_US);
        c.selecciona(0);
        check(!c.parado() && dos.ram32(s_contador) > a0 && dos.ram32(s_contador1) == b0,
              "parado el 1, el 0 sigue contando y el 1 no");
        uint32_t hs = 0;
        d.dmi_leer(dm::HALTSUM0, hs);
        check_eq(hs, 2u, "haltsum0 = 0b10");
        c.selecciona(1);
        c.seguir();
        d.pon_disparo_cruzado(true);
        c.selecciona(0);
        c.parar();
        wait(5, SC_US);
        d.dmi_leer(dm::HALTSUM0, hs);
        check_eq(hs, 3u, "con el disparo cruzado, parar el 0 para también el 1");
        d.pon_disparo_cruzado(false);
        c.selecciona(1); c.seguir();
        c.selecciona(0); c.seguir();
        wait(5, SC_US);
        d.dmi_leer(dm::HALTSUM0, hs);
        check_eq(hs, 0u, "y los dos siguen");
        d.disponible = [](unsigned h) { return h != 1; };
        c.selecciona(1);
        const uint32_t st = c.dmstatus();
        check((st & dm::ALLUNAVAIL) && !(st & dm::ANYRUNNING), "un hart no disponible: allunavail");
        wr_cmd(d);
        check_eq((leer(d, dm::ABSTRACTCS) >> 8) & 7u, 4u, "y un comando, cmderr = 4");
        d.dmi_escribir(dm::ABSTRACTCS, 7u << 8);
        d.disponible = nullptr;
        c.selecciona(0);
    }
    static uint32_t leer(RvDm& d, uint32_t a) { uint32_t v = 0; d.dmi_leer(a, v); return v; }
    static void wr_cmd(RvDm& d) { d.dmi_escribir(dm::COMMAND, dm::AR_SIZE32 | dm::AR_TRANSFER | 0x1008u); }

    // --- GDB -----------------------------------------------------------------
    static std::string hex(const std::string& s) {
        std::string r; char b[3];
        for (unsigned char ch : s) { std::snprintf(b, 3, "%02x", ch); r += b; }
        return r;
    }
    static uint32_t de_hex_le(const std::string& s) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4 && 2 * i + 2 <= s.size(); ++i)
            v |= uint32_t(std::stoul(s.substr(2 * i, 2), nullptr, 16)) << (8 * i);
        return v;
    }
    static std::string le_hex(uint32_t v) {
        char b[9];
        std::snprintf(b, sizeof b, "%02x%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24);
        return b;
    }
    uint32_t reg(unsigned n) { char b[8]; std::snprintf(b, sizeof b, "p%x", n); return de_hex_le(cli.pedir(b)); }
    std::string espera_parada(double ms = 200) { return cli.recibir(sc_time(ms, SC_MS)); }
    std::string z(char c, unsigned t, uint32_t a, unsigned n) {
        char b[48]; std::snprintf(b, sizeof b, "%c%u,%x,%u", c, t, unsigned(a), n);
        return cli.pedir(b);
    }

    struct ResGdb {
        bool conecta = false, xml = false, g = false, p = false, mem = false, bp = false,
             hw = false, wp = false, paso = false, reset = false, carga = false, corre = false;
        uint32_t pc_bp = 0, pc_hw = 0, pc_wp = 0, pc_paso = 0, pc_reset = 0, a0_carga = 0;
    };
    ResGdb sesion_gdb(GdbStubRv& st, unsigned puerto, SocLabRv& lab) {
        ResGdb r;
        arranca_dbg(lab);
        st.set_enabled(true);
        wait(1, SC_MS);
        if (!cli.conectar(puerto)) { st.set_enabled(false); return r; }
        wait(1, SC_MS);
        const std::string sup = cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=riscv");
        r.conecta = sup.find("PacketSize") != std::string::npos &&
                    cli.pedir("QStartNoAckMode") == "OK" && cli.pedir("?").rfind("T0", 0) == 0;
        const std::string x = cli.pedir("qXfer:features:read:target.xml:0,fff");
        r.xml = x.find("riscv:rv32") != std::string::npos && x.find("org.gnu.gdb.riscv.cpu") != std::string::npos;
        const std::string g = cli.pedir("g");
        r.g = g.size() == 33 * 8 && en_bucle(de_hex_le(g.substr(32 * 8)));
        const uint32_t s0 = reg(8);
        cli.pedir("P8=" + le_hex(0xA5A5A5A5u));
        r.p = reg(8) == 0xA5A5A5A5u && reg(0) == 0;
        cli.pedir("P8=" + le_hex(s0));
        char m[48];
        std::snprintf(m, sizeof m, "m%x,4", unsigned(s_contador));
        r.mem = de_hex_le(cli.pedir(m)) == lab.ram32(s_contador);
        // ebreak (c.ebreak, kind 2) en objetivo
        const bool zb = z('Z', 0, s_objetivo, 2) == "OK";
        cli.enviar("c");
        const std::string pb = espera_parada();
        r.pc_bp = reg(32);
        r.bp = zb && pb.find("swbreak") != std::string::npos && r.pc_bp == s_objetivo;
        z('z', 0, s_objetivo, 2);
        // un trigger de ejecución
        const bool zh = z('Z', 1, s_objetivo, 2) == "OK";
        cli.enviar("c");
        const std::string ph = espera_parada();
        r.pc_hw = reg(32);
        r.hw = zh && ph.find("hwbreak") != std::string::npos && r.pc_hw == s_objetivo;
        z('z', 1, s_objetivo, 2);
        // un watchpoint de escritura en vigilada: salta ANTES del sw
        const bool zw = z('Z', 2, s_vigilada, 4) == "OK";
        cli.enviar("c");
        const std::string pw = espera_parada();
        r.pc_wp = reg(32);
        char w[32];
        std::snprintf(w, sizeof w, "watch:%08x", unsigned(s_vigilada));
        r.wp = zw && pw.find(w) != std::string::npos && en_bucle(r.pc_wp) &&
               lab.ram32(s_vigilada) != reg(8);
        z('z', 2, s_vigilada, 4);
        // un paso: el sw se hace
        cli.enviar("s");
        const std::string ps = espera_parada();
        r.pc_paso = reg(32);
        r.paso = ps.rfind("T05", 0) == 0 && r.pc_paso == r.pc_wp + 4 && lab.ram32(s_vigilada) == reg(8);
        // monitor reset halt
        r.reset = cli.pedir("qRcmd," + hex("reset halt"), sc_time(50, SC_MS)) == "OK";
        r.pc_reset = reg(32);
        r.reset = r.reset && r.pc_reset == 0x80000000u;
        // un programa a la RAM: li a0, 0x55; ebreak
        const uint32_t prog = 0x80100000u;
        r.carga = cli.pedir("M80100000,8:" + le_hex(0x05500513u) + le_hex(0x00100073u)) == "OK";
        cli.pedir("P20=" + le_hex(prog));
        cli.enviar("c");
        const std::string pc_ = espera_parada();
        r.a0_carga = reg(10);
        r.carga = r.carga && pc_.find("swbreak") != std::string::npos && r.a0_carga == 0x55u &&
                  reg(32) == prog + 4;
        // Y que corra de nuevo el programa, con Ctrl-C
        cli.pedir("P20=" + le_hex(s_main));
        cli.enviar("c");
        wait(200, SC_US);
        cli.crudo("\x03");
        const std::string pi = espera_parada();
        r.corre = pi.rfind("T02", 0) == 0 && en_bucle(reg(32));
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        st.set_enabled(false);
        return r;
    }
    void informa(const char* q, const ResGdb& r) {
        const std::string p = std::string(q) + ": ";
        check(r.conecta && r.xml && r.g && r.p && r.mem,
              p + "conectar parado, target.xml riscv:rv32, g (33 registros), P y m");
        check(r.bp, p + "ebreak (Z0, c.ebreak) en objetivo: swbreak");
        check(r.hw, p + "trigger de ejecución (Z1): hwbreak");
        check(r.wp, p + "watchpoint de escritura (Z2): watch, antes del sw");
        check(r.paso, p + "un paso: el sw se hace");
        check(r.reset, p + "monitor reset halt: pc = 0x80000000");
        check(r.carga, p + "un programa cargado en la RAM con M, ejecutado hasta su ebreak");
        check(r.corre, p + "y Ctrl-C con el programa corriendo: T02");
    }

    void w4_w6() {
        const sc_time t0 = sc_time_stamp();
        grupo("W4 GDB por G1 (el DM en directo)");
        const ResGdb r1 = sesion_gdb(g1, puerto_gdb(0), imac);
        informa("G1", r1);
        grupo("W5 GDB por G2' (el DTM por los pines, con la sonda de bancada)");
        const uint64_t c0 = sonda.ciclos();
        const ResGdb r2 = sesion_gdb(g2p, puerto_gdb(1), imac);
        informa("G2'", r2);
        std::printf("    %llu ciclos de TCK en la sesión\n", (unsigned long long)(sonda.ciclos() - c0));
        check(r1.pc_bp == r2.pc_bp && r1.pc_hw == r2.pc_hw && r1.pc_wp == r2.pc_wp &&
              r1.pc_paso == r2.pc_paso && r1.pc_reset == r2.pc_reset && r1.a0_carga == r2.a0_carga,
              "los dos caminos dan lo mismo: el mismo depurador, otro cable");
        sonda.suelta();
        grupo("W6 GDB con dos harts: dos hilos");
        arranca_dbg(dos);
        g1dos.set_enabled(true);
        wait(1, SC_MS);
        bool ok = cli.conectar(puerto_gdb(2));
        wait(1, SC_MS);
        cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=riscv");
        cli.pedir("QStartNoAckMode");
        cli.pedir("?");
        const std::string hilos = cli.pedir("qfThreadInfo");
        check(ok && hilos.find(',') != std::string::npos && cli.pedir("qsThreadInfo") == "l",
              "qfThreadInfo: dos hilos (" + hilos + ")");
        const bool h2 = cli.pedir("Hg2") == "OK";
        const uint32_t pc2 = reg(32);
        cli.pedir("Hg1");
        const uint32_t pc1 = reg(32);
        check(h2 && pc2 >= s_otro && pc2 < s_otro + 24 && en_bucle(pc1),
              "Hg2: el hart 1 en otro_hart; Hg1: el 0 en main");
        check(cli.pedir("T2") == "OK" && cli.pedir("T3").rfind("E", 0) == 0, "T2 vive, T3 no");
        const uint32_t b0 = dos.ram32(s_contador1);
        cli.enviar("c");
        wait(100, SC_US);
        cli.crudo("\x03");
        const std::string pi = espera_parada();
        check(pi.rfind("T02", 0) == 0 && dos.ram32(s_contador1) > b0 && dos.hart(1).parado() &&
                  dos.hart(0).parado(),
              "c los suelta a los dos; Ctrl-C los para a los dos");
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        g1dos.set_enabled(false);
        g_t_gdb += sc_time_stamp() - t0;
    }

    void run() {
        sin_tiempo();
        riscv_tests();
        propias();
        lockstep();
        coremark();
        w1_pines();
        w2_dm();
        w3_dos();
        w4_w6();
        std::printf("\nTOTAL RV: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};


// =============================================================================
// EL MODO SERVIDOR (G2 con OpenOCD): el laboratorio y una sonda, y nada más
// =============================================================================
static const RvCaps* const CAPS_SERV[] = {&RV_LAB_IMAC, &RV_LAB_IMAC, &RV_LAB_IMAC, &RV_LAB_IMAC};
SC_MODULE(ServidorRv) {
    SocLabRv lab;
    std::unique_ptr<SondaOpenOcd> bitbang;
    std::unique_ptr<SondaCmsisDap> dap;
    std::unique_ptr<GdbStubRv> gdb;
    std::string elf;

    ServidorRv(sc_module_name nm, const std::string& modo, unsigned puerto, unsigned harts,
               const std::string& e)
        : sc_module(nm), lab("lab", std::vector<const RvCaps*>(CAPS_SERV, CAPS_SERV + harts)),
          elf(e) {
        auto srst = [this](bool v) { lab.reset(v); };
        if (modo == "bitbang") {
            bitbang.reset(new SondaOpenOcd("sonda", SondaOpenOcd::PinesJtag{&lab.jtag_tck,
                &lab.jtag_tms, &lab.jtag_tdi, &lab.jtag_tdo, &lab.jtag_ntrst}, puerto, srst, 10e6));
        } else if (modo == "cmsis-dap") {
            dap.reset(new SondaCmsisDap("sonda", SondaCmsisDap::PinesJtag{&lab.jtag_tck,
                &lab.jtag_tms, &lab.jtag_tdi, &lab.jtag_tdo, &lab.jtag_ntrst}, puerto, srst));
        } else {
            gdb.reset(new GdbStubRv("gdb", *lab.dm, puerto, "gdb-rv"));
        }
        SC_HAS_PROCESS(ServidorRv);
        SC_THREAD(run);
    }
    void run() {
        lab.reset(true);
        wait(2, SC_US);
        const std::string e = lab.carga_elf(elf);
        if (!e.empty()) { std::printf("%s\n", e.c_str()); sc_stop(); return; }
        lab.eco_uart = true;
        lab.reset(false);
        if (bitbang) bitbang->set_enabled(true);
        if (dap) dap->set_enabled(true);
        if (gdb) gdb->set_enabled(true);
        std::fflush(stdout);
    }
};

static int servidor(int argc, char* argv[]) {
    std::string modo, elf = std::string(ELF_DIR) + "dbg.elf";
    unsigned puerto = 3333, harts = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--servir=", 0) == 0) modo = a.substr(9);
        else if (a.rfind("--port=", 0) == 0) puerto = unsigned(std::atoi(a.c_str() + 7));
        else if (a.rfind("--harts=", 0) == 0) harts = unsigned(std::atoi(a.c_str() + 8));
        else elf = a;
    }
    if (harts < 1 || harts > 4 || (modo != "bitbang" && modo != "cmsis-dap" && modo != "gdb")) {
        std::printf("uso: testrv --servir=bitbang|cmsis-dap|gdb --port=N [--harts=1..4] [elf]\n");
        return 2;
    }
    ServidorRv s("srv", modo, puerto, harts, elf);
    sc_start();
    return 0;
}

int sc_main(int argc, char* argv[]) {
    sc_report_handler::set_actions(SC_WARNING,
        std::getenv("TESTRV_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    if (argc > 1 && std::strncmp(argv[1], "--servir=", 9) == 0) return servidor(argc, argv);
    TbRv tb("tb", argc > 1 ? argv[1] : "build/rvt");
    sc_start();
    // En ps siempre (ci/comprueba_invariante.sh lee «N ps»): to_string() lo
    // diría en ns o en us cuando la cifra es redonda
    auto ps = [](const sc_time& t) {
        return (unsigned long long)(t.value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    };
    std::printf("\nTiempo simulado: %llu ps\n", ps(sc_time_stamp()));
    if (g_t_gdb != SC_ZERO_TIME)
        std::printf("  de los cuales W4-W6 (socket de GDB)  : %llu ps\n"
                    "  el resto                             : %llu ps\n",
                    ps(g_t_gdb), ps(sc_time_stamp() - g_t_gdb));
    return g_fallos ? 1 : 0;
}
