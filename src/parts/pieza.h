// =============================================================================
// pieza.h — La pieza que se mecaniza: un mapa de alturas que la fresa va
//           comiendo, su imagen vista desde arriba y las fuerzas del corte
//
// (Fase F4 de doc/analisis_cnc.md, §9.)
//
// LA PIEZA es un bloque de `largo` x `ancho` x `espesor` mm sujeto a la mesa,
// con su esquina (0, 0) donde estén los carros X e Y en (`x_mm`, `y_mm`). Por
// dentro, una REJILLA de celdas de `celda_mm` de lado, y en cada una la altura
// del material que queda, de 0 (la mesa) a `espesor`.
//
// LA FRESA está donde la ponen los tres carros (parts/transmision.h): su punta,
// en la pieza, en (X - x_mm, Y - y_mm) y a `Z - mesa_mm` por encima de la
// mesa, con `mesa_mm` la cota del carro Z con la punta tocando la mesa. Es
// plana, en V o de bola, de `fresa_mm` de diámetro: a una distancia r del eje
// su filo está a
//
//     plana    0                       por encima de la punta
//     bola     R - sqrt(R² - r²)
//     V        r / tan(ángulo / 2)
//
// Cada vez que la fresa se mueve, la pieza ESTAMPA ese perfil a lo largo del
// camino, a pasos de un cuarto de celda, y en cada celda se queda con lo más
// bajo: lo que la fresa ha barrido ya no está. Con la fresa por encima de la
// superficie no corta, pero deja su rastro, una línea tenue: la trayectoria en
// vacío.
//
// LO QUE SE VE es la imagen de la pieza desde arriba, una celda por píxel, con
// +Y hacia arriba, como en un plano -y en la planta de la fresadora-: el
// color del material, más oscuro cuanto más hondo, y la mesa donde se ha
// pasado de lado a lado. Cambia solo donde la fresa corta, y por eso sale a
// trozos (T_IMAGEN_RECT, common/protocolo.h).
//
// LAS FUERZAS DEL CORTE (Kienzle), si `fuerzas="si"`. De la rejilla sale lo
// que de verdad se quita por segundo, Q (mm³/s), y la energía específica del
// material da la potencia:
//
//     h_m = f_z = v_f / (n · z)          el espesor de viruta, por diente
//     k_c = k_c1.1 · h_m^(−m_c)          la fuerza específica (N/mm²)
//     P   = k_c · Q                      la potencia del corte
//     T   = P / ω                        el par en el cabezal
//     F_c = T / R                        la fuerza tangencial media
//
// T frena el cabezal -el MotorCC de la ref `cabezal`, un par resistente más-,
// y 0,6·F_c se reparte entre los carros según su velocidad, como un rozamiento
// seco más: avanzar más deprisa o más hondo frena el cabezal y puede hacer
// perder pasos. Los k_c1.1 son ORIENTATIVOS: valen para ver tendencias, no
// para predecir un mecanizado (§12 del análisis).
// =============================================================================
#ifndef STM32_PARTS_PIEZA_H
#define STM32_PARTS_PIEZA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "ext_parts.h"
#include "transmision.h"
#include "motor_cc.h"

namespace stm32 {

// Un material: su k_c1.1 (N/mm²), su exponente m_c y su color
struct MaterialPieza {
    const char* nombre;
    const char* texto;
    double      kc11, mc;
    uint8_t     r, g, b;
};
inline const MaterialPieza* material_pieza(const std::string& n) {
    static const MaterialPieza m[] = {
        {"mdf",         "MDF",         40.0,  0.30, 196, 164, 118},
        {"madera",      "madera",      25.0,  0.30, 226, 196, 140},
        {"metacrilato", "metacrilato", 150.0, 0.30, 196, 224, 234},
        {"aluminio",    "aluminio",    700.0, 0.25, 178, 186, 194},
    };
    for (const MaterialPieza& x : m) if (n == x.nombre) return &x;
    return nullptr;
}

enum class TipoFresa { Plana, V, Bola };

struct ConfigPieza {
    double largo_mm = 100.0, ancho_mm = 60.0, espesor_mm = 12.0;   // X, Y, Z
    double x_mm = 100.0, y_mm = 70.0;    // los carros X e Y con la fresa en la esquina (0, 0)
    double mesa_mm = 3.0;                // el carro Z con la punta en la mesa
    double celda_mm = 0.25;
    TipoFresa fresa = TipoFresa::Plana;
    double fresa_mm = 3.175;             // el diámetro
    double angulo_v = 60.0;              // el de la punta de una fresa en V, en grados
    unsigned filos = 2;
    const MaterialPieza* material = nullptr;
    double rpm = 10000.0;                // el cabezal, si no hay ref `cabezal`
    bool fuerzas = true;
    std::string mapa;                    // si no está vacío: el mapa de alturas, al acabar
    std::string cabezal;                 // el id del MotorCC del cabezal, si lo hay
};

SC_MODULE(Pieza), public ExtPartBase {
    Pieza(sc_core::sc_module_name nm, Carro& x, Carro& y, Carro& z, const ConfigPieza& c)
        : sc_core::sc_module(nm), ExtPartBase("Pieza", nm), cx_(&x), cy_(&y), cz_(&z), c_(c) {
        if (!c_.material) c_.material = material_pieza("mdf");
        nx_ = std::max(1, int(std::lround(c_.largo_mm / c_.celda_mm)));
        ny_ = std::max(1, int(std::lround(c_.ancho_mm / c_.celda_mm)));
        r_ = 0.5 * c_.fresa_mm;
        tan_v_ = std::tan(0.5 * c_.angulo_v * transmision::PI / 180.0);
        nueva();
        x.vigilado();
        y.vigilado();
        z.vigilado();
        SC_HAS_PROCESS(Pieza);
        SC_THREAD(vigila);
    }

    // --- Lo que se le pregunta -----------------------------------------------
    const ConfigPieza& config() const { return c_; }
    int nx() const { return nx_; }
    int ny() const { return ny_; }
    // La altura del material en la celda (i, j), de 0 a espesor
    double altura(int i, int j) const { return h_[std::size_t(j) * nx_ + i]; }
    double quitado_mm3() const { return quitado_; }
    double hondo_mm() const { return hondo_; }
    double q_mm3_s() const { return q_; }
    double par_nm() const { return par_; }
    double fuerza_n() const { return fuerza_; }
    bool   choque() const { return choque_; }
    const MotorCC* cabezal() const { return cab_; }
    // La pieza nueva, sin mecanizar
    void nueva() {
        h_.assign(std::size_t(nx_) * ny_, float(c_.espesor_mm));
        paso_.assign(h_.size(), 0);
        quitado_ = hondo_ = 0.0;
        choque_ = false;
        ++version_;
    }
    // Escribe el mapa de alturas: una fila por cada celda en Y, de Y = 0 hacia
    // arriba, y en cada fila la altura de cada celda en X, en mm
    bool guarda_mapa(const std::string& ruta) const {
        std::FILE* f = std::fopen(ruta.c_str(), "w");
        if (!f) return false;
        std::fprintf(f, "# pieza %s: %s de %g x %g x %g mm, celdas de %g mm (%d x %d)\n",
                     pieza().c_str(), c_.material->texto, c_.largo_mm, c_.ancho_mm,
                     c_.espesor_mm, c_.celda_mm, nx_, ny_);
        std::fprintf(f, "# fila j: Y = (j + 0,5) x celda; columna i: X = (i + 0,5) x celda\n");
        for (int j = 0; j < ny_; ++j) {
            for (int i = 0; i < nx_; ++i)
                std::fprintf(f, i ? ",%.4f" : "%.4f", altura(i, j));
            std::fputc('\n', f);
        }
        std::fclose(f);
        return true;
    }

    // --- Observables, mandos e imagen ------------------------------------------
    // `mesa` es la posición del carro Y: lo que mueve la imagen con la mesa en
    // el dibujo (efecto pantalla con observable="mesa" y su vector)
    unsigned n_observables() const override { return 7; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"quitado", "mm3", 0.f, 1e5f, false};
            case 1: return {"hondo", "mm", 0.f, float(c_.espesor_mm), true};
            case 2: return {"q", "mm3/s", 0.f, 2000.f, false};
            case 3: return {"par", "N·cm", 0.f, 50.f, false};
            case 4: return {"fuerza", "N", 0.f, 200.f, false};
            case 5: return {"mesa", "mm", -1000.f, 1000.f, false};
            default: return {"choque", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(quitado_);
            case 1: return float(hondo_);
            case 2: return float(q_);
            case 3: return float(par_ * 100.0);
            case 4: return float(fuerza_);
            case 5: return float(cy_->posicion_mm());
            default: return choque_ ? 1.f : 0.f;
        }
    }
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"nueva", Mando::Boton, 0.f, 1.f}; }
    float valor_mando(unsigned) const override { return 0.f; }
    void acciona(unsigned, float v) override { if (v >= 0.5f) nueva(); }

    unsigned n_imagenes() const override { return 1; }
    Imagen   imagen(unsigned) const override { return {"pieza", uint16_t(nx_), uint16_t(ny_)}; }
    uint64_t version_imagen(unsigned) const override { return version_; }
    // Siempre con toda su luz: la luz acumulada es el tiempo
    double   luz_acumulada(unsigned) const override { return sc_core::sc_time_stamp().to_seconds(); }
    // Como se ve desde arriba en un plano: X a la derecha e Y hacia ARRIBA -la
    // fila de arriba de la imagen es la última de la rejilla-
    void pinta_imagen(unsigned, std::string& rgb) const override {
        rgb.resize(h_.size() * 3u);
        const MaterialPieza& m = *c_.material;
        const double e = c_.espesor_mm;
        for (std::size_t q = 0; q < h_.size(); ++q) {
            const std::size_t fila = q / std::size_t(nx_), col = q % std::size_t(nx_);
            const std::size_t k = (std::size_t(ny_) - 1u - fila) * std::size_t(nx_) + col;
            double r = m.r, g = m.g, b = m.b;
            const double d = e - h_[k];
            if (h_[k] <= 1e-4) {                       // de lado a lado: la mesa
                r = 70; g = 72; b = 78;
            } else if (d > 1e-3) {                     // cortado: más oscuro cuanto más hondo
                const double f = 0.82 - 0.5 * (d / e);
                r *= f; g *= f; b *= f;
            } else if (paso_[k]) {                     // la fresa pasó por encima
                r = 0.7 * r + 0.3 * 90; g = 0.7 * g + 0.3 * 140; b = 0.7 * b + 0.3 * 220;
            }
            rgb[3 * q]     = char(uint8_t(r));
            rgb[3 * q + 1] = char(uint8_t(g));
            rgb[3 * q + 2] = char(uint8_t(b));
        }
    }

    void end_of_simulation() override {
        if (!c_.mapa.empty() && !guarda_mapa(c_.mapa))
            SC_REPORT_WARNING("pieza", (pieza() + ": no puedo escribir el mapa en " + c_.mapa).c_str());
    }

private:
    void start_of_simulation() override {
        if (c_.cabezal.empty()) return;
        for (ExtPartBase* p : ExtPartBase::inventario())
            if (p->pieza() == c_.cabezal) cab_ = dynamic_cast<MotorCC*>(p);
        if (!cab_)
            SC_REPORT_WARNING("pieza", (pieza() + ": su cabezal, '" + c_.cabezal + "', no es un "
                                        "MotorCC de este montaje: el corte no lo frena, y la "
                                        "fresa gira a " + std::to_string(int(c_.rpm)) + " rpm")
                                           .c_str());
    }

    // Dónde está la punta, en la pieza: (u, v) en mm y t por encima de la mesa
    void punta(double& u, double& v, double& t) const {
        u = cx_->posicion_mm() - c_.x_mm;
        v = cy_->posicion_mm() - c_.y_mm;
        t = cz_->posicion_mm() - c_.mesa_mm;
    }
    double perfil(double r) const {
        switch (c_.fresa) {
            case TipoFresa::Plana: return 0.0;
            case TipoFresa::Bola:  return r_ - std::sqrt(std::max(0.0, r_ * r_ - r * r));
            default:               return r / tan_v_;
        }
    }
    // La fresa, quieta en (u, v, t): lo que queda por debajo de su filo se va.
    // Devuelve el volumen quitado, en mm³
    double estampa(double u, double v, double t) {
        const double c = c_.celda_mm;
        const int ic = int(std::floor(u / c)), jc = int(std::floor(v / c));
        if (ic >= 0 && ic < nx_ && jc >= 0 && jc < ny_ && t >= h_[std::size_t(jc) * nx_ + ic]) {
            uint8_t& p = paso_[std::size_t(jc) * nx_ + ic];
            if (!p) { p = 1; ++version_; }
        }
        if (t >= c_.espesor_mm) return 0.0;              // por encima de todo: no toca
        const int i0 = std::max(0, int(std::floor((u - r_) / c))),
                  i1 = std::min(nx_ - 1, int(std::floor((u + r_) / c)));
        const int j0 = std::max(0, int(std::floor((v - r_) / c))),
                  j1 = std::min(ny_ - 1, int(std::floor((v + r_) / c)));
        double dv = 0.0;
        for (int j = j0; j <= j1; ++j) {
            const double dy = (j + 0.5) * c - v;
            for (int i = i0; i <= i1; ++i) {
                const double dx = (i + 0.5) * c - u;
                const double r = std::sqrt(dx * dx + dy * dy);
                if (r > r_) continue;
                const double z = t + perfil(r);
                float& h = h_[std::size_t(j) * nx_ + i];
                if (z >= h) continue;
                if (z < -1e-3 && !choque_) {
                    choque_ = true;
                    char m[200];
                    std::snprintf(m, sizeof m, "%s: la fresa entra %.2f mm en la mesa, en X %.2f "
                                  "Y %.2f de la pieza", pieza().c_str(), -z, (i + 0.5) * c,
                                  (j + 0.5) * c);
                    SC_REPORT_WARNING("pieza", m);
                }
                const double zn = std::max(0.0, z);
                dv += (h - zn) * c * c;
                h = float(zn);
                hondo_ = std::max(hondo_, c_.espesor_mm - zn);
            }
        }
        if (dv > 0.0) { quitado_ += dv; ++version_; }
        return dv;
    }
    // De la última punta estampada a la de ahora, a pasos de un cuarto de celda
    double barre(double u, double v, double t) {
        const double du = u - u0_, dw = v - v0_, dt = t - t0_;
        const double l = std::max(std::sqrt(du * du + dw * dw), std::fabs(dt));
        const int n = std::max(1, int(std::ceil(l / (0.25 * c_.celda_mm))));
        double dv = 0.0;
        for (int k = 1; k <= n; ++k) {
            const double f = double(k) / n;
            dv += estampa(u0_ + f * du, v0_ + f * dw, t0_ + f * dt);
        }
        u0_ = u; v0_ = v; t0_ = t;
        return dv;
    }

    // Las fuerzas, con Q filtrado (5 ms): el par al cabezal y la resistencia a
    // los carros. Solo se tocan si cambian de verdad: cada cambio rehace la
    // cadena mecánica
    void fuerzas(double dv, double ahora) {
        const double tau = 5e-3;
        const double dt = std::max(0.0, ahora - t_q_);
        t_q_ = ahora;
        q_ = q_ * std::exp(-dt / tau) + dv / tau;
        if (q_ < 1e-3) q_ = 0.0;
        if (!c_.fuerzas) return;
        const double vx = cx_->velocidad_mm_s(), vy = cy_->velocidad_mm_s(),
                     vz = cz_->velocidad_mm_s();
        const double v3 = std::sqrt(vx * vx + vy * vy + vz * vz);
        const double n = cab_ ? std::fabs(cab_->rpm()) : c_.rpm;
        const MaterialPieza& m = *c_.material;
        double par = 0.0, fc = 0.0;
        if (q_ > 0.0) {
            if (n > 60.0) {
                const double fz = std::sqrt(vx * vx + vy * vy + vz * vz) / (n / 60.0 * c_.filos);
                const double hm = std::min(0.5, std::max(0.002, fz));
                const double kc = m.kc11 * std::pow(hm, -m.mc);
                const double w = n * 2.0 * transmision::PI / 60.0;
                par = kc * q_ * 1e-3 / w;                     // N·m
                fc = par / (r_ * 1e-3);
            } else {
                // La fresa no gira: empuja el material en vez de cortarlo. Con
                // la sección que barre, Q / v, y como mucho todo su costado
                fc = m.kc11 * std::min(q_ / std::max(v3, 0.5), c_.fresa_mm * c_.espesor_mm);
                if (!parada_avisada_) {
                    parada_avisada_ = true;
                    SC_REPORT_WARNING("pieza", (pieza() + ": la fresa esta parada y la maquina la "
                                                "mete en el material").c_str());
                }
            }
        }
        const double ff = 0.6 * fc;
        par_ = par;
        fuerza_ = ff;
        auto cambia = [](double a, double b, double abs_tol) {
            return std::fabs(a - b) > std::max(abs_tol, 0.05 * std::max(std::fabs(a), std::fabs(b)));
        };
        if (cab_ && cambia(cab_->par_externo(), par, 1e-4)) cab_->pon_par_externo(par);
        const double f[3] = {v3 > 1e-6 ? ff * std::fabs(vx) / v3 : 0.0,
                             v3 > 1e-6 ? ff * std::fabs(vy) / v3 : 0.0,
                             v3 > 1e-6 ? ff * std::fabs(vz) / v3 : 0.0};
        Carro* cs[3] = {cx_, cy_, cz_};
        for (int k = 0; k < 3; ++k)
            if (cambia(cs[k]->rozamiento_corte(), f[k], 0.05)) cs[k]->pon_rozamiento_corte(f[k]);
    }

    void vigila() {
        sc_core::sc_event_or_list l;
        l |= cx_->evento_mueve();
        l |= cy_->evento_mueve();
        l |= cz_->evento_mueve();
        punta(u0_, v0_, t0_);
        barre(u0_, v0_, t0_);
        const sc_core::sc_time quieto(1.0, sc_core::SC_MS);
        for (;;) {
            // Con un trozo pendiente o con fuerzas puestas, se espera también
            // a que pase un milisegundo sin moverse: entonces se estampa lo que
            // falte -el final exacto de un movimiento- y las fuerzas se apagan
            const bool pendiente = pend_ || q_ > 0.0 || fuerza_ > 0.0 || par_ > 0.0;
            if (pendiente) wait(quieto, l);
            else           wait(l);
            double u, v, t;
            punta(u, v, t);
            const double paso = 0.25 * c_.celda_mm;
            const bool lejos = std::fabs(u - u0_) >= paso || std::fabs(v - v0_) >= paso ||
                               std::fabs(t - t0_) >= paso;
            const bool parado = pendiente && sc_core::sc_time_stamp() - t_mov_ >= quieto;
            double dv = 0.0;
            if (lejos || (parado && pend_)) {
                dv = barre(u, v, t);
                pend_ = false;
            } else if (u != u0_ || v != v0_ || t != t0_) {
                pend_ = true;
            }
            if (lejos || pend_) t_mov_ = sc_core::sc_time_stamp();
            fuerzas(dv, sc_core::sc_time_stamp().to_seconds());
        }
    }

    Carro *cx_, *cy_, *cz_;
    ConfigPieza c_;
    MotorCC* cab_ = nullptr;
    int nx_ = 1, ny_ = 1;
    double r_ = 1.0, tan_v_ = 1.0;
    std::vector<float> h_;
    std::vector<uint8_t> paso_;
    uint64_t version_ = 0;
    double quitado_ = 0.0, hondo_ = 0.0;
    bool choque_ = false, pend_ = false, parada_avisada_ = false;
    double u0_ = 0.0, v0_ = 0.0, t0_ = 0.0;
    sc_core::sc_time t_mov_ = sc_core::SC_ZERO_TIME;
    double q_ = 0.0, t_q_ = 0.0, par_ = 0.0, fuerza_ = 0.0;
};

} // namespace stm32

#endif // STM32_PARTS_PIEZA_H
