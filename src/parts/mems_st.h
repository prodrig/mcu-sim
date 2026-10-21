// =============================================================================
// mems_st.h — Los sensores de la STM32F3DISCOVERY (fase S4 del F303)
//
//   Postura     la orientación de la placa -alabeo, cabeceo y rumbo-, su
//               velocidad angular y el campo de la Tierra en el sitio. Tres
//               mandos; al mover uno, la placa GIRA hasta ahí a su velocidad,
//               y de ese giro sale lo que ve el giróscopo. Sin ella, giróscopo,
//               acelerómetro y brújula contarían historias distintas
//   L3gd20      el giróscopo de las revisiones A-D (WHO_AM_I 0xD4), por SPI
//   I3g4250d    el de la E en adelante (0xD3), el mismo mapa
//   Lsm303dlhc  acelerómetro (0x19) y magnetómetro (0x1E) de las A-D, por I2C
//   Lsm303agr   los de la E en adelante (el magnetómetro, estilo LIS2MDL)
//
// LOS EJES. La placa: x hacia el ESTE -LD7, a la derecha de la rosa-, y hacia
// el NORTE -LD3, arriba, el lado del ST-LINK- y z saliendo por la cara de los
// componentes; la serigrafía de la rosa dice N, E, S y W [MB1035, hoja 4].
// Cada sensor dice los suyos respecto a esos en `ejes="..."` ("y,-x,z": su x
// es la y de la placa, su y la -x...). Los de la placa los fija su XML, y de
// dónde salen lo dice allí.
//
// LAS FUENTES. Los mapas de registros, los valores de reset, las tasas, las
// sensibilidades y los fondos de escala, de las hojas de datos de ST (L3GD20,
// I3G4250D, LSM303DLHC, LSM303AGR) [1F: no están en doc/refs/] y de los
// controladores de ST que se versionan en verif/fw/stm32f3/sdk/bsp/Components,
// que son los que los usan. Lo marcado [de modelo] no lo dice ninguno de los
// dos: está elegido y escrito aquí.
//
// Lo que NO hacen: la FIFO (solo el modo bypass: avisa una vez si se pide
// otra cosa), el «click» y el 6D de los acelerómetros (avisan), la duración
// de las interrupciones, el SPI de tres hilos, el modo I2C del giróscopo, el
// ruido y el desvío de cero. Los filtros paso alto, de primer orden.
// =============================================================================
#ifndef STM32_PARTS_MEMS_ST_H
#define STM32_PARTS_MEMS_ST_H

#include <systemc>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include "ext_parts.h"

namespace stm32 {

namespace mems {

struct V3 { double x = 0, y = 0, z = 0; };
struct M3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    V3 por(const V3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    V3 por_t(const V3& v) const {          // la traspuesta: del mundo a la placa
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }
    M3 operator*(const M3& b) const {
        M3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                r.m[i][j] = 0;
                for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * b.m[k][j];
            }
        return r;
    }
    M3 t() const {
        M3 r;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        return r;
    }
};
inline constexpr double PI = 3.14159265358979323846;
inline double rad(double g) { return g * PI / 180.0; }

// De la placa al mundo (x este, y norte, z arriba). Rumbo: hacia dónde mira el
// norte de la placa, en grados desde el norte magnético, en el sentido de las
// agujas; cabeceo: el borde norte hacia arriba, positivo; alabeo: el borde
// este hacia abajo, positivo. R = Rz(-rumbo)·Rx(cabeceo)·Ry(alabeo)
inline M3 rotacion(double alabeo, double cabeceo, double rumbo) {
    const double a = rad(alabeo), c = rad(cabeceo), r = rad(-rumbo);
    M3 ry, rx, rz;
    ry.m[0][0] = std::cos(a);  ry.m[0][2] = std::sin(a);
    ry.m[2][0] = -std::sin(a); ry.m[2][2] = std::cos(a);
    rx.m[1][1] = std::cos(c);  rx.m[1][2] = -std::sin(c);
    rx.m[2][1] = std::sin(c);  rx.m[2][2] = std::cos(c);
    rz.m[0][0] = std::cos(r);  rz.m[0][1] = -std::sin(r);
    rz.m[1][0] = std::sin(r);  rz.m[1][1] = std::cos(r);
    return rz * rx * ry;
}

// `ejes="y,-x,z"`: cada eje del sensor, como eje de la placa con su signo.
// Tiene que ser una rotación: tres ejes distintos y la regla de la mano
// derecha (un sensor no se suelda del revés por dentro).
struct Ejes {
    int idx[3] = {0, 1, 2};
    int sig[3] = {1, 1, 1};
    static std::string lee(const std::string& t, Ejes& e) {
        int n = 0;
        size_t i = 0;
        while (i < t.size() && n < 3) {
            int s = 1;
            while (i < t.size() && (t[i] == ' ' || t[i] == ',')) ++i;
            if (i < t.size() && (t[i] == '-' || t[i] == '+')) { s = t[i] == '-' ? -1 : 1; ++i; }
            if (i >= t.size()) break;
            const char c = char(t[i] | 0x20);
            if (c < 'x' || c > 'z') return "ejes=\"" + t + "\": cada eje es x, y o z, con su signo";
            e.idx[n] = c - 'x'; e.sig[n] = s; ++n; ++i;
        }
        while (i < t.size() && (t[i] == ' ' || t[i] == ',')) ++i;
        if (n != 3 || i != t.size()) return "ejes=\"" + t + "\": son tres, separados por comas";
        if (e.idx[0] == e.idx[1] || e.idx[0] == e.idx[2] || e.idx[1] == e.idx[2])
            return "ejes=\"" + t + "\": los tres tienen que ser distintos";
        // El determinante de la matriz de signos y permutación: +1
        const int par = ((e.idx[0] + 1) % 3 == e.idx[1]) ? 1 : -1;
        if (par * e.sig[0] * e.sig[1] * e.sig[2] != 1)
            return "ejes=\"" + t + "\": no es una rotacion (la regla de la mano derecha)";
        return std::string();
    }
    V3 aplica(const V3& v) const {
        const double b[3] = {v.x, v.y, v.z};
        return {sig[0] * b[idx[0]], sig[1] * b[idx[1]], sig[2] * b[idx[2]]};
    }
};

inline int16_t satura16(double v) {
    const long l = std::lround(v);
    return int16_t(std::max(-32768L, std::min(32767L, l)));
}

// Un filtro paso alto de primer orden, muestreado: y = a·(y + x - x_ant)
struct PasoAlto {
    double y = 0, x_ant = 0;
    bool   primero = true;
    double paso(double x, double fc, double fs) {
        if (primero) { primero = false; x_ant = x; y = 0; return 0; }
        const double rc = 1.0 / (2 * PI * fc), dt = 1.0 / fs;
        const double a = rc / (rc + dt);
        y = a * (y + x - x_ant);
        x_ant = x;
        return y;
    }
    void reinicia(double x) { x_ant = x; y = 0; primero = false; }
};

} // namespace mems

// ---------------------------------------------------------------------------
// LA POSTURA DE LA PLACA
// ---------------------------------------------------------------------------
SC_MODULE(Postura), public ExtPartBase {
    Postura(sc_core::sc_module_name nm, double alabeo = 0, double cabeceo = 0,
            double rumbo = 0, double velocidad = 90, double campo = 0.45,
            double inclinacion = 55, double temperatura = 25)
        : sc_core::sc_module(nm), ExtPartBase("Postura", nm),
          vel_(std::max(1.0, velocidad)), campo_(campo), incl_(inclinacion), temp_(temperatura) {
        a_[0] = obj_[0] = norm180(alabeo);
        a_[1] = obj_[1] = std::max(-90.0, std::min(90.0, cabeceo));
        a_[2] = obj_[2] = norm360(rumbo);
        R_ = mems::rotacion(a_[0], a_[1], a_[2]);
        SC_HAS_PROCESS(Postura);
        SC_THREAD(mueve);
    }

    // --- Lo que leen los sensores, en el sistema de la PLACA ----------------
    // La fuerza específica (lo que mide un acelerómetro): 1 g hacia arriba en
    // reposo, en mg
    mems::V3 aceleracion() const { return R_.por_t({0, 0, 1000.0}); }
    // La velocidad angular, en grados por segundo
    mems::V3 giro() const { return w_; }
    // El campo de la Tierra: horizontal hacia el norte magnético y, con la
    // inclinación, hacia abajo; en gauss
    mems::V3 campo() const {
        const double i = mems::rad(incl_);
        return R_.por_t({0, campo_ * std::cos(i), -campo_ * std::sin(i)});
    }
    double temperatura() const { return temp_; }

    // --- Para el banco y los mandos ------------------------------------------
    // La placa gira hasta ahí a su velocidad (cada ángulo por el camino más
    // corto); con `ya`, de golpe, sin velocidad angular
    void pon(double alabeo, double cabeceo, double rumbo, bool ya = false) {
        obj_[0] = norm180(alabeo);
        obj_[1] = std::max(-90.0, std::min(90.0, cabeceo));
        obj_[2] = norm360(rumbo);
        if (ya) {
            for (int k = 0; k < 3; ++k) a_[k] = obj_[k];
            R_ = mems::rotacion(a_[0], a_[1], a_[2]);
            w_ = {};
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pon_velocidad(double dps) { vel_ = std::max(1.0, dps); }
    void pon_temperatura(double t) { temp_ = t; }
    bool moviendose() const { return !quieta(); }
    double alabeo() const { return a_[0]; }
    double cabeceo() const { return a_[1]; }
    double rumbo() const { return a_[2]; }

    unsigned n_mandos() const override { return 3; }
    Mando mando(unsigned i) const override {
        if (i == 0) return {"alabeo", Mando::Continuo, -180.f, 180.f};
        if (i == 1) return {"cabeceo", Mando::Continuo, -90.f, 90.f};
        return {"rumbo", Mando::Continuo, 0.f, 360.f};
    }
    float valor_mando(unsigned i) const override { return float(obj_[i < 3 ? i : 2]); }
    void acciona(unsigned i, float v) override {
        double o[3] = {obj_[0], obj_[1], obj_[2]};
        if (i < 3) o[i] = v;
        pon(o[0], o[1], o[2]);
    }
    unsigned n_observables() const override { return 4; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"alabeo", "grados", -180.f, 180.f, true};
        if (i == 1) return {"cabeceo", "grados", -90.f, 90.f, true};
        if (i == 2) return {"rumbo", "grados", 0.f, 360.f, true};
        return {"giro", "dps", 0.f, 360.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i < 3) return float(a_[i]);
        return float(std::sqrt(w_.x * w_.x + w_.y * w_.y + w_.z * w_.z));
    }

private:
    double a_[3], obj_[3];
    double vel_, campo_, incl_, temp_;
    mems::M3 R_;
    mems::V3 w_;
    sc_core::sc_event ev_;
    static constexpr double PASO_S = 1e-3;     // 1 ms por paso del giro [de modelo]

    static double norm180(double g) {
        g = std::fmod(g + 180.0, 360.0);
        if (g < 0) g += 360.0;
        return g - 180.0;
    }
    static double norm360(double g) {
        g = std::fmod(g, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    // Lo que le falta a cada ángulo, por el camino corto (el cabeceo no da la vuelta)
    double falta(int k) const {
        const double d = obj_[k] - a_[k];
        return k == 1 ? d : norm180(d);
    }
    bool quieta() const {
        for (int k = 0; k < 3; ++k) if (std::fabs(falta(k)) > 1e-9) return false;
        return true;
    }
    void mueve() {
        for (;;) {
            while (quieta()) { w_ = {}; wait(ev_); }
            // Un paso: los tres ángulos a la vez, cada uno a lo sumo vel·dt
            const double maxp = vel_ * PASO_S;
            double n[3];
            for (int k = 0; k < 3; ++k) {
                const double d = falta(k);
                n[k] = a_[k] + (std::fabs(d) <= maxp ? d : (d > 0 ? maxp : -maxp));
            }
            n[0] = norm180(n[0]); n[2] = norm360(n[2]);
            const mems::M3 Rn = mems::rotacion(n[0], n[1], n[2]);
            // ω en la placa: la parte antisimétrica de Rᵀ·Rn, entre dt
            const mems::M3 W = R_.t() * Rn;
            const double k = 180.0 / mems::PI / (2 * PASO_S);
            w_ = {(W.m[2][1] - W.m[1][2]) * k, (W.m[0][2] - W.m[2][0]) * k,
                  (W.m[1][0] - W.m[0][1]) * k};
            wait(sc_core::sc_time(PASO_S, sc_core::SC_SEC));
            for (int j = 0; j < 3; ++j) a_[j] = n[j];
            R_ = Rn;
        }
    }
};

// ---------------------------------------------------------------------------
// Lo común de los sensores: una patilla de salida (INT, DRDY) que conduce a
// nivel, en contrafase o en drenador abierto
// ---------------------------------------------------------------------------
class SalidaMems {
public:
    void conecta(ExtPartBase* p, analog_net_if* n, int id, double vdd) { n_ = n; id_ = id; vdd_ = vdd; (void)p; }
    void pon(bool activo, bool activo_bajo, bool drenador) {
        if (!n_) return;
        const bool nivel = activo != activo_bajo;
        if (drenador) {
            if (!nivel) n_->set_drive(id_, 0.0f, 50.0f); else n_->set_hiz(id_);
        } else {
            n_->set_drive(id_, nivel ? float(vdd_) : 0.0f, 50.0f);
        }
    }
    void suelta() { if (n_) n_->set_hiz(id_); }
private:
    analog_net_if* n_ = nullptr;
    int id_ = -1;
    double vdd_ = 3.3;
};

// ---------------------------------------------------------------------------
// EL GIRÓSCOPO: L3GD20 e I3G4250D, el mismo mapa (0x0F-0x38), por SPI de cuatro
// hilos en modo 0 o 3. El primer byte: R/W (7), MS -autoincremento- (6) y la
// dirección (5:0). Muestrea MOSI en el flanco de subida y saca MISO en el de
// bajada; MISO, en alta impedancia salvo mientras saca un dato.
//
//   CTRL_REG1 0x20 = 0x07  DR[7:6] (95/190/380/760 Hz) BW[5:4] PD Zen Yen Xen
//   CTRL_REG2 0x21         HPM[5:4] HPCF[3:0]
//   CTRL_REG3 0x22         I1_Int1 I1_Boot H_Lactive PP_OD I2_DRDY I2_WTM I2_ORun I2_Empty
//   CTRL_REG4 0x23         BDU BLE FS[5:4] (250/500/2000) - - - SIM
//   CTRL_REG5 0x24         BOOT FIFO_EN - HPen INT1_Sel[3:2] Out_Sel[1:0]
//   OUT_TEMP 0x26, STATUS 0x27 (ZYXOR..XOR ZYXDA..XDA), OUT_X/Y/Z 0x28-0x2D
//   INT1_CFG 0x30 (AND/OR LIR ZHIE..XLIE), INT1_SRC 0x31, THS 0x32-0x37, DUR 0x38
//
// La salida: Out_Sel = 00, la del primer paso bajo (sin el paso alto aunque
// HPen esté puesto: así lo usa el BSP de ST); 01 y 11, tras el paso alto de
// HPCF. Sensibilidad 8,75 / 17,5 / 70 mdps por cuenta.
// ---------------------------------------------------------------------------
SC_MODULE(MemsGiro), public ExtPartBase {
    MemsGiro(sc_core::sc_module_name nm, const char* tipo, uint8_t quien,
             analog_net_if& spc, analog_net_if& sdi, analog_net_if& sdo, analog_net_if& cs,
             analog_net_if* int1, analog_net_if* drdy, Postura* postura,
             const mems::Ejes& ejes, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase(tipo, nm), quien_(quien),
          spc_(&spc), sdi_(&sdi), sdo_(&sdo), cs_(&cs), postura_(postura), ejes_(ejes), vdd_(vdd) {
        add_ref("spc", spc);
        add_ref("sdi", sdi);
        add_ref("cs", cs);
        id_sdo_ = add_pin("sdo", sdo, "mems_sdo");
        sdo.set_hiz(id_sdo_);
        if (int1) o_int1_.conecta(this, int1, add_pin("int1", *int1, "mems_int1"), vdd);
        if (drdy) o_drdy_.conecta(this, drdy, add_pin("drdy", *drdy, "mems_drdy"), vdd);
        reinicia();
        SC_HAS_PROCESS(MemsGiro);
        SC_METHOD(bus);
        sensitive << spc.value_changed_event() << cs.value_changed_event();
        dont_initialize();
        SC_THREAD(muestrea);
    }
    uint8_t quien_soy() const { return quien_; }
    // Sin alimentación y con ella otra vez: los registros, a los de fábrica
    void reinicio() { reinicia(); ev_cfg_.notify(sc_core::SC_ZERO_TIME); pinta(); }
    unsigned muestras() const { return n_muestras_; }
    unsigned transacciones() const { return n_trans_; }
    // La última muestra, en dps, en los ejes del sensor
    double dps(int eje) const { return out_[eje] * sens_mdps() / 1000.0; }
    uint8_t registro(uint8_t r) const { return lee_reg(r); }

    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (!on) { sdo_->set_hiz(id_sdo_); o_int1_.suelta(); o_drdy_.suelta(); }
        else pinta();
    }
    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        static const char* n[3] = {"giro_x", "giro_y", "giro_z"};
        return {n[i < 3 ? i : 0], "dps", -500.f, 500.f, i == 2};
    }
    float valor_observable(unsigned i) const override { return float(dps(int(i < 3 ? i : 0))); }

private:
    uint8_t quien_;
    analog_net_if *spc_, *sdi_, *sdo_, *cs_;
    Postura* postura_;
    mems::Ejes ejes_;
    double vdd_;
    int id_sdo_ = -1;
    SalidaMems o_int1_, o_drdy_;

    std::array<uint8_t, 0x40> r_{};
    int16_t out_[3] = {0, 0, 0};
    bool bloq_[3] = {false, false, false};      // BDU: se ha leído la parte baja
    uint8_t src1_ = 0;
    mems::PasoAlto hp_[3];
    unsigned n_muestras_ = 0, n_trans_ = 0;
    bool aviso_fifo_ = false, aviso_sim_ = false;
    sc_core::sc_event ev_cfg_;

    // El estado del bus
    bool cs_ant_ = true, spc_ant_ = false, lee_ = false, ms_ = false, datos_ = false;
    unsigned nbit_ = 0, obit_ = 0;
    uint8_t sh_ = 0, reg_ = 0, out_byte_ = 0;

    bool alto(analog_net_if* n) const { return n->voltage() > 0.5 * vdd_; }

    void reinicia() {
        r_.fill(0);
        r_[0x20] = 0x07;
        src1_ = 0;
        for (auto& o : out_) o = 0;
        for (auto& h : hp_) h = mems::PasoAlto();
    }
    double odr() const {
        static const double t[4] = {95, 190, 380, 760};
        return t[r_[0x20] >> 6];
    }
    double sens_mdps() const {
        switch ((r_[0x23] >> 4) & 3u) { case 0: return 8.75; case 1: return 17.5; default: return 70.0; }
    }
    bool encendido() const { return (r_[0x20] & 0x08) != 0; }
    // La frecuencia de corte del paso alto [hoja de datos, tabla de HPCF a 95,
    // 190, 380 y 760 Hz; la columna de 95 Hz, y las demás la escalan]
    double fc() const {
        static const double t95[10] = {7.2, 3.5, 1.8, 0.9, 0.45, 0.18, 0.09, 0.045, 0.018, 0.009};
        const unsigned h = std::min(9u, unsigned(r_[0x21] & 0x0Fu));
        return t95[h] * odr() / 95.0;
    }

    uint8_t lee_reg(uint8_t r) const {
        r &= 0x3Fu;
        if (r == 0x0F) return quien_;
        if (r >= 0x28 && r <= 0x2D) {
            const int e = (r - 0x28) / 2;
            const bool alta = ((r - 0x28) & 1) != 0;
            const bool ble = (r_[0x23] & 0x40) != 0;
            const uint16_t v = uint16_t(out_[e]);
            return (alta != ble) ? uint8_t(v >> 8) : uint8_t(v);
        }
        if (r == 0x2F) return 0x20;                            // FIFO vacía (bypass)
        if (r == 0x31) return src1_;
        return r_[r];
    }
    // Leer tiene consecuencias: los DA, el BDU y el INT1 retenido
    void leido(uint8_t r) {
        r &= 0x3Fu;
        if (r >= 0x28 && r <= 0x2D) {
            const int e = (r - 0x28) / 2;
            const bool alta = ((r - 0x28) & 1) != (((r_[0x23] & 0x40) != 0) ? 1 : 0);
            if (!alta) bloq_[e] = true;
            else {
                bloq_[e] = false;
                r_[0x27] &= uint8_t(~((1u << e) | (0x10u << e)));
                if (!(r_[0x27] & 0x07u)) r_[0x27] &= uint8_t(~0x88u);
                pinta();
            }
        }
        if (r == 0x31) { src1_ = 0; pinta(); }
    }
    void escribe_reg(uint8_t r, uint8_t v) {
        r &= 0x3Fu;
        const bool rw = (r >= 0x20 && r <= 0x25) || r == 0x2E || r == 0x30 || (r >= 0x32 && r <= 0x38);
        if (!rw) return;
        if (r == 0x24 && (v & 0x80u)) {                         // BOOT: recarga
            reinicia();
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            pinta();
            return;
        }
        r_[r] = v;
        if (r == 0x23 && (v & 0x01u) && !aviso_sim_) {
            aviso_sim_ = true;
            SC_REPORT_WARNING("/mcu-sim/mems", (pieza() + ": SPI de tres hilos (SIM) no modelado").c_str());
        }
        if ((r == 0x24 && (v & 0x40u)) || (r == 0x2E && (v & 0xE0u))) {
            if (!aviso_fifo_) {
                aviso_fifo_ = true;
                SC_REPORT_WARNING("/mcu-sim/mems", (pieza() + ": la FIFO no esta modelada; "
                                  "los datos siguen en OUT_X/Y/Z").c_str());
            }
        }
        if (r == 0x25 && ((r_[0x21] >> 4) & 3u) == 0)            // REFERENCE con HPM = 00
            for (int e = 0; e < 3; ++e) hp_[e].primero = true;
        if (r == 0x20 || r == 0x23 || r == 0x21) ev_cfg_.notify(sc_core::SC_ZERO_TIME);
        pinta();
    }

    void pinta() {
        if (!conectada_) return;
        const uint8_t c3 = r_[0x22];
        const bool bajo = (c3 & 0x20u) != 0, od = (c3 & 0x10u) != 0;
        o_drdy_.pon((c3 & 0x08u) && (r_[0x27] & 0x08u), bajo, od);
        o_int1_.pon((c3 & 0x80u) && (src1_ & 0x40u), bajo, od);
    }

    void muestrea() {
        for (;;) {
            while (!encendido()) wait(ev_cfg_);
            const sc_core::sc_time t(1.0 / odr(), sc_core::SC_SEC), t0 = sc_core::sc_time_stamp();
            wait(t, ev_cfg_);
            if (!encendido() || sc_core::sc_time_stamp() - t0 < t) continue;
            mems::V3 w = postura_ ? postura_->giro() : mems::V3();
            w = ejes_.aplica(w);
            const double wv[3] = {w.x, w.y, w.z};
            const bool hp = (r_[0x24] & 0x10u) && (r_[0x24] & 0x01u);
            bool nuevo = false;
            for (int e = 0; e < 3; ++e) {
                const double f = hp_[e].paso(wv[e], fc(), odr());
                const double v = hp ? f : wv[e];
                if (!(r_[0x20] & (1u << e))) continue;           // eje apagado
                if ((r_[0x23] & 0x80u) && bloq_[e]) continue;     // BDU
                out_[e] = mems::satura16(v * 1000.0 / sens_mdps());
                if (r_[0x27] & (1u << e)) r_[0x27] |= uint8_t(0x10u << e);   // xOR
                r_[0x27] |= uint8_t(1u << e);
                nuevo = true;
            }
            if (nuevo) {
                if (r_[0x27] & 0x08u) r_[0x27] |= 0x80u;
                r_[0x27] |= 0x08u;
            }
            // OUT_TEMP: -1 cuenta por grado, 0 a 25 °C [de modelo]
            const double tc = postura_ ? postura_->temperatura() : 25.0;
            r_[0x26] = uint8_t(int8_t(std::max(-128L, std::min(127L, std::lround(25.0 - tc)))));
            interrupcion();
            ++n_muestras_;
            pinta();
        }
    }
    // INT1: eventos alto (> umbral) y bajo (< -umbral) por eje, en OR o en AND
    // de los habilitados; retenido con LIR hasta leer INT1_SRC [de modelo: sin
    // la duración]
    void interrupcion() {
        const uint8_t cfg = r_[0x30];
        if (!(cfg & 0x3Fu)) { if (!(cfg & 0x40u)) src1_ = 0; return; }
        uint8_t ev = 0;
        for (int e = 0; e < 3; ++e) {
            const int ths = ((r_[0x32 + 2 * e] & 0x7F) << 8) | r_[0x33 + 2 * e];
            if (out_[e] > ths)  ev |= uint8_t(2u << (2 * e));
            if (out_[e] < -ths) ev |= uint8_t(1u << (2 * e));
        }
        const uint8_t hab = cfg & 0x3Fu;
        const bool ia = (cfg & 0x80u) ? ((ev & hab) == hab) : ((ev & hab) != 0);
        if (cfg & 0x40u) { if (ia) src1_ = uint8_t(0x40u | (ev & 0x3Fu)); }
        else src1_ = ia ? uint8_t(0x40u | (ev & 0x3Fu)) : uint8_t(ev & 0x3Fu);
    }

    void bus() {
        if (!conectada_) return;
        const bool cs = alto(cs_), ck = alto(spc_);
        const bool cs_ant = cs_ant_, ck_ant = spc_ant_;
        cs_ant_ = cs; spc_ant_ = ck;
        if (cs != cs_ant) {
            if (!cs) { nbit_ = 0; sh_ = 0; datos_ = false; obit_ = 0; ++n_trans_; }
            sdo_->set_hiz(id_sdo_);
            return;
        }
        if (cs || ck == ck_ant) return;
        if (ck) {                                    // subida: se muestrea MOSI
            sh_ = uint8_t((sh_ << 1) | (alto(sdi_) ? 1u : 0u));
            if (++nbit_ < 8) return;
            nbit_ = 0;
            if (!datos_) {
                lee_ = (sh_ & 0x80u) != 0;
                ms_ = (sh_ & 0x40u) != 0;
                reg_ = sh_ & 0x3Fu;
                datos_ = true;
                if (lee_) { out_byte_ = lee_reg(reg_); leido(reg_); obit_ = 0; }
            } else if (lee_) {
                if (ms_) reg_ = uint8_t((reg_ + 1) & 0x3Fu);
                out_byte_ = lee_reg(reg_); leido(reg_); obit_ = 0;
            } else {
                escribe_reg(reg_, sh_);
                if (ms_) reg_ = uint8_t((reg_ + 1) & 0x3Fu);
            }
            sh_ = 0;
        } else if (datos_ && lee_ && obit_ < 8) {   // bajada: se saca MISO
            const bool b = (out_byte_ >> (7 - obit_)) & 1u;
            ++obit_;
            sdo_->set_drive(id_sdo_, b ? float(vdd_) : 0.0f, 50.0f);
        }
    }
};

class L3gd20 : public MemsGiro {
public:
    L3gd20(sc_core::sc_module_name nm, analog_net_if& spc, analog_net_if& sdi,
           analog_net_if& sdo, analog_net_if& cs, analog_net_if* int1, analog_net_if* drdy,
           Postura* p, const mems::Ejes& e, uint8_t quien = 0xD4)
        : MemsGiro(nm, "L3gd20", quien, spc, sdi, sdo, cs, int1, drdy, p, e) {}
};
class I3g4250d : public MemsGiro {
public:
    I3g4250d(sc_core::sc_module_name nm, analog_net_if& spc, analog_net_if& sdi,
             analog_net_if& sdo, analog_net_if& cs, analog_net_if* int1, analog_net_if* drdy,
             Postura* p, const mems::Ejes& e)
        : MemsGiro(nm, "I3g4250d", 0xD3, spc, sdi, sdo, cs, int1, drdy, p, e) {}
};

// ---------------------------------------------------------------------------
// LA BRÚJULA CON ACELERÓMETRO: LSM303DLHC y LSM303AGR. Dos esclavos I2C en un
// encapsulado -el acelerómetro en 0x19 y el magnetómetro en 0x1E- y una sola
// máquina de bits, la de I2cReg16 (boosterpack.h) con registros de un byte:
// [dir|W][SUB][dato]... o [dir|W][SUB] Sr [dir|R][dato]... Del acelerómetro,
// el bit 7 de SUB pide el autoincremento; el magnetómetro avanza siempre.
//
// ACELERÓMETRO (los dos; los del AGR, además, 0x07, 0x0C-0x0E, 0x1F y 0x3E-0x3F)
//   WHO_AM_I_A 0x0F = 0x33 (en el DLHC no lo documenta su hoja de datos, pero
//   está, y el BSP de ST lo lee)
//   CTRL_REG1_A 0x20 = 0x07  ODR[7:4] (1/10/25/50/100/200/400/1620/1344 Hz) LPen Z Y X
//   CTRL_REG2_A 0x21  HPM HPCF FDS HPCLICK HPIS2 HPIS1
//   CTRL_REG3_A 0x22  I1_CLICK I1_AOI1 I1_AOI2 I1_DRDY1 I1_DRDY2 I1_WTM I1_OVERRUN
//   CTRL_REG4_A 0x23  BDU BLE FS[5:4] (2/4/8/16 g) HR ...
//   CTRL_REG5_A 0x24  BOOT FIFO_EN - - LIR_INT1 D4D_INT1 LIR_INT2 D4D_INT2
//   CTRL_REG6_A 0x25  I2_CLICK I2_INT1 I2_INT2 BOOT_I2 P2_ACT - H_LACTIVE -
//   STATUS_REG_A 0x27, OUT_X/Y/Z_A 0x28-0x2D (justificados a la izquierda:
//   12 bits con HR, 10 en normal y 8 con LPen), FIFO 0x2E-0x2F, INT1 0x30-0x33,
//   INT2 0x34-0x37, CLICK 0x38-0x3D
//
// MAGNETÓMETRO DEL DLHC (0x00-0x0C, 0x31-0x32)
//   CRA_REG_M 0x00 = 0x10  TEMP_EN DO[4:2] (0,75 a 220 Hz)
//   CRB_REG_M 0x01 = 0x20  GN[7:5] (1,3 a 8,1 gauss)
//   MR_REG_M 0x02 = 0x03   MD (00 continuo, 01 una vez, 1x dormido)
//   OUT_X_H_M 0x03 ... en orden X, Z, Y, el byte alto primero; 12 bits con
//   signo, y -4096 si se sale de escala. SR_REG_M 0x09 (LOCK DRDY),
//   IRA/B/C 0x0A-0x0C = 'H' '4' '3', TEMP_OUT 0x31-0x32 (8 cuentas por grado)
//
// MAGNETÓMETRO DEL AGR (estilo LIS2MDL)
//   OFFSET 0x45-0x4A, WHO_AM_I_M 0x4F = 0x40, CFG_REG_A_M 0x60 = 0x03
//   (COMP_TEMP REBOOT SOFT_RST LP ODR[3:2] (10/20/50/100 Hz) MD), CFG_REG_B/C
//   0x61-0x62 (C: INT_MAG_PIN BDU BLE ... INT_MAG), INT 0x63-0x66,
//   STATUS_REG_M 0x67, OUTX_L_REG_M 0x68-0x6D (el byte bajo primero), 1,5 mG
//   por cuenta
// ---------------------------------------------------------------------------
SC_MODULE(Lsm303), public I2cPart {
    enum Modelo { DLHC, AGR };
    Lsm303(sc_core::sc_module_name nm, Modelo m, analog_net_if& scl, analog_net_if& sda,
           analog_net_if* int1, analog_net_if* int2, analog_net_if* drdy, Postura* postura,
           const mems::Ejes& ejes_a, const mems::Ejes& ejes_m, double vdd = 3.3)
        : sc_core::sc_module(nm),
          I2cPart(scl, sda, m == DLHC ? "Lsm303dlhc" : "Lsm303agr", "mems_i2c", nm, vdd),
          mod_(m), postura_(postura), ea_(ejes_a), em_(ejes_m) {
        if (int1) o_int1_.conecta(this, int1, add_pin("int1", *int1, "mems_int1"), vdd);
        if (int2) o_int2_.conecta(this, int2, add_pin("int2", *int2, "mems_int2"), vdd);
        if (drdy) o_drdy_.conecta(this, drdy, add_pin("drdy", *drdy, "mems_drdy"), vdd);
        reinicia_a();
        reinicia_m();
        SC_HAS_PROCESS(Lsm303);
        SC_METHOD(bordes);
        sensitive << scl_->value_changed_event() << sda_->value_changed_event();
        dont_initialize();
        SC_THREAD(acel);
        SC_THREAD(magn);
    }
    static constexpr uint8_t DIR_A = 0x19, DIR_M = 0x1E;
    // Sin alimentación y con ella otra vez: los registros, a los de fábrica
    void reinicio() {
        reinicia_a(); reinicia_m(); st_ = IDLE;
        ev_a_.notify(sc_core::SC_ZERO_TIME); ev_m_.notify(sc_core::SC_ZERO_TIME); pinta();
    }
    unsigned muestras_a() const { return n_a_; }
    unsigned muestras_m() const { return n_m_; }
    unsigned escrituras() const { return n_wr_; }
    unsigned lecturas() const { return n_rd_; }
    // Lo último medido, en los ejes del sensor: mg y miligauss
    double mg(int e) const { return ult_a_[e]; }
    double mgauss(int e) const { return ult_m_[e]; }
    uint8_t registro_a(uint8_t r) const { return lee_a(r); }
    uint8_t registro_m(uint8_t r) const { return lee_m(r); }

    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (!on) { release_scl(); release_sda(); o_int1_.suelta(); o_int2_.suelta(); o_drdy_.suelta(); }
        else pinta();
    }
    unsigned n_observables() const override { return 6; }
    Observable observable(unsigned i) const override {
        static const char* n[6] = {"acel_x", "acel_y", "acel_z", "campo_x", "campo_y", "campo_z"};
        return i < 3 ? Observable{n[i], "mg", -2000.f, 2000.f, i == 2}
                     : Observable{n[i < 6 ? i : 5], "mG", -1000.f, 1000.f, false};
    }
    float valor_observable(unsigned i) const override {
        return float(i < 3 ? ult_a_[i] : ult_m_[i < 6 ? i - 3 : 2]);
    }

private:
    Modelo mod_;
    Postura* postura_;
    mems::Ejes ea_, em_;
    SalidaMems o_int1_, o_int2_, o_drdy_;
    std::array<uint8_t, 0x40> a_{};
    std::array<uint8_t, 0x80> m_{};
    int16_t oa_[3] = {0, 0, 0};          // el dato, ya justificado a la izquierda
    int16_t om_[3] = {0, 0, 0};
    bool bloq_a_[3] = {false, false, false};
    uint8_t src_[2] = {0, 0};
    double ult_a_[3] = {0, 0, 0}, ult_m_[3] = {0, 0, 0};
    mems::PasoAlto hp_[3];
    unsigned n_a_ = 0, n_m_ = 0, n_wr_ = 0, n_rd_ = 0;
    bool aviso_fifo_ = false, aviso_click_ = false, aviso_6d_ = false;
    sc_core::sc_event ev_a_, ev_m_;

    // ---------------- el acelerómetro --------------------------------------
    void reinicia_a() {
        a_.fill(0);
        a_[0x20] = 0x07;
        for (auto& o : oa_) o = 0;
        src_[0] = src_[1] = 0;
        for (auto& h : hp_) h = mems::PasoAlto();
    }
    double odr_a() const {
        static const double t[10] = {0, 1, 10, 25, 50, 100, 200, 400, 1620, 1344};
        const unsigned o = a_[0x20] >> 4;
        if (o == 9 && (a_[0x20] & 0x08u)) return 5376;
        return o < 10 ? t[o] : 0;
    }
    unsigned bits_a() const {
        if (a_[0x20] & 0x08u) return 8;
        if (a_[0x23] & 0x08u) return 12;
        return 10;
    }
    // mg por cuenta, con la resolución de ese modo
    double lsb_a() const {
        const unsigned fs = (a_[0x23] >> 4) & 3u;
        if (mod_ == AGR) {
            static const double hr[4] = {0.98, 1.95, 3.9, 11.72}, no[4] = {3.9, 7.82, 15.63, 46.9},
                                lp[4] = {15.63, 31.26, 62.52, 187.58};
            const unsigned b = bits_a();
            return b == 12 ? hr[fs] : b == 10 ? no[fs] : lp[fs];
        }
        // DLHC: la tabla de su hoja de datos da 1/2/4/12 mg con HR; en los
        // otros modos, las mismas por 4 y por 16 [de modelo]
        static const double hr[4] = {1, 2, 4, 12};
        const unsigned b = bits_a();
        return hr[fs] * (b == 12 ? 1 : b == 10 ? 4 : 16);
    }
    // El umbral de INT1/INT2, en mg por cuenta [hoja de datos: 16/32/62/186]
    double lsb_ths() const {
        static const double t[4] = {16, 32, 62, 186};
        return t[(a_[0x23] >> 4) & 3u];
    }
    // El corte del paso alto: una fracción de ODR por HPCF [1F: la tabla de
    // la hoja de datos, a 50 Hz 1/0,5/0,2/0,1 Hz]
    double fc_a() const {
        static const double k[4] = {0.02, 0.01, 0.004, 0.002};
        return std::max(1e-4, odr_a() * k[(a_[0x21] >> 4) & 3u]);
    }
    uint8_t lee_a(uint8_t r) const {
        r &= 0x7Fu;
        if (r >= 0x40) return 0;
        if (r == 0x0F) return 0x33;
        if (r >= 0x28 && r <= 0x2D) {
            const int e = (r - 0x28) / 2;
            const bool alta = ((r - 0x28) & 1) != 0;
            const bool ble = (a_[0x23] & 0x40u) != 0;
            const uint16_t v = uint16_t(oa_[e]);
            return (alta != ble) ? uint8_t(v >> 8) : uint8_t(v);
        }
        if (r == 0x2F) return 0x20;
        if (r == 0x31) return src_[0];
        if (r == 0x35) return src_[1];
        if (mod_ == AGR && r == 0x0C) return 0;
        if (mod_ == AGR && r == 0x0D) {
            const double tc = postura_ ? postura_->temperatura() : 25.0;
            return uint8_t(int8_t(std::max(-128L, std::min(127L, std::lround(tc - 25.0)))));
        }
        if (mod_ == AGR && r == 0x07) return (a_[0x1F] & 0xC0u) ? 0x04 : 0x00;   // TDA
        return a_[r];
    }
    void leido_a(uint8_t r) {
        r &= 0x7Fu;
        if (r >= 0x28 && r <= 0x2D) {
            const int e = (r - 0x28) / 2;
            const bool alta = ((r - 0x28) & 1) != (((a_[0x23] & 0x40u) != 0) ? 1 : 0);
            if (!alta) bloq_a_[e] = true;
            else {
                bloq_a_[e] = false;
                a_[0x27] &= uint8_t(~((1u << e) | (0x10u << e)));
                if (!(a_[0x27] & 0x07u)) a_[0x27] &= uint8_t(~0x88u);
            }
            pinta();
        }
        if (r == 0x31 && (a_[0x24] & 0x08u)) { src_[0] = 0; pinta(); }
        if (r == 0x35 && (a_[0x24] & 0x02u)) { src_[1] = 0; pinta(); }
    }
    void escribe_a(uint8_t r, uint8_t v) {
        r &= 0x7Fu;
        const bool rw = (r >= 0x20 && r <= 0x26) || r == 0x2E || r == 0x30 || r == 0x32 ||
                        r == 0x33 || r == 0x34 || r == 0x36 || r == 0x37 || r == 0x38 ||
                        (r >= 0x3A && r <= 0x3D) ||
                        (mod_ == AGR && (r == 0x1F || r == 0x3E || r == 0x3F));
        if (!rw) return;
        if (r == 0x24 && (v & 0x80u)) { reinicia_a(); ev_a_.notify(sc_core::SC_ZERO_TIME); pinta(); return; }
        a_[r] = v;
        if ((r == 0x24 && (v & 0x40u)) || (r == 0x2E && (v & 0xC0u))) {
            if (!aviso_fifo_) {
                aviso_fifo_ = true;
                SC_REPORT_WARNING("/mcu-sim/mems", (pieza() + ": la FIFO del acelerometro no esta "
                                  "modelada; los datos siguen en OUT_X/Y/Z_A").c_str());
            }
        }
        if (r == 0x38 && v && !aviso_click_) {
            aviso_click_ = true;
            SC_REPORT_WARNING("/mcu-sim/mems", (pieza() + ": el click no esta modelado").c_str());
        }
        if ((r == 0x30 || r == 0x34) && (v & 0x40u) && !aviso_6d_) {
            aviso_6d_ = true;
            SC_REPORT_WARNING("/mcu-sim/mems", (pieza() + ": el 6D no esta modelado; "
                              "las interrupciones van como OR o AND").c_str());
        }
        if (r == 0x20 || r == 0x23 || r == 0x21) ev_a_.notify(sc_core::SC_ZERO_TIME);
        pinta();
    }
    void acel() {
        for (;;) {
            while (odr_a() <= 0 || !(a_[0x20] & 0x07u)) wait(ev_a_);
            const sc_core::sc_time t(1.0 / odr_a(), sc_core::SC_SEC), t0 = sc_core::sc_time_stamp();
            wait(t, ev_a_);
            if (odr_a() <= 0 || sc_core::sc_time_stamp() - t0 < t) continue;
            mems::V3 f = postura_ ? postura_->aceleracion() : mems::V3{0, 0, 1000};
            f = ea_.aplica(f);
            const double fv[3] = {f.x, f.y, f.z};
            const bool fds = (a_[0x21] & 0x08u) != 0;
            const unsigned b = bits_a();
            const long tope = 1L << (b - 1);
            double eval[3];
            bool nuevo = false;
            for (int e = 0; e < 3; ++e) {
                const double h = hp_[e].paso(fv[e], fc_a(), odr_a());
                const double v = fds ? h : fv[e];
                eval[e] = v;
                if (!(a_[0x20] & (1u << e))) continue;
                if ((a_[0x23] & 0x80u) && bloq_a_[e]) continue;
                const long c = std::max(-tope, std::min(tope - 1, std::lround(v / lsb_a())));
                oa_[e] = int16_t(c << (16 - b));
                ult_a_[e] = double(c) * lsb_a();
                if (a_[0x27] & (1u << e)) a_[0x27] |= uint8_t(0x10u << e);
                a_[0x27] |= uint8_t(1u << e);
                nuevo = true;
            }
            if (nuevo) {
                if (a_[0x27] & 0x08u) a_[0x27] |= 0x80u;
                a_[0x27] |= 0x08u;
            }
            for (int k = 0; k < 2; ++k) aoi(k, eval);
            ++n_a_;
            pinta();
        }
    }
    // INT1/INT2: alto si |a| > umbral, bajo si |a| < umbral, en OR o en AND de
    // los eventos habilitados; con LIR, hasta leer INT_SRC [de modelo: sin la
    // duración]
    void aoi(int k, const double* v) {
        const uint8_t cfg = a_[0x30 + 4 * k];
        const uint8_t lir = k == 0 ? 0x08u : 0x02u;
        if (!(cfg & 0x3Fu)) { if (!(a_[0x24] & lir)) src_[k] = 0; return; }
        const double ths = (a_[0x32 + 4 * k] & 0x7Fu) * lsb_ths();
        uint8_t ev = 0;
        for (int e = 0; e < 3; ++e) {
            if (std::fabs(v[e]) > ths) ev |= uint8_t(2u << (2 * e));
            else ev |= uint8_t(1u << (2 * e));
        }
        const uint8_t hab = cfg & 0x3Fu;
        const bool ia = (cfg & 0x80u) ? ((ev & hab) == hab) : ((ev & hab) != 0);
        if (a_[0x24] & lir) { if (ia) src_[k] = uint8_t(0x40u | (ev & 0x3Fu)); }
        else src_[k] = ia ? uint8_t(0x40u | (ev & 0x3Fu)) : uint8_t(ev & 0x3Fu);
    }

    // ---------------- el magnetómetro ---------------------------------------
    void reinicia_m() {
        m_.fill(0);
        if (mod_ == DLHC) { m_[0x00] = 0x10; m_[0x01] = 0x20; m_[0x02] = 0x03; }
        else { m_[0x60] = 0x03; m_[0x63] = 0xE0; }
        for (auto& o : om_) o = 0;
    }
    unsigned md_m() const { return mod_ == DLHC ? (m_[0x02] & 3u) : (m_[0x60] & 3u); }
    double odr_m() const {
        if (mod_ == DLHC) {
            static const double t[8] = {0.75, 1.5, 3, 7.5, 15, 30, 75, 220};
            return t[(m_[0x00] >> 2) & 7u];
        }
        static const double t[4] = {10, 20, 50, 100};
        return t[(m_[0x60] >> 2) & 3u];
    }
    uint8_t lee_m(uint8_t r) const {
        r &= 0x7Fu;
        if (mod_ == DLHC) {
            if (r >= 0x03 && r <= 0x08) {
                // X, Z, Y; el byte alto primero
                static const int eje[3] = {0, 2, 1};
                const int k = (r - 0x03) / 2;
                const uint16_t v = uint16_t(om_[eje[k]]);
                return ((r - 0x03) & 1) ? uint8_t(v) : uint8_t(v >> 8);
            }
            if (r == 0x0A) return 0x48;
            if (r == 0x0B) return 0x34;
            if (r == 0x0C) return 0x33;
            if (r == 0x31 || r == 0x32) {
                // 8 cuentas por grado en [15:4], 0 a 25 °C [de modelo]
                const double tc = postura_ ? postura_->temperatura() : 25.0;
                const uint16_t v = uint16_t(int16_t(std::lround((tc - 25.0) * 8.0) * 16));
                return r == 0x31 ? uint8_t(v >> 8) : uint8_t(v);
            }
            return r <= 0x0C ? m_[r] : 0;
        }
        if (r == 0x4F) return 0x40;
        if (r >= 0x68 && r <= 0x6D) {
            const int e = (r - 0x68) / 2;
            const bool alta = ((r - 0x68) & 1) != 0;
            const bool ble = (m_[0x62] & 0x08u) != 0;
            const uint16_t v = uint16_t(om_[e]);
            return (alta != ble) ? uint8_t(v >> 8) : uint8_t(v);
        }
        return m_[r];
    }
    void leido_m(uint8_t r) {
        r &= 0x7Fu;
        // DRDY se baja al leer el último byte de datos [de modelo]
        if (mod_ == DLHC && r == 0x08) { m_[0x09] &= uint8_t(~0x01u); pinta(); }
        if (mod_ == AGR && r == 0x6D) { m_[0x67] = 0; pinta(); }
    }
    void escribe_m(uint8_t r, uint8_t v) {
        r &= 0x7Fu;
        if (mod_ == DLHC) {
            if (r > 0x02) return;
            m_[r] = v;
        } else {
            const bool rw = (r >= 0x45 && r <= 0x4A) || (r >= 0x60 && r <= 0x63) || r == 0x65 || r == 0x66;
            if (!rw) return;
            if (r == 0x60 && (v & 0x60u)) { reinicia_m(); ev_m_.notify(sc_core::SC_ZERO_TIME); pinta(); return; }
            m_[r] = v;
        }
        ev_m_.notify(sc_core::SC_ZERO_TIME);
        pinta();
    }
    // Cuentas por gauss del DLHC, por GN [la tabla de lsm303dlhc.h de ST]
    void gan_dlhc(double& xy, double& z) const {
        static const double gxy[8] = {1100, 1100, 855, 670, 450, 400, 330, 230};
        static const double gz[8]  = {980, 980, 760, 600, 400, 355, 295, 205};
        const unsigned g = (m_[0x01] >> 5) & 7u;
        xy = gxy[g]; z = gz[g];
    }
    void magn() {
        for (;;) {
            while (md_m() >= 2) wait(ev_m_);
            const sc_core::sc_time t(1.0 / odr_m(), sc_core::SC_SEC), t0 = sc_core::sc_time_stamp();
            wait(t, ev_m_);
            if (md_m() >= 2 || sc_core::sc_time_stamp() - t0 < t) continue;
            mems::V3 b = postura_ ? postura_->campo() : mems::V3{0, 0.45 * 0.574, -0.45 * 0.819};
            b = em_.aplica(b);
            const double bv[3] = {b.x, b.y, b.z};
            if (mod_ == DLHC) {
                double gxy, gz;
                gan_dlhc(gxy, gz);
                for (int e = 0; e < 3; ++e) {
                    const double g = e == 2 ? gz : gxy;
                    const long c = std::lround(bv[e] * g);
                    om_[e] = int16_t(c > 2047 || c < -2048 ? -4096 : c);
                    ult_m_[e] = double(c) / g * 1000.0;
                }
                m_[0x09] |= 0x01u;
                if (md_m() == 1) m_[0x02] = uint8_t((m_[0x02] & ~3u) | 3u);
            } else {
                for (int e = 0; e < 3; ++e) {
                    const long c = std::lround(bv[e] * 1000.0 / 1.5);
                    om_[e] = mems::satura16(double(c));
                    ult_m_[e] = double(c) * 1.5;
                }
                if (m_[0x67] & 0x08u) m_[0x67] |= 0x80u;
                m_[0x67] |= 0x08u | 0x07u;
                if (md_m() == 1) m_[0x60] = uint8_t((m_[0x60] & ~3u) | 3u);
            }
            ++n_m_;
            pinta();
        }
    }

    // ---------------- las patillas ------------------------------------------
    void pinta() {
        if (!conectada_) return;
        const uint8_t c3 = a_[0x22], c6 = a_[0x25];
        const bool bajo = (c6 & 0x02u) != 0;
        const bool ia1 = (src_[0] & 0x40u) != 0, ia2 = (src_[1] & 0x40u) != 0;
        const bool zyxda = (a_[0x27] & 0x08u) != 0;
        o_int1_.pon(((c3 & 0x40u) && ia1) || ((c3 & 0x20u) && ia2) ||
                    ((c3 & 0x10u) && zyxda) || ((c3 & 0x08u) && zyxda), bajo, false);
        o_int2_.pon(((c6 & 0x40u) && ia1) || ((c6 & 0x20u) && ia2), bajo, false);
        if (mod_ == DLHC) o_drdy_.pon((m_[0x09] & 0x01u) != 0, false, false);
        else o_drdy_.pon((m_[0x62] & 0x01u) && (m_[0x67] & 0x08u), false, false);
    }

    // ---------------- el bus: la máquina de I2cReg16, con bytes -------------
    enum St { IDLE, ADDR, ACK_HOLD, RX, TX, MACK };
    St st_ = IDLE;
    unsigned bit_ = 0, nrx_ = 0;
    uint8_t sh_ = 0, dev_ = 0, reg_a_ = 0, reg_m_ = 0;
    bool auto_a_ = false;
    bool c_prev_ = true, d_prev_ = true, lee_ = false, ack_next_ = false;

    uint8_t& reg() { return dev_ == DIR_A ? reg_a_ : reg_m_; }
    uint8_t siguiente_tx() {
        uint8_t& r = reg();
        uint8_t v;
        if (dev_ == DIR_A) { v = lee_a(r); leido_a(r); if (auto_a_) r = uint8_t((r + 1) & 0x7Fu); }
        else { v = lee_m(r); leido_m(r); r = uint8_t((r + 1) & 0x7Fu); }
        ++n_rd_;
        return v;
    }
    void bordes() {
        if (!conectada_) return;
        const bool c = scl(), d = sda();
        const bool cp = c_prev_, dp = d_prev_;
        c_prev_ = c; d_prev_ = d;
        if (c && cp) {
            if (dp && !d) { st_ = ADDR; bit_ = 0; sh_ = 0; nrx_ = 0; ack_next_ = false; release_sda(); return; }
            if (!dp && d) { st_ = IDLE; ack_next_ = false; release_sda(); return; }
        }
        if (c && !cp) {
            switch (st_) {
                case ADDR:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        const uint8_t a = sh_ >> 1;
                        if (a == DIR_A || a == DIR_M) { dev_ = a; lee_ = (sh_ & 1u) != 0; ack_next_ = true; }
                        else { st_ = IDLE; ack_next_ = false; }
                    }
                    break;
                case RX:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        if (nrx_ == 0) {
                            if (dev_ == DIR_A) { reg_a_ = sh_ & 0x7Fu; auto_a_ = (sh_ & 0x80u) != 0; }
                            else reg_m_ = sh_ & 0x7Fu;
                        } else {
                            uint8_t& r = reg();
                            if (dev_ == DIR_A) { escribe_a(r, sh_); if (auto_a_) r = uint8_t((r + 1) & 0x7Fu); }
                            else { escribe_m(r, sh_); r = uint8_t((r + 1) & 0x7Fu); }
                            ++n_wr_;
                        }
                        ++nrx_;
                        ack_next_ = true;
                    }
                    break;
                case ACK_HOLD:
                    if (lee_) { st_ = TX; bit_ = 0; sh_ = siguiente_tx(); }
                    else      { st_ = RX; bit_ = 0; sh_ = 0; }
                    break;
                case TX:
                    if (bit_ >= 8) st_ = MACK;
                    break;
                case MACK:
                    if (d) { st_ = IDLE; release_sda(); }
                    else   { st_ = TX; bit_ = 0; sh_ = siguiente_tx(); }
                    break;
                default: break;
            }
            return;
        }
        if (!c && cp) {
            if (ack_next_) { ack_next_ = false; pull_sda(); st_ = ACK_HOLD; return; }
            switch (st_) {
                case RX:   if (bit_ == 0) release_sda(); break;
                case TX:   if (bit_ < 8) { drive_sda((sh_ >> (7 - bit_)) & 1u); ++bit_; } break;
                case MACK: release_sda(); break;
                default: break;
            }
        }
    }
};

class Lsm303dlhc : public Lsm303 {
public:
    Lsm303dlhc(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
               analog_net_if* int1, analog_net_if* int2, analog_net_if* drdy, Postura* p,
               const mems::Ejes& ea, const mems::Ejes& em)
        : Lsm303(nm, DLHC, scl, sda, int1, int2, drdy, p, ea, em) {}
};
class Lsm303agr : public Lsm303 {
public:
    Lsm303agr(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
              analog_net_if* int1, analog_net_if* int2, analog_net_if* drdy, Postura* p,
              const mems::Ejes& ea, const mems::Ejes& em)
        : Lsm303(nm, AGR, scl, sda, int1, int2, drdy, p, ea, em) {}
};

} // namespace stm32
#endif // STM32_PARTS_MEMS_ST_H
