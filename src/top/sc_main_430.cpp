// =============================================================================
// sc_main_430.cpp — LA SUITE DEL NÚCLEO MSP430 (fase M1 de doc/analisis_msp430.md)
//
// Un banco aparte, como testrv: el núcleo genérico (src/m430/cpu) en el SoC
// de laboratorio (m430/lab/soc_lab_m430.h), sin ningún periférico de TI. Si
// algo falla aquí, es del núcleo. Las tres CPU a la vez: CPU, CPUX y CPUXV2.
//
//   N1  el decodificador contra binutils: cada forma de instrucción de los
//       programas versionados, su clase y su longitud según msp430-elf-objdump
//       (verif/m430/vectores_dec.inc), sin tiempo simulado
//   N2  los programas diferenciales (verif/m430/gen_dif.py): 450 bloques de
//       una instrucción al azar cada uno, con R4-R15, el SR y la memoria
//       tocada. Los de CPU contra mspdebug sim; los de CPUX (y -mlarge, con
//       datos y pila por encima de 64 KB) contra msp430-elf-run, con sus
//       erratas tapadas (gen_dif.py lo explica). Los de CPUX, también en la
//       CPUXV2: la misma ISA
//   N3  los ciclos: una fila por fila de las tablas de la guía de cada CPU
//       (verif/m430/ciclos.inc, la segunda transcripción), medidos con el
//       contador del laboratorio
//   N4  las pruebas propias (verif/m430/propias.S): lo que el simulador de GDB
//       hace distinto de la guía, las interrupciones, el bajo consumo y la
//       interrupción desde por encima de 64 KB
//   N5  printf de newlib (entero, largo, cadena, coma flotante) con y sin
//       -mlarge, igual que en msp430-elf-run
//   N6  el cargador: TI-TXT y ELF_MSP430
//
//   (M2) la depuración, en el laboratorio:
//   N7  los códigos de IR de [JTAG] y los de JTAG_defs.h (al revés), bit a
//       bit, sin tiempo
//   N8  la sonda de bancada (el port de las macros HAL de la MSP Debug
//       Stack) por JTAG de 4 hilos: el JTAG ID, la sincronización, memoria,
//       registros, el PC, un paso, soltar, el EEM, el POR y el fusible
//   N9  lo mismo por Spy-Bi-Wire, y su salida por el tope de 100 µs
//   N10 el EEM en el núcleo: un disparador de búsqueda para la CPU, el de
//       escritura la para tras la instrucción; GENCLKCTRL
//   N11-N12 GDB por --gdb-dap (directo) y por --gdb (la sonda, en 4 hilos y
//       en SBW): parar, registros, memoria, Z0 (0x4343), Z1, Z2, paso,
//       Ctrl-C y monitor reset halt. Hablan por un socket de verdad: van al
//       final y su tiempo no cuenta en «el resto»
//
// Uso: test430
//      test430 --elf a.elf [cpu|cpux|cpuxv2]: no es la suite: el ELF en el
//      laboratorio, con su salida a la estándar (el código de salida es el de
//      exit()).
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include "common/asan_opciones.h"
#include "m430/lab/soc_lab_m430.h"
#include "m430/cpu/m430_decod.h"
#include "m430/sonda/sonda_m430.h"
#include "m430/gdb/gdb_m430.h"
#include "verif/gdb_client.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) {
    std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str());
}
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}

struct VecDec { uint16_t w, op; uint8_t cpux, n; M430Clase clase; };
static const VecDec VEC_DEC[] = {
#include "verif/m430/vectores_dec.inc"
};
struct FilaCiclos { const char* elf; unsigned fila; const char* que; int cpu, cpux, cpuxv2; };
static const FilaCiclos FILAS_CICLOS[] = {
#include "verif/m430/ciclos.inc"
};

static const char* const DIR = "verif/m430/elf/";
static sc_time g_t_gdb = SC_ZERO_TIME;
static unsigned puerto_gdb(unsigned i) {
    const char* e = std::getenv("TEST430_PUERTO_GDB");
    return (e ? unsigned(std::atoi(e)) : 3471u) + i;
}
static std::string lee_fichero(const std::string& r) {
    std::ifstream f(r, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static std::vector<std::string> lineas(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) v.push_back(l);
    return v;
}

// Compara la salida del laboratorio con la esperada. Una línea esperada puede
// acabar en " m=XXX": el SR (la última cifra) se compara con esa máscara.
static unsigned compara(const std::string& obt, const std::string& esp, std::string& primera) {
    const std::vector<std::string> a = lineas(obt), b = lineas(esp);
    unsigned mal = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        std::string e = b[i];
        unsigned m = 0xFFFFFu;
        const size_t pm = e.find(" m=");
        if (pm != std::string::npos) { m = unsigned(std::strtoul(e.c_str() + pm + 3, nullptr, 16)); e.resize(pm); }
        const std::string o = i < a.size() ? a[i] : std::string("(nada)");
        bool igual = o == e;
        if (!igual && pm != std::string::npos) {
            const size_t uo = o.rfind(' '), ue = e.rfind(' ');
            igual = uo != std::string::npos && ue != std::string::npos && o.substr(0, uo) == e.substr(0, ue) &&
                    (std::strtoul(o.c_str() + uo + 1, nullptr, 16) & m) == (std::strtoul(e.c_str() + ue + 1, nullptr, 16) & m);
        }
        if (!igual) {
            if (!mal) primera = "esperado «" + b[i] + "», obtenido «" + o + "»";
            ++mal;
        }
    }
    if (a.size() > b.size()) { if (!mal) primera = "sobra salida"; ++mal; }
    return mal;
}

SC_MODULE(TbM430) {
    SocLabM430 cpu{"lab_cpu", M430_LAB_CPU};
    SocLabM430 cpux{"lab_cpux", M430_LAB_CPUX};
    SocLabM430 cpuxv2{"lab_cpuxv2", M430_LAB_CPUXV2};

    // Los stubs de GDB de N11-N12 (sus hilos se crean en la elaboración)
    std::unique_ptr<DbgDirectoM430> dbg_dap_;
    std::unique_ptr<SondaM430> sonda_4h_, sonda_sbw_;
    std::unique_ptr<DbgSondaM430> dbg_4h_, dbg_sbw_;
    std::unique_ptr<GdbStubM430> stub_dap_, stub_4h_, stub_sbw_;

    SC_CTOR(TbM430) {
        SC_THREAD(run);
        dbg_dap_.reset(new DbgDirectoM430(cpu.nucleo, cpu.eem));
        dbg_dap_->al_reset = [this] { cpu.por(); };
        stub_dap_.reset(new GdbStubM430("gdb_dap", *dbg_dap_, puerto_gdb(0), "gdb-dap-430"));
        SondaM430::Pines p4; p4.tck = &cpu.jtag_tck; p4.tms = &cpu.jtag_tms; p4.tdi = &cpu.jtag_tdi; p4.tdo = &cpu.jtag_tdo;
        sonda_4h_.reset(new SondaM430(p4, SondaM430::Modo::Jtag4));
        dbg_4h_.reset(new DbgSondaM430(*sonda_4h_, 2));
        stub_4h_.reset(new GdbStubM430("gdb_4h", *dbg_4h_, puerto_gdb(1), "gdb-430-4h"));
        SondaM430::Pines ps; ps.test = &cpu.sbw_test; ps.rst = &cpu.sbw_rst;
        sonda_sbw_.reset(new SondaM430(ps, SondaM430::Modo::Sbw));
        dbg_sbw_.reset(new DbgSondaM430(*sonda_sbw_, 2));
        stub_sbw_.reset(new GdbStubM430("gdb_sbw", *dbg_sbw_, puerto_gdb(2), "gdb-430-sbw"));
    }

    // Corre un ELF hasta exit() (o `tope`); "" si se ha podido cargar
    std::string corre(SocLabM430& lab, const std::string& elf, sc_time tope = sc_time(2, SC_SEC)) {
        lab.reset(true);
        wait(1, SC_US);
        const std::string e = lab.carga_elf(elf);
        if (!e.empty()) return e;
        lab.reset(false);
        wait(tope, lab.ev_fin());
        return std::string();
    }
    std::vector<uint16_t> res(SocLabM430& lab, const std::string& elf) {
        uint32_t r = 0, f = 0;
        std::vector<uint16_t> v;
        if (!simbolo_elf(elf, "res", r) || !simbolo_elf(elf, "fin_res", f)) return v;
        const uint32_t fin = lab.ram16(f);
        for (uint32_t a = r; a < fin; a += 2) v.push_back(lab.ram16(a));
        return v;
    }

    void n1() {
        grupo("N1 el decodificador contra binutils (vectores_dec.inc), sin tiempo");
        static const char* const NOMBRES[] = {"F1", "F2", "Salto", "Direccion", "RotM", "PushM", "PopM",
                                              "Calla", "Reti", "Ilegal"};
        for (unsigned c = 0; c < 9; ++c) {
            unsigned n = 0, mal = 0;
            char primera[160] = "";
            for (const VecDec& v : VEC_DEC) {
                if (unsigned(v.clase) != c) continue;
                ++n;
                const bool ext = m430_es_ext(v.w) && v.cpux;
                const M430Ins in = ext ? m430_decodifica(v.op, true, true, v.w) : m430_decodifica(v.w, v.cpux != 0);
                const unsigned largo = (ext ? 2u : 1u) + m430_palabras_op(in);
                if (in.clase != v.clase || largo != v.n) {
                    if (!mal) std::snprintf(primera, sizeof primera, " (0x%04X 0x%04X: clase %u, %u palabras)",
                                            v.w, v.op, unsigned(in.clase), largo);
                    ++mal;
                }
            }
            check(n && !mal, std::string("formas de ") + NOMBRES[c] + ": " + std::to_string(n) +
                             " como binutils" + (mal ? std::string(", ") + std::to_string(mal) + " no" + primera : ""));
        }
    }

    void dif(SocLabM430& lab, const char* nombre, const char* cpu) {
        const std::string elf = std::string(DIR) + nombre + ".elf";
        const std::string e = corre(lab, elf);
        if (!e.empty()) { check(false, e); return; }
        std::string primera;
        const unsigned mal = lab.terminado()
            ? compara(lab.salida(), lee_fichero(std::string(DIR) + nombre + ".txt"), primera) : 1u;
        check(lab.terminado() && !mal, std::string(nombre) + " en " + cpu + ": 458 líneas como el oráculo" +
              (mal ? " — " + std::to_string(mal) + " distintas; la 1.ª: " + primera : ""));
    }
    void n2() {
        grupo("N2 los programas diferenciales (gen_dif.py): CPU contra mspdebug; CPUX contra msp430-elf-run");
        for (const char* n : {"dif_cpu_11", "dif_cpu_12", "dif_cpu_13", "dif_cpu_14"}) dif(cpu, n, "CPU");
        for (const char* n : {"dif_cpux_21", "dif_cpux_22", "dif_cpux_23", "dif_cpux_24", "dif_cpuxl_31", "dif_cpuxl_32"}) {
            dif(cpux, n, "CPUX");
            dif(cpuxv2, n, "CPUXV2");
        }
    }

    void n3() {
        grupo("N3 los ciclos, fila a fila (ciclos.inc: SLAU144K cap. 3 y 4, SLAU391F)");
        struct Caso { SocLabM430* lab; const char* elf; int col; const char* cpu; };
        const Caso casos[] = {{&cpu, "ciclos_cpu", 0, "CPU"}, {&cpux, "ciclos_x", 1, "CPUX"},
                              {&cpuxv2, "ciclos_x", 2, "CPUXV2"}};
        for (const Caso& c : casos) {
            const std::string elf = std::string(DIR) + c.elf + ".elf";
            const std::string e = corre(*c.lab, elf);
            if (!e.empty() || !c.lab->terminado()) { check(false, std::string(c.elf) + " no acaba " + e); continue; }
            uint32_t r = 0;
            simbolo_elf(elf, "res", r);
            for (const FilaCiclos& f : FILAS_CICLOS) {
                if (std::strcmp(f.elf, c.elf)) continue;
                const int esp = c.col == 0 ? f.cpu : c.col == 1 ? f.cpux : f.cpuxv2;
                if (esp < 0) continue;
                const int med = int16_t(c.lab->ram16(r + 2 * f.fila));
                char t[160];
                std::snprintf(t, sizeof t, "%s %-26s %2d ciclos%s", c.cpu, f.que, med,
                              med == esp ? "" : (" (la tabla dice " + std::to_string(esp) + ")").c_str());
                check(med == esp, t);
            }
        }
    }

    void n4() {
        grupo("N4 las pruebas propias (propias.S): la guía donde el de GDB no la sigue; irq y LPM");
        // Lo esperado, prueba a prueba, con su fuente en propias.S
        struct Prueba { const char* que; std::vector<uint16_t> cpu, x, v2; };
        const std::vector<uint16_t> P1 = {0, 0x34}, P2 = {0, 0xFF5A}, P5 = {0xF0E8}, P6 = {1}, P7 = {1, 0}, P8 = {2};
        const std::vector<Prueba> pruebas = {
            {"P1 MOV.B @SP+: el SP sube 2 [UG2 POP.B]", P1, P1, P1},
            {"P2 PUSH.B: SP - 2, el byte alto no se toca", P2, P2, P2},
            {"P3 irq y RETI; la instrucción tras EINT va antes", {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}},
            {"P4 el SR al entrar: a 0 (SCG0 se queda en CPUXV2); el apilado", {0, 0x48}, {0, 0x48}, {0x40, 0x48}},
            {"P5 prioridad: 0xFFF0 antes que 0xFFE8", P5, P5, P5},
            {"P6 NMI del laboratorio con GIE a 0", P6, P6, P6},
            {"P7 LPM0 y despertar; MCLK no cuenta dormida", P7, P7, P7},
            {"P8 RETI vuelve a dormir si no se toca el SR apilado", P8, P8, P8},
        };
        const std::vector<Prueba> pruebas_x = {
            {"X1 SXT Rn: el bit 7 a 19:8 [SLAU391F SXT]", {}, {0xFF80, 0x000F}, {0xFF80, 0x000F}},
            {"X2 SXTX.W Rn: también 19:8", {}, {0x0045, 0}, {0x0045, 0}},
            {"X3 SXT en memoria, N y C, V a 0", {}, {0xFF80, 0x0005}, {0xFF80, 0x0005}},
            {"X4 SWPBX.A y .W en registro (los ejemplos de SLAU391F)", {}, {0x5634, 2, 0x5634, 0}, {0x5634, 2, 0x5634, 0}},
            {"X5 SWPBX.A en memoria: 19:16 se quedan", {}, {0x5634, 2}, {0x5634, 2}},
            {"X6 DADDX.A: cinco cifras, C y Z", {}, {0, 2, 0, 0, 3}, {0, 2, 0, 0, 3}},
            {"X7 V de SUBA, ADDA y CMPA", {}, {0x104, 0x104, 0x101}, {0x104, 0x104, 0x101}},
            {"X8 RRC y RRCX borran V", {}, {0x0004, 0}, {0x0004, 0}},
            {"X9 CALLA EDE (la base es la palabra de índice) y RETA", {}, {0x00F1}, {0x00F1}},
            {"X10-12 MOVX.A a memoria, PUSHX/POPX.A, PUSHM/POPM.A", {}, {0xBCDE, 0x000A, 0, 1, 1, 3},
             {0xBCDE, 0x000A, 0, 1, 1, 3}},
            {"X13 MOV.W y MOV.B a un registro borran 19:16 y 19:8", {}, {0, 0x12}, {0, 0x12}},
        };
        struct Caso { SocLabM430* lab; const char* elf; int col; const char* cpu; };
        const Caso casos[] = {{&cpu, "propias_cpu", 0, "CPU"}, {&cpux, "propias_x", 1, "CPUX"},
                              {&cpuxv2, "propias_x", 2, "CPUXV2"}, {&cpux, "propias_l", 1, "CPUX -mlarge"},
                              {&cpuxv2, "propias_l", 2, "CPUXV2 -mlarge"}};
        for (const Caso& c : casos) {
            const std::string elf = std::string(DIR) + c.elf + ".elf";
            const std::string e = corre(*c.lab, elf);
            if (!e.empty() || !c.lab->terminado()) { check(false, std::string(c.elf) + " no acaba " + e); continue; }
            const std::vector<uint16_t> r = res(*c.lab, elf);
            size_t k = 0;
            auto mira = [&](const Prueba& p) {
                const std::vector<uint16_t>& esp = c.col == 0 ? p.cpu : c.col == 1 ? p.x : p.v2;
                bool ok = k + esp.size() <= r.size();
                std::string obt;
                for (size_t i = 0; i < esp.size() && k + i < r.size(); ++i) {
                    char b[8]; std::snprintf(b, sizeof b, " %04X", r[k + i]); obt += b;
                    if (r[k + i] != esp[i]) ok = false;
                }
                k += esp.size();
                check(ok, std::string(c.cpu) + " " + p.que + (ok ? "" : " — obtenido" + obt));
            };
            for (const Prueba& p : pruebas) mira(p);
            if (c.col) for (const Prueba& p : pruebas_x) mira(p);
            if (std::strcmp(c.elf, "propias_l") == 0) {
                // L1: la interrupción desde 0x1xxxx: PC.19:16 = 1 en SR.15:12, y vuelve a su sitio
                const bool ok = k + 3 <= r.size() && r[k] == 0x1008 && uint16_t(r[k + 1] - r[k + 2]) == 10;
                check(ok, std::string(c.cpu) + " L1 irq por encima de 64 KB: SR apilado 0x1008 y el PC de vuelta");
                k += 3;
            }
            check(k == r.size(), std::string(c.cpu) + " " + c.elf + ": " + std::to_string(r.size()) + " resultados");
        }
    }

    void n5() {
        grupo("N5 printf de newlib, con y sin -mlarge (como msp430-elf-run)");
        struct Caso { SocLabM430* lab; const char* n; const char* cpu; };
        for (const Caso& c : {Caso{&cpu, "printf_cpu", "CPU"}, Caso{&cpux, "printf_x", "CPUX"},
                              Caso{&cpux, "printf_xl", "CPUX -mlarge"}, Caso{&cpuxv2, "printf_xl", "CPUXV2 -mlarge"}}) {
            const std::string e = corre(*c.lab, std::string(DIR) + c.n + ".elf");
            std::string primera;
            const unsigned mal = compara(c.lab->salida(), lee_fichero(std::string(DIR) + c.n + ".txt"), primera);
            check(e.empty() && c.lab->terminado() && c.lab->codigo() == 0 && !mal,
                  std::string(c.cpu) + " " + c.n + (mal ? " — " + primera : ""));
        }
    }

    void n6() {
        grupo("N6 el cargador: TI-TXT y ELF_MSP430");
        // El mismo programa en TI-TXT (escrito aquí desde los segmentos del ELF)
        const std::string elf = std::string(DIR) + "printf_cpu.elf";
        std::vector<TrozoFw> t;
        const std::string e = lee_elf(elf, t, ELF_MSP430);
        const std::string txt = "build/printf_cpu.txt";
        if (FILE* f = std::fopen(txt.c_str(), "w")) {
            for (const TrozoFw& tr : t) {
                std::fprintf(f, "@%04X\n", unsigned(tr.dir));
                for (size_t i = 0; i < tr.datos.size(); ++i)
                    std::fprintf(f, "%02X%s", tr.datos[i], (i % 16 == 15 || i + 1 == tr.datos.size()) ? "\n" : " ");
            }
            std::fprintf(f, "q\n");
            std::fclose(f);
        }
        // Sin símbolos no hay E/S de TI: se mira que acaba con exit(0)
        const std::string e2 = corre(cpu, txt);
        check(e.empty() && e2.empty() && cpu.terminado() && cpu.codigo() == 0,
              "printf_cpu en TI-TXT: carga y acaba con exit(0)");
        std::vector<TrozoFw> t2;
        const std::string e3 = lee_elf("verif/rv/elf/dbg.elf", t2, ELF_MSP430);
        check(e3.find("no para MSP430") != std::string::npos, "un ELF de RISC-V no se carga: «" + e3 + "»");
    }

    // --- M2 -------------------------------------------------------------------
    void n7() {
        grupo("N7 los códigos de IR: [JTAG] y JTAG_defs.h de la MSP Debug Stack, al revés");
        // {código de JTAG_defs.h (desplazado con el MSB primero), código de [JTAG]}
        static const uint8_t T[][2] = {
            {0xC1, IR430_ADDR_16BIT}, {0x21, IR430_ADDR_CAPTURE}, {0xA1, IR430_DATA_TO_ADDR},
            {0x61, IR430_CAPTURE_CPU_REG}, {0xE1, IR430_DEVICE_ID}, {0x82, IR430_DATA_16BIT},
            {0x42, IR430_DATA_CAPTURE}, {0xC2, IR430_DATA_QUICK}, {0x22, IR430_DATA_PSA},
            {0x62, IR430_SHIFT_OUT_PSA}, {0x90, IR430_EMEX_DATA_EXCHANGE}, {0x50, IR430_EMEX_READ_TRIGGER},
            {0xD0, IR430_EMEX_READ_CONTROL}, {0x30, IR430_EMEX_WRITE_CONTROL}, {0x88, IR430_CNTRL_SIG_HIGH_BYTE},
            {0x48, IR430_CNTRL_SIG_LOW_BYTE}, {0xC8, IR430_CNTRL_SIG_16BIT}, {0x28, IR430_CNTRL_SIG_CAPTURE},
            {0xA8, IR430_CNTRL_SIG_RELEASE}, {0xE8, IR430_COREIP_ID}, {0x44, IR430_PREPARE_BLOW},
            {0x24, IR430_EX_BLOW}, {0x86, IR430_JMB_EXCHANGE}, {0x46, IR430_JSTATE_ID},
            {0x26, IR430_TDO_EVENT}, {0xA6, IR430_TDO_EVENT_CTL}, {0xFF, IR430_BYPASS}};
        unsigned mal = 0;
        for (const auto& t : T) {
            uint8_t r = 0;
            for (unsigned i = 0; i < 8; ++i) if ((t[0] >> i) & 1u) r = uint8_t(r | (0x80u >> i));
            if (r != t[1]) ++mal;
        }
        check(!mal, std::to_string(sizeof T / sizeof T[0]) + " instrucciones: las dos fuentes, bit a bit");
    }

    // La sonda de bancada sobre el laboratorio de CPU, con dbg_cpu.elf corriendo
    void sonda(SondaM430::Modo modo, const char* q) {
        const std::string elf = std::string(DIR) + "dbg_cpu.elf";
        uint32_t s_cont = 0, s_vig = 0, s_obj = 0, s_main = 0;
        simbolo_elf(elf, "contador", s_cont); simbolo_elf(elf, "vigilada", s_vig);
        simbolo_elf(elf, "objetivo", s_obj); simbolo_elf(elf, "main", s_main);
        corre(cpu, elf, sc_time(10, SC_US));
        wait(200, SC_US);
        SondaM430::Pines pp;
        if (modo == SondaM430::Modo::Jtag4) { pp.tck = &cpu.jtag_tck; pp.tms = &cpu.jtag_tms; pp.tdi = &cpu.jtag_tdi;
                                              pp.tdo = &cpu.jtag_tdo; pp.test = &cpu.sbw_test; }   // TEST: la VPP
        else { pp.test = &cpu.sbw_test; pp.rst = &cpu.sbw_rst; }
        SondaM430 s(pp, modo);
        const std::string p = std::string(q) + ": ";
        const uint8_t id = s.conecta();
        check(id == 0x89, p + "el JTAG ID al desplazar la IR: 0x" + hex2(id));
        const bool sinc = s.sincroniza(false);
        const uint32_t pc_cpu = cpu.cpu().pc();
        const uint16_t pc = s.lee_pc();
        check(sinc && pc == pc_cpu && cpu.nucleo.en_tclk(), p + "sincronizada (TCE, INSTR_LOAD) y el PC por ADDR_CAPTURE");
        const uint16_t c0 = cpu.ram16(s_cont);
        check(s.lee_palabra(uint16_t(s_cont)) == c0, p + "ReadMemWord de contador");
        s.escribe_palabra(uint16_t(s_vig), 0x1234);
        s.escribe_byte(uint16_t(s_vig + 1), 0x56);
        check(cpu.ram16(s_vig) == 0x5634, p + "WriteMemWord y WriteMemByte en vigilada");
        const uint32_t r4 = cpu.cpu().reg(4), sr = cpu.cpu().reg(2);
        const uint16_t l4 = s.lee_reg(4), lsr = s.lee_reg(2);
        check(l4 == r4 && lsr == sr, p + "ReadCpuReg (MOV Rn,&0x00FE por el MDB): R4 y SR");
        s.escribe_reg(5, 0xBEEF);
        check(cpu.cpu().reg(5) == 0xBEEF && s.lee_pc() == pc + 8,
              p + "WriteCpuReg (MOV #n,Rn y JMP $-4): R5 = 0xBEEF; el PC, 4 palabras más (las de las dos lecturas)");
        s.pon_pc(uint16_t(s_obj));
        check(cpu.cpu().pc() == s_obj && s.lee_pc() == s_obj, p + "SetPc: el PC en objetivo");
        const bool paso = s.un_paso();
        check(paso && s.lee_pc() == s_obj + 4 && cpu.ram16(s_cont) == uint16_t(c0 + 1),
              p + "un paso por TCLK: INC &contador se hace y el PC avanza");
        s.escribe_eem(0x88, 0x0026);
        check(s.lee_eem(0x88) == 0x0026 && cpu.eem.genclkctrl() == 0x26, p + "el EEM por IR_EMEX_DATA_EXCHANGE: GENCLKCTRL");
        s.pon_pc(uint16_t(s_main));
        s.suelta_cpu();
        const uint16_t c1 = cpu.ram16(s_cont);
        wait(500, SC_US);
        check(!cpu.nucleo.en_tclk() && cpu.ram16(s_cont) != c1, p + "IR_CNTRL_SIG_RELEASE: la CPU corre sola");
        const bool por = s.sincroniza(true);
        // Las tres bajadas de TCLK de ExecutePOR cargan el vector y pueden ejecutar
        // la primera instrucción: el PC, en el vector o justo después [de modelo]
        const uint16_t vpc = s.lee_pc(), vec = cpu.ram16(0xFFFE);
        check(por && vpc >= vec && vpc <= vec + 6, p + "POR por el registro de control: la CPU, en el vector de reset");
        s.suelta_cpu();
        // El fusible: sin VPP no se funde; con VPP en TEST (BlowFuse del
        // Replicator430), sí, y después todos los registros son de 1 bit
        s.ir_shift(IR430_PREPARE_BLOW);
        s.ir_shift(IR430_EX_BLOW);
        const bool sin_vpp = !cpu.tap.fusible() && !s.fusible_fundido();
        s.funde_fusible();
        const uint8_t id2 = uint8_t(s.ir_shift(IR430_BYPASS));
        check(sin_vpp && cpu.tap.fusible() && s.fusible_fundido() && id2 == 0x89,
              p + "el fusible: sin VPP no; con VPP en TEST, sí, y el TAP da su ID (0x" + hex2(id2) +
              ") con registros de 1 bit (IsFuseBlown)");
        cpu.tap.restaura_fusible();
        s.desconecta();
        wait(100, SC_US);
        if (modo == SondaM430::Modo::Sbw) {
            check(!cpu.pines.en_sbw(), p + "SBWTCK bajo más de 100 µs: fuera de Spy-Bi-Wire");
        }
    }
    static std::string hex2(unsigned v) { char b[8]; std::snprintf(b, sizeof b, "%02X", v); return b; }
    void n8() { grupo("N8 la sonda de bancada por JTAG de 4 hilos (las macros HAL de 2xx)"); sonda(SondaM430::Modo::Jtag4, "4 hilos"); }
    void n9() { grupo("N9 la sonda de bancada por Spy-Bi-Wire"); sonda(SondaM430::Modo::Sbw, "SBW"); }

    void n10() {
        grupo("N10 el EEM en el núcleo: búsqueda, escritura y control de relojes");
        const std::string elf = std::string(DIR) + "dbg_cpu.elf";
        uint32_t s_obj = 0, s_vig = 0;
        simbolo_elf(elf, "objetivo", s_obj); simbolo_elf(elf, "vigilada", s_vig);
        corre(cpu, elf, sc_time(10, SC_US));
        EemM430& e = cpu.eem;
        e.escribe(0x00, uint16_t(s_obj)); e.escribe(0x02, 0x0000); e.escribe(0x06, 0x0001);
        e.escribe(0x80, 0x0001); e.escribe(0x82, EemM430::EEM_EN);
        wait(200, SC_US);
        check(cpu.nucleo.parado() && cpu.cpu().pc() == s_obj && (e.lee(0x82) & EemM430::E_STOPPED) && (e.lee(0x8E) & 1u),
              "disparador de búsqueda en objetivo: la CPU se para ANTES, E_STOPPED y TRIGFLAG");
        e.escribe(0x06, 0); e.escribe(0x8E, 0xFFFF);
        e.escribe(0x08, uint16_t(s_vig)); e.escribe(0x0A, 0x0026); e.escribe(0x0E, 0x0002); e.escribe(0x80, 0x0002);
        cpu.nucleo.pide_parada(false);
        wait(200, SC_US);
        const uint32_t pc = cpu.cpu().pc();
        check(cpu.nucleo.parado() && (e.lee(0x8E) & 2u) && pc != s_obj, "disparador de escritura en vigilada: se para tras la instrucción");
        e.escribe(0x82, EemM430::EEM_RST);
        e.escribe(0x82, 0);
        check(e.lee(0x80) == 0 && !e.parado(), "EEM_RST borra los disparadores");
        cpu.nucleo.pide_parada(false);
    }

    // --- GDB --------------------------------------------------------------------
    GdbClient cli;
    uint32_t reg(unsigned n) {
        char b[8]; std::snprintf(b, sizeof b, "p%x", n);
        const std::string r = cli.pedir(b);
        uint32_t v = 0;
        for (size_t i = 0; i + 1 < r.size() && i < 8; i += 2) v |= uint32_t(std::strtoul(r.substr(i, 2).c_str(), nullptr, 16)) << (4 * i);
        return v;
    }
    static std::string hexs(const std::string& t) { std::string o; char b[4];
        for (unsigned char c : t) { std::snprintf(b, sizeof b, "%02x", c); o += b; } return o; }
    std::string z(char c, unsigned t, uint32_t a, unsigned k) {
        char b[48]; std::snprintf(b, sizeof b, "%c%u,%x,%x", c, t, unsigned(a), k); return cli.pedir(b, sc_time(200, SC_MS));
    }
    std::string espera_parada() {
        for (int i = 0; i < 400; ++i) { const std::string r = cli.recibir(sc_time(5, SC_MS)); if (!r.empty()) return r; }
        return std::string();
    }
    void sesion(GdbStubM430& st, unsigned puerto, const char* q) {
        const std::string elf = std::string(DIR) + "dbg_cpu.elf";
        uint32_t s_obj = 0, s_vig = 0, s_main = 0;
        simbolo_elf(elf, "objetivo", s_obj); simbolo_elf(elf, "vigilada", s_vig); simbolo_elf(elf, "main", s_main);
        corre(cpu, elf, sc_time(10, SC_US));
        const sc_time t0 = sc_time_stamp();
        if (std::getenv("TEST430_DEPURA")) std::printf("  [antes] tclk=%d parado=%d pc=%x tce1=%x\n", cpu.nucleo.en_tclk(), cpu.nucleo.parado(), unsigned(cpu.cpu().pc()), cpu.tap.cntrl());
        st.set_halt_on_attach(true);
        st.set_enabled(true);
        wait(1, SC_MS);
        const std::string p = std::string(q) + ": ";
        if (!cli.conectar(puerto)) { check(false, p + "no conecta"); st.set_enabled(false); return; }
        wait(1, SC_MS);
        const std::string sup = cli.pedir("qSupported:swbreak+;hwbreak+", sc_time(5, SC_SEC));
        const bool con = sup.find("PacketSize") != std::string::npos && cli.pedir("QStartNoAckMode") == "OK" &&
                         cli.pedir("?", sc_time(5, SC_SEC)).rfind("T0", 0) == 0;
        const std::string g = cli.pedir("g", sc_time(5, SC_SEC));
        const uint32_t pc0 = reg(0);
        check(con && g.size() == 16 * 8 && pc0 >= s_obj && pc0 < s_main + 16,
              p + "conectar parado; g (16 registros de 4 bytes); el PC en el bucle" +
              (con && g.size() == 16 * 8 && pc0 >= s_obj && pc0 < s_main + 16 ? std::string()
               : " — con=" + std::to_string(con) + " g=" + std::to_string(g.size()) + " pc=" + std::to_string(pc0)));
        cli.pedir("P5=efbe0000", sc_time(5, SC_SEC));
        char m[32]; std::snprintf(m, sizeof m, "m%x,2", unsigned(s_vig));
        const std::string mv = cli.pedir(m, sc_time(5, SC_SEC));
        check(reg(5) == 0xBEEF && mv.size() == 4, p + "P y m");
        // Z0: 0x4343 en objetivo
        const bool z0 = z('Z', 0, s_obj, 2) == "OK";
        std::snprintf(m, sizeof m, "m%x,2", unsigned(s_obj));
        const bool en_ram = cli.pedir(m, sc_time(5, SC_SEC)) == "4343";
        cli.enviar("c");
        const std::string pb = espera_parada();
        check(z0 && en_ram && pb.find("swbreak") != std::string::npos && reg(0) == s_obj,
              p + "Z0: 0x4343 (CLR.B R3) en objetivo y el disparador de su búsqueda: swbreak");
        z('z', 0, s_obj, 2);
        const bool z1 = z('Z', 1, s_obj, 2) == "OK";
        cli.enviar("c");
        const std::string ph = espera_parada();
        check(z1 && ph.find("hwbreak") != std::string::npos && reg(0) == s_obj, p + "Z1: un disparador de búsqueda: hwbreak");
        z('z', 1, s_obj, 2);
        const bool z2 = z('Z', 2, s_vig, 2) == "OK";
        cli.enviar("c");
        const std::string pw = espera_parada();
        char w[32]; std::snprintf(w, sizeof w, "watch:%x", unsigned(s_vig));
        const uint32_t pc_w = reg(0);
        check(z2 && pw.find(w) != std::string::npos && pc_w > s_main, p + "Z2: escritura en vigilada: watch, tras el MOV");
        z('z', 2, s_vig, 2);
        cli.enviar("s");
        const std::string ps = espera_parada();
        check(ps.rfind("T05", 0) == 0 && reg(0) != pc_w, p + "un paso");
        const bool rh = cli.pedir("qRcmd," + hexs("reset halt"), sc_time(5, SC_SEC)) == "OK";
        check(rh && reg(0) == cpu.ram16(0xFFFE), p + "monitor reset halt: el PC en el vector de reset");
        cli.enviar("c");
        wait(300, SC_US);
        cli.crudo("\x03");
        const std::string pi = espera_parada();
        const uint32_t pc_i = reg(0);
        check(pi.rfind("T02", 0) == 0 && pc_i >= s_obj && pc_i < s_main + 16, p + "Ctrl-C con el programa en marcha: T02 en el bucle");
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        st.set_enabled(false);
        g_t_gdb += sc_time_stamp() - t0;
    }
    void n11() {
        grupo("N11 GDB por --gdb-dap (el núcleo y el EEM en directo)");
        sesion(*stub_dap_, puerto_gdb(0), "--gdb-dap");
    }
    void n12() {
        grupo("N12 GDB por --gdb (la sonda de bancada, por 4 hilos y por SBW)");
        sesion(*stub_4h_, puerto_gdb(1), "--gdb 4 hilos");
        sonda_4h_->desconecta();
        sesion(*stub_sbw_, puerto_gdb(2), "--gdb SBW");
        sonda_sbw_->desconecta();
    }

    void run() {
        if (const char* solo = std::getenv("TEST430_SOLO")) {       // para depurar la suite
            const std::string g = solo;
            if (g == "N8") n8(); else if (g == "N9") n9(); else if (g == "N10") n10();
            else if (g == "N11") n11(); else if (g == "N12") n12();
            std::printf("\nTOTAL 430: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
            sc_stop();
            return;
        }
        n1();
        n2();
        n3();
        n4();
        n5();
        n6();
        n7();
        n8();
        n9();
        n10();
        n11();
        n12();
        std::printf("\nTOTAL 430: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

// test430 --elf a.elf [cpu|cpux|cpuxv2]
SC_MODULE(CorreElf430) {
    SocLabM430 lab;
    std::string elf;
    int res = 0;
    CorreElf430(sc_module_name nm, const M430Caps& c, std::string e) : sc_module(nm), lab("lab", c), elf(std::move(e)) {
        SC_HAS_PROCESS(CorreElf430);
        SC_THREAD(run);
    }
    void run() {
        lab.eco_uart = true;
        lab.reset(true);
        wait(1, SC_US);
        const std::string e = lab.carga_elf(elf);
        if (!e.empty()) { std::printf("%s\n", e.c_str()); res = 2; sc_stop(); return; }
        lab.reset(false);
        wait(sc_time(10, SC_SEC), lab.ev_fin());
        if (!lab.terminado()) { std::printf("\n%s: no acaba en 10 s simulados\n", elf.c_str()); res = 124; }
        else res = int(lab.codigo());
        sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    sc_report_handler::set_actions(SC_WARNING, std::getenv("TEST430_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    if (argc > 2 && std::strcmp(argv[1], "--elf") == 0) {
        const std::string a = argc > 3 ? argv[3] : "cpux";
        const M430Caps& c = a == "cpu" ? M430_LAB_CPU : a == "cpuxv2" ? M430_LAB_CPUXV2 : M430_LAB_CPUX;
        CorreElf430 k("correelf", c, argv[2]);
        sc_start();
        return k.res;
    }
    TbM430 tb("tb");
    sc_start();
    auto ps = [](const sc_time& t) {
        return (unsigned long long)(t.value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    };
    std::printf("\nTiempo simulado: %llu ps\n", ps(sc_time_stamp()));
    if (g_t_gdb != SC_ZERO_TIME)
        std::printf("  de los cuales N11-N12 (socket de GDB): %llu ps\n"
                    "  el resto                             : %llu ps\n",
                    ps(g_t_gdb), ps(sc_time_stamp() - g_t_gdb));
    return g_fallos ? 1 : 0;
}
