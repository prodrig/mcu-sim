// =============================================================================
// sc_main_f417.cpp — el banco del acelerador criptográfico del F415/F417
//
// TERCER EJECUTABLE, y por la misma razón que el del F446 es el segundo:
// construir piezas nuevas dentro del banco del F407 MUEVE su invariante, porque
// el orden en que SystemC despierta los procesos depende de cuántos módulos hay
// en la simulación. El invariante del F407 -`2336217899213 ps`- lleva ocho
// fases sin moverse un picosegundo y no se negocia. [vs_446re §17.4]
//
// QUÉ COMPRUEBA ESTE BANCO, y qué NO. Aquí se mira **el protocolo de
// registros**: que el intercambio de `DATATYPE` sea el que dice la figura 235,
// que `NBLW` recorte la última palabra donde toca, que `DCAL` dispare el
// relleno, que las cuatro fases del HMAC se recorran como manda el §25.3.6, que
// `BUSY` dure los 66 o 50 ciclos de HCLK que dice el §25.3.1, que las dos
// interrupciones salgan cuando se las habilita y que el contexto se pueda
// guardar y restaurar de verdad.
//
// Lo que NO mira es si MD5 y SHA-1 están bien: eso es aritmética, no necesita
// simulación y se comprueba aparte con los vectores oficiales (`make hash`).
// Separarlo no es una manía: un `DATATYPE` mal interpretado da un resumen
// perfectamente formado y equivocado, y si las dos cosas se probaran juntas no
// habría forma de saber cuál de las dos falló.
//
// EL BLOQUE VA SUELTO, sin SoC alrededor. En la fase 2 todavía no está
// integrado -eso es la fase 4: ventana de AHB2, bit de reloj, IRQ 80 compartida
// con el RNG y celda de DMA-, así que el maestro de pruebas se ata directamente
// a su puerto de esclavo. Se gana algo con ello: lo que aquí falle es del
// bloque y no del cableado.
//
//   make test417
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
// La razón de esta línea está escrita en el propio fichero, y merece leerse
// antes de perder una tarde: sin ella, `make asan417` no da un aviso, da un
// SIGSEGV en un sitio que no tiene nada que ver. Es lo que pasó la primera vez
// que se corrió este banco con los sanitizers, y lo que pasa siempre que un
// ejecutable nuevo del proyecto se olvida de incluirlo.
#include "../common/asan_opciones.h"
#include "../periph/hash.h"
#include "../periph/cryp.h"
#include "../verif/bus_test_master.h"

using namespace sc_core;
using namespace stm32;

namespace {

unsigned g_ok = 0, g_fallo = 0;

void grupo(const char* t) { std::printf("--- %s ---\n", t); }

void check(bool c, const char* q) {
    if (c) { ++g_ok;    std::printf("  [OK  ] %s\n", q); }
    else   { ++g_fallo; std::printf("  [FALLO] %s\n", q); }
}
void check_eq(uint32_t got, uint32_t esp, const char* q) {
    if (got == esp) { ++g_ok; std::printf("  [OK  ] %s\n", q); }
    else { ++g_fallo;
           std::printf("  [FALLO] %s (obtenido 0x%08X, esperado 0x%08X)\n",
                       q, got, esp); }
}
void check_hex(const std::string& got, const std::string& esp, const char* q) {
    if (got == esp) { ++g_ok; std::printf("  [OK  ] %s\n", q); }
    else { ++g_fallo;
           std::printf("  [FALLO] %s\n           obtenido %s\n           esperado %s\n",
                       q, got.c_str(), esp.c_str()); }
}

std::string a_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

// ---------------------------------------------------------------------------
// El banco
// ---------------------------------------------------------------------------
struct Tb : sc_module {
    Hash          dut{"hash"};
    Cryp          cdut{"cryp"};
    BusTestMaster mst{"mst"};
    BusTestMaster cmst{"cmst"};

    sc_signal<bool>   s_clk{"s_clk"}, s_rst_n{"s_rst_n"}, s_clk_en{"s_clk_en"};
    sc_signal<double> s_clk_hz{"s_clk_hz"};
    sc_signal<bool>   s_irq{"s_irq"}, s_dma{"s_dma"};
    sc_signal<bool>   c_irq{"c_irq"}, c_din{"c_din"}, c_dout{"c_dout"};

    static constexpr double HCLK = 168e6;      // el F417 a tope, como el F407

    SC_CTOR(Tb) {
        mst.isk.bind(dut.tsk);
        dut.clk(s_clk); dut.rst_n(s_rst_n); dut.clk_en(s_clk_en);
        dut.clk_hz(s_clk_hz); dut.irq(s_irq); dut.dma_req(s_dma);
        // El CRYP va en su propio socket: son dos esclavos sueltos, sin SoC
        // alrededor, y cada uno con su maestro para que las direcciones de uno
        // no tengan que pasar por el otro.
        cmst.isk.bind(cdut.tsk);
        cdut.clk(s_clk); cdut.rst_n(s_rst_n); cdut.clk_en(s_clk_en);
        cdut.clk_hz(s_clk_hz);
        cdut.irq(c_irq); cdut.dma_in(c_din); cdut.dma_out(c_dout);
        // Un megabyte de pila para el hilo del banco, igual que en
        // `sc_main.cpp`: este hilo lleva el mensaje, el contexto de 51 palabras
        // y las cadenas de cada comprobacion encima.
        SC_THREAD(run);  set_stack_size(1024 * 1024);
    }

    // --- Acceso cómodo a los registros --------------------------------------
    void wr(uint32_t off, uint32_t v) { mst.write32(addr::HASH_B + off, v); }
    uint32_t rd(uint32_t off) { uint32_t v = 0; mst.read32(addr::HASH_B + off, v); return v; }

    // Mete `n` bytes por `HASH_DIN` con el `DATATYPE` que se le diga, y cierra
    // con `DCAL`. Es exactamente lo que hace el HAL de ST: `NBLW = 8*(n%4)`.
    void mete_mensaje(const uint8_t* p, size_t n, unsigned datatype) {
        size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            // Cada 16 palabras el nucleo se pone a moler y baja `DINIS`. El HAL
            // de ST sondea ese bit antes de seguir, y aqui se hace igual: es la
            // diferencia entre entregar el mensaje a bloques, como el firmware
            // de verdad, y volcarlo de golpe sobre un bufer que no cabe.
            if (i && (i % 64) == 0) espera_datos();
            wr(Hash::R_DIN, empaqueta(p + i, 4, datatype));
        }
        const size_t resto = n - i;
        if (resto) wr(Hash::R_DIN, empaqueta(p + i, resto, datatype));
        wr(Hash::R_STR, unsigned(8 * (n % 4)));
        wr(Hash::R_STR, Hash::STR_DCAL | unsigned(8 * (n % 4)));
    }

    // Empaqueta hasta cuatro bytes del mensaje en la palabra que el firmware
    // escribiría con ese `DATATYPE`. Es la operación inversa a la del bloque, y
    // está aquí para que la prueba entre por donde entraría el firmware.
    static uint32_t empaqueta(const uint8_t* p, size_t n, unsigned datatype) {
        uint8_t b[4] = {0,0,0,0};
        for (size_t i = 0; i < n; ++i) b[i] = p[i];
        const uint32_t be = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
                            (uint32_t(b[2]) << 8)  |  uint32_t(b[3]);
        switch (datatype) {
            case 0: return be;                                  // 32 bits
            case 1: return (be >> 16) | (be << 16);             // medias palabras
            case 2: return __builtin_bswap32(be);               // bytes
            default: {                                          // bits
                uint32_t r = 0;
                for (unsigned i = 0; i < 32; ++i) if ((be >> (31 - i)) & 1u) r |= 1u << i;
                return r;
            }
        }
    }

    std::string digest(unsigned n) {
        uint8_t d[20];
        for (unsigned i = 0; i < n / 4; ++i) {
            const uint32_t v = rd(Hash::R_HR0 + 4 * i);
            d[4*i+0] = uint8_t(v >> 24); d[4*i+1] = uint8_t(v >> 16);
            d[4*i+2] = uint8_t(v >> 8);  d[4*i+3] = uint8_t(v);
        }
        return a_hex(d, n);
    }

    // Un resumen entero, de principio a fin, como lo haría el firmware.
    std::string resume(bool md5, const uint8_t* p, size_t n, unsigned datatype = 2) {
        wr(Hash::R_CR, Hash::CR_INIT | (md5 ? Hash::CR_ALGO : 0u) | (datatype << 4));
        mete_mensaje(p, n, datatype);
        espera_digest();
        return digest(md5 ? 16 : 20);
    }

    // --- El CRYP ------------------------------------------------------------
    void cwr(uint32_t off, uint32_t v) { cmst.write32(addr::CRYP_B + off, v); }
    uint32_t crd(uint32_t off) { uint32_t v = 0; cmst.read32(addr::CRYP_B + off, v); return v; }

    // Monta la configuracion y procesa un mensaje entero, como haria el
    // firmware: clave, IV, CRYPEN, y luego bloque a bloque mirando la FIFO.
    std::vector<uint8_t> cryp(unsigned modo, bool descifra, unsigned keysize,
                              const std::vector<uint8_t>& clave,
                              const std::vector<uint8_t>& iv,
                              const std::vector<uint8_t>& in,
                              unsigned datatype = 0) {
        const bool aes = modo >= Cryp::M_AES_ECB;
        const unsigned np = aes ? 4u : 2u;          // palabras por bloque

        cwr(Cryp::R_CR, Cryp::CR_FFLUSH);           // con CRYPEN = 0, vacia las FIFO
        // La clave entra por el TROZO BAJO de los ocho registros: AES-128 por
        // K2LR, AES-192 y TDES por K1LR, AES-256 por K0LR, DES por K1LR.
        const unsigned nw = unsigned(clave.size() / 4);
        const unsigned desde = (clave.size() == 8) ? 2u : (8u - nw);
        for (unsigned i = 0; i < nw; ++i)
            cwr(Cryp::R_K0LR + 4 * (desde + i), pal(&clave[4*i]));
        for (unsigned i = 0; i < iv.size() / 4; ++i)
            cwr(Cryp::R_IV0LR + 4 * i, pal(&iv[4*i]));

        const uint32_t base = (modo << 3) | (descifra ? Cryp::CR_ALGODIR : 0u)
                            | (keysize << 8) | (datatype << 6);
        // Descifrar en ECB o CBC necesita PREPARAR LA CLAVE antes: es el modo
        // 111, y el hardware baja CRYPEN el solo cuando termina. [23.6.1]
        if (aes && descifra && modo != Cryp::M_AES_CTR) {
            cwr(Cryp::R_CR, (Cryp::M_AES_PREP << 3) | (keysize << 8) | Cryp::CR_CRYPEN);
            for (unsigned i = 0; i < 1000 && (crd(Cryp::R_CR) & Cryp::CR_CRYPEN); ++i)
                wait(10, SC_NS);
        }
        cwr(Cryp::R_CR, base | Cryp::CR_CRYPEN);

        std::vector<uint8_t> out;
        for (size_t i = 0; i < in.size(); i += 4 * np) {
            for (unsigned w = 0; w < np; ++w) cwr(Cryp::R_DIN, pal(&in[i + 4*w]));
            for (unsigned w = 0; w < np; ++w) {
                for (unsigned t = 0; t < 10000 && !(crd(Cryp::R_SR) & Cryp::SR_OFNE); ++t)
                    wait(5, SC_NS);
                const uint32_t v = crd(Cryp::R_DOUT);
                out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16));
                out.push_back(uint8_t(v >> 8));  out.push_back(uint8_t(v));
            }
        }
        cwr(Cryp::R_CR, base);                      // CRYPEN a cero
        return out;
    }

    // Cuatro bytes del mensaje, en la palabra que el firmware escribiria con
    // DATATYPE = 00 (sin reordenar): el primer byte en la parte alta.
    static uint32_t pal(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
             | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
    }

    void espera_digest() {
        for (unsigned i = 0; i < 100000 && !(rd(Hash::R_SR) & Hash::SR_DCIS); ++i)
            wait(10, SC_NS);
    }
    void espera_datos() {
        for (unsigned i = 0; i < 100000 && !(rd(Hash::R_SR) & Hash::SR_DINIS); ++i)
            wait(10, SC_NS);
    }

    void run();
};

// ---------------------------------------------------------------------------
// Los casos de `verif/vectores/hash.vec`, que es de donde salen los números
// ---------------------------------------------------------------------------
struct Caso {
    std::string id, algoritmo, clave, mensaje, salida, lento;
    unsigned repite = 1;
};

std::vector<uint8_t> de_hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        v.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return v;
}

std::vector<Caso> lee_vec() {
    std::ifstream f("verif/vectores/hash.vec");
    std::vector<Caso> v;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty()) continue;
        const size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos || l[p] == '#') continue;
        if (l.substr(p) == "[caso]") { v.push_back(Caso{}); continue; }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || v.empty()) continue;
        std::string k = l.substr(0, eq), val = l.substr(eq + 1);
        auto rec = [](std::string& s) {
            const size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, s.find_last_not_of(" \t") - a + 1);
        };
        rec(k); rec(val);
        Caso& c = v.back();
        if      (k == "id")        c.id = val;
        else if (k == "algoritmo") c.algoritmo = val;
        else if (k == "clave")     c.clave = val;
        else if (k == "mensaje")   c.mensaje = val;
        else if (k == "salida")    c.salida = val;
        else if (k == "lento")     c.lento = val;
        else if (k == "repite")    c.repite = unsigned(std::stoul(val));
    }
    return v;
}

struct CasoC {
    std::string id, algoritmo, clave, iv, entrada, salida;
};

std::vector<CasoC> lee_cryp() {
    std::ifstream f("verif/vectores/cryp.vec");
    std::vector<CasoC> v;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty()) continue;
        const size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos || l[p] == '#') continue;
        if (l.substr(p) == "[caso]") { v.push_back(CasoC{}); continue; }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || v.empty()) continue;
        std::string k = l.substr(0, eq), val = l.substr(eq + 1);
        auto rec = [](std::string& x) {
            const size_t a = x.find_first_not_of(" \t");
            if (a == std::string::npos) { x.clear(); return; }
            x = x.substr(a, x.find_last_not_of(" \t") - a + 1);
        };
        rec(k); rec(val);
        CasoC& c = v.back();
        if      (k == "id")        c.id = val;
        else if (k == "algoritmo") c.algoritmo = val;
        else if (k == "clave")     c.clave = val;
        else if (k == "iv")        c.iv = val;
        else if (k == "entrada")   c.entrada = val;
        else if (k == "salida")    c.salida = val;
    }
    return v;
}

unsigned modo_de(const std::string& a) {
    if (a == "tdes-ecb") return Cryp::M_TDES_ECB;
    if (a == "tdes-cbc") return Cryp::M_TDES_CBC;
    if (a == "des-ecb")  return Cryp::M_DES_ECB;
    if (a == "des-cbc")  return Cryp::M_DES_CBC;
    if (a == "aes-ecb")  return Cryp::M_AES_ECB;
    if (a == "aes-cbc")  return Cryp::M_AES_CBC;
    return Cryp::M_AES_CTR;
}

void Tb::run() {
    s_clk_hz.write(HCLK);
    s_clk_en.write(true);
    s_rst_n.write(false);
    wait(1, SC_NS);
    s_rst_n.write(true);
    wait(1, SC_NS);

    // =======================================================================
    grupo("A1 Reset: lo que el manual promete al arrancar");
    // =======================================================================
    check_eq(rd(Hash::R_CR) & ~Hash::CR_DINNE, 0u, "HASH_CR reset = 0");
    check_eq(rd(Hash::R_SR), Hash::SR_DINIS,
             "HASH_SR reset = 0x00000001: DINIS a uno, el bloque pide datos");
    check_eq(rd(Hash::R_IMR), 0u, "HASH_IMR reset = 0, sin interrupciones");
    check_eq(rd(Hash::R_STR), 0u, "HASH_STR reset = 0, y DCAL lee siempre cero");
    for (unsigned i = 0; i < 5; ++i)
        check_eq(rd(Hash::R_HR0 + 4 * i), 0u, "los HASH_HRx arrancan a cero");
    // Tabla 117: CSR0 vale 2 y los otros cincuenta, cero. Es el punto que la
    // fase 0 cerro leyendo la tabla entera.
    check_eq(rd(Hash::R_CSR0) & 0xFFu, 0x02u,
             "HASH_CSR0 reset = 0x00000002 [RM0090 Rev 22, tabla 117]");
    check_eq(rd(Hash::R_CSR0 + 4 * 50), 0u, "y HASH_CSR50, el ultimo de los 51, cero");

    // =======================================================================
    grupo("A2 El mapa: 51 CSR, y el alias del resumen en 0x310");
    // =======================================================================
    check(Hash::R_CSR50 - Hash::R_CSR0 == 4 * 50,
          "CSR0..CSR50 van de 0x0F8 a 0x1C0: son 51 y no 54, que son los del F43x");
    check(Hash::R_HR_ALIAS == 0x310,
          "y los cinco registros de resumen tienen un alias en 0x310, que en el "
          "F41x TAMBIEN existe");

    // =======================================================================
    grupo("B1 SHA-1 y MD5 por el bus, con los vectores de hash.vec");
    // =======================================================================
    const auto casos = lee_vec();
    check(!casos.empty(), "verif/vectores/hash.vec se lee (ejecuta desde src/)");

    for (const Caso& c : casos) {
        if (c.lento == "si") continue;                 // el del millon de letras
        if (c.algoritmo != "md5" && c.algoritmo != "sha1") continue;
        const auto unidad = de_hex(c.mensaje);
        std::vector<uint8_t> m;
        for (unsigned r = 0; r < c.repite; ++r) m.insert(m.end(), unidad.begin(), unidad.end());
        const bool md5 = (c.algoritmo == "md5");
        check_hex(resume(md5, m.data(), m.size()), c.salida, c.id.c_str());
    }

    // =======================================================================
    grupo("B2 Los CUATRO valores de DATATYPE dan el MISMO resumen");
    // =======================================================================
    // Es la prueba que mas falta hacia. El bloque trabaja sobre un bit-string
    // big-endian y el bus entrega palabras little-endian; `DATATYPE` dice como
    // estaba organizado el original. Si el intercambio esta mal, el resumen
    // sale perfectamente formado y equivocado. Se mete el MISMO mensaje con las
    // cuatro organizaciones y tiene que salir lo mismo cuatro veces.
    {
        const char* txt = "abc";
        const uint8_t* m = reinterpret_cast<const uint8_t*>(txt);
        const std::string esp = "a9993e364706816aba3e25717850c26c9cd0d89d";
        for (unsigned dt = 0; dt < 4; ++dt) {
            char q[96];
            std::snprintf(q, sizeof q, "SHA-1(\"abc\") con DATATYPE = %u", dt);
            check_hex(resume(false, m, 3, dt), esp, q);
        }
    }

    // =======================================================================
    grupo("B3 NBLW: las tres fronteras del relleno");
    // =======================================================================
    // 55 bytes caben en un bloque con su relleno; 56 ya no y hacen falta dos;
    // 64 es un bloque justo. Y ademas 55 y 56 no son multiplos de cuatro, asi
    // que ejercitan NBLW distinto de cero.
    {
        std::vector<uint8_t> m;
        struct { size_t n; const char* esp; } t[] = {
            { 55, "c1c8bbdc22796e28c0e15163d20899b65621d65a" },
            { 56, "49b2aec2594bdf3bd7ee9e0a76b0c1c6d5a4b7e1" },
            { 64, "0098ba824b5c16427bd7a1122a5a442a25ec644d" }
        };
        // Los tres esperados NO estan escritos a mano: se calculan con el mismo
        // nucleo que la prueba de vectores ya valido contra el RFC 3174. Lo que
        // se comprueba aqui es el CAMINO por el bus, no la aritmetica.
        for (auto& x : t) {
            m.assign(x.n, 'a');
            Resumen r; r.inicia(AlgoHash::Sha1);
            r.bytes(m.data(), m.size()); r.termina();
            uint8_t d[20]; r.digest(d);
            char q[96];
            std::snprintf(q, sizeof q, "SHA-1 de %zu bytes: NBLW = %u", x.n,
                          unsigned(8 * (x.n % 4)));
            check_hex(resume(false, m.data(), m.size()), a_hex(d, 20), q);
            (void)x.esp;
        }
        // Y el mensaje vacio, que es el caso que mas modelos rompe: su relleno
        // es un bloque entero.
        check_hex(resume(false, nullptr, 0), "da39a3ee5e6b4b0d3255bfef95601890afd80709",
                  "SHA-1 del mensaje VACIO: cero palabras, NBLW = 0");
    }

    // =======================================================================
    grupo("C1 HMAC: las cuatro fases del manual, una por una");
    // =======================================================================
    // Clave corta y clave larga, que es el bit LKEY. Con clave larga el bloque
    // resume primero la clave, y eso son bloques de mas que hay que pagar.
    for (const Caso& c : casos) {
        if (c.algoritmo.rfind("hmac", 0) != 0) continue;
        const bool md5 = c.algoritmo.find("md5") != std::string::npos;
        const auto k = de_hex(c.clave);
        const auto m = de_hex(c.mensaje);
        const bool larga = k.size() > 64;

        wr(Hash::R_CR, Hash::CR_INIT | Hash::CR_MODE | (2u << 4)
                     | (md5 ? Hash::CR_ALGO : 0u) | (larga ? Hash::CR_LKEY : 0u));
        mete_mensaje(k.data(), k.size(), 2);          // fase 2: la clave interna
        espera_datos();
        mete_mensaje(m.data(), m.size(), 2);          // fase 3: el mensaje
        espera_datos();
        mete_mensaje(k.data(), k.size(), 2);          // fase 4: la clave externa
        espera_digest();
        check_hex(digest(md5 ? 16 : 20), c.salida, c.id.c_str());
    }

    // =======================================================================
    grupo("D1 BUSY dura los ciclos que dice el manual");
    // =======================================================================
    // 66 ciclos de HCLK por bloque en SHA-1 y 50 en MD5 [RM0090 Rev 22,
    // 25.3.1]. Se mide un mensaje de UN bloque justo -64 bytes, que con su
    // relleno son dos- y se compara el tiempo que el nucleo pasa ocupado.
    {
        std::vector<uint8_t> m(64, 'a');
        const uint64_t b0 = dut.bloques_procesados();
        resume(false, m.data(), m.size());
        const uint64_t b1 = dut.bloques_procesados();
        check_eq(unsigned(b1 - b0), 2u,
                 "64 bytes en SHA-1 son DOS bloques: el mensaje y el del relleno");

        const uint64_t b2 = dut.bloques_procesados();
        resume(true, m.data(), m.size());
        check_eq(unsigned(dut.bloques_procesados() - b2), 2u, "y en MD5, los mismos dos");
    }
    {
        // El tiempo: se arranca un resumen y se mira cuanto tarda BUSY en
        // bajar. Con HCLK a 168 MHz, 66 ciclos son 392,857 ns.
        std::vector<uint8_t> m(64, 'a');
        wr(Hash::R_CR, Hash::CR_INIT);
        for (unsigned i = 0; i < 16; ++i)
            wr(Hash::R_DIN, Tb::empaqueta(m.data() + 4 * i, 4, 0));
        wr(Hash::R_DIN, 0);                     // dispara el bloque
        const sc_time t0 = sc_time_stamp();
        check(rd(Hash::R_SR) & Hash::SR_BUSY, "tras el bloque 16, BUSY esta a uno");
        while (rd(Hash::R_SR) & Hash::SR_BUSY) wait(5, SC_NS);
        const double ns = (sc_time_stamp() - t0).to_seconds() * 1e9;
        const double esp = 66.0 / HCLK * 1e9;
        char q[128];
        std::snprintf(q, sizeof q, "y baja tras %.1f ns: los 66 ciclos de SHA-1 "
                      "a 168 MHz son %.1f ns", ns, esp);
        check(ns >= esp && ns < esp + 20.0, q);
    }

    // =======================================================================
    grupo("E1 Las dos interrupciones");
    // =======================================================================
    {
        wr(Hash::R_CR, Hash::CR_INIT);
        wr(Hash::R_IMR, 0);
        check(!s_irq.read(), "sin IMR, la linea esta baja");
        wr(Hash::R_IMR, Hash::IMR_DCIE);
        const char* txt = "abc";
        mete_mensaje(reinterpret_cast<const uint8_t*>(txt), 3, 2);
        espera_digest();
        wait(SC_ZERO_TIME);
        check(s_irq.read(), "con DCIE, el resumen terminado levanta la IRQ 80");
        wr(Hash::R_SR, 0);                        // rc_w0: baja DCIS
        wait(SC_ZERO_TIME);
        check(!(rd(Hash::R_SR) & Hash::SR_DCIS), "escribir cero en DCIS lo baja");
        check(!s_irq.read(), "y con el la linea");

        wr(Hash::R_IMR, Hash::IMR_DINIE);
        wr(Hash::R_CR, Hash::CR_INIT);
        wait(SC_ZERO_TIME);
        check(s_irq.read(), "con DINIE, el bloque pidiendo datos tambien la levanta");
        wr(Hash::R_IMR, 0);
        wait(SC_ZERO_TIME);
        check(!s_irq.read(), "y quitando la mascara se cae");
    }

    // =======================================================================
    grupo("E2 La peticion de DMA");
    // =======================================================================
    {
        wr(Hash::R_IMR, 0);
        wr(Hash::R_CR, Hash::CR_INIT);
        wait(SC_ZERO_TIME);
        check(!s_dma.read(), "sin DMAE no hay peticion");
        check(!(rd(Hash::R_SR) & Hash::SR_DMAS), "y DMAS lee cero");
        wr(Hash::R_CR, Hash::CR_DMAE);
        wait(SC_ZERO_TIME);
        check(s_dma.read(), "con DMAE, el bloque pide dato en cuanto esta listo");
        check(rd(Hash::R_SR) & Hash::SR_DMAS, "y DMAS lo dice");
        wr(Hash::R_CR, 0);
        wait(SC_ZERO_TIME);
        check(!s_dma.read(), "quitando DMAE se cae la peticion");
    }

    // =======================================================================
    grupo("F1 El contexto: dos mensajes intercalados");
    // =======================================================================
    // Es para lo que existen los 51 CSR: una tarea prioritaria se lleva el
    // bloque a media faena. Se empieza un mensaje, se guarda el contexto, se
    // resume OTRO mensaje entero por encima, se restaura y se termina el
    // primero. Si el contexto no sirviera, el primero saldria mal.
    {
        const char* a = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        const char* b = "The quick brown fox";
        const uint8_t* pa = reinterpret_cast<const uint8_t*>(a);
        const uint8_t* pb = reinterpret_cast<const uint8_t*>(b);

        wr(Hash::R_CR, Hash::CR_INIT | (2u << 4));
        for (unsigned i = 0; i < 8; ++i) wr(Hash::R_DIN, Tb::empaqueta(pa + 4*i, 4, 2));

        uint32_t ctx[Hash::N_CSR];
        for (unsigned i = 0; i < Hash::N_CSR; ++i) ctx[i] = rd(Hash::R_CSR0 + 4*i);

        const std::string otro = resume(false, pb, std::strlen(b));
        check(!otro.empty(), "en medio cabe otro resumen entero");

        for (unsigned i = 0; i < Hash::N_CSR; ++i) wr(Hash::R_CSR0 + 4*i, ctx[i]);
        for (unsigned i = 8; i < 14; ++i) wr(Hash::R_DIN, Tb::empaqueta(pa + 4*i, 4, 2));
        wr(Hash::R_STR, 0);
        wr(Hash::R_STR, Hash::STR_DCAL);
        espera_digest();
        check_hex(digest(20), "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
                  "y el primero termina bien: el contexto se guarda y se restaura");
    }

    // =======================================================================
    grupo("G1 Lo que este chip NO tiene");
    // =======================================================================
    check(Hash::N_CSR == 51,
          "51 registros de contexto, no 54: los tres de mas son del F43x");
    {
        // No hay SHA-224 ni SHA-256: `ALGO` es UN bit en este silicio, y el
        // bit 18 -que en el F43x es `ALGO[1]`- no existe. Escribirlo no debe
        // cambiar el algoritmo.
        const char* txt = "abc";
        const uint8_t* m = reinterpret_cast<const uint8_t*>(txt);
        wr(Hash::R_CR, Hash::CR_INIT | (1u << 18) | (2u << 4));
        mete_mensaje(m, 3, 2);
        espera_digest();
        check_hex(digest(20), "a9993e364706816aba3e25717850c26c9cd0d89d",
                  "escribir el bit 18 -el ALGO[1] del F43x- no hace nada: sigue "
                  "siendo SHA-1, porque aqui SHA-2 no existe");
    }

    // =======================================================================
    grupo("H1 CRYP: lo que el manual promete al arrancar");
    // =======================================================================
    check_eq(crd(Cryp::R_CR), 0u, "CRYP_CR reset = 0");
    check_eq(crd(Cryp::R_SR), Cryp::SR_IFEM | Cryp::SR_IFNF,
             "CRYP_SR reset = 0x00000003: FIFO de entrada vacia y no llena");
    check_eq(crd(Cryp::R_RISR), Cryp::I_IN,
             "CRYP_RISR reset = 0x00000001: la FIFO de entrada pide dato");
    check_eq(crd(Cryp::R_MISR), 0u, "CRYP_MISR reset = 0, sin mascaras");
    check_eq(crd(Cryp::R_DMACR), 0u, "CRYP_DMACR reset = 0");
    check_eq(crd(Cryp::R_K0LR), 0u,
             "y los registros de clave leen cero: son de SOLO ESCRITURA");
    check_eq(crd(0x50), 0u,
             "en 0x50 no hay nada: los CSGCMCCM son del F43x, y la tabla 114 "
             "-la de este chip- termina en 0x4C");

    // =======================================================================
    grupo("H2 CRYP: los vectores de cryp.vec, por el bus y en las dos direcciones");
    // =======================================================================
    {
        const auto vc = lee_cryp();
        check(!vc.empty(), "verif/vectores/cryp.vec se lee");
        for (const CasoC& c : vc) {
            const auto clave = de_hex(c.clave);
            const auto iv    = de_hex(c.iv);
            const auto ent   = de_hex(c.entrada);
            const auto sal   = de_hex(c.salida);
            const unsigned m = modo_de(c.algoritmo);
            const unsigned ks = (clave.size() == 32) ? 2u : (clave.size() == 24 &&
                                 m >= Cryp::M_AES_ECB) ? 1u : 0u;

            check_hex(a_hex(cryp(m, false, ks, clave, iv, ent).data(), sal.size()),
                      c.salida, (c.id + " / cifrar").c_str());
            check_hex(a_hex(cryp(m, true, ks, clave, iv, sal).data(), ent.size()),
                      c.entrada, (c.id + " / descifrar").c_str());
        }
    }

    // =======================================================================
    grupo("H3 CRYP: los cuatro DATATYPE dan el mismo texto cifrado");
    // =======================================================================
    // La misma prueba que B2 hizo con el HASH, y por el mismo motivo: si el
    // intercambio esta mal, el bloque cifrado sale perfectamente formado y
    // equivocado. Aqui ademas se comprueba que la transformacion es una
    // INVOLUCION: la salida se reordena igual que la entrada.
    {
        const auto clave = de_hex("2b7e151628aed2a6abf7158809cf4f3c");
        const auto ent   = de_hex("6bc1bee22e409f96e93d7e117393172a");
        const std::string esp = "3ad77bb40d7a3660a89ecaf32466ef97";
        for (unsigned dt = 0; dt < 4; ++dt) {
            // Con DATATYPE != 0 el firmware entrega las palabras reordenadas, y
            // el resultado vuelve reordenado igual.
            std::vector<uint8_t> in2 = ent;
            const auto reord = [&](std::vector<uint8_t> v) {
                for (size_t i = 0; i < v.size(); i += 4) {
                    const uint32_t w = Tb::pal(&v[i]);
                    const uint32_t r = Tb::empaqueta(&v[i], 4, dt);
                    (void)w;
                    v[i+0] = uint8_t(r >> 24); v[i+1] = uint8_t(r >> 16);
                    v[i+2] = uint8_t(r >> 8);  v[i+3] = uint8_t(r);
                }
                return v;
            };
            const auto sal = cryp(Cryp::M_AES_ECB, false, 0, clave, {}, reord(in2), dt);
            char q[96];
            std::snprintf(q, sizeof q, "AES-ECB-128 con DATATYPE = %u", dt);
            check_hex(a_hex(reord(sal).data(), 16), esp, q);
        }
    }

    // =======================================================================
    grupo("H4 CRYP: las FIFO y el vaciado");
    // =======================================================================
    {
        cwr(Cryp::R_CR, Cryp::CR_FFLUSH);
        check(crd(Cryp::R_SR) & Cryp::SR_IFEM, "tras FFLUSH, IFEM dice vacia");
        for (unsigned i = 0; i < 3; ++i) cwr(Cryp::R_DIN, 0x11111111u * (i + 1));
        check(!(crd(Cryp::R_SR) & Cryp::SR_IFEM), "con tres palabras ya no esta vacia");
        check(crd(Cryp::R_SR) & Cryp::SR_IFNF, "y con tres de ocho, tampoco llena");
        check(crd(Cryp::R_RISR) & Cryp::I_IN,
              "con TRES dentro sigue pidiendo: el manual dice «menos de cuatro»");
        cwr(Cryp::R_DIN, 0x44444444u);
        check(!(crd(Cryp::R_RISR) & Cryp::I_IN),
              "y con la CUARTA se calla, que es la frontera exacta del 23.5");
        cwr(Cryp::R_CR, Cryp::CR_FFLUSH);
        check(crd(Cryp::R_SR) & Cryp::SR_IFEM,
              "FFLUSH con CRYPEN = 0 vacia las dos FIFO");
        check(crd(Cryp::R_RISR) & Cryp::I_IN, "y vuelve a pedir dato");
        check_eq(crd(Cryp::R_CR) & Cryp::CR_FFLUSH, 0u, "FFLUSH lee siempre cero");
    }

    // =======================================================================
    grupo("H5 CRYP: BUSY dura los ciclos de la tabla 111");
    // =======================================================================
    {
        const auto clave = de_hex("2b7e151628aed2a6abf7158809cf4f3c");
        const auto ent   = de_hex("6bc1bee22e409f96e93d7e117393172a");
        cwr(Cryp::R_CR, Cryp::CR_FFLUSH);
        for (unsigned i = 0; i < 4; ++i) cwr(Cryp::R_K0LR + 4*(4+i), Tb::pal(&clave[4*i]));
        cwr(Cryp::R_CR, (Cryp::M_AES_ECB << 3) | Cryp::CR_CRYPEN);
        const uint64_t b0 = cdut.bloques_procesados();
        const sc_time t0 = sc_time_stamp();
        for (unsigned i = 0; i < 4; ++i) cwr(Cryp::R_DIN, Tb::pal(&ent[4*i]));
        while (!(crd(Cryp::R_SR) & Cryp::SR_OFNE)) wait(2, SC_NS);
        const double ns = (sc_time_stamp() - t0).to_seconds() * 1e9;
        check_eq(unsigned(cdut.bloques_procesados() - b0), 1u, "un bloque, uno");
        const double esp = 14.0 / HCLK * 1e9;
        char q[128];
        std::snprintf(q, sizeof q, "y sale tras %.1f ns: los 14 ciclos de AES-128 "
                      "a 168 MHz son %.1f ns", ns, esp);
        check(ns >= esp, q);
        cwr(Cryp::R_CR, 0);
    }

    // =======================================================================
    grupo("H6 CRYP: las dos interrupciones y las dos peticiones de DMA");
    // =======================================================================
    {
        cwr(Cryp::R_CR, Cryp::CR_FFLUSH);
        cwr(Cryp::R_IMSCR, 0);
        cwr(Cryp::R_DMACR, 0);
        wait(SC_ZERO_TIME);
        check(!c_irq.read(), "sin IMSCR la linea esta baja, aunque RISR pida");
        cwr(Cryp::R_IMSCR, Cryp::I_IN);
        wait(SC_ZERO_TIME);
        check(!c_irq.read(),
              "y con la mascara puesta pero CRYPEN a cero, tampoco: el manual "
              "dice que INMIS solo cuenta con CRYPEN = 1");
        cwr(Cryp::R_CR, (Cryp::M_AES_ECB << 3) | Cryp::CR_CRYPEN);
        wait(SC_ZERO_TIME);
        check(c_irq.read(), "con CRYPEN, la FIFO de entrada vacia SI interrumpe");
        cwr(Cryp::R_IMSCR, 0);
        wait(SC_ZERO_TIME);
        check(!c_irq.read(), "quitando la mascara se cae");

        cwr(Cryp::R_DMACR, Cryp::DMA_DIEN);
        wait(SC_ZERO_TIME);
        check(c_din.read(), "DIEN levanta la peticion de la FIFO de entrada");
        check(!c_dout.read(), "y la de salida no, que esta vacia");
        cwr(Cryp::R_DMACR, 0);
        cwr(Cryp::R_CR, 0);
        wait(SC_ZERO_TIME);
        check(!c_din.read(), "sin DIEN no hay peticion");
    }

    std::printf("\nTOTAL F417 : %u comprobaciones OK, %u fallos\n", g_ok, g_fallo);
    sc_stop();
}

} // namespace

int sc_main(int, char**) {
    Tb tb("tb");
    sc_start();
    std::printf("Tiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    return g_fallo ? 1 : 0;
}
