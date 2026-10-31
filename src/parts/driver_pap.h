// =============================================================================
// driver_pap.h — Un DRIVER DE PASO A PASO con entradas de paso y dirección, y un
//                generador de pasos de banco que lo mueve sin MCU
//
// (Fase F1a de doc/analisis_cnc.md, §5.2.)
//
//   DriverPaP       el chip de un módulo de los de la CNC Shield: el A4988 de
//                   Allegro (el del módulo Pololu), el DRV8825 de TI (el de la
//                   placa GRBL de la 3018) y el TMC2209 de Trinamic, como tres
//                   juegos de parámetros de la misma pieza
//   GeneradorPasos  un instrumento de banco: STEP y DIR con una rampa, para
//                   probar un driver y un motor sin firmware
//
// EL MODELO ELÉCTRICO ES LA CORRIENTE IMPUESTA CON TOPE DE TENSIÓN. Un driver
// de verdad es un chopper que conmuta su puente a 20-40 kHz para que la
// corriente de cada fase siga la consigna del micropaso; simularlo conmutando
// costaría cientos de miles de sucesos por segundo con tres ejes. Aquí cada fase
// es una RamaCorriente (parts/ext_parts.h): «quiero i*, y puedo poner de -VMOT
// a +VMOT». La bobina que esté entre sus dos salidas la sigue mientras la
// tensión le llegue (parts/motor_pap.h). El driver solo despierta en los
// flancos de STEP, en los cambios de sus entradas y de sus alimentaciones, y
// cada milisegundo mientras conduce, para pedir a VMOT lo que gasta: la potencia
// de las bobinas -Σ v·i, lo que la bobina deja en la rama- más lo que se queda
// en sus transistores, más su consumo en reposo. No hay sucesos de chopper; su
// rizado se pierde, y no lo ve nadie.
//
// EL TRADUCTOR. Cada flanco de subida de STEP avanza la posición un micropaso
// hacia donde diga DIR (alto: hacia delante). La posición va en 1/256 de paso
// entero; la fase eléctrica es φ = posición·90°/256, y las consignas, I·cos φ en
// la fase 1 (OUT1A -> OUT1B) e I·sin φ en la 2. Empieza, como el A4988 y el
// DRV8825, en 45°: en paso entero, las dos fases al 70,71 %. Las patillas MS
// eligen el micropaso en cada flanco; el traductor cuenta aunque las salidas
// estén apagadas por EN, como el de verdad.
//
// LO QUE COMPRUEBA, PORQUE ES LO QUE SE EQUIVOCA EN LA VIDA REAL. Cada fallo es
// un aviso con nombre, una vez:
//
//   * el ancho del pulso de STEP, alto y bajo, y el tiempo de DIR antes y
//     después del flanco;
//   * el nivel de cada entrada: el A4988 pide 0,7·VDD para un alto -con VDD a
//     5 V, 3,3 V no llegan: funciona casi siempre, fuera de especificación-;
//     el DRV8825, 2,2 V fijos; una entrada al aire;
//   * EN activo a nivel bajo, RESET y SLEEP; un RESET al aire (el del módulo
//     Pololu del A4988 no lleva resistencia: se puentea con SLEEP); los pasos
//     en el milisegundo que tarda en despertar;
//   * la corriente que pide Vref, contra lo que da el chip;
//   * VMOT fuera de su margen, y la lógica sin tensión;
//   * la TEMPERATURA: lo que se queda en los transistores, I²·RDS(on) por fase,
//     con una resistencia térmica y una constante de tiempo; pasar del umbral
//     apaga las salidas hasta que se enfría (la protección térmica).
//
// Lo que NO hace: el rizado del chopper y sus modos de decaimiento; la
// interpolación a 1/256 del TMC2209 (cada paso salta lo que diga su micropaso),
// su UART, su StallGuard, su StealthChop y su reducción de corriente parado; la
// protección de cortocircuito de las salidas; la energía que vuelve al bus al
// frenar, que se queda en nada (es de la fase F5).
// =============================================================================
#ifndef STM32_PARTS_DRIVER_PAP_H
#define STM32_PARTS_DRIVER_PAP_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// Las cifras de cada chip. Las de las hojas de datos, y las que no lo son se
// dicen: [A4988 de Allegro, rev. 6; DRV8825 de TI, SLVSA73F; TMC2209 de
// Trinamic/ADI, rev. 1.08; módulos de Pololu]
struct ModeloDriverPaP {
    const char* nombre;
    double vm_min, vm_max;      // V: el margen de VMOT
    double vm_apaga;            // V: por debajo, las salidas se apagan
    bool   con_vdd;             // tiene patilla de lógica (VDD o VIO)
    double vdd_min;             // V: la lógica, como mínimo
    bool   umbral_rel;          // los umbrales, en fracción de VDD (o en V)
    double vil, vih, v_corte;   // bajo hasta, alto desde (especificación), y donde corta de verdad
    double t_alto, t_bajo;      // s: el ancho mínimo del pulso de STEP
    double t_dir, t_hold;       // s: DIR antes y después del flanco
    double t_despierta;         // s: tras soltar SLEEP; 0, sin SLEEP
    double k_vref;              // A por V de Vref, con su rs
    double i_max;               // A: lo que da el chip, de pico
    double rds;                 // ohm: los dos transistores de una fase, en serie
    double t_corte, t_hist;     // °C: la protección térmica y su histéresis
    double rth, rth_disipador;  // K/W: la del módulo, sin y con disipador (estimadas)
    double iq_vm, iq_vdd;       // A: en reposo
    double vref_omision;        // V
    // Un driver externo con entradas OPTOACOPLADAS (el DM542): cada entrada es
    // el LED de un optoacoplador con su resistencia, que pide corriente al que
    // la mueve; el micropaso y la corriente se ponen con sus interruptores
    bool   optos = false;
    double v_led = 1.2, r_opto = 270.0;   // V y ohm: el LED y su resistencia
};

inline const ModeloDriverPaP* modelo_driver_pap(const std::string& n) {
    // A4988: VIN(1) >= 0,7·VDD y VIN(0) <= 0,3·VDD; STEP 1 us alto y 1 us bajo,
    // DIR 200 ns antes y después; 1 ms al despertar. ITrip = Vref/(8·Rs), Rs =
    // 0,068 ohm en el módulo de Pololu. RDS(on) 0,32 + 0,43 ohm. TSD 165 °C con
    // 15 de histéresis. IBB 4 mA, IDD 8 mA. La resistencia térmica del módulo es
    // una estimación: Pololu dice 1,2 A por fase sin disipador
    static const ModeloDriverPaP a4988 = {
        "A4988", 8.0, 35.0, 6.0, true, 3.0, true, 0.3, 0.7, 0.5,
        1e-6, 1e-6, 200e-9, 200e-9, 1e-3, 1.0 / (8.0 * 0.068), 2.0, 0.75, 165.0, 15.0,
        50.0, 25.0, 4e-3, 8e-3, 0.6};
    // DRV8825: VM de 8,2 a 45 V; la lógica es suya (V3P3OUT): VIH 2,2 V y VIL
    // 0,8 fijos. STEP 1,9 us; DIR 650 ns; 1,7 ms al despertar. ITrip =
    // Vref/(5·Rs), Rs = 0,1 ohm en el de Pololu. RDS(on) 0,2 + 0,2. TSD 150 °C
    static const ModeloDriverPaP drv8825 = {
        "DRV8825", 8.2, 45.0, 7.0, false, 0.0, false, 0.8, 2.2, 1.5,
        1.9e-6, 1.9e-6, 650e-9, 650e-9, 1.7e-3, 1.0 / (5.0 * 0.1), 2.5, 0.4, 150.0, 20.0,
        40.0, 20.0, 5e-3, 0.0, 0.5};
    // TMC2209: VM de 4,75 a 29 V; VIO de 3 a 5 V, umbrales de 0,3 y 0,7·VIO.
    // STEP de 100 ns. Con VREF, I = (Vref/2,5)·0,325/(Rs + 0,02) de pico, Rs =
    // 0,11 ohm en los módulos comunes. RDS(on) 0,17 + 0,17. Sobretemperatura a
    // 143 °C (OTTRIM de fábrica)
    static const ModeloDriverPaP tmc2209 = {
        "TMC2209", 4.75, 29.0, 4.2, true, 3.0, true, 0.3, 0.7, 0.5,
        100e-9, 100e-9, 20e-9, 20e-9, 0.0, 0.325 / (2.5 * (0.11 + 0.02)), 2.8, 0.34, 143.0, 20.0,
        40.0, 20.0, 10e-3, 1e-3, 1.4};
    // DRIVER EXTERNO tipo DM542 (Leadshine y sus copias): VMOT de 20 a 50 V
    // (18 V y se apaga), hasta 4,2 A de pico, el micropaso y la corriente con
    // sus interruptores. Las entradas, PUL, DIR y ENA, son OPTOACOPLADAS: el LED
    // con 270 ohm por dentro, de 7 a 16 mA; con PUL-, DIR- y ENA- a masa, un
    // alto de 5 V da 14 mA y uno de 3,3 V unos 8: funciona, por poco (el
    // análisis de la CNC, §5.2). ENA con corriente APAGA las salidas. PUL de
    // 2,5 us; DIR 5 us antes. Lo que se queda en sus transistores y su consumo
    // son estimaciones, y lleva un disipador grande
    static const ModeloDriverPaP dm542 = {
        "DM542", 20.0, 50.0, 18.0, false, 0.0, false, 1.2 + 0.007 * 270.0 * 0.5,
        1.2 + 0.007 * 270.0, 1.2 + 0.006 * 270.0,
        2.5e-6, 2.5e-6, 5e-6, 5e-6, 0.0, 0.0, 4.2, 0.1, 150.0, 20.0,
        2.0, 2.0, 30e-3, 0.0, 0.0, true, 1.2, 270.0};
    if (n == "A4988") return &a4988;
    if (n == "DM542") return &dm542;
    if (n == "DRV8825") return &drv8825;
    if (n == "TMC2209") return &tmc2209;
    return nullptr;
}

struct ConfigDriverPaP {
    std::string modelo = "A4988";
    double vref = -1.0;           // V; < 0: el de omisión del modelo
    bool   disipador = false;
    double t_ambiente = 25.0;     // °C
    double tau_termica_s = 3.0;   // la constante de tiempo del módulo (estimada)
    // Los de un DM542, que no tiene MS ni Vref: sus interruptores
    unsigned micropasos = 8;      // de 2 a 128, potencias de dos
    double corriente_a = 2.0;     // de pico, de 1 a 4,2
};

SC_MODULE(DriverPaP), public ExtPartBase {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr int CICLO = 1024;           // una vuelta eléctrica, en 1/256 de paso
    static constexpr double REFRESCO = 1e-3;     // s, mientras conduce
    static constexpr float R_PULL = 100e3f;      // las resistencias de las entradas, por dentro

    // Las patillas que no tiene un modelo van a nullptr: vdd en el DRV8825;
    // ms3, rst y slp en el TMC2209; en el DM542, vdd, las ms, rst y slp, y en
    // si no se conecta (sin corriente, habilitado)
    DriverPaP(sc_core::sc_module_name nm, const ConfigDriverPaP& c, analog_net_if& vmot,
              analog_net_if& gnd, analog_net_if* vdd, analog_net_if* en, analog_net_if* ms1,
              analog_net_if* ms2, analog_net_if* ms3, analog_net_if* rst, analog_net_if* slp,
              analog_net_if& step, analog_net_if& dir, analog_net_if& o1a, analog_net_if& o1b,
              analog_net_if& o2a, analog_net_if& o2b)
        : sc_core::sc_module(nm), ExtPartBase("DriverPaP", nm), c_(c),
          m_(*modelo_driver_pap(c.modelo)), vmot_(&vmot), gnd_(&gnd), vdd_(vdd) {
        vref_ = c_.vref >= 0.0 ? c_.vref : m_.vref_omision;
        tj_ = c_.t_ambiente;
        const int ivm = add_pin("vmot", vmot, "driver_pap");
        const int ig = add_pin("gnd", gnd, "driver_pap");
        carga_vm_.reset(new RamaDosNodos(vmot, ivm, gnd, ig));
        if (vdd_) {
            const int ivd = add_pin("vdd", *vdd_, "driver_pap");
            const int ig2 = add_drv("gnd", "driver_pap");
            carga_vdd_.reset(new RamaDosNodos(*vdd_, ivd, gnd, ig2));
        }
        // Las entradas, con las resistencias a masa que llevan por dentro. El
        // A4988 las lleva en MS1, MS2 y MS3; el DRV8825 y el TMC2209, en todas
        // las de lógica (en el DRV8825 también en nRESET y nSLEEP)
        const bool a4988 = std::string(m_.nombre) == "A4988";
        const bool sin_pull = a4988 || m_.optos;
        auto pon = [&](int k, analog_net_if* n, const char* nombre, bool pull) {
            ent_[k].n = n;
            ent_[k].nombre = nombre;
            ent_[k].pull = pull;
        };
        pon(EN, en, "en", !sin_pull);
        pon(MS1, ms1, "ms1", true);
        pon(MS2, ms2, "ms2", true);
        pon(MS3, ms3, "ms3", true);
        pon(RST, rst, "rst", std::string(m_.nombre) == "DRV8825");
        pon(SLP, slp, "slp", std::string(m_.nombre) == "DRV8825");
        pon(STEP, &step, "step", !sin_pull);
        pon(DIR, &dir, "dir", !sin_pull);
        for (Entrada& e : ent_) {
            if (!e.n) continue;
            if (m_.optos) {
                // El LED del optoacoplador, de la entrada a masa
                const int id = add_pin(e.nombre, *e.n, "driver_pap_opto");
                const int idg = add_drv("gnd", "driver_pap_opto");
                e.opto.reset(new RamaDosNodos(*e.n, id, gnd, idg));
            } else if (e.pull) {
                const int id = add_pin(e.nombre, *e.n, "driver_pap_pd");
                e.n->set_drive(id, 0.0f, R_PULL);
            } else {
                add_ref(e.nombre, *e.n);
            }
        }
        const int i1a = add_pin("out1a", o1a, "driver_pap_out");
        const int i1b = add_pin("out1b", o1b, "driver_pap_out");
        const int i2a = add_pin("out2a", o2a, "driver_pap_out");
        const int i2b = add_pin("out2b", o2b, "driver_pap_out");
        fase_[0].reset(new RamaCorriente(o1a, i1a, o1b, i1b, nombre_c(), 1));
        fase_[1].reset(new RamaCorriente(o2a, i2a, o2b, i2b, nombre_c(), 2));
        pos_ = 128;                              // 45°: el estado de casa
        SC_HAS_PROCESS(DriverPaP);
        SC_METHOD(vigila);
    }
    ~DriverPaP() override {
        carga_vm_->suelta();
        if (carga_vdd_) carga_vdd_->suelta();
        for (auto& f : fase_) f->suelta();
    }

    // --- Desde C++ -----------------------------------------------------------
    const ModeloDriverPaP& modelo() const { return m_; }
    double vref() const { return vref_; }
    void   pon_vref(double v) { vref_ = std::max(0.0, v); ev_mando_.notify(sc_core::SC_ZERO_TIME); }
    double corriente_ajustada() const { return std::min(vref_ * m_.k_vref, m_.i_max); }
    double consigna_ma(int f) const { return fase_[f]->consigna() * 1e3; }
    double corriente_ma(int f) const { return fase_[f]->i_carga() * 1e3; }
    unsigned micropasos() const { return micro_; }
    long   pasos() const { return pasos_; }
    int    posicion() const { return pos_; }       // en 1/256 de paso, de 0 a 1023
    bool   activo() const { return activa_; }
    bool   logica() const { return logica_; }
    double temperatura() const { return tj_; }
    bool   sobretemperatura() const { return tsd_; }
    double corriente_vmot_ma() const { return i_vm_ * 1e3; }
    double disipacion() const { return p_; }

    unsigned n_observables() const override { return 8; }
    Observable observable(unsigned i) const override {
        const float im = float(m_.i_max * 1000.0);
        switch (i) {
            case 0: return {"corriente_1", "mA", -im, im, false};
            case 1: return {"corriente_2", "mA", -im, im, false};
            case 2: return {"micropasos", "", 1.f, 256.f, false};
            case 3: return {"pasos", "", -1e7f, 1e7f, false};
            case 4: return {"corriente_vmot", "mA", 0.f, 4000.f, true};
            case 5: return {"temperatura", "°C", 0.f, 200.f, false};
            case 6: return {"sobretemperatura", "", 0.f, 1.f, true, true};
            default: return {"sin_tension", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(consigna_ma(0));
            case 1: return float(consigna_ma(1));
            case 2: return float(micro_);
            case 3: return float(pasos_);
            case 4: return float(i_vm_ * 1e3);
            case 5: return float(tj_);
            case 6: return tsd_ ? 1.f : 0.f;
            default: return sin_tension_ ? 1.f : 0.f;
        }
    }
    // El potenciómetro de Vref
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"vref", Mando::Continuo, 0.f, 3.3f}; }
    float valor_mando(unsigned) const override { return float(vref_); }
    void acciona(unsigned, float v) override { pon_vref(double(v)); }

private:
    enum { EN, MS1, MS2, MS3, RST, SLP, STEP, DIR, N_ENT };
    struct Entrada {
        analog_net_if* n = nullptr;
        const char* nombre = "";
        bool pull = false;
        bool nivel = false;
        std::shared_ptr<RamaDosNodos> opto;    // el LED de su optoacoplador
    };
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    const char* nombre_c() const { return pieza().c_str(); }
    double v(const analog_net_if* n) const { return double(n->voltage()) - double(gnd_->voltage()); }

    // Una entrada: alto o bajo con el umbral de verdad, y el aviso si está en la
    // zona que la hoja de datos no garantiza, o al aire
    bool nivel(Entrada& e, double vl) {
        if (!e.n) return false;
        if (e.n->floating()) {
            if (&e != &ent_[RST] && ahora() > 0.0 && !m_.optos)          // ese tiene su aviso, que dice qué hacer
                avisa(std::string("aire_") + e.nombre, std::string(nombre_c()) + ": " +
                      e.nombre + " esta al aire, y el " + m_.nombre + " no sabe que leer");
            return false;
        }
        const double x = v(e.n);
        const double k = m_.umbral_rel ? vl : 1.0;
        const double bajo = m_.vil * k, alto = m_.vih * k, corte = m_.v_corte * k;
        if (x > bajo && x < alto) {
            char t[260];
            std::snprintf(t, sizeof t, "%s: %.2f V en %s, y el %s pide %.2f V para un alto y "
                          "%.2f para un bajo%s: fuera de especificacion (lo lee %s)", nombre_c(),
                          x, e.nombre, m_.nombre, alto, bajo,
                          m_.umbral_rel ? " con su VDD" : "", x >= corte ? "alto" : "bajo");
            avisa(std::string("dudoso_") + e.nombre, t);
        }
        return x >= corte;
    }

    void vigila() {
        const double t = ahora();
        const double vg = double(gnd_->voltage());
        const double vm = conectada_ ? double(vmot_->voltage()) - vg : 0.0;
        const double vl = vdd_ ? (conectada_ ? double(vdd_->voltage()) - vg : 0.0) : 3.3;
        // La temperatura, hasta ahora con lo que se disipaba
        calienta(t);
        // Las alimentaciones
        logica_ = conectada_ && (vdd_ ? vl >= m_.vdd_min * 0.9 : vm >= m_.vm_apaga);
        const bool potencia = conectada_ && vm >= m_.vm_apaga;
        if (conectada_ && vdd_ && vl > 0.5 && vl < m_.vdd_min) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.2f V en VDD, y la logica del %s pide %.1f", nombre_c(),
                          vl, m_.nombre, m_.vdd_min);
            avisa("vdd", b);
        }
        if (conectada_ && vm > 0.5 && vm < m_.vm_min) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.1f V en VMOT, y el %s pide de %.2f a %.0f V%s",
                          nombre_c(), vm, m_.nombre, m_.vm_min, m_.vm_max,
                          potencia ? "" : ": las salidas se apagan");
            avisa("vmot_bajo", b);
        }
        if (vm > m_.vm_max) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.1f V en VMOT, y el %s aguanta %.0f: en una placa "
                          "de verdad se rompe", nombre_c(), vm, m_.nombre, m_.vm_max);
            avisa("vmot_alto", b);
        }
        sin_tension_ = conectada_ && (!logica_ || !potencia);
        // Los LEDs de los optoacopladores: piden su corriente al que los mueve
        if (m_.optos)
            for (Entrada& e : ent_)
                if (e.opto) e.opto->resuelve_diodo(conectada_, m_.v_led, m_.r_opto);
        // Las entradas
        for (int k = 0; k < N_ENT; ++k) {
            if (k == STEP || k == DIR) continue;
            Entrada& e = ent_[k];
            // RST y SLP que no tiene, sueltos; un EN que no está, habilitado
            if (!e.n) { e.nivel = (k == RST || k == SLP); continue; }
            e.nivel = logica_ ? nivel(e, vl) : false;
        }
        if (ent_[RST].n && logica_ && ent_[RST].n->floating() && !ent_[RST].pull && t > 0.0)
            avisa("rst_aire", std::string(nombre_c()) + ": RESET esta al aire. En el modulo de "
                  "Pololu no lleva resistencia: se une a SLEEP, o a VDD");
        const bool despierto = ent_[SLP].nivel;
        if (despierto && !despierto_) t_despierta_ = t;
        despierto_ = despierto;
        if (!ent_[RST].nivel) pos_ = 128;                    // RESET: a casa
        // El micropaso, de las MS
        micro_ = micropaso();
        // DIR y STEP: los flancos, con sus tiempos
        const bool dir = logica_ ? nivel(ent_[DIR], vl) : false;
        if (dir != ent_[DIR].nivel) {
            if (t - t_sube_ < m_.t_hold && t_sube_ >= 0.0)
                avisa("dir_hold", texto_tiempo("DIR cambia", t - t_sube_, "despues del flanco de "
                                               "STEP, y necesita", m_.t_hold));
            ent_[DIR].nivel = dir;
            t_dir_ = t;
        }
        const bool step = logica_ ? nivel(ent_[STEP], vl) : false;
        if (step != ent_[STEP].nivel) {
            ent_[STEP].nivel = step;
            if (step) {
                if (t_baja_ >= 0.0 && t - t_baja_ < m_.t_bajo)
                    avisa("ancho_bajo", texto_tiempo("STEP baja", t - t_baja_, "y el minimo es",
                                                     m_.t_bajo));
                if (t - t_dir_ < m_.t_dir && t_dir_ > 0.0)
                    avisa("dir_setup", texto_tiempo("DIR cambia", t - t_dir_, "antes del flanco "
                                                    "de STEP, y necesita", m_.t_dir));
                t_sube_ = t;
                da_paso(t);
            } else {
                if (t - t_sube_ < m_.t_alto)
                    avisa("ancho_alto", texto_tiempo("STEP sube", t - t_sube_, "y el minimo es",
                                                     m_.t_alto));
                t_baja_ = t;
            }
        }
        // La protección térmica
        if (!tsd_ && tj_ >= m_.t_corte) {
            tsd_ = true;
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.0f °C: la proteccion termica apaga las salidas "
                          "hasta que baje a %.0f%s", nombre_c(), tj_, m_.t_corte - m_.t_hist,
                          c_.disipador ? "" : " (sin disipador)");
            avisa("termica", b);
        } else if (tsd_ && tj_ <= m_.t_corte - m_.t_hist) {
            tsd_ = false;
        }
        // Las salidas
        const double i_aj = m_.optos ? c_.corriente_a : vref_ * m_.k_vref;
        if (i_aj > m_.i_max + 1e-9) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: Vref = %.2f V pide %.2f A, y el %s da %.1f: se queda "
                          "en %.1f", nombre_c(), vref_, i_aj, m_.nombre, m_.i_max, m_.i_max);
            avisa("vref", b);
        }
        const double imax = std::min(i_aj, m_.i_max);
        activa_ = logica_ && potencia && !ent_[EN].nivel && ent_[RST].nivel && despierto && !tsd_;
        const double phi = double(pos_) * (PI / 2.0) / 256.0;
        fase_[0]->manda(imax * std::cos(phi), std::max(0.0, vm), activa_);
        fase_[1]->manda(imax * std::sin(phi), std::max(0.0, vm), activa_);
        // Lo que gasta, de media desde la última vez que la bobina dejó cuentas:
        // lo que ha entrado en las bobinas, lo que se ha quedado en sus
        // transistores y el reposo. Con lo que vuelve de las bobinas no se hace
        // nada: una fuente de laboratorio no lo absorbe (es de la fase F5)
        {
            double e = 0.0, q = 0.0, tc = 0.0;
            for (auto& f : fase_) { e += f->energia(); q += f->i2s(); tc = std::max(tc, f->t_carga()); }
            if (tc > t_cuentas_ + 1e-12) {
                const double dt = tc - t_cuentas_;
                pc_ = (e - e_ant_) / dt;
                pt_ = (q - q_ant_) / dt * m_.rds;
                t_cuentas_ = tc; e_ant_ = e; q_ant_ = q;
            } else if (!activa_ && fase_[0]->i_carga() == 0.0 && fase_[1]->i_carga() == 0.0) {
                pc_ = pt_ = 0.0;
            }
        }
        const double iq = logica_ ? m_.iq_vm : 0.0;
        const double i_ahora = (vm > 0.5) ? std::max(0.0, pc_ + pt_) / vm + iq : 0.0;
        // De media, como la ve una fuente: un filtro de 5 ms
        i_vm_ = i_ahora + (i_vm_ - i_ahora) * std::exp(-(t - t_ivm_) / 5e-3);
        t_ivm_ = t;
        p_ = pt_ + (vm > 0.5 ? vm * iq : 0.0);
        pide(*carga_vm_, r_vm_, vm, i_vm_);
        if (carga_vdd_) pide(*carga_vdd_, r_vdd_, vl, logica_ ? m_.iq_vdd : 0.0);
        for (auto& f : fase_) f->muestra(vg);
        // La próxima vez: las entradas, y cada milisegundo mientras conduce o se
        // enfría
        const bool vivo = activa_ || std::fabs(tj_ - c_.t_ambiente) > 0.5 ||
                          fase_[0]->i_carga() != 0.0 || fase_[1]->i_carga() != 0.0;
        if (vivo) next_trigger(sc_core::sc_time(REFRESCO, sc_core::SC_SEC), despierta());
        else next_trigger(despierta());
    }

    // Un flanco de subida de STEP: un micropaso, si el traductor puede
    void da_paso(double t) {
        if (!logica_ || !ent_[RST].nivel || !ent_[SLP].nivel) return;
        if (ent_[SLP].n && t - t_despierta_ < m_.t_despierta && t_despierta_ > 0.0) {
            avisa("despierta", texto_tiempo("STEP llega", t - t_despierta_, "despues de soltar "
                                            "SLEEP, y hay que esperar", m_.t_despierta) +
                  ": el paso se pierde");
            return;
        }
        const int inc = 256 / int(micro_);
        pos_ = (pos_ + (ent_[DIR].nivel ? inc : -inc) + CICLO) % CICLO;
        pasos_ += ent_[DIR].nivel ? 1 : -1;
    }

    // El micropaso que dicen las MS, o los interruptores
    unsigned micropaso() {
        if (m_.optos) return c_.micropasos;
        const bool a = ent_[MS1].nivel, b = ent_[MS2].nivel, c = ent_[MS3].nivel;
        const std::string n = m_.nombre;
        if (n == "A4988") {
            if (!a && !b && !c) return 1;
            if (a && !b && !c) return 2;
            if (!a && b && !c) return 4;
            if (a && b && !c) return 8;
            if (a && b && c) return 16;
            avisa("ms", std::string(nombre_c()) + ": MS1, MS2 y MS3 en una combinacion que "
                  "el A4988 no tiene; se queda en la que habia");
            return micro_;
        }
        if (n == "DRV8825") {
            const int m = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0);    // MODE0, 1 y 2
            static const unsigned tabla[8] = {1, 2, 4, 8, 16, 32, 32, 32};
            return tabla[m];
        }
        // TMC2209, sin UART: MS2 y MS1
        if (!a && !b) return 8;
        if (a && !b) return 32;
        if (!a && b) return 64;
        return 16;
    }

    // La temperatura: un primer orden hacia Ta + P·Rth, desde la última vez
    void calienta(double t) {
        const double dt = t - t_term_;
        t_term_ = t;
        if (dt <= 0.0) return;
        const double rth = c_.disipador ? m_.rth_disipador : m_.rth;
        const double tss = c_.t_ambiente + p_ * rth;
        tj_ = tss + (tj_ - tss) * std::exp(-dt / c_.tau_termica_s);
    }

    std::string texto_tiempo(const char* que, double dt, const char* como, double minimo) const {
        char b[260];
        std::snprintf(b, sizeof b, "%s: %s %.0f ns %s %.0f ns", nombre_c(), que, dt * 1e9, como,
                      minimo * 1e9);
        return b;
    }
    void pide(RamaDosNodos& rama, double& r_ant, double vv, double i) {
        if (!conectada_ || vv < 0.05 || i <= 1e-6) {
            if (r_ant != 0.0) { rama.suelta(); r_ant = 0.0; }
            return;
        }
        const double r = vv / i;
        if (r_ant != 0.0 && std::fabs(r - r_ant) < 1e-3 * r_ant) return;
        r_ant = r;
        rama.resuelve(true, r);
    }
    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= vmot_->value_changed_event();
            *despierta_ |= gnd_->value_changed_event();
            if (vdd_) *despierta_ |= vdd_->value_changed_event();
            for (const Entrada& e : ent_)
                if (e.n) *despierta_ |= e.n->value_changed_event();
            *despierta_ |= evento_conexion();
            *despierta_ |= ev_mando_;
        }
        return *despierta_;
    }
    void avisa(const std::string& que, const std::string& texto) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("driver_pap", texto.c_str());
    }

    ConfigDriverPaP c_;
    ModeloDriverPaP m_;
    analog_net_if *vmot_, *gnd_, *vdd_;
    Entrada ent_[N_ENT];
    std::unique_ptr<RamaCorriente> fase_[2];
    std::unique_ptr<RamaDosNodos> carga_vm_, carga_vdd_;
    double r_vm_ = 0.0, r_vdd_ = 0.0;
    double vref_ = 0.6;
    int pos_ = 128;
    unsigned micro_ = 1;
    long pasos_ = 0;
    bool logica_ = false, activa_ = false, despierto_ = false, tsd_ = false, sin_tension_ = false;
    double t_sube_ = -1.0, t_baja_ = -1.0, t_dir_ = 0.0, t_despierta_ = 0.0;
    double tj_ = 25.0, t_term_ = 0.0, p_ = 0.0, i_vm_ = 0.0;
    double t_cuentas_ = 0.0, e_ant_ = 0.0, q_ant_ = 0.0, pc_ = 0.0, pt_ = 0.0, t_ivm_ = 0.0;
    sc_core::sc_event ev_mando_;
    std::unique_ptr<sc_core::sc_event_or_list> despierta_;
    std::set<std::string> dichos_;
};

// ---------------------------------------------------------------------------
// UN GENERADOR DE PASOS, de banco: STEP y DIR para mover un driver sin MCU.
// Va a `hz` pasos por segundo, y llega con una rampa de aceleración constante,
// `aceleracion` pasos/s² (0: sin rampa, de golpe); un cambio de `sentido` en marcha
// frena hasta parar, cambia DIR, espera `dir_us` y vuelve a arrancar. El pulso
// dura `ancho_us`. Las salidas son de `vdd` voltios con 50 ohm.
// Los MANDOS: `hz`, el objetivo, que se alcanza con la rampa, y `sentido`.
// Deja ver `hz` -la de ahora- y `pasos`, con su signo.
// ---------------------------------------------------------------------------
SC_MODULE(GeneradorPasos), public ExtPartBase {
    GeneradorPasos(sc_core::sc_module_name nm, analog_net_if& step, analog_net_if& dir,
                   double hz = 0.0, double aceleracion = 0.0, double ancho_us = 2.0,
                   double dir_us = 5.0, bool sentido = true, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("GeneradorPasos", nm), step_(&step), dir_(&dir),
          objetivo_(hz), acel_(aceleracion), ancho_(ancho_us * 1e-6),
          dir_s_(dir_us * 1e-6), sentido_(sentido), vdd_(vdd) {
        id_step_ = add_pin("step", step, "generador_pasos");
        id_dir_ = add_pin("dir", dir, "generador_pasos");
        step.set_drive(id_step_, 0.0f, 50.0f);
        dir.set_drive(id_dir_, sentido ? float(vdd) : 0.0f, 50.0f);
        SC_HAS_PROCESS(GeneradorPasos);
        SC_THREAD(genera);
    }
    double hz() const { return rate_; }
    double aceleracion() const { return acel_; }
    long   pasos() const { return pasos_; }
    void   pon_hz(double h) { objetivo_ = std::max(0.0, h); ev_.notify(sc_core::SC_ZERO_TIME); }
    void   pon_sentido(bool s) { sentido_ = s; ev_.notify(sc_core::SC_ZERO_TIME); }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"hz", "Hz", 0.f, 100000.f, true};
        return {"pasos", "", -1e7f, 1e7f, false};
    }
    float valor_observable(unsigned i) const override {
        return i == 0 ? float(rate_) : float(pasos_);
    }
    unsigned n_mandos() const override { return 2; }
    Mando mando(unsigned i) const override {
        if (i == 0) return {"hz", Mando::Continuo, 0.f, 200000.f};
        return {"sentido", Mando::Interruptor, 0.f, 1.f};
    }
    float valor_mando(unsigned i) const override {
        return i == 0 ? float(objetivo_) : (sentido_ ? 1.f : 0.f);
    }
    void acciona(unsigned i, float v) override {
        if (i == 0) pon_hz(double(v));
        else pon_sentido(v >= 0.5f);
    }

private:
    void pon(analog_net_if* n, int id, bool alto) {
        if (!conectada_) { n->set_hiz(id); return; }
        n->set_drive(id, alto ? float(vdd_) : 0.0f, 50.0f);
    }
    void genera() {
        dir_actual_ = sentido_;
        pon(dir_, id_dir_, dir_actual_);
        pon(step_, id_step_, false);
        for (;;) {
            const bool cambia = dir_actual_ != sentido_;
            const double obj = cambia ? 0.0 : objetivo_;
            // Parado y sin nada que hacer
            if (rate_ == 0.0 && obj == 0.0) {
                if (cambia) {
                    dir_actual_ = sentido_;
                    pon(dir_, id_dir_, dir_actual_);
                    wait(sc_core::sc_time(dir_s_, sc_core::SC_SEC));
                    continue;
                }
                wait(ev_ | evento_conexion());
                pon(dir_, id_dir_, dir_actual_);
                continue;
            }
            // La velocidad del próximo paso: v² crece o mengua 2·a por paso
            const double a = acel_;
            if (a == 0.0) rate_ = obj;
            else if (rate_ < obj) rate_ = std::min(obj, std::sqrt(rate_ * rate_ + 2.0 * a));
            else if (rate_ > obj) {
                const double v2 = rate_ * rate_ - 2.0 * a;
                rate_ = v2 > 0.0 ? std::max(obj, std::sqrt(v2)) : obj;
            }
            if (rate_ <= 0.0) { rate_ = 0.0; continue; }
            // El pulso
            const double periodo = 1.0 / rate_;
            const double alto = std::min(ancho_, 0.5 * periodo);
            pon(step_, id_step_, true);
            wait(sc_core::sc_time(alto, sc_core::SC_SEC));
            pon(step_, id_step_, false);
            pasos_ += dir_actual_ ? 1 : -1;
            wait(sc_core::sc_time(periodo - alto, sc_core::SC_SEC));
        }
    }
    analog_net_if *step_, *dir_;
    int id_step_ = -1, id_dir_ = -1;
    double objetivo_, acel_, ancho_, dir_s_;
    bool sentido_, dir_actual_ = true;
    double vdd_;
    double rate_ = 0.0;
    long pasos_ = 0;
    sc_core::sc_event ev_;
};

} // namespace stm32

#endif // STM32_PARTS_DRIVER_PAP_H
