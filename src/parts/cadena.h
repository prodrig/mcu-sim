// =============================================================================
// cadena.h — LA CADENA MECÁNICA: lo que hay entre el eje de un motor y su carga
//
// (Fase F0 de doc/analisis_cnc.md, §4, opción C: una CADENA RÍGIDA DECLARADA.)
//
// Hasta aquí cada motor llevaba su carga dentro: el MotorCC integraba
// J·dw/dt = Kt·i - Tf - Tc con su J y su Tc de mando. Una máquina necesita que
// el par pase por una reductora, un acople, un husillo y una tuerca hasta un
// carro, y que todo eso vuelva al motor como inercia y como par resistente.
//
// Con acoples rígidos -las resonancias torsionales de varios kHz no interesan
// aquí- toda la cadena es UN SOLO GRADO DE LIBERTAD: el ángulo th y la
// velocidad w de su EJE DE REFERENCIA, que es el de los motores. Cada cosa se
// refleja en él:
//
//     J = Σ J_motor + Σ J_k / n_k²          (n_k: vueltas del eje de referencia
//     F = Σ F_motor + Σ F_k / (n_k · η_k)    por vuelta de la entrada del
//                                           elemento k; η_k, el rendimiento
//                                           de lo que hay antes de él)
//
// y la cadena integra una sola ecuación mecánica, con los estados eléctricos
// de TODOS sus motores a la vez:
//
//     J·dw/dt = Σ T_motor(e, w, th) - F·sgn(w) - B·w
//     de/dt   = f_motor(e, w, th)            (las corrientes de cada motor)
//
// Las piezas son de dos clases:
//
//   ACTUADOR   un motor: tiene estados eléctricos (la corriente de su bobina,
//              o las de sus dos fases) y da un par. Es quien despierta a la
//              cadena: sus bornes, sus mandos, sus flancos.
//   ELEMENTO   algo pasivo: una inercia, un rozamiento y una relación de
//              transmisión. El Volante de aquí; la reductora, el acople, el
//              husillo y el carro de la fase F2.
//
// Una cadena la declara una placa con `<transmision id="X" cadena="M V"/>`:
// primero los motores, después los elementos, del motor hacia la carga. UN
// MOTOR SUELTO tiene su cadena IMPLÍCITA, de él solo, con su carga de siempre:
// la que integra su propio proceso. Por eso el MotorCC de los sistemas de antes
// sigue dando las mismas rpm, al decimal (M9 y M10).
//
// LA INTEGRACIÓN es la que tenía el MotorCC, generalizada: Runge-Kutta de
// cuarto orden a pasos de 5 us como mucho -y nunca más de lo que diga cada
// motor-, pero SystemC no despierta en cada paso. El proceso de la cadena
// despierta en los cambios de los bornes de sus motores, en sus mandos, en lo
// que cada motor diga que hay que hacer ver (un flanco de su encoder, un
// cambio de sentido de su corriente) -ensayado de antemano con los mismos
// pasos, así que cae en su instante-, cuando se para, y cada 0,1 ms mientras
// alguna corriente se asienta o cada milisegundo mientras acelera.
//
// EL ROZAMIENTO SECO tiene dos caras: parado, el eje no se mueve mientras el
// par no pase del de ARRANQUE (la suma de los de cada pieza); en marcha, se
// opone al giro con el de MARCHA, y al pararse no lo empuja hacia atrás.
//
// EL ÁNGULO se integra con Runge-Kutta, como lo demás, si algún motor da un par
// que depende de él -el paso a paso-; si ninguno, con la regla del trapecio
// sobre la velocidad, que es lo que hacía el MotorCC.
// =============================================================================
#ifndef STM32_PARTS_CADENA_H
#define STM32_PARTS_CADENA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "part_base.h"

namespace stm32 {

class Cadena;

// ---------------------------------------------------------------------------
// UN ACTUADOR: un motor, visto por la cadena. Sus estados eléctricos los guarda
// él (lee_e, pon_e); la cadena los copia, los integra y se los devuelve.
// Todos los pares van en el EJE DE REFERENCIA de la cadena, que es el suyo.
// ---------------------------------------------------------------------------
class Actuador {
public:
    static constexpr unsigned MAX_E = 4;          // estados eléctricos, como mucho
    static constexpr double INF = std::numeric_limits<double>::infinity();
    virtual ~Actuador() {}

    // --- Los estados eléctricos -------------------------------------------
    virtual unsigned n_e() const = 0;
    virtual void lee_e(double* e) const = 0;
    virtual void pon_e(const double* e) = 0;
    // Las derivadas de sus estados y el par que da, con el eje en (w, th)
    virtual void deriva(const double* e, double w, double th, double* de, double& par) const = 0;
    // ¿Su par depende del ángulo? (el de un paso a paso, sí)
    virtual bool depende_del_angulo() const { return false; }
    // Antes de cada paso, con el eje en (w, th): lo que el régimen impone (sin
    // rama, sin corriente), y lo que no puede cambiar a mitad del paso -hacia
    // dónde empuja un chopper, por dónde vuelve una corriente-: una
    // discontinuidad dentro de un paso de Runge-Kutta no se integra
    virtual void prepara(double* /*e*/, double /*w*/, double /*th*/) const {}
    // Después de cada paso, con el estado de antes: un límite que se cruza
    // (la corriente impuesta que llega a su consigna) se queda en él
    virtual void recorta(const double* /*e0*/, double* /*e*/) const {}
    // El paso máximo que pide su parte eléctrica; INF, ninguno
    virtual double paso_max() const { return INF; }
    // Sin corriente posible: ni da par ni tiene nada eléctrico que integrar
    virtual bool inerte() const = 0;
    // ¿Se queda así? La corriente asentada, y el par que daría así
    virtual bool asentado(const double* e) const = 0;
    virtual double par_asentado(double th) const = 0;
    // Lo que se le hace al estado cuando la cadena se queda quieta: `limpia`,
    // al empezar a avanzar ya quieta; `asienta`, al llegar a quieta
    virtual void limpia(double* /*e*/) const {}
    virtual void asienta(double* /*e*/) const {}
    // ¿Se está asentando su corriente? (la cadena se mira cada 0,1 ms) ¿Está
    // del todo estable? (y entonces, solo sus marcas)
    virtual bool asentando(const double* /*e*/, const double* /*de*/) const { return false; }
    virtual bool estable(const double* /*e*/, const double* /*de*/) const { return false; }
    // Cuánto falta para su próxima marca -un flanco de su encoder- con la
    // velocidad de ahora; INF, ninguna
    virtual double hasta_marca(double /*th*/, double /*w*/) const { return INF; }
    // ¿Hay algo que ensayar para despertar en su instante -un flanco de su
    // encoder, un cambio de sentido de su corriente-? Si no, la cadena se mira
    // cada 0,1 o 1 ms sin ensayar nada: un paso a paso despierta de todas
    // formas en cada flanco de STEP
    virtual bool con_marcas() const { return true; }
    // ¿Ha pasado algo que haya que hacer ver entre e0 y (e, th, w)?
    virtual bool cambia(const double* /*e0*/, const double* /*e*/, double /*th*/,
                        double /*w*/) const { return false; }
    // Un paso de h segundos HECHO de verdad -no ensayado-, de e0 a e, con el
    // eje en (w, th) al final: para llevar la cuenta de lo que gasta
    virtual void hecho(const double* /*e0*/, const double* /*e*/, double /*w*/, double /*th*/,
                       double /*h*/) {}

    // --- Lo mecánico suyo ---------------------------------------------------
    virtual double inercia() const = 0;           // kg·m², en su eje
    virtual double rozamiento() const { return 0.0; }   // N·m, seco, en marcha
    virtual double arranque() const { return rozamiento(); }  // N·m, seco, parado
    virtual double viscoso() const { return 0.0; }      // N·m·s/rad
    virtual bool   bloqueado() const { return false; }  // una mano que lo sujeta
    virtual double posicion_inicial() const { return 0.0; }  // rad

    // --- El proceso -----------------------------------------------------------
    // Antes de avanzar (apuntar sus medias) y después (resolver sus ramas, sus
    // salidas, sus avisos). Lo que lo despierta
    virtual void antes() {}
    virtual void regimen() = 0;
    virtual void eventos(sc_core::sc_event_or_list& l) = 0;
    // El nombre, para los mensajes
    virtual std::string nombre_actuador() const = 0;

    Cadena* cadena() const { return cadena_; }
    void    engancha(Cadena* c) { cadena_ = c; }
    // La <transmision> que lo lleva; nullptr, ninguna (va en la suya)
    const ExtPartBase* transmision() const { return transmision_; }
    void pon_transmision(const ExtPartBase* t) { transmision_ = t; }
protected:
    Cadena* cadena_ = nullptr;
    const ExtPartBase* transmision_ = nullptr;
};

// ---------------------------------------------------------------------------
// UN ELEMENTO PASIVO de la cadena. Sus cifras van en su ENTRADA, el eje que da
// al motor; su `relacion` dice cuántas vueltas da la entrada por cada una de
// la salida, que es la entrada del siguiente.
// ---------------------------------------------------------------------------
class ElementoMecanico {
public:
    virtual ~ElementoMecanico() {}
    virtual double relacion() const { return 1.0; }
    virtual double rendimiento() const { return 1.0; }
    virtual double inercia() const { return 0.0; }       // kg·m²
    virtual double rozamiento() const { return 0.0; }    // N·m, seco, en marcha
    virtual double arranque() const { return rozamiento(); }
    virtual double viscoso() const { return 0.0; }       // N·m·s/rad
    virtual bool   bloqueado() const { return false; }
    // Su entrada se ha movido: th en rad, w en rad/s
    virtual void mueve(double /*th*/, double /*w*/) {}
    // El nombre, para los mensajes
    virtual std::string nombre_elemento() const = 0;

    Cadena* cadena() const { return cadena_; }
    void    engancha(Cadena* c) { cadena_ = c; }
    const ExtPartBase* transmision() const { return transmision_; }
    void pon_transmision(const ExtPartBase* t) { transmision_ = t; }
protected:
    Cadena* cadena_ = nullptr;
    const ExtPartBase* transmision_ = nullptr;
};

// ---------------------------------------------------------------------------
// LA CADENA: los motores y los elementos, el estado mecánico y su integrador.
// No es un proceso: la mueve el de quien la tiene -el MotorCC con la suya
// implícita, o la Transmision- llamando a corre().
// ---------------------------------------------------------------------------
class Cadena {
public:
    static constexpr double PASO = 5e-6;           // s: el paso máximo de integración
    static constexpr double REFRESCO = 1e-3;       // s, mientras acelera o frena
    static constexpr double REFRESCO_I = 1e-4;     // s, mientras la corriente se asienta
    static constexpr unsigned MAX_A = 4;           // motores por cadena
    static constexpr double INF = std::numeric_limits<double>::infinity();

    explicit Cadena(double th0 = 0.0) { x_.th = th0; }

    // --- Montaje (en la elaboración) -------------------------------------
    // false si no cabe: hay MAX_A motores como mucho
    bool pon_actuador(Actuador* a) {
        if (act_.size() >= MAX_A) return false;
        act_.push_back(a);
        a->engancha(this);
        return true;
    }
    void pon_elemento(ElementoMecanico* e) {
        elem_.push_back(e);
        e->engancha(this);
    }
    // Lo reflejado, una vez montada. Las cifras de cada pieza no cambian
    void cierra() {
        n_.clear();
        double n = 1.0, eta = 1.0;
        for (ElementoMecanico* e : elem_) {
            n_.push_back(n);
            eta_.push_back(eta);
            n *= e->relacion();
            eta *= e->rendimiento();
        }
        th_angulo_ = false;
        for (Actuador* a : act_) th_angulo_ = th_angulo_ || a->depende_del_angulo();
        if (!act_.empty()) x_.th = act_[0]->posicion_inicial();
    }

    // --- Lo que se lee ---------------------------------------------------
    double w() const  { return x_.w; }                // rad/s del eje de referencia
    double th() const { return x_.th; }               // rad
    double rpm() const { return x_.w * 60.0 / (2.0 * PI); }
    // Del elemento k: su entrada
    double th_de(unsigned k) const { return x_.th / n_[k]; }
    double w_de(unsigned k) const { return x_.w / n_[k]; }
    double relacion_total() const {
        double n = 1.0;
        for (const ElementoMecanico* e : elem_) n *= e->relacion();
        return n;
    }
    // Todo reflejado en el eje de referencia
    double inercia() const {
        double j = 0.0;
        for (const Actuador* a : act_) j += a->inercia();
        for (size_t k = 0; k < elem_.size(); ++k)
            if (elem_[k]->inercia() != 0.0) j += elem_[k]->inercia() / (n_[k] * n_[k]);
        return j;
    }
    bool bloqueada() const {
        for (const Actuador* a : act_) if (a->bloqueado()) return true;
        for (const ElementoMecanico* e : elem_) if (e->bloqueado()) return true;
        return false;
    }
    const std::vector<Actuador*>& actuadores() const { return act_; }
    const std::vector<ElementoMecanico*>& elementos() const { return elem_; }
    // EL MODELO IDEAL de un motor (MotorPaP, modelo="ideal") no se integra:
    // pone el eje donde dice, parado, y la cadena lo sigue
    void impone(double th) {
        x_.th = th;
        x_.w = 0.0;
        t0_ = ahora();
        for (size_t k = 0; k < elem_.size(); ++k) elem_[k]->mueve(th_de(unsigned(k)), 0.0);
    }
    // Algo ha cambiado en un mando: que la cadena lo mire
    void toca() { ev_mando_.notify(sc_core::SC_ZERO_TIME); }
    const sc_core::sc_event& evento_mando() const { return ev_mando_; }

    // --- El proceso: lo llama el SC_THREAD de quien la tiene ---------------
    void corre() {
        sc_core::sc_event_or_list ev;
        ev |= ev_mando_;
        for (Actuador* a : act_) a->eventos(ev);
        for (;;) {
            for (Actuador* a : act_) a->antes();
            avanza(ahora());
            for (size_t k = 0; k < elem_.size(); ++k) elem_[k]->mueve(th_de(unsigned(k)),
                                                                      w_de(unsigned(k)));
            for (Actuador* a : act_) a->regimen();
            // Nunca menos de un nanosegundo: un plazo que redondea a cero
            // sería un bucle de ciclos delta
            const double plazo = proximo();
            if (!std::isfinite(plazo)) sc_core::wait(ev);
            else sc_core::wait(sc_core::sc_time(std::max(plazo, 1e-9), sc_core::SC_SEC), ev);
        }
    }

private:
    static constexpr double PI = 3.14159265358979323846;
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static int signo(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

    // El estado entero: los eléctricos de cada motor, la velocidad y el ángulo
    struct Estado {
        double e[MAX_A][Actuador::MAX_E] = {};
        double w = 0.0, th = 0.0;
    };

    // --- Lo que suman todos ------------------------------------------------
    double rozamiento() const {
        double f = 0.0;
        for (const Actuador* a : act_) f += a->rozamiento();
        for (size_t k = 0; k < elem_.size(); ++k)
            if (elem_[k]->rozamiento() != 0.0) f += elem_[k]->rozamiento() / (n_[k] * eta_[k]);
        return f;
    }
    double arranque() const {
        double f = 0.0;
        for (const Actuador* a : act_) f += a->arranque();
        for (size_t k = 0; k < elem_.size(); ++k)
            if (elem_[k]->arranque() != 0.0) f += elem_[k]->arranque() / (n_[k] * eta_[k]);
        return f;
    }
    double viscoso() const {
        double b = 0.0;
        for (const Actuador* a : act_) b += a->viscoso();
        for (size_t k = 0; k < elem_.size(); ++k)
            if (elem_[k]->viscoso() != 0.0) b += elem_[k]->viscoso() / (n_[k] * n_[k] * eta_[k]);
        return b;
    }
    bool inerte() const {
        for (const Actuador* a : act_) if (!a->inerte()) return false;
        return true;
    }
    double paso_max() const {
        double h = INF;
        for (const Actuador* a : act_) h = std::min(h, a->paso_max());
        return std::isfinite(h) ? h : 1e-4;
    }

    // Las derivadas de todo, con el sentido s del rozamiento fijo (0: parado).
    // Devuelve el par de los motores en `par`
    void deriva(const Estado& x, int s, Estado& d, double& par) const {
        par = 0.0;
        for (size_t k = 0; k < act_.size(); ++k) {
            double t = 0.0;
            act_[k]->deriva(x.e[k], x.w, x.th, d.e[k], t);
            par += t;
        }
        d.th = x.w;
        if (s == 0) { d.w = 0.0; return; }
        double m = par - f_ * s;
        if (b_ != 0.0) m -= b_ * x.w;
        d.w = m / j_;
    }
    // x + h·d, en todo
    void suma(const Estado& x, double h, const Estado& d, Estado& r) const {
        for (size_t k = 0; k < act_.size(); ++k)
            for (unsigned i = 0; i < act_[k]->n_e(); ++i) r.e[k][i] = x.e[k][i] + h * d.e[k][i];
        r.w = x.w + h * d.w;
        r.th = x.th + h * d.th;
    }

    // Un paso de h segundos
    Estado paso(Estado x, double h) const {
        for (size_t k = 0; k < act_.size(); ++k) act_[k]->prepara(x.e[k], x.w, x.th);
        int s = signo(x.w);
        if (bloq_) { x.w = 0.0; s = 0; }
        else if (s == 0) {
            // Parado: arranca si el par pasa del de arranque
            Estado d;
            double par = 0.0;
            deriva(x, 0, d, par);
            if (std::fabs(par) > fa_) s = signo(par);
        }
        if (inerte()) {
            // Sin corriente: el rozamiento lo frena en línea recta, exacto
            if (s == 0) return x;
            const double a = -f_ * s / j_;
            double t = h;
            if (-x.w / a < h) t = -x.w / a;
            x.th += x.w * t + 0.5 * a * t * t;
            x.w = (t < h) ? 0.0 : x.w + a * h;
            if (signo(x.w) == -s) x.w = 0.0;
            return x;
        }
        Estado k1, k2, k3, k4, t;
        double p;
        deriva(x, s, k1, p);
        suma(x, 0.5 * h, k1, t);
        deriva(t, s, k2, p);
        suma(x, 0.5 * h, k2, t);
        deriva(t, s, k3, p);
        suma(x, h, k3, t);
        deriva(t, s, k4, p);
        const Estado x0 = x;
        for (size_t k = 0; k < act_.size(); ++k)
            for (unsigned i = 0; i < act_[k]->n_e(); ++i)
                x.e[k][i] += h / 6.0 * (k1.e[k][i] + 2 * k2.e[k][i] + 2 * k3.e[k][i] + k4.e[k][i]);
        x.w += h / 6.0 * (k1.w + 2 * k2.w + 2 * k3.w + k4.w);
        if (s != 0 && signo(x.w) != s) x.w = 0.0;     // se para
        if (th_angulo_) x.th += h / 6.0 * (k1.th + 2 * k2.th + 2 * k3.th + k4.th);
        else            x.th += 0.5 * (x0.w + x.w) * h;
        for (size_t k = 0; k < act_.size(); ++k) act_[k]->recorta(x0.e[k], x.e[k]);
        return x;
    }
    // ¿Se queda como está? Parada, sin arrancar, con las corrientes asentadas
    bool quieto(const Estado& x) const {
        if (x.w != 0.0 && !bloq_) return false;
        double par = 0.0;
        for (size_t k = 0; k < act_.size(); ++k) {
            if (!act_[k]->asentado(x.e[k])) return false;
            par += act_[k]->par_asentado(x.th);
        }
        const bool arranca = !bloq_ && std::fabs(par) > fa_;
        return !arranca;
    }
    // Lo que valen ahora las sumas: cambian con los mandos de cada pieza
    void cifras() {
        j_ = inercia();
        f_ = rozamiento();
        fa_ = arranque();
        b_ = viscoso();
        bloq_ = bloqueada();
    }
    void lee(Estado& x) const {
        for (size_t k = 0; k < act_.size(); ++k) act_[k]->lee_e(x.e[k]);
    }
    void escribe(const Estado& x) {
        for (size_t k = 0; k < act_.size(); ++k) act_[k]->pon_e(x.e[k]);
    }
    // Lleva el estado de t0_ al t1 con el régimen que había
    void avanza(double t1) {
        cifras();
        lee(x_);
        double dt = t1 - t0_;
        t0_ = t1;
        if (dt <= 0.0) return;
        if (quieto(x_)) {
            for (size_t k = 0; k < act_.size(); ++k) act_[k]->limpia(x_.e[k]);
            quieto_hecho(dt);
            escribe(x_);
            return;
        }
        const double hm = paso_max();
        const long n = std::max(1L, long(std::ceil(dt / hm - 1e-9)));
        const double h = dt / double(n);
        for (long k = 0; k < n; ++k) {
            const Estado x0 = x_;
            x_ = paso(x_, h);
            for (size_t a = 0; a < act_.size(); ++a) act_[a]->hecho(x0.e[a], x_.e[a], x_.w, x_.th, h);
            if (quieto(x_)) {
                for (size_t a = 0; a < act_.size(); ++a) act_[a]->asienta(x_.e[a]);
                quieto_hecho(dt - double(k + 1) * h);
                break;
            }
        }
        escribe(x_);
    }
    // Lo que queda del intervalo, quieta: también cuenta para lo que se gasta
    void quieto_hecho(double resto) {
        if (resto <= 0.0) return;
        for (size_t a = 0; a < act_.size(); ++a) act_[a]->hecho(x_.e[a], x_.e[a], x_.w, x_.th, resto);
    }
    // Cuándo pasa algo que hay que hacer ver, sin que nadie toque los bornes:
    // lo que diga cada motor, que se pare o arranque, o el refresco. Se ensaya
    // con los mismos pasos que dará avanza(), así que el instante es exacto
    double proximo() {
        cifras();
        lee(x_);
        if (quieto(x_)) return INF;
        const double hm = paso_max();
        Estado d, xp = x_;
        double par = 0.0;
        for (size_t k = 0; k < act_.size(); ++k) act_[k]->prepara(xp.e[k], xp.w, xp.th);
        deriva(xp, signo(xp.w), d, par);
        bool asentando = false, estable = true;
        for (size_t k = 0; k < act_.size(); ++k) {
            asentando = asentando || act_[k]->asentando(xp.e[k], d.e[k]);
            estable = estable && act_[k]->estable(xp.e[k], d.e[k]);
        }
        const double horizonte = asentando ? REFRESCO_I : REFRESCO;
        // Estable del todo: las marcas, con la velocidad de ahora
        if (estable && std::fabs(d.w) < 1e-6 && x_.w != 0.0) {
            double t = INF;
            for (const Actuador* a : act_) t = std::min(t, a->hasta_marca(x_.th, x_.w));
            return t;
        }
        bool marcas = false;
        for (const Actuador* a : act_) marcas = marcas || a->con_marcas();
        if (!marcas) return horizonte;
        Estado x = x_;
        const int s0 = signo(x.w);
        double t = 0.0;
        while (t < horizonte - 1e-12) {
            const double h = std::min(hm, horizonte - t);
            x = paso(x, h);
            t += h;
            for (size_t k = 0; k < act_.size(); ++k)
                if (act_[k]->cambia(x_.e[k], x.e[k], x.th, x.w)) return t;
            if (signo(x.w) != s0) return t;
            if (quieto(x)) return t;
        }
        return horizonte;
    }

    std::vector<Actuador*> act_;
    std::vector<ElementoMecanico*> elem_;
    std::vector<double> n_, eta_;       // de cada elemento: relación y rendimiento antes de él
    bool th_angulo_ = false;
    // Las sumas, al día en cada vuelta
    double j_ = 1e-3, f_ = 0.0, fa_ = 0.0, b_ = 0.0;
    bool bloq_ = false;
    // El estado, en t0_
    double t0_ = 0.0;
    Estado x_;
    sc_core::sc_event ev_mando_;
};

// ---------------------------------------------------------------------------
// UNA TRANSMISIÓN: la cadena que declara una placa, `<transmision id="X"
// cadena="MX VX"/>`. Primero los motores (uno o varios: el pórtico de dos
// motores), después los elementos, del motor hacia la carga. Es una pieza sin
// patillas: lo que tiene es su proceso, el que mueve la cadena, y lo que deja
// ver del eje de referencia.
// ---------------------------------------------------------------------------
SC_MODULE(Transmision), public ExtPartBase {
    Transmision(sc_core::sc_module_name nm, const std::vector<Actuador*>& act,
                const std::vector<ElementoMecanico*>& elem)
        : sc_core::sc_module(nm), ExtPartBase("Transmision", nm) {
        for (Actuador* a : act) { cadena_.pon_actuador(a); a->pon_transmision(this); }
        for (ElementoMecanico* e : elem) { cadena_.pon_elemento(e); e->pon_transmision(this); }
        cadena_.cierra();
        SC_HAS_PROCESS(Transmision);
        SC_THREAD(mueve);
    }
    Cadena& cadena() { return cadena_; }
    const Cadena& cadena() const { return cadena_; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"angulo", "°", 0.f, 360.f, true};
        if (i == 1) return {"rpm", "rpm", -6000.f, 6000.f, false};
        return {"inercia", "g·cm²", 0.f, 0.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) {
            double g = std::fmod(cadena_.th() * 180.0 / 3.14159265358979323846, 360.0);
            return float(g < 0 ? g + 360.0 : g);
        }
        if (i == 1) return float(cadena_.rpm());
        return float(cadena_.inercia() * 1e7);
    }

private:
    void mueve() { cadena_.corre(); }
    Cadena cadena_;
};

// ---------------------------------------------------------------------------
// UN VOLANTE: una inercia en un eje, con su rozamiento -seco y viscoso-, sin
// nada más. Lo que se cuelga de un motor para comprobar que la cadena suma
// bien: con uno de inercia conocida, la constante de tiempo del motor es la
// que sale de la cuenta. Va en una <transmision>; suelto, no se mueve.
//
// Su inercia se da directamente (`inercia_gcm2`) o como la de un disco macizo
// de acero de `diametro_mm` y `espesor_mm` (J = m·r²/2, 7850 kg/m³).
// ---------------------------------------------------------------------------
class Volante : public ExtPartBase, public ElementoMecanico {
public:
    Volante(const char* nombre, double inercia_kgm2, double rozamiento_nm = 0.0,
            double viscoso_nms = 0.0)
        : ExtPartBase("Volante", nombre), j_(inercia_kgm2), f_(rozamiento_nm),
          b_(viscoso_nms) {}

    double inercia() const override { return j_; }
    double rozamiento() const override { return f_; }
    double viscoso() const override { return b_; }
    void   mueve(double th, double w) override { th_ = th; w_ = w; }
    std::string nombre_elemento() const override { return pieza(); }

    double angulo() const {
        double g = std::fmod(th_ * 180.0 / 3.14159265358979323846, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double rpm() const { return w_ * 60.0 / (2.0 * 3.14159265358979323846); }
    double vueltas() const { return th_ / (2.0 * 3.14159265358979323846); }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"angulo", "°", 0.f, 360.f, true};
        return {"rpm", "rpm", -6000.f, 6000.f, false};
    }
    float valor_observable(unsigned i) const override {
        return i == 0 ? float(angulo()) : float(rpm());
    }

private:
    double j_, f_, b_;
    double th_ = 0.0, w_ = 0.0;
};

} // namespace stm32

#endif // STM32_PARTS_CADENA_H
