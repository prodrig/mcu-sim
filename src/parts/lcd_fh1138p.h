// =============================================================================
// lcd_fh1138p.h — El vidrio LCD FH-1138P de la MSP-EXP430FR4133 (M7 de
// doc/analisis_msp430.md)
//
// Un LCD de segmentos de 4-mux con 38 patillas: seis caracteres de 14
// segmentos (A1-A6, con su punto), dos puntos en A2 y A4, y símbolos (el
// signo menos, la antena, los grados, la batería y sus seis barras, el
// temporizador, el corazón, el «REC», «!», TX y RX) [SLAU595B, tabla 3]. Las
// patillas 21-24 son COM3-COM0; las 25-31 no van a nada.
//
// LO QUE VE: no hay memoria del controlador; el vidrio mira las TENSIONES de
// sus patillas, como un vidrio de verdad. En cada fase de la onda (cada
// cambio de un COM) el COM seleccionado es el que está en un extremo (0 o
// VLCD) mientras los otros están en medio; un segmento de ese COM se ve
// encendido si su patilla está en el extremo contrario (|Vseg - Vcom| > 0,8
// VLCD). VLCD es la mayor tensión vista en los COM. Sin cambios en los COM en
// 50 ms el vidrio se apaga (el LCD_E apagado o el chip sin alimentar)
// [de modelo: la respuesta del cristal, inmediata; no se integra el RMS].
//
// LOS CARACTERES: cada uno ocupa cuatro patillas, que con los cuatro COM dan
// 16 bits; ordenados como los dos bytes de LCDMEM que les da la placa
// (SLAU595B, tabla 4), el carácter se lee con las tablas de hal_LCD.c de la
// demo de TI (BSD): `digit` y `alphabetBig`. [Error de TI] La tabla 3 de
// SLAU595B pone «A3R» y «A4R» en las patillas 9 y 13: es la E (como en A1,
// A2, A5 y A6, y como lo usa hal_LCD.c).
//
// Observables: car1..car6 (el código ASCII de cada carácter, 32 en blanco) y
// tramas (las tramas completas vistas). Con muestra=si escribe el texto cada
// vez que cambia: «[LCD1] "WELCOM"».
// =============================================================================
#ifndef STM32_PARTS_LCD_FH1138P_H
#define STM32_PARTS_LCD_FH1138P_H

#include <systemc>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../common/analog_net.h"
#include "part_base.h"

namespace stm32 {

SC_MODULE(LcdFh1138p), public ExtPartBase {
    static constexpr unsigned NP = 38;
    // `pines[k]`: la patilla k+1 (nullptr: al aire)
    LcdFh1138p(sc_core::sc_module_name nm, const std::vector<analog_net_if*>& pines, bool muestra = false)
        : sc_core::sc_module(nm), ExtPartBase("LcdFh1138p", nm), muestra_(muestra) {
        pines_.assign(NP, nullptr);
        for (unsigned k = 0; k < NP && k < pines.size(); ++k) {
            pines_[k] = pines[k];
            if (pines[k]) add_ref(std::to_string(k + 1), *pines[k]);
        }
        for (unsigned i = 0; i < 6; ++i) nombres_.push_back("car" + std::to_string(i + 1));
        nombres_.push_back("tramas");
        SC_HAS_PROCESS(LcdFh1138p);
        SC_METHOD(mira);
        for (unsigned c = 0; c < 4; ++c) if (com(c)) { sensitive << com(c)->value_changed_event(); lista_ |= com(c)->value_changed_event(); }
        dont_initialize();
    }

    // Los seis caracteres, tal como se ven (' ' en blanco, '?' lo que no es letra ni cifra)
    std::string texto() const { return texto_; }
    // Con los puntos y los dos puntos: «12:34.5»
    std::string texto_con_signos() const {
        std::string s;
        for (unsigned i = 0; i < 6; ++i) {
            s += texto_[i];
            if (i == 1 && simbolo("A2COL")) s += ':';
            if (i == 3 && simbolo("A4COL")) s += ':';
            if (i < 5 && seg(DIG[i][2], 0)) s += '.';                  // AxDP: COM0 de la tercera patilla
        }
        return s;
    }
    // Un segmento por su nombre de la tabla 3 («A1A», «HRT», «B3»...)
    bool simbolo(const std::string& n) const {
        for (unsigned p = 0; p < NP; ++p)
            for (unsigned c = 0; c < 4; ++c)
                if (MAPA[p][3 - c] && n == MAPA[p][3 - c]) return seg(p, c);
        return false;
    }
    bool encendido() const { return vivo_; }
    uint64_t tramas() const { return tramas_; }

    unsigned n_observables() const override { return 7; }
    Observable observable(unsigned i) const override {
        if (i < 6) return {nombres_[i].c_str(), "ASCII", 32.f, 126.f, true};
        return {nombres_[6].c_str(), "", 0.f, 0.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i < 6) return float(uint8_t(texto_[i]));
        return float(tramas_);
    }

private:
    // [SLAU595B tabla 3] patilla → {COM3, COM2, COM1, COM0}
    static constexpr const char* MAPA[NP][4] = {
        {"A1E", "A1F", "A1G", "A1M"}, {"A1A", "A1B", "A1C", "A1D"}, {"A1Q", "NEG", "A1N", "A1DP"}, {"A1H", "A1J", "A1K", "A1P"},
        {"A2E", "A2F", "A2G", "A2M"}, {"A2A", "A2B", "A2C", "A2D"}, {"A2Q", "A2COL", "A2N", "A2DP"}, {"A2H", "A2J", "A2K", "A2P"},
        {"A3E", "A3F", "A3G", "A3M"}, {"A3A", "A3B", "A3C", "A3D"}, {"A3Q", "ANT", "A3N", "A3DP"}, {"A3H", "A3J", "A3K", "A3P"},
        {"A4E", "A4F", "A4G", "A4M"}, {"A4A", "A4B", "A4C", "A4D"}, {"A4Q", "A4COL", "A4N", "A4DP"}, {"A4H", "A4J", "A4K", "A4P"},
        {"A5E", "A5F", "A5G", "A5M"}, {"A5A", "A5B", "A5C", "A5D"}, {"A5Q", "DEG", "A5N", "A5DP"}, {"A5H", "A5J", "A5K", "A5P"},
        {"COM3", nullptr, nullptr, nullptr}, {nullptr, "COM2", nullptr, nullptr}, {nullptr, nullptr, "COM1", nullptr},
        {nullptr, nullptr, nullptr, "COM0"},
        {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr},
        {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr},
        {nullptr, nullptr, nullptr, nullptr},
        {"TMR", "HRT", "REC", "!"}, {"B6", "B4", "B2", "BATT"}, {"B5", "B3", "B1", "[]"},
        {"A6E", "A6F", "A6G", "A6M"}, {"A6A", "A6B", "A6C", "A6D"}, {"A6Q", "TX", "A6N", "RX"}, {"A6H", "A6J", "A6K", "A6P"},
    };
    // Las cuatro patillas de cada carácter (índice 0: la patilla 1)
    static constexpr unsigned DIG[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, 11}, {12, 13, 14, 15}, {16, 17, 18, 19}, {34, 35, 36, 37}};
    // [hal_LCD.c de la demo de TI, BSD] Los bytes de cada carácter: el bajo
    // con las dos primeras patillas, el alto con las otras dos
    static constexpr uint8_t CIFRA[10][2] = {
        {0xFC, 0x28}, {0x60, 0x20}, {0xDB, 0x00}, {0xF3, 0x00}, {0x67, 0x00},
        {0xB7, 0x00}, {0xBF, 0x00}, {0xE4, 0x00}, {0xFF, 0x00}, {0xF7, 0x00}};
    static constexpr uint8_t LETRA[26][2] = {
        {0xEF, 0x00}, {0xF1, 0x50}, {0x9C, 0x00}, {0xF0, 0x50}, {0x9F, 0x00}, {0x8F, 0x00}, {0xBD, 0x00},
        {0x6F, 0x00}, {0x90, 0x50}, {0x78, 0x00}, {0x0E, 0x22}, {0x1C, 0x00}, {0x6C, 0xA0}, {0x6C, 0x82},
        {0xFC, 0x00}, {0xCF, 0x00}, {0xFC, 0x02}, {0xCF, 0x02}, {0xB7, 0x00}, {0x80, 0x50}, {0x7C, 0x00},
        {0x0C, 0x28}, {0x6C, 0x0A}, {0x00, 0xAA}, {0x00, 0xB0}, {0x90, 0x28}};

    std::vector<analog_net_if*> pines_;
    std::vector<std::string> nombres_;
    bool muestra_;
    bool on_[NP][4] = {}, trama_ant_[NP][4] = {}, estable_[NP][4] = {};
    bool visto_[4] = {};
    bool vivo_ = false;
    double vlcd_ = 0.0;
    uint64_t tramas_ = 0;
    std::string texto_ = "      ";

    analog_net_if* com(unsigned c) const { return pines_[23 - c]; }   // COM0 en la patilla 24
    bool seg(unsigned p, unsigned c) const { return p < NP && c < 4 && estable_[p][c]; }
    static double v(const analog_net_if* n) { return !n || n->floating() ? -1.0 : double(n->voltage()); }

    void mira() {
        double vc[4];
        for (unsigned c = 0; c < 4; ++c) { vc[c] = v(com(c)); if (vc[c] > vlcd_) vlcd_ = vc[c]; }
        int sel = -1, n = 0;
        if (vlcd_ > 0.5) {
            for (unsigned c = 0; c < 4; ++c) {
                if (vc[c] < 0) continue;
                if (vc[c] < 0.15 * vlcd_ || vc[c] > 0.85 * vlcd_) { sel = int(c); ++n; }
            }
        }
        if (n == 1) {
            for (unsigned p = 0; p < NP; ++p) {
                if (p >= 20 && p <= 30) continue;                     // los COM y las patillas al aire
                const double vs = v(pines_[p]);
                on_[p][sel] = vs >= 0 && std::fabs(vs - vc[sel]) > 0.8 * vlcd_;
            }
            visto_[sel] = true;
            if (visto_[0] && visto_[1] && visto_[2] && visto_[3]) {
                visto_[0] = visto_[1] = visto_[2] = visto_[3] = false;
                ++tramas_;
                vivo_ = true;
                // Lo que se ve es lo que se queda dos tramas seguidas: una
                // trama a medio escribir (el programa cambia la memoria entre
                // dos COM) no llega a verse [de modelo: el cristal tarda
                // decenas de ms en responder]
                if (std::memcmp(on_, trama_ant_, sizeof on_) == 0) { std::memcpy(estable_, on_, sizeof on_); publica(); }
                std::memcpy(trama_ant_, on_, sizeof on_);
            }
        }
        // Sin onda en 50 ms: apagado
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        bool cambia = false;
        for (unsigned c = 0; c < 4; ++c) if (vc[c] != vc_ant_[c]) { cambia = true; vc_ant_[c] = vc[c]; }
        if (cambia) t_cambio_ = ahora;
        else if (ahora - t_cambio_ >= sc_core::sc_time(50, sc_core::SC_MS) && vivo_) {
            for (auto& f : on_) for (bool& b : f) b = false;
            std::memcpy(estable_, on_, sizeof on_); std::memcpy(trama_ant_, on_, sizeof on_);
            vivo_ = false; vlcd_ = 0.0;
            publica();
        }
        sc_core::next_trigger(sc_core::sc_time(50, sc_core::SC_MS), lista_);
    }
    sc_core::sc_event_or_list lista_;
    double vc_ant_[4] = {-9, -9, -9, -9};
    sc_core::sc_time t_cambio_{sc_core::SC_ZERO_TIME};
    char caracter(unsigned i) const {
        uint8_t b[2] = {0, 0};
        for (unsigned k = 0; k < 4; ++k)
            for (unsigned c = 0; c < 4; ++c)
                if (seg(DIG[i][k], c)) b[k / 2] = uint8_t(b[k / 2] | (1u << ((k & 1u) * 4 + c)));
        b[1] = uint8_t(b[1] & 0xFA);                                   // sin el punto ni el símbolo de la tercera patilla
        if (!b[0] && !b[1]) return ' ';
        for (unsigned d = 0; d < 10; ++d) if (b[0] == CIFRA[d][0] && b[1] == CIFRA[d][1]) return char('0' + d);
        for (unsigned l = 0; l < 26; ++l) if (b[0] == LETRA[l][0] && b[1] == LETRA[l][1]) return char('A' + l);
        if (b[0] == 0xFF && b[1] == 0xFA) return '*';                  // todos (lo que hal_LCD pone si no es letra ni cifra)
        return '?';
    }
    void publica() {
        std::string t(6, ' ');
        for (unsigned i = 0; i < 6; ++i) t[i] = caracter(i);
        if (t != texto_) {
            texto_ = t;
            if (muestra_) std::printf("[%s] \"%s\"\n", pieza().c_str(), texto_con_signos().c_str());
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_LCD_FH1138P_H
