// =============================================================================
// lea.h — El LEA (Low-Energy Accelerator) del MSP430FR5994 (M8 de
// doc/analisis_msp430.md)
//
// TI no publica el LEA a nivel de registro: la guía [UG FR5 SLAU367P cap. 9]
// remite a la DSPLib. Este modelo sale de dos fuentes:
//
//   * los registros, de las cabeceras de TI (msp430fr5994.h: LEA_BASE 0xA80,
//     los OFS_LEA* y los campos) y del `targetdb` (LEA_367.0);
//   * lo que hace cada orden, de la MSP DSP Library (DSPLib, BSD-3; la copia
//     de EMCLab-Sinica/DSPLib en GitHub, 2017): cómo la llama su camino con
//     LEA (los MSP_LEA_*_PARAMS de DSPLib_lea.h, LEAPMS0/LEAPMS1/LEAPMCB) y
//     qué calcula su camino sin LEA (el de la CPU con el MPY32), que es la
//     segunda fuente de cada resultado. Una orden da lo mismo que la función
//     de DSPLib sin LEA, bit a bit, mientras no desborde [de modelo: el LEA de
//     verdad puede redondear distinto; DSPLib lo admite en la FFT].
//
// LA MEMORIA. El LEA ve palabras de 32 bits: su dirección es la del MSP430
// entre 4 (MSP_LEA_CONVERT_ADDRESS). Solo trabaja en su RAM (LEAMB-LEAMT:
// 0x2C00-0x3BFF, 4 KB) y en sus constantes: 0xA000 (ceros), 0xAC00 (0x7FFF),
// 0xB000 (0x8000, -1 en Q15) y 0xE000 (0x80000000, -1 en IQ31) [DSPLib_lea.h].
// Una dirección fuera da LEAOORIFG y la orden no hace nada. LEACNF2 es el
// puntero de su pila (DSPLib reserva en ella los parámetros).
//
// LAS ÓRDENES (LEAPMCB, con LEACMDEN en LEAPMCTL; la palabra alta la
// completa [de modelo: el compilador escribe la baja y luego la alta]).
// LEAPMS0, el primer operando; LEAPMS1, los parámetros. Las de DSPLib:
//   ADDMATRIX, SUBMATRIX, MPYMATRIX (Q15, de dos en dos, con desplazamientos
//   en palabras), sus LONG (IQ31), MAC, MACLONGMATRIX, MACCOMPLEXMATRIX,
//   MPYCOMPLEXMATRIX, MPYMATRIXROW, MAX/MIN y sus UNSIGNED, LONGMATRIX y
//   UNSIGNEDLONGMATRIX (el valor y su índice, el más bajo si hay empate),
//   FIR, FIRLONG, FIRCOMPLEX, FIRCOMPLEXLONG (con el búfer circular de
//   bufferMask), IIRBQ1 (DF1), IIRBQ2 (DF2), IIRBQ2EXTENDED (con el sesgo y
//   los extremos), BITREVERSECOMPLEX{EVEN,ODD} y su LONG, FFTCOMPLEXFIXEDSCALING,
//   FFTCOMPLEXAUTOSCALING (el desplazamiento en LEAPMDST), FFTCOMPLEXLONG, FFT
//   y FFTLONG (el paso de partir de la FFT real), DEINTERLEAVE{4} y su LONG.
//   Los coeficientes de giro, los de las tablas de DSPLib: round(32768·x)
//   saturado a ±32767.
// Las que DSPLib carga en la memoria de código (msp_lea_loadCommand, por
// LEACMCTL/LEACMA): se reconocen por la huella FNV-1a de lo cargado (las de
// msp_lea_patch.c: las INTERLEAVE de la revisión B y todas las de la A) y
// hacen lo mismo que la orden de ROM. La memoria de código da en 0x2FFC la
// suma de la revisión B (0xAE790580): DSPLib la lee para saber cuál es.
// Las demás (POLYNOMIAL, WINDOW, MAC3, SCALEDMAC...) avisan una vez y acaban
// sin hacer nada [de modelo].
//
// EL TIEMPO [de modelo]: 24 ciclos de MCLK más uno por cada par de muestras o
// por cada producto (N/2·log2 N en la FFT), con LEABUSY mientras tanto. Al
// acabar, LEAPMCMDIFG (y la interrupción con LEAPMCMDIE) y el disparo 27 del
// DMA. Una orden con LEABUSY da LEACOVLIFG. LEAIV: COVL 2, TI 4, OOR 6, SDI 8,
// PMCMD 10; leerlo borra la bandera. LEAIFG se borra escribiendo un 1;
// LEAIFGSET la pone.
// =============================================================================
#ifndef STM32_PERIPH_TI_LEA_H
#define STM32_PERIPH_TI_LEA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>
#include "../../common/verbosidad.h"

namespace stm32 {

class Lea : public sc_core::sc_module {
public:
    // La memoria del SoC (un byte por dirección); lo da el SoC
    std::function<uint8_t*(uint32_t)> mem;
    std::function<double()> f_mclk;             // el reloj del LEA (la fuente de MCLK)
    std::function<void()> al_cambiar;           // una bandera: el SoC mira pide()
    std::function<void()> al_acabar;            // el disparo 27 del DMA
    uint32_t mb = 0x2C00, mt = 0x3C00;           // LEAMB y LEAMT

    SC_HAS_PROCESS(Lea);
    explicit Lea(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        SC_THREAD(proc);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[CNF2] = mt >> 2;
        busy_ = false; cma_dir_ = 0; codigo_.clear();
    }
    bool pide() const { return (r_[IFG] & r_[IE] & 0x1Fu) != 0; }
    bool ocupado() const { return busy_; }
    unsigned ordenes() const { return n_ordenes_; }

    // Los registros, de 16 bits (las dos mitades de cada uno de 32)
    uint16_t lee16(uint32_t off) {
        const unsigned i = (off >> 2) & 0x1Fu;
        const bool alta = (off & 2) != 0;
        uint32_t v = 0;
        switch (i) {
        case MB: v = mb; break;
        case MT: v = mt; break;
        case CNF1: v = (r_[CNF1] & ~1u) | (busy_ ? 1u : 0u); break;
        case CMA:
            v = lee_cma();                                         // sin LEAINC al leer [de modelo: DSPLib lee una palabra]
            break;
        case IV: {
            v = 0;
            const uint32_t p = r_[IFG] & r_[IE] & 0x1Fu;
            if (!alta && p) {
                unsigned b = 0;
                while (!((p >> b) & 1u)) ++b;
                v = 2u * (b + 1);
                r_[IFG] &= ~(1u << b);
                if (al_cambiar) al_cambiar();
            }
            break;
        }
        default: v = r_[i]; break;
        }
        return uint16_t(alta ? v >> 16 : v);
    }
    void escribe16(uint32_t off, uint16_t w) {
        const unsigned i = (off >> 2) & 0x1Fu;
        const bool alta = (off & 2) != 0;
        auto pon = [&](uint32_t& r) { r = alta ? (r & 0x0000FFFFu) | (uint32_t(w) << 16) : (r & 0xFFFF0000u) | w; };
        switch (i) {
        case MB: case MT: case CMDSTAT: case S1STAT: case S0STAT: case DSTSTAT: case IV: return;
        case CNF0:
            pon(r_[CNF0]);
            if (r_[CNF0] & 1u) { const uint32_t c = r_[CNF0]; reset(); r_[CNF0] = c & ~1u; }   // LEASWRST
            return;
        case CNF1: pon(r_[CNF1]); r_[CNF1] &= ~1u; return;
        case CMCTL:
            pon(r_[CMCTL]);
            if (alta) cma_dir_ = uint16_t(r_[CMCTL] >> 16);
            r_[CMCTL] &= ~0x40u;                                   // LEACRACTION: ya está
            return;
        case CMA:
            pon(r_[CMA]);
            if (alta && (r_[CMCTL] & 1u)) {                         // LEACMAE
                codigo_[cma_dir_] = r_[CMA];
                if (r_[CMCTL] & 4u) cma_dir_ = uint16_t(cma_dir_ + 1);
            }
            return;
        case PMCB:
            pon(r_[PMCB]);
            if (alta) orden(uint16_t(r_[PMCB]));
            return;
        case IFGSET:
            if (!alta) { r_[IFG] |= w & 0x1Fu; if (al_cambiar) al_cambiar(); }
            return;
        case IFG:
            if (!alta) { r_[IFG] &= ~uint32_t(w); if (al_cambiar) al_cambiar(); }
            return;
        default: pon(r_[i]); return;
        }
    }

private:
    enum : unsigned { CAP = 0, CNF0, CNF1, CNF2, MB, MT, CMA, CMCTL, R8, R9, CMDSTAT, S1STAT, S0STAT, DSTSTAT,
                      R14, R15, PMCTL, PMDST, PMS1, PMS0, PMCB, R21, R22, R23, R24, R25, R26, R27, IFGSET, IE, IFG, IV };
    enum : uint32_t { COVL = 1, TI = 2, OOR = 4, SDI = 8, PMCMD = 16 };
    uint32_t r_[32];
    bool busy_ = false, oor_ = false;
    uint16_t cma_dir_ = 0;
    std::map<uint16_t, uint32_t> codigo_;       // la memoria de código cargada
    unsigned n_ordenes_ = 0;
    uint64_t ciclos_ = 0;
    std::vector<uint16_t> avisadas_;
    sc_core::sc_event ev_;
    sc_core::sc_time fin_;

    uint32_t lee_cma() const {
        if (!(r_[CMCTL] & 1u)) return 0;
        if (cma_dir_ == 0x2FFC) return 0xAE790580u;                // la suma de la ROM de la revisión B [msp_lea_patch.c]
        auto it = codigo_.find(cma_dir_);
        return it == codigo_.end() ? 0u : it->second;
    }

    // --- La memoria, en direcciones del LEA (palabras de 32 bits) ---------------
    bool en_ram(uint32_t w) const { const uint32_t a = w << 2; return a >= mb && a + 4 <= mt; }
    static int32_t constante(uint32_t w, bool& es) {
        es = true;
        if (w >= 0xE000) return int32_t(0x80000000u);
        if (w >= 0xB000) return int32_t(0x80008000u);
        if (w >= 0xAC00) return 0x7FFF7FFF;
        if (w >= 0xA000) return 0;
        es = false;
        return 0;
    }
    uint32_t rd32(uint32_t w) {
        w &= 0xFFFFu;
        bool es;
        const int32_t c = constante(w, es);
        if (es) return uint32_t(c);
        if (!en_ram(w)) { oor_ = true; return 0; }
        const uint8_t* p = mem(w << 2);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    void wr32(uint32_t w, uint32_t v) {
        w &= 0xFFFFu;
        if (!en_ram(w)) { oor_ = true; return; }
        uint8_t* p = mem(w << 2);
        p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
    }
    // Un Q15 por su índice desde una palabra (el índice 2k+1 es la mitad alta)
    int16_t rd16(uint32_t w, uint32_t e) { const uint32_t v = rd32(w + (e >> 1)); return int16_t((e & 1u) ? v >> 16 : v); }
    void wr16(uint32_t w, uint32_t e, int16_t x) {
        const uint32_t ww = w + (e >> 1);
        const uint32_t v = rd32(ww);
        wr32(ww, (e & 1u) ? (v & 0xFFFFu) | (uint32_t(uint16_t(x)) << 16) : (v & 0xFFFF0000u) | uint16_t(x));
    }
    uint16_t par(unsigned k) { const uint32_t v = rd32(r_[PMS1] + k / 2); return uint16_t((k & 1u) ? v >> 16 : v); }
    int32_t par32(unsigned k) { return int32_t(rd32(r_[PMS1] + k)); }

    // --- La aritmética (la de DSPLib con el MPY32) -------------------------------
    static int16_t sat16(int64_t x) { return int16_t(std::max<int64_t>(INT16_MIN, std::min<int64_t>(INT16_MAX, x))); }
    static int32_t sat32(int64_t x) { return int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, x))); }
    static int64_t sra(int64_t x, unsigned n) { return x >= 0 ? x >> n : -((-x - 1) >> n) - 1; }
    static int16_t q15mpy(int16_t a, int16_t b) { return int16_t(sra(int64_t(a) * b, 15)); }
    static int32_t q31mpy(int32_t a, int32_t b) { return int32_t(uint32_t(sra(int64_t(a) * b, 31))); }
    static int32_t q15iq31(int16_t a, int32_t b) { return int32_t(uint32_t(sra(int64_t(a) * b, 15))); }
    // El giro W_N^k [msp_transform_tables.c]
    static int16_t tw(double x) { return int16_t(std::max(-32767.0, std::min(32767.0, std::round(32768.0 * x)))); }
    static void giro(unsigned k, unsigned n, int16_t& c, int16_t& s) {
        // M_PI no es de C++ estandar: MinGW con -std=c++17 no lo define
        const double pi = 3.14159265358979323846;
        const double a = 2.0 * pi * double(k) / double(n);
        c = tw(std::cos(a)); s = tw(-std::sin(a));
    }

    void aviso_una(uint16_t c, const char* q) {
        if (std::find(avisadas_.begin(), avisadas_.end(), c) != avisadas_.end()) return;
        avisadas_.push_back(c);
        ::stm32::avisa_consola("[lea] orden 0x%03X: %s\n", unsigned(c), q);
    }

    // --- Las órdenes ---------------------------------------------------------------
    void orden(uint16_t pmcb) {
        if (!(r_[PMCTL] & 1u)) return;                              // LEACMDEN
        if (busy_) { r_[IFG] |= COVL; if (al_cambiar) al_cambiar(); return; }
        uint16_t cmd = uint16_t(pmcb & ~3u);
        // Una de la memoria de código: por su huella
        if (cmd >= 0x100 && codigo_.count(uint16_t(cmd << 5))) cmd = de_codigo(uint16_t(cmd << 5));
        oor_ = false;
        ciclos_ = 24;
        ++n_ordenes_;
        ejecuta(cmd);
        if (oor_) r_[IFG] |= OOR;
        busy_ = true;
        const double f = f_mclk ? f_mclk() : 8e6;
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time(double(ciclos_) / (f > 0 ? f : 8e6), sc_core::SC_SEC);
        ev_.notify(fin_ - sc_core::sc_time_stamp());
    }
    void proc() {
        for (;;) {
            wait(ev_);
            if (!busy_) continue;
            busy_ = false;
            r_[IFG] |= PMCMD;
            if (al_cambiar) al_cambiar();
            if (al_acabar) al_acabar();
        }
    }
    uint16_t de_codigo(uint16_t dir) {
        // La huella FNV-1a de lo cargado desde `dir` (hasta un hueco)
        uint32_t h = 0x811C9DC5u;
        for (uint16_t d = dir; codigo_.count(d); ++d) {
            const uint32_t x = codigo_[d];
            for (unsigned b = 0; b < 4; ++b) { h ^= (x >> (8 * b)) & 0xFFu; h *= 0x01000193u; }
        }
        static const struct { uint32_t h; uint16_t cmd; } T[] = {
            {0xF86EADA2u, 0x108}, {0x1FAFD922u, 0x110}, {0x6EB6F806u, 0x0DC}, {0x54EE9E46u, 0x118},
            {0xF92168E8u, 0x104}, {0x8A767B68u, 0x10C}, {0xD79F7686u, 0x0D8}, {0xA8E27CC6u, 0x114},
            {0xF28477DCu, 0x0D0}, {0x097736EFu, 0x0C0}, {0x23797890u, 0x09C}, {0xD4A00707u, 0x074},
            {0x0C6A3099u, 0x04C}, {0x27B6BEACu, 0x11C}, {0x49968EFDu, 0x090}, {0x4B2EEFD1u, 0x0C8},
            {0x0AC75EAFu, 0x124},
            {0x65A3D9DBu, 0xF000}, {0x2C860AACu, 0xF001}, {0x0133E973u, 0xF002}, {0x561963BDu, 0xF003},
        };
        for (const auto& t : T) if (t.h == h) return t.cmd;
        return 0xFFFF;
    }
    void ejecuta(uint16_t cmd) {
        const uint32_t s0 = r_[PMS0] & 0xFFFFu;
        switch (cmd) {
        case 0x040: case 0x070: case 0x03C: vec16(cmd, s0); break;          // ADD, SUB, MPY
        case 0x07C: case 0x0D4: case 0x074: vec32(cmd, s0); break;          // ADDLONG, SUBLONG, MPYLONG
        case 0x054: mac16(s0); break;
        case 0x0D0: mac32(s0); break;
        case 0x068: maccomplex(s0); break;
        case 0x078: mpycomplex(s0); break;
        case 0x034: mpyrow(s0); break;
        case 0x104: case 0x108: case 0x10C: case 0x110: extremo16(cmd, s0); break;
        case 0x0D8: case 0x0DC: case 0x114: case 0x118: extremo32(cmd, s0); break;
        case 0x018: fir16(s0); break;
        case 0x0C8: fir32(s0); break;
        case 0x0E0: fircomplex16(s0); break;
        case 0x124: fircomplex32(s0); break;
        case 0x04C: iir_df1(s0); break;
        case 0x11C: iir_df2(s0, false); break;
        case 0x090: iir_df2(s0, true); break;
        case 0x084: case 0x088: bitrev(s0, par(0), cmd == 0x088, false); break;
        case 0x0AC: case 0x0B4: bitrev(s0, par(0), cmd == 0x0B4, true); break;
        case 0x010: fft16(s0, 2u * par(0), false); break;
        case 0x0C0: fft16(s0, 2u * par(0), true); break;
        case 0x09C: fft32(s0, par(0)); break;
        case 0x028: split16(s0, par(0)); break;
        case 0x0A4: split32(s0, par(0)); break;
        case 0x058: case 0x05C: case 0x060: case 0x064: deinter16(cmd, s0); break;
        case 0x12C: deinter32(s0); break;
        case 0xF000: case 0xF001: case 0xF002: case 0xF003: inter16(s0); break;
        case 0x000: case 0x008: break;                                     // SUSPEND, RESUME
        default: aviso_una(cmd, "no está en el modelo (DSPLib no la usa): acaba sin hacer nada"); break;
        }
    }
    // ADDMATRIX, SUBMATRIX, MPYMATRIX: {vectorSize, input2, output, in1Off, in2Off, outOff}
    void vec16(uint16_t cmd, uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2), da = par(3), db = par(4), dout = par(5);
        for (unsigned k = 0; k < n / 2; ++k) {
            const uint32_t x = rd32(a + k * da), y = rd32(b + k * db);
            uint32_t z = 0;
            for (unsigned h = 0; h < 2; ++h) {
                const int16_t p = int16_t(x >> (16 * h)), q = int16_t(y >> (16 * h));
                const int16_t r = cmd == 0x040 ? sat16(int32_t(p) + q) : cmd == 0x070 ? sat16(int32_t(p) - q) : q15mpy(p, q);
                z |= uint32_t(uint16_t(r)) << (16 * h);
            }
            wr32(o + k * dout, z);
        }
        ciclos_ += n / 2;
    }
    void vec32(uint16_t cmd, uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2), da = par(3), db = par(4), dout = par(5);
        for (unsigned k = 0; k < n; ++k) {
            const int32_t x = int32_t(rd32(a + k * da)), y = int32_t(rd32(b + k * db));
            const int32_t r = cmd == 0x07C ? sat32(int64_t(x) + y) : cmd == 0x0D4 ? sat32(int64_t(x) - y) : q31mpy(x, y);
            wr32(o + k * dout, uint32_t(r));
        }
        ciclos_ += n;
    }
    // MAC: {vectorSize, input2, output}: el producto escalar Q15 en IQ31
    void mac16(uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2);
        int64_t acc = 0;
        for (unsigned e = 0; e < n; ++e) acc += int64_t(rd16(a, e)) * rd16(b, e);
        wr32(o, uint32_t(sat32(acc * 2)));
        ciclos_ += n / 2;
    }
    // MACLONGMATRIX: {vectorSize, input2, output, in1Off, in2Off}
    void mac32(uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2), da = par(3), db = par(4);
        int64_t acc = 0;
        for (unsigned k = 0; k < n; ++k) acc += q31mpy(int32_t(rd32(a + k * da)), int32_t(rd32(b + k * db)));
        wr32(o, uint32_t(sat32(acc)));
        ciclos_ += n;
    }
    // MACCOMPLEXMATRIX: {vectorSize, input2, output, in1Off, in2Off}: complejos Q15, el resultado IQ31 complejo
    void maccomplex(uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2), da = par(3), db = par(4);
        int64_t re = 0, im = 0;
        for (unsigned k = 0; k < n; ++k) {
            const uint32_t x = rd32(a + k * da), y = rd32(b + k * db);
            const int16_t ar = int16_t(x), ai = int16_t(x >> 16), br = int16_t(y), bi = int16_t(y >> 16);
            re += int64_t(ar) * br - int64_t(ai) * bi;
            im += int64_t(ar) * bi + int64_t(ai) * br;
        }
        wr32(o, uint32_t(sat32(re * 2))); wr32(o + 1, uint32_t(sat32(im * 2)));
        ciclos_ += 2 * n;
    }
    // MPYCOMPLEXMATRIX: {vectorSize, input2, output, in1Off, in2Off, outOff}
    void mpycomplex(uint32_t a) {
        const unsigned n = par(0), b = par(1), o = par(2), da = par(3), db = par(4), dout = par(5);
        for (unsigned k = 0; k < n; ++k) {
            const uint32_t x = rd32(a + k * da), y = rd32(b + k * db);
            const int16_t ar = int16_t(x), ai = int16_t(x >> 16), br = int16_t(y), bi = int16_t(y >> 16);
            const int16_t re = int16_t(sra(int64_t(ar) * br - int64_t(ai) * bi, 15));
            const int16_t im = int16_t(sra(int64_t(ar) * bi + int64_t(ai) * br, 15));
            wr32(o + k * dout, uint32_t(uint16_t(re)) | uint32_t(uint16_t(im)) << 16);
        }
        ciclos_ += 2 * n;
    }
    // MPYMATRIXROW: {rowSize, colSize, colVector, output}: una fila de A por B
    void mpyrow(uint32_t a) {
        const unsigned filas = par(0), cols = par(1), b = par(2), o = par(3);
        for (unsigned j = 0; j < cols; ++j) {
            int64_t acc = 0;
            for (unsigned k = 0; k < filas; ++k) acc += int64_t(rd16(a, k)) * rd16(b, k * cols + j);
            wr16(o, j, sat16(sra(acc, 15)));
        }
        ciclos_ += uint64_t(filas) * cols / 2;
    }
    // MAX, MIN y los UNSIGNED (Q15): {vectorSize, output}: el valor y su índice
    void extremo16(uint16_t cmd, uint32_t a) {
        const unsigned n = par(0), o = par(1);
        const bool max = cmd == 0x104 || cmd == 0x10C, sin_signo = cmd == 0x10C || cmd == 0x110;
        int32_t mejor = 0; unsigned idx = 0;
        for (unsigned e = 0; e < n; ++e) {
            const int16_t v16 = rd16(a, e);
            const int32_t v = sin_signo ? int32_t(uint16_t(v16)) : int32_t(v16);
            if (e == 0 || (max ? v > mejor : v < mejor)) { mejor = v; idx = e; }
        }
        wr32(o, uint32_t(uint16_t(mejor)) | uint32_t(uint16_t(idx)) << 16);
        ciclos_ += n / 2;
    }
    // MAXLONGMATRIX, MINLONGMATRIX y los UNSIGNED: {vectorSize, output, inputOffset}
    void extremo32(uint16_t cmd, uint32_t a) {
        const unsigned n = par(0), o = par(1), d = par(2) ? par(2) : 1u;
        const bool max = cmd == 0x0D8 || cmd == 0x114, sin_signo = cmd == 0x114 || cmd == 0x118;
        int64_t mejor = 0; unsigned idx = 0;
        for (unsigned k = 0; k < n; ++k) {
            const uint32_t w = rd32(a + k * d);
            const int64_t v = sin_signo ? int64_t(w) : int64_t(int32_t(w));
            if (k == 0 || (max ? v > mejor : v < mejor)) { mejor = v; idx = k; }
        }
        wr32(o, uint32_t(mejor)); wr32(o + 1, idx);
        ciclos_ += n;
    }
    // El búfer circular de los FIR: la dirección `base + k` (en palabras) con bufferMask
    static uint32_t circ(uint32_t base, uint32_t k, uint32_t mask) { return (base & ~mask) | ((base + k) & mask); }
    // FIR: {vectorSize, coeffs, output, tapLength, bufferMask}
    void fir16(uint32_t a) {
        const unsigned n = par(0), c = par(1), o = par(2), t = par(3);
        const uint32_t mask = par(4);
        auto x = [&](unsigned e) { const uint32_t v = rd32(circ(a, e >> 1, mask)); return int16_t((e & 1u) ? v >> 16 : v); };
        for (unsigned i = 0; i < n; ++i) {
            int64_t acc = 0;
            for (unsigned j = 0; j < t; ++j) acc += int64_t(x(i + j)) * rd16(c, t - 1 - j);
            wr16(o, i, sat16(sra(acc, 15)));
        }
        ciclos_ += uint64_t(n) * t / 2;
    }
    void fir32(uint32_t a) {
        const unsigned n = par(0), c = par(1), o = par(2), t = par(3);
        const uint32_t mask = par(4);
        for (unsigned i = 0; i < n; ++i) {
            int64_t acc = 0;
            for (unsigned j = 0; j < t; ++j) acc = sat32(acc + q31mpy(int32_t(rd32(circ(a, i + j, mask))), int32_t(rd32(c + t - 1 - j))));
            wr32(o + i, uint32_t(int32_t(acc)));
        }
        ciclos_ += uint64_t(n) * t;
    }
    void fircomplex16(uint32_t a) {
        const unsigned n = par(0), c = par(1), o = par(2), t = par(3);
        const uint32_t mask = par(4);
        for (unsigned i = 0; i < n; ++i) {
            int64_t re = 0, im = 0;
            for (unsigned j = 0; j < t; ++j) {
                const uint32_t x = rd32(circ(a, i + j, mask)), y = rd32(c + t - 1 - j);
                const int16_t sr = int16_t(x), si = int16_t(x >> 16), cr = int16_t(y), ci = int16_t(y >> 16);
                re += int64_t(sr) * cr - int64_t(si) * ci;
                im += int64_t(sr) * ci + int64_t(si) * cr;
            }
            wr32(o + i, uint32_t(uint16_t(sat16(sra(re, 15)))) | uint32_t(uint16_t(sat16(sra(im, 15)))) << 16);
        }
        ciclos_ += uint64_t(n) * t * 2;
    }
    void fircomplex32(uint32_t a) {
        const unsigned n = par(0), c = par(1), o = par(2), t = par(3);
        const uint32_t mask = par(4);
        for (unsigned i = 0; i < n; ++i) {
            int64_t re = 0, im = 0;
            for (unsigned j = 0; j < t; ++j) {
                const int32_t sr = int32_t(rd32(circ(a, 2 * (i + j), mask))), si = int32_t(rd32(circ(a, 2 * (i + j) + 1, mask)));
                const int32_t cr = int32_t(rd32(c + 2 * (t - 1 - j))), ci = int32_t(rd32(c + 2 * (t - 1 - j) + 1));
                re += int64_t(q31mpy(sr, cr)) - q31mpy(si, ci);
                im += int64_t(q31mpy(sr, ci)) + q31mpy(si, cr);
            }
            wr32(o + 2 * i, uint32_t(sat32(re))); wr32(o + 2 * i + 1, uint32_t(sat32(im)));
        }
        ciclos_ += uint64_t(n) * t * 4;
    }
    // IIRBQ1 (DF1): {vectorSizeBy2, output, state, coeffs, direction}
    //   coeffs: b2, b1By2, b0, -, a2, a1By2; state: x2, x1, y2, y1
    void iir_df1(uint32_t a) {
        const unsigned n = 2u * par(0), o = par(1), st = par(2), cf = par(3);
        const int16_t b2 = rd16(cf, 0), b1 = rd16(cf, 1), b0 = rd16(cf, 2), a2 = rd16(cf, 4), a1 = rd16(cf, 5);
        int16_t x2 = rd16(st, 0), x1 = rd16(st, 1), y2 = rd16(st, 2), y1 = rd16(st, 3);
        for (unsigned i = 0; i < n; ++i) {
            const int16_t x0 = rd16(a, i);
            const int64_t acc = int64_t(x2) * b2 + 2 * int64_t(x1) * b1 + int64_t(x0) * b0 + int64_t(y2) * a2 + 2 * int64_t(y1) * a1;
            const int16_t y0 = sat16(sra(acc, 15));
            x2 = x1; x1 = x0; y2 = y1; y1 = y0;
            wr16(o, i, y0);
        }
        wr16(st, 0, x2); wr16(st, 1, x1); wr16(st, 2, y2); wr16(st, 3, y1);
        ciclos_ += 3u * n;
    }
    // IIRBQ2 (DF2) e IIRBQ2EXTENDED: {vectorSizeBy2, output, state, coeffs}
    //   coeffs: (ext: bias IQ31), -, a2, a1By2, a0, b0, b1By2, b2, -
    //   state: q1, q2 (ext: minEven, minOdd, maxEven, maxOdd, min, max)
    void iir_df2(uint32_t a, bool ext) {
        const unsigned n = 2u * par(0), o = par(1), st = par(2), cf = par(3);
        const unsigned b = ext ? 2u : 0u;
        const int64_t bias = ext ? int64_t(int32_t(rd32(cf))) : 0;
        const int16_t a2 = rd16(cf, b + 1), a1 = rd16(cf, b + 2), a0 = rd16(cf, b + 3), b0 = rd16(cf, b + 4), b1 = rd16(cf, b + 5),
                      b2c = rd16(cf, b + 6);
        int16_t w1 = rd16(st, 0), w2 = rd16(st, 1);
        // los extremos parten de los del estado (DSPLib los pone a 0 antes)
        int16_t mn[2] = {INT16_MAX, INT16_MAX}, mx[2] = {INT16_MIN, INT16_MIN};
        if (ext) { mn[0] = rd16(st, 2); mn[1] = rd16(st, 3); mx[0] = rd16(st, 4); mx[1] = rd16(st, 5); }
        for (unsigned i = 0; i < n; ++i) {
            const int16_t x = rd16(a, i);
            const int64_t s0 = int64_t(x) * a0 + 2 * int64_t(w1) * a1 + int64_t(w2) * a2;
            const int16_t w0 = ext ? sat16(sra(bias + 2 * s0, 16)) : sat16(sra(s0, 15));
            const int16_t y = sat16(sra(int64_t(w0) * b0 + 2 * int64_t(w1) * b1 + int64_t(w2) * b2c, 15));
            wr16(o, i, y);
            w2 = w1; w1 = w0;
            if (ext) { mn[i & 1] = std::min(mn[i & 1], w0); mx[i & 1] = std::max(mx[i & 1], w0); }
        }
        wr16(st, 0, w1); wr16(st, 1, w2);
        if (ext) {
            wr16(st, 2, mn[0]); wr16(st, 3, mn[1]); wr16(st, 4, mx[0]); wr16(st, 5, mx[1]);
            wr16(st, 6, std::min(mn[0], mn[1])); wr16(st, 7, std::max(mx[0], mx[1]));
        }
        ciclos_ += 4u * n;
    }
    // BITREVERSE: {sqrtVectorSize}: N = s² (EVEN) o 2·s² (ODD) complejos
    void bitrev(uint32_t a, unsigned s, bool impar, bool largo) {
        const unsigned n = s * s * (impar ? 2u : 1u);
        unsigned bits = 0; while ((1u << bits) < n) ++bits;
        const unsigned esc = largo ? 2u : 1u;
        for (unsigned i = 0; i < n; ++i) {
            unsigned r = 0;
            for (unsigned b = 0; b < bits; ++b) if (i & (1u << b)) r |= 1u << (bits - 1 - b);
            if (i < r)
                for (unsigned h = 0; h < esc; ++h) {
                    const uint32_t x = rd32(a + esc * i + h), y = rd32(a + esc * r + h);
                    wr32(a + esc * i + h, y); wr32(a + esc * r + h, x);
                }
        }
        ciclos_ += n;
    }
    // FFTCOMPLEXFIXEDSCALING / AUTOSCALING (Q15, N complejos ya invertidos)
    void fft16(uint32_t a, unsigned n, bool autoesc) {
        auto rd = [&](unsigned k, int16_t& re, int16_t& im) { const uint32_t v = rd32(a + k); re = int16_t(v); im = int16_t(v >> 16); };
        auto wr = [&](unsigned k, int16_t re, int16_t im) { wr32(a + k, uint32_t(uint16_t(re)) | uint32_t(uint16_t(im)) << 16); };
        auto fuera = [](int16_t v) { return v > (INT16_MAX >> 2) || v < (INT16_MIN >> 2); };
        bool esc = true, ovf = false;
        unsigned shift = 0;
        if (autoesc) {
            for (unsigned k = 0; k < n && !ovf; ++k) { int16_t r, i; rd(k, r, i); ovf = fuera(r) || fuera(i); }
            esc = ovf; ovf = false; shift += esc ? 1 : 0;
        }
        auto mariposa = [&](unsigned ia, unsigned ib, int16_t tr, int16_t ti) {
            int16_t ar, ai, br, bi; rd(ia, ar, ai); rd(ib, br, bi);
            const int16_t pr = int16_t(sra(int64_t(tr) * br - int64_t(ti) * bi, 15));
            const int16_t pi = int16_t(sra(int64_t(ti) * br + int64_t(tr) * bi, 15));
            int16_t nbr = int16_t(ar - pr), nbi = int16_t(ai - pi), nar = int16_t(ar + pr), nai = int16_t(ai + pi);
            if (esc) { nbr = int16_t((ar - pr) >> 1); nbi = int16_t((ai - pi) >> 1); nar = int16_t((ar + pr) >> 1); nai = int16_t((ai + pi) >> 1); }
            wr(ib, nbr, nbi); wr(ia, nar, nai);
            if (autoesc && !ovf) ovf = fuera(nar) || fuera(nai) || fuera(nbr) || fuera(nbi);
        };
        auto c0 = [&](unsigned ia, unsigned ib) {
            int16_t ar, ai, br, bi; rd(ia, ar, ai); rd(ib, br, bi);
            int16_t nbr = int16_t(ar - br), nbi = int16_t(ai - bi), nar = int16_t(ar + br), nai = int16_t(ai + bi);
            if (esc) { nbr = int16_t((ar - br) >> 1); nbi = int16_t((ai - bi) >> 1); nar = int16_t((ar + br) >> 1); nai = int16_t((ai + bi) >> 1); }
            wr(ib, nbr, nbi); wr(ia, nar, nai);
            if (autoesc && !ovf) ovf = fuera(nar) || fuera(nai) || fuera(nbr) || fuera(nbi);
        };
        auto c1 = [&](unsigned ia, unsigned ib) {
            int16_t ar, ai, br, bi; rd(ia, ar, ai); rd(ib, br, bi);
            int16_t nbr = int16_t(ar - bi), nbi = int16_t(ai + br), nar = int16_t(ar + bi), nai = int16_t(ai - br);
            if (esc) { nbr = int16_t((ar - bi) >> 1); nbi = int16_t((ai + br) >> 1); nar = int16_t((ar + bi) >> 1); nai = int16_t((ai - br) >> 1); }
            wr(ib, nbr, nbi); wr(ia, nar, nai);
            if (autoesc && !ovf) ovf = fuera(nar) || fuera(nai) || fuera(nbr) || fuera(nbi);
        };
        if (2 <= n) for (unsigned j = 0; j < n; j += 2) c0(j, j + 1);
        if (autoesc) { esc = ovf; ovf = false; shift += esc ? 1 : 0; }
        if (4 <= n) for (unsigned j = 0; j < n; j += 4) { c0(j, j + 2); c1(j + 1, j + 3); }
        for (unsigned paso = 8; paso <= n; paso *= 2) {
            if (autoesc) { esc = ovf; ovf = false; shift += esc ? 1 : 0; }
            for (unsigned i = 0; i < paso / 2; ++i) {
                int16_t tr, ti; giro(i, paso, tr, ti);
                for (unsigned j = i; j < n; j += paso) mariposa(j, j + paso / 2, tr, ti);
            }
        }
        if (autoesc) r_[PMDST] = shift;
        unsigned lg = 0; while ((1u << lg) < n) ++lg;
        ciclos_ += uint64_t(n / 2) * lg;
    }
    // FFTCOMPLEXLONG (IQ31, sin escalar): {vectorSize, log2Size}
    void fft32(uint32_t a, unsigned n) {
        auto rd = [&](unsigned k, int32_t& re, int32_t& im) { re = int32_t(rd32(a + 2 * k)); im = int32_t(rd32(a + 2 * k + 1)); };
        auto wr = [&](unsigned k, int32_t re, int32_t im) { wr32(a + 2 * k, uint32_t(re)); wr32(a + 2 * k + 1, uint32_t(im)); };
        auto m = [&](unsigned ia, unsigned ib, int64_t pr, int64_t pi) {
            int32_t ar, ai; rd(ia, ar, ai);
            wr(ib, int32_t(uint32_t(ar - pr)), int32_t(uint32_t(ai - pi)));
            wr(ia, int32_t(uint32_t(ar + pr)), int32_t(uint32_t(ai + pi)));
        };
        if (2 <= n) for (unsigned j = 0; j < n; j += 2) { int32_t br, bi; rd(j + 1, br, bi); m(j, j + 1, br, bi); }
        if (4 <= n) for (unsigned j = 0; j < n; j += 4) {
            int32_t br, bi; rd(j + 2, br, bi); m(j, j + 2, br, bi);
            rd(j + 3, br, bi);
            int32_t ar, ai; rd(j + 1, ar, ai);
            wr(j + 3, int32_t(uint32_t(ar - bi)), int32_t(uint32_t(ai + br)));
            wr(j + 1, int32_t(uint32_t(ar + bi)), int32_t(uint32_t(ai - br)));
        }
        for (unsigned paso = 8; paso <= n; paso *= 2)
            for (unsigned i = 0; i < paso / 2; ++i) {
                int16_t tr, ti; giro(i, paso, tr, ti);
                for (unsigned j = i; j < n; j += paso) {
                    int32_t br, bi; rd(j + paso / 2, br, bi);
                    const int64_t pr = sra(int64_t(tr) * br - int64_t(ti) * bi, 15);
                    const int64_t pi = sra(int64_t(ti) * br + int64_t(tr) * bi, 15);
                    m(j, j + paso / 2, int32_t(uint32_t(pr)), int32_t(uint32_t(pi)));
                }
            }
        unsigned lg = 0; while ((1u << lg) < n) ++lg;
        ciclos_ += uint64_t(n / 2) * lg * 2;
    }
    // FFT y FFTLONG: el paso de partir de la FFT real [msp_split_q15.c]: N
    // reales (N/2 complejos), los giros W_N^k
    void split16(uint32_t a, unsigned n) {
        auto rd = [&](unsigned k, int16_t& re, int16_t& im) { const uint32_t v = rd32(a + k); re = int16_t(v); im = int16_t(v >> 16); };
        auto wr = [&](unsigned k, int16_t re, int16_t im) { wr32(a + k, uint32_t(uint16_t(re)) | uint32_t(uint16_t(im)) << 16); };
        int16_t r0, i0; rd(0, r0, i0); wr(0, int16_t(r0 + i0), 0);
        unsigned k = 1, nk = n / 2 - 1;
        for (unsigned c = 0; c + 1 < n / 4; ++c, ++k, --nk) {
            int16_t kr, ki, nr, ni; rd(k, kr, ki); rd(nk, nr, ni);
            int16_t br = int16_t(kr - nr), bi = int16_t(ki + ni);
            int16_t cr, ci; giro(k, n, cr, ci); cr = int16_t(cr >> 1); ci = int16_t(ci >> 1);
            const int16_t nbi = int16_t(sra(int64_t(bi) * cr + int64_t(br) * ci, 15));
            const int16_t nbr = int16_t(sra(int64_t(br) * cr - int64_t(bi) * ci, 15));
            br = nbr; bi = nbi;
            const int16_t ar = int16_t((kr + nr) >> 1), ai = int16_t((ki - ni) >> 1);
            wr(k, int16_t(ar + bi), int16_t(ai - br));
            wr(nk, int16_t(ar - bi), int16_t(-(ai + br)));
        }
        int16_t r, i; rd(k, r, i); wr(k, r, int16_t(-i));
        ciclos_ += n / 2;
    }
    // La de IQ31 no divide entre 2 (G(0) y G(N/2), por 2) [msp_split_iq31.c]
    void split32(uint32_t a, unsigned n) {
        auto rd = [&](unsigned k, int32_t& re, int32_t& im) { re = int32_t(rd32(a + 2 * k)); im = int32_t(rd32(a + 2 * k + 1)); };
        auto wr = [&](unsigned k, int32_t re, int32_t im) { wr32(a + 2 * k, uint32_t(re)); wr32(a + 2 * k + 1, uint32_t(im)); };
        auto u = [](int64_t x) { return int32_t(uint32_t(uint64_t(x))); };
        int32_t r0, i0; rd(0, r0, i0); wr(0, u(int64_t(uint32_t(r0) + uint32_t(i0)) << 1), 0);
        unsigned k = 1, nk = n / 2 - 1;
        for (unsigned c = 0; c + 1 < n / 4; ++c, ++k, --nk) {
            int32_t kr, ki, nr, ni; rd(k, kr, ki); rd(nk, nr, ni);
            const int32_t br = u(int64_t(kr) - nr), bi = u(int64_t(ki) + ni);
            int16_t cr, ci; giro(k, n, cr, ci);
            const int32_t nbr = u(sra(int64_t(cr) * br - int64_t(ci) * bi, 15));
            const int32_t nbi = u(sra(int64_t(cr) * bi + int64_t(ci) * br, 15));
            const int32_t ar = u(int64_t(kr) + nr), ai = u(int64_t(ki) - ni);
            wr(k, u(int64_t(ar) + nbi), u(int64_t(ai) - nbr));
            wr(nk, u(int64_t(ar) - nbi), u(-(int64_t(ai) + nbr)));
        }
        int32_t r, i; rd(k, r, i); wr(k, u(int64_t(r) << 1), u(-(int64_t(i) << 1)));
        ciclos_ += n;
    }
    // DEINTERLEAVE{EVEN,ODD}{EVEN,ODD}: {vectorSize, interleaveDepth, output}; la
    // primera paridad, la del canal (la mitad de la palabra de LEAPMS0)
    void deinter16(uint16_t cmd, uint32_t a) {
        const unsigned n = par(0), canales = par(1), o = par(2);
        const unsigned ini = (cmd == 0x060 || cmd == 0x064) ? 1u : 0u;
        for (unsigned i = 0; i < n; ++i) wr16(o, i, rd16(a, ini + i * canales));
        ciclos_ += n / 2;
    }
    void deinter32(uint32_t a) {
        const unsigned n = par(0), canales = par(1), o = par(2);
        for (unsigned i = 0; i < n; ++i) wr32(o + i, rd32(a + i * canales));
        ciclos_ += n;
    }
    // INTERLEAVE (cargada): {output, channel, numChannels, vectorSize}
    void inter16(uint32_t a) {
        const unsigned o = par(0), canal = par(1), canales = par(2), n = par(3);
        for (unsigned i = 0; i < n; ++i) wr16(o, canal + i * canales, rd16(a, i));
        ciclos_ += n / 2;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_LEA_H
