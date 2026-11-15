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
#include "common/asan_opciones.h"
#include "m430/lab/soc_lab_m430.h"
#include "m430/cpu/m430_decod.h"

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

    SC_CTOR(TbM430) { SC_THREAD(run); }

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

    void run() {
        n1();
        n2();
        n3();
        n4();
        n5();
        n6();
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
    return g_fallos ? 1 : 0;
}
