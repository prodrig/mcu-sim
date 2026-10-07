// =============================================================================
// tft_st7735.h — Una pantalla TFT de 1,8" y 128x160 con su ST7735S, por SPI de
//                cuatro hilos
//
// El módulo de siempre, el rojo de ocho pines: el vidrio de 128x160 píxeles,
// su controlador ST7735S [ST7735S, Sitronix, v1.1, 2011] en modo serie de
// CUATRO hilos -CSX, D/CX, SCL y SDA- y, en la placa, un regulador de 3,3 V y
// la retroiluminación. Los ocho contactos son los del módulo:
//
//   vcc     la alimentación del módulo, 3,3 a 5 V: va al regulador, que da
//           los 3,3 V del chip (VDD y VDDI). Con menos de 2,5 V el chip no
//           arranca
//   gnd     la masa
//   cs      CSX: con él alto la interfaz no atiende y se reinicia
//   reset   RESX: un pulso bajo de más de 5 µs es un reset del chip
//   ad      D/CX (A0, «RS»): bajo, el byte es una orden; alto, un dato
//   sda     SDA: el dato, en las dos direcciones -el chip lo pone al leer-
//   sck     SCL: el reloj. El chip toma SDA en el flanco de SUBIDA
//   led     la retroiluminación: el ánodo de los LEDs blancos, con su
//           resistencia en la placa
//
// LO QUE SE VE es lo que enseñaría la pantalla de verdad (`pinta_imagen`):
//
//   * el chip sin alimentar, en reset, dormido (SLPIN, el estado tras
//     arrancar) o con la pantalla apagada (DISPOFF): BLANCO. El cristal es
//     de los normalmente blancos: sin tensión deja pasar la luz. Es la
//     pantalla blanca que enseña un módulo que nadie ha iniciado;
//   * despierto y encendido: la memoria de imagen, con la geometría de
//     MADCTL, el desplazamiento vertical (SCRLAR/VSCSAD), el modo parcial
//     (PTLAR/PTLON: fuera del área, blanco), el modo de ocho colores
//     (IDMON: el bit alto de cada color), la inversión (INVON) y el orden
//     RGB/BGR del panel contra el bit RGB de MADCTL;
//   * la memoria, al dar tensión, tiene lo que tenga: RUIDO. Un firmware que
//     enciende la pantalla sin borrarla enseña colores al azar, como el chip;
//   * y todo eso, con la LUZ de la retroiluminación: sin ella, negro. La luz
//     va aparte (`brillo_imagen`), media en el tiempo: con PWM en el pin LED
//     se ve la luz media, que es lo que ve el ojo.
//
// LA INTERFAZ, como dice la hoja [§9.4]: con CSX bajo, cada flanco de subida
// de SCL toma un bit de SDA, el primero el más alto; en el octavo se toma
// también D/CX. Un CSX que sube a medio byte lo descarta [§9.5]; uno que sube
// entre bytes es una pausa y no rompe nada [§9.6]. Las ÓRDENES de lectura
// (RDDID, RDDST, RDDPM, RDDMADCTL, RDDCOLMOD, RDDIM, RDDSM, RDDSDR, RDID1-3 y
// RAMRD) contestan por SDA, poniendo cada bit en el flanco de BAJADA; las de
// 24 y 32 bits -y RAMRD- con un ciclo de reloj vacío delante [figura 20].
// Tras una lectura hay que subir CSX antes de la siguiente orden.
//
// Las ÓRDENES del sistema están todas [§10.1]; las del panel (B1h-FCh: marco,
// potencia, VCOM, gamma) se aceptan con sus parámetros y no cambian nada de lo
// que se ve. Los formatos de píxel de COLMOD -12, 16 y 18 bits- van por la
// tabla de color (RGBSET) [§9.18]; mientras nadie la escriba, por la expansión
// natural de cada formato: la hoja dice que al arrancar «es aleatoria», pero
// los módulos de verdad pintan bien en 16 bits sin tocarla.
//
// LO QUE SE DICE en vez de callar (un aviso por cosa, una vez):
//
//   * órdenes en los 5 ms que siguen a soltar RESET: el chip está cargando
//     sus registros [§9.17, nota 7] y la orden SE PIERDE;
//   * órdenes antes de los 120 ms que piden SLPOUT, SLPIN y SWRESET; y
//     SLPOUT antes de 120 ms tras un reset. Se aplican, pero la hoja no lo
//     garantiza;
//   * un reloj más rápido de lo que admite la hoja: 66 ns de ciclo al
//     escribir, 150 al leer [§8.4];
//   * una entrada por encima de VDDI + 0,3 V: 5 V en las patillas de un chip
//     de 3,3 V [§7.1]. Este módulo no lleva adaptadores de nivel;
//   * una orden que el ST7735S no tiene.
//
// GEOMETRÍA. El panel es de 128x160 y el ST7735S puede direccionar la memoria
// como 128x160 (GM = 11, el de este módulo) o como 132x162 (GM = 00, el de
// algunos módulos: la imagen visible empieza entonces en la columna 2 y la
// fila 1). Lo dice `memoria`. Y el ORDEN DE LOS FILTROS de color del panel,
// RGB o BGR, `panel`: con uno BGR los colores salen cambiados si el firmware
// no pone el bit RGB de MADCTL.
//
// LO QUE NO SE MODELA: la gamma y los ajustes de potencia (se aceptan), la
// salida TE -este módulo no la saca-, los tiempos de refresco del cristal
// (lo escrito se ve en cuanto está escrito) y el autodiagnóstico de SLPOUT
// (RDDSDR lee 0).
// =============================================================================
#ifndef STM32_PARTS_TFT_ST7735_H
#define STM32_PARTS_TFT_ST7735_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "ext_parts.h"

namespace stm32 {

SC_MODULE(Tft128x160), public ExtPartBase {
    static constexpr unsigned ANCHO = 128, ALTO = 160;  // el panel, en píxeles
    static constexpr double   VDD_REG = 3.3;   // lo que da el regulador del módulo
    static constexpr double   CAIDA_REG = 0.15;// lo que se come el regulador
    static constexpr double   V_ARRANQUE = 2.5;// VDD mínima [§7.2]
    static constexpr double   R_CONSUMO = 1000.0;  // chip y regulador: ~3 mA
    static constexpr double   I_LUZ_PLENA = 0.020; // a esta corriente, luz entera

    // `memoria_132x162`: GM = 00 (véase la cabecera). `panel_bgr`: los filtros
    // del panel en orden BGR. `vf_luz` y `r_luz`: los LEDs de la
    // retroiluminación y su resistencia en la placa.
    Tft128x160(sc_core::sc_module_name nm, analog_net_if& vcc, analog_net_if& gnd,
               analog_net_if& cs, analog_net_if& reset, analog_net_if& ad,
               analog_net_if& sda, analog_net_if& sck, analog_net_if& led,
               bool memoria_132x162 = false, bool panel_bgr = false,
               double vf_luz = 2.9, double r_luz = 15.0)
        : sc_core::sc_module(nm), ExtPartBase("Tft128x160", nm),
          vcc_(&vcc), gnd_(&gnd), cs_(&cs), reset_(&reset), ad_(&ad), sda_(&sda),
          sck_(&sck), led_(&led),
          w_(memoria_132x162 ? 132u : 128u), h_(memoria_132x162 ? 162u : 160u),
          offx_(memoria_132x162 ? 2u : 0u), offy_(memoria_132x162 ? 1u : 0u),
          panel_bgr_(panel_bgr), vf_luz_(vf_luz), r_luz_(r_luz < 0.1 ? 0.1 : r_luz),
          mem_(std::size_t(w_) * h_, 0u) {
        const int iv = add_pin("vcc", vcc, "tft_vcc");
        const int ig = add_pin("gnd", gnd, "tft_gnd");
        const int il = add_pin("led", led, "tft_luz");
        const int igl = add_drv("gnd", "tft_luz_gnd");
        add_ref("cs", cs);
        add_ref("reset", reset);
        add_ref("ad", ad);
        id_sda_ = add_pin("sda", sda, "tft_sda");
        add_ref("sck", sck);
        sda_->set_hiz(id_sda_);
        alim_.reset(new RamaDosNodos(vcc, iv, gnd, ig));
        luz_.reset(new RamaDosNodos(led, il, gnd, igl));
        SC_HAS_PROCESS(Tft128x160);
        SC_THREAD(electrica);
        SC_METHOD(entradas);
        sensitive << sck_->value_changed_event() << cs_->value_changed_event()
                  << reset_->value_changed_event();
        dont_initialize();
    }
    ~Tft128x160() override {
        alim_->suelta();
        luz_->suelta();
        sda_->set_hiz(id_sda_);
    }

    // --- Lo que se ve, desde C++ ------------------------------------------------
    bool alimentada() const { return alimentada_; }
    bool en_reset()   const { return en_reset_; }
    bool dormida()    const { return sleep_; }
    bool encendida()  const { return disp_; }
    // Si enseña la memoria: alimentada, fuera de reset, despierta y encendida
    bool mostrando()  const { return alimentada_ && !en_reset_ && !sleep_ && disp_; }
    uint8_t  madctl() const { return madctl_; }
    unsigned bits_por_pixel() const { return colmod_ == 3 ? 12u : colmod_ == 5 ? 16u : 18u; }
    uint64_t ordenes() const { return n_ordenes_; }
    uint64_t pixeles() const { return n_pixeles_; }
    uint64_t ordenes_perdidas() const { return n_perdidas_; }
    double   luz_ma() const { return i_luz_ * 1000.0; }
    // El color que se ve en el píxel (x, y) del panel -128 de ancho, 160 de
    // alto, desde arriba a la izquierda tal como sale la imagen-, en RGB888 y
    // sin la luz
    uint32_t color_en(unsigned x, unsigned y) const {
        if (x >= ANCHO || y >= ALTO) return 0;
        uint8_t p[3];
        pixel_visible(x, y, p);
        return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    }
    // Lo que hay en la memoria, en 6-6-6 bits: (R << 12) | (G << 6) | B
    uint32_t memoria(unsigned col, unsigned fila) const {
        return col < w_ && fila < h_ ? mem_[std::size_t(fila) * w_ + col] : 0u;
    }

    // --- Lo que deja ver a la ventana -------------------------------------------
    unsigned   n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"encendida", "", 0.f, 1.f, true};
        return {"luz", "mA", 0.f, 40.f, false};
    }
    float valor_observable(unsigned i) const override {
        return i == 0 ? (mostrando() ? 1.f : 0.f) : float(luz_ma());
    }
    unsigned n_imagenes() const override { return 1; }
    Imagen   imagen(unsigned) const override { return {"pantalla", ANCHO, ALTO}; }
    uint64_t version_imagen(unsigned) const override { return version_; }
    void pinta_imagen(unsigned, std::string& rgb) const override {
        rgb.resize(std::size_t(ANCHO) * ALTO * 3u);
        uint8_t* d = reinterpret_cast<uint8_t*>(&rgb[0]);
        for (unsigned y = 0; y < ALTO; ++y)
            for (unsigned x = 0; x < ANCHO; ++x, d += 3) pixel_visible(x, y, d);
    }
    float  brillo_imagen(unsigned) const override { return float(brillo_); }
    double luz_acumulada(unsigned) const override {
        return luz_acum_ + brillo_ * (sc_core::sc_time_stamp() - t_luz_).to_seconds();
    }

private:
    // =========================================================================
    // Lo eléctrico: el consumo, la retroiluminación y si el chip tiene tensión
    // =========================================================================
    void electrica() {
        for (;;) {
            alim_->resuelve(conectada_, R_CONSUMO);
            const bool luce = luz_->resuelve_diodo(conectada_, vf_luz_, r_luz_);
            const double i = luce ? std::max(0.0, luz_->corriente()) : 0.0;
            // La luz media se integra en el tiempo: lo de antes con su brillo
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            luz_acum_ += brillo_ * (ahora - t_luz_).to_seconds();
            t_luz_ = ahora;
            const double b = std::min(1.0, i / I_LUZ_PLENA);
            if (std::fabs(b - brillo_) > 1e-6 || std::fabs(i - i_luz_) > 1e-7) {
                brillo_ = b;
                i_luz_ = i;
            }
            // El chip: lo que deja el regulador, con algo de histéresis
            const double v = double(vcc_->voltage()) - double(gnd_->voltage());
            vddi_ = std::max(0.0, std::min(VDD_REG, v - CAIDA_REG));
            const bool on = conectada_ && vddi_ >= (alimentada_ ? V_ARRANQUE - 0.2 : V_ARRANQUE);
            if (on != alimentada_) {
                if (on) enciende();
                else    apaga();
            }
            wait(vcc_->value_changed_event() | gnd_->value_changed_event() |
                 led_->value_changed_event() | evento_conexion());
        }
    }

    void enciende() {
        alimentada_ = true;
        // Lo que tenga la memoria al dar tensión: ruido, el mismo cada vez
        uint32_t x = 2463534242u;
        for (char c : pieza()) x = x * 33u + uint8_t(c);
        for (uint32_t& p : mem_) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            p = x & 0x3FFFFu;
        }
        reset_hw();
        // Al dar tensión, RESX ya puede estar arriba: se cuenta como su subida
        t_reset_alto_ = sc_core::sc_time_stamp();
        hay_reset_ = true;
        reset_bajo_ = nivel(*reset_, true) == 0;
        en_reset_ = reset_bajo_;
        cs_alto_ = nivel(*cs_, true) != 0;
        sck_alto_ = nivel(*sck_, false) != 0;
        reinicia_interfaz();
        ++version_;
    }
    void apaga() {
        alimentada_ = false;
        sda_->set_hiz(id_sda_);
        reinicia_interfaz();
        ++version_;
    }

    // Un nivel lógico, con los umbrales de la hoja (30 % y 70 % de VDDI) y,
    // en medio, el anterior. Mide contra la masa del módulo.
    int nivel(const analog_net_if& n, bool anterior) const {
        const double v = double(n.voltage()) - double(gnd_->voltage());
        if (v > vddi_ + 0.3) sobretension(n, v);
        if (v >= 0.7 * vddi_) return 1;
        if (v <= 0.3 * vddi_) return 0;
        return anterior ? 1 : 0;
    }

    // =========================================================================
    // La interfaz serie
    // =========================================================================
    void entradas() {
        if (!alimentada_) return;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        // RESX, que manda sobre todo lo demás
        const bool rb = nivel(*reset_, !reset_bajo_) == 0;
        if (rb != reset_bajo_) {
            reset_bajo_ = rb;
            if (rb) {
                t_reset_bajo_ = ahora;
                en_reset_ = true;
                reinicia_interfaz();
                ++version_;
            } else {
                // Más corto que 5 µs es un pico, y se rechaza [§9.17]
                if (ahora - t_reset_bajo_ >= sc_core::sc_time(5, sc_core::SC_US)) {
                    reset_hw();
                    t_reset_alto_ = ahora;
                    hay_reset_ = true;
                }
                en_reset_ = false;
                ++version_;
            }
        }
        if (reset_bajo_) return;
        // CSX
        const bool ca = nivel(*cs_, cs_alto_) != 0;
        if (ca != cs_alto_) {
            cs_alto_ = ca;
            if (ca) {
                // A medio byte, lo recibido se tira [§9.5]; entre bytes es una
                // pausa [§9.6]. Una lectura se acaba aquí.
                bits_ = 0;
                byte_ = 0;
                fin_lectura();
            } else {
                bits_ = 0;
                byte_ = 0;
            }
        }
        // SCL
        const bool s = nivel(*sck_, sck_alto_) != 0;
        if (s == sck_alto_) return;
        sck_alto_ = s;
        if (cs_alto_) return;
        if (s) sube(ahora);
        else   baja();
    }

    void sube(const sc_core::sc_time& ahora) {
        // El ciclo del reloj, contra lo que admite la hoja [§8.4]
        if (hay_flanco_) {
            const double ns = (ahora - t_flanco_).to_seconds() * 1e9;
            const double min = leyendo_ ? 150.0 : 66.0;
            if (ns > 0 && ns < min - 0.5) {
                char b[160];
                std::snprintf(b, sizeof b,
                              "SCK con un ciclo de %.0f ns (%.1f MHz): el ST7735S admite %.0f ns "
                              "al %s [hoja, 8.4]", ns, 1e3 / ns, min,
                              leyendo_ ? "leer" : "escribir");
                avisa(leyendo_ ? "reloj_lectura" : "reloj_escritura", b);
            }
        }
        t_flanco_ = ahora;
        hay_flanco_ = true;
        if (leyendo_ || lectura_acabada_) return;   // el bit lo toma el MCU
        byte_ = uint8_t((byte_ << 1) | (nivel(*sda_, false) ? 1u : 0u));
        if (++bits_ < 8) return;
        const bool dato = nivel(*ad_, false) != 0;
        const uint8_t b = byte_;
        bits_ = 0;
        byte_ = 0;
        if (dato) parametro(b);
        else      orden(b);
    }

    // En el flanco de bajada el chip pone el siguiente bit de una lectura
    void baja() {
        if (!leyendo_) return;
        if (vacio_ > 0) { --vacio_; return; }    // el ciclo de reloj vacío
        int bit = siguiente_bit();
        if (bit < 0) { fin_lectura(); lectura_acabada_ = true; return; }
        sda_->set_drive(id_sda_, bit ? float(vddi_) : 0.0f, 50.0f);
    }

    void reinicia_interfaz() {
        bits_ = 0;
        byte_ = 0;
        fin_lectura();
        hay_flanco_ = false;
    }

    // =========================================================================
    // Las lecturas
    // =========================================================================
    void lee(std::vector<uint8_t> datos, bool vacio) {
        leyendo_ = true;
        lectura_acabada_ = false;
        lec_ = std::move(datos);
        lec_bit_ = 0;
        lec_ram_ = false;
        vacio_ = vacio ? 1 : 0;
    }
    int siguiente_bit() {
        if (lec_ram_ && lec_bit_ >= lec_.size() * 8u) {
            // RAMRD: el siguiente píxel, en 18 bits, un byte por color
            const uint32_t p = lee_pixel();
            lec_ = {uint8_t(((p >> 12) & 63u) << 2), uint8_t(((p >> 6) & 63u) << 2),
                    uint8_t((p & 63u) << 2)};
            lec_bit_ = 0;
        }
        if (lec_bit_ >= lec_.size() * 8u) return -1;
        const unsigned k = lec_bit_++;
        return (lec_[k / 8] >> (7u - k % 8)) & 1u;
    }
    void fin_lectura() {
        if (leyendo_ || lectura_acabada_) sda_->set_hiz(id_sda_);
        leyendo_ = false;
        lectura_acabada_ = false;
        lec_ram_ = false;
    }

    // =========================================================================
    // Las órdenes [§10]
    // =========================================================================
    void orden(uint8_t c) {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        ++n_ordenes_;
        // Lo que estuviera a medias se acaba con cualquier orden
        escribiendo_ = false;
        npix_ = 0;
        cmd_ = c;
        npar_ = 0;
        // Los 5 ms tras soltar RESET, el chip carga sus registros: no oye
        if (hay_reset_ && ahora - t_reset_alto_ < sc_core::sc_time(5, sc_core::SC_MS)) {
            ++n_perdidas_;
            cmd_ = 0x00;
            char b[200];
            std::snprintf(b, sizeof b,
                          "orden 0x%02X a los %.3f ms de soltar RESET: el ST7735S no atiende "
                          "hasta 5 ms despues [hoja, 9.17] y la orden se pierde",
                          c, (ahora - t_reset_alto_).to_seconds() * 1e3);
            avisa("orden_tras_reset", b);
            return;
        }
        if (c == 0x11 && hay_reset_ &&
            ahora - t_reset_alto_ < sc_core::sc_time(120, sc_core::SC_MS))
            avisa("slpout_tras_reset",
                  "SLPOUT antes de 120 ms tras el reset: la hoja no lo admite [9.17, nota 7]");
        if (hay_espera_ && ahora - t_espera_ < sc_core::sc_time(120, sc_core::SC_MS) &&
            c != 0x00) {
            char b[200];
            std::snprintf(b, sizeof b,
                          "orden 0x%02X a los %.1f ms de %s: la hoja pide esperar 120 ms "
                          "[10.1]; se aplica, pero no esta garantizado",
                          c, (ahora - t_espera_).to_seconds() * 1e3, espera_por_);
            avisa(std::string("espera_") + espera_por_, b);
        }
        switch (c) {
            case 0x00: break;                                    // NOP
            case 0x01: reset_sw(); marca_espera(ahora, "SWRESET"); break;
            case 0x04: lee({ID1, ID2, ID3}, true); break;        // RDDID
            case 0x09: {                                         // RDDST
                const uint8_t mad = madctl_ & 0xFC;
                lee({uint8_t((!sleep_ ? 0x80 : 0) | (mad >> 1)),
                     uint8_t(((colmod_ & 7u) << 4) |
                             (idle_ ? 0x08 : 0) | (parcial_ ? 0x04 : 0) |
                             (!sleep_ ? 0x02 : 0) | (!parcial_ ? 0x01 : 0)),
                     uint8_t((scroll_ ? 0x80 : 0) | (inv_ ? 0x20 : 0) |
                             (disp_ ? 0x04 : 0) | (te_ ? 0x02 : 0)),
                     uint8_t((gamma_idx() << 6) | (tem_ ? 0x20 : 0))},
                    true);
                break;
            }
            case 0x0A:                                           // RDDPM
                lee({uint8_t((!sleep_ ? 0x80 : 0) | (idle_ ? 0x40 : 0) |
                             (parcial_ ? 0x20 : 0) | (!sleep_ ? 0x10 : 0) |
                             (!parcial_ ? 0x08 : 0) | (disp_ ? 0x04 : 0))}, false);
                break;
            case 0x0B: lee({uint8_t(madctl_ & 0xFC)}, false); break;          // RDDMADCTL
            case 0x0C: lee({uint8_t(colmod_ & 0x07)}, false); break;          // RDDCOLMOD
            case 0x0D: lee({uint8_t((inv_ ? 0x20 : 0) | gamma_idx())}, false); break;
            case 0x0E: lee({uint8_t((te_ ? 0x80 : 0) | (tem_ ? 0x40 : 0))}, false); break;
            case 0x0F: lee({0x00}, false); break;                             // RDDSDR
            case 0x10:                                                        // SLPIN
                if (!sleep_) { sleep_ = true; ++version_; }
                marca_espera(ahora, "SLPIN");
                break;
            case 0x11:                                                        // SLPOUT
                if (sleep_) { sleep_ = false; ++version_; }
                marca_espera(ahora, "SLPOUT");
                break;
            case 0x12: parcial_ = true;  scroll_ = false; ++version_; break; // PTLON
            case 0x13: parcial_ = false; scroll_ = false; ++version_; break; // NORON
            case 0x20: inv_ = false; ++version_; break;                      // INVOFF
            case 0x21: inv_ = true;  ++version_; break;                      // INVON
            case 0x26: break;                                                 // GAMSET
            case 0x28: disp_ = false; ++version_; break;                     // DISPOFF
            case 0x29: disp_ = true;  ++version_; break;                     // DISPON
            case 0x2A: case 0x2B: break;                                     // CASET, RASET
            case 0x2C:                                                        // RAMWR
                escribiendo_ = true;
                col_ = xs_;
                fila_ = ys_;
                break;
            case 0x2D: break;                                                 // RGBSET
            case 0x2E:                                                        // RAMRD
                col_ = xs_;
                fila_ = ys_;
                lee({}, true);
                lec_ram_ = true;
                break;
            case 0x30: case 0x33: break;                                      // PTLAR, SCRLAR
            case 0x34: te_ = false; break;                                    // TEOFF
            case 0x35: te_ = true; tem_ = false; break;                       // TEON
            case 0x36: case 0x37: case 0x3A: break;                           // MADCTL, VSCSAD, COLMOD
            case 0x38: idle_ = false; ++version_; break;                     // IDMOFF
            case 0x39: idle_ = true;  ++version_; break;                     // IDMON
            case 0xDA: lee({ID1}, false); break;                              // RDID1
            case 0xDB: lee({ID2}, false); break;                              // RDID2
            case 0xDC: lee({ID3}, false); break;                              // RDID3
            default:
                // Las del panel -marco, potencia, VCOM, gamma, NVM-: se aceptan
                // con lo que traigan y no cambian nada de lo que se ve
                if (c >= 0xB0) break;
                {
                    char b[120];
                    std::snprintf(b, sizeof b,
                                  "orden 0x%02X: el ST7735S no la tiene; se ignora, con sus "
                                  "parametros", c);
                    avisa("orden_" + std::to_string(c), b);
                }
                cmd_ = 0xFF;
                break;
        }
    }

    void parametro(uint8_t d) {
        if (leyendo_ || lectura_acabada_) return;
        const unsigned k = npar_++;
        switch (cmd_) {
            case 0x2C: dato_ram(d); break;
            case 0x2A: case 0x2B: {                       // CASET, RASET
                if (k < 4) par_[k] = d;
                unsigned& s = cmd_ == 0x2A ? xs_ : ys_;
                unsigned& e = cmd_ == 0x2A ? xe_ : ye_;
                if (k == 1) s = (unsigned(par_[0]) << 8) | par_[1];
                if (k == 3) e = (unsigned(par_[2]) << 8) | par_[3];
                break;
            }
            case 0x2D:                                    // RGBSET
                if (k < 128) { lut_[k] = d & 63u; lut_escrita_ = true; }
                break;
            case 0x30:                                    // PTLAR
                if (k < 4) par_[k] = d;
                if (k == 1) { psl_ = (unsigned(par_[0]) << 8) | par_[1]; ++version_; }
                if (k == 3) { pel_ = (unsigned(par_[2]) << 8) | par_[3]; ++version_; }
                break;
            case 0x33:                                    // SCRLAR
                if (k < 6) par_[k] = d;
                if (k == 1) tfa_ = (unsigned(par_[0]) << 8) | par_[1];
                if (k == 3) vsa_ = (unsigned(par_[2]) << 8) | par_[3];
                if (k == 5) bfa_ = (unsigned(par_[4]) << 8) | par_[5];
                ++version_;
                break;
            case 0x37:                                    // VSCSAD: arranca el desplazamiento
                if (k < 2) par_[k] = d;
                if (k == 1) {
                    ssa_ = (unsigned(par_[0]) << 8) | par_[1];
                    scroll_ = true;
                    if (tfa_ + vsa_ + bfa_ != h_) {
                        char b[160];
                        std::snprintf(b, sizeof b,
                                      "desplazamiento con TFA + VSA + BFA = %u, y tiene que ser "
                                      "%u: la hoja dice que la imagen no esta definida [10.1.26]",
                                      tfa_ + vsa_ + bfa_, h_);
                        avisa("scrlar", b);
                    }
                    ++version_;
                }
                break;
            case 0x35: if (k == 0) tem_ = (d & 1u) != 0; break;   // TEON
            case 0x36:                                    // MADCTL
                if (k == 0) { madctl_ = d & 0xFC; ++version_; }
                break;
            case 0x3A:                                    // COLMOD
                if (k == 0) {
                    const uint8_t f = d & 7u;
                    if (f == 3 || f == 5 || f == 6) colmod_ = f;
                    else {
                        char b[120];
                        std::snprintf(b, sizeof b,
                                      "COLMOD 0x%02X: el ST7735S solo tiene 12, 16 y 18 bits por "
                                      "pixel (3, 5 y 6); se queda el de antes", d);
                        avisa("colmod", b);
                    }
                }
                break;
            case 0x26: if (k == 0) gamma_ = d; break;    // GAMSET
            default: break;                              // lo que no guarda nada
        }
    }

    // Los datos de RAMWR, con el formato de COLMOD [§9.8.20-22]
    void dato_ram(uint8_t d) {
        if (!escribiendo_) return;
        pix_[npix_++] = d;
        if (colmod_ == 5) {                          // 16 bits: RRRRRGGG GGGBBBBB
            if (npix_ < 2) return;
            const unsigned r = pix_[0] >> 3, g = ((pix_[0] & 7u) << 3) | (pix_[1] >> 5),
                           b = pix_[1] & 31u;
            escribe_pixel(color65k(r, g, b));
            npix_ = 0;
        } else if (colmod_ == 3) {                   // 12 bits: dos píxeles en tres bytes
            if (npix_ == 2) {
                // El primero ya está entero: R1G1 B1·
                escribe_pixel(color4k(pix_[0] >> 4, pix_[0] & 15u, pix_[1] >> 4));
            } else if (npix_ == 3) {
                escribe_pixel(color4k(pix_[1] & 15u, pix_[2] >> 4, pix_[2] & 15u));
                npix_ = 0;
            }
        } else {                                     // 18 bits: un byte por color
            if (npix_ < 3) return;
            escribe_pixel((uint32_t(pix_[0] >> 2) << 12) | (uint32_t(pix_[1] >> 2) << 6) |
                          uint32_t(pix_[2] >> 2));
            npix_ = 0;
        }
    }

    uint32_t color65k(unsigned r, unsigned g, unsigned b) const {
        if (lut_escrita_)
            return (uint32_t(lut_[r]) << 12) | (uint32_t(lut_[32 + g]) << 6) | lut_[96 + b];
        return (uint32_t((r << 1) | (r >> 4)) << 12) | (uint32_t(g) << 6) | ((b << 1) | (b >> 4));
    }
    uint32_t color4k(unsigned r, unsigned g, unsigned b) const {
        if (lut_escrita_)
            return (uint32_t(lut_[r]) << 12) | (uint32_t(lut_[32 + g]) << 6) | lut_[96 + b];
        auto e = [](unsigned c) { return (c << 2) | (c >> 2); };
        return (uint32_t(e(r)) << 12) | (uint32_t(e(g)) << 6) | e(b);
    }

    // Del contador de columna y fila al sitio de la memoria, con MV, MX y MY
    // [§9.11.1]. Fuera de la memoria, nada: «los datos fuera de rango se
    // ignoran» [10.1.20].
    bool fisica(unsigned& pc, unsigned& pr) const {
        const bool mv = madctl_ & 0x20, mx = madctl_ & 0x40, my = madctl_ & 0x80;
        const unsigned maxc = mv ? h_ : w_, maxr = mv ? w_ : h_;
        if (col_ >= maxc || fila_ >= maxr) return false;
        if (!mv) { pc = mx ? w_ - 1 - col_ : col_;  pr = my ? h_ - 1 - fila_ : fila_; }
        else     { pr = my ? h_ - 1 - col_ : col_;  pc = mx ? w_ - 1 - fila_ : fila_; }
        return true;
    }
    void avanza() {
        if (++col_ > xe_) {
            col_ = xs_;
            if (++fila_ > ye_) fila_ = ys_;
        }
    }
    void escribe_pixel(uint32_t p) {
        unsigned pc, pr;
        if (fisica(pc, pr)) {
            mem_[std::size_t(pr) * w_ + pc] = p;
            ++version_;
        }
        ++n_pixeles_;
        avanza();
    }
    uint32_t lee_pixel() {
        unsigned pc, pr;
        const uint32_t p = fisica(pc, pr) ? mem_[std::size_t(pr) * w_ + pc] : 0u;
        avanza();
        return p;
    }

    // =========================================================================
    // Los resets [§9.15]
    // =========================================================================
    void reset_comun() {
        sleep_ = true;
        disp_ = false;
        parcial_ = false;
        inv_ = false;
        idle_ = false;
        scroll_ = false;
        te_ = false;
        tem_ = false;
        gamma_ = 1;
        xs_ = 0;
        ys_ = 0;
        const bool mv = madctl_ & 0x20;
        xe_ = (mv ? h_ : w_) - 1;
        ye_ = (mv ? w_ : h_) - 1;
        psl_ = 0;
        pel_ = h_ - 1;
        tfa_ = 0;
        vsa_ = h_;
        bfa_ = 0;
        ssa_ = 0;
        escribiendo_ = false;
        npix_ = 0;
        cmd_ = 0;
        npar_ = 0;
        hay_espera_ = false;
        ++version_;
    }
    void reset_hw() {
        madctl_ = 0;
        colmod_ = 6;
        lut_escrita_ = false;
        reset_comun();
    }
    void reset_sw() { reset_comun(); }    // MADCTL, COLMOD y la tabla se quedan

    void marca_espera(const sc_core::sc_time& t, const char* por) {
        hay_espera_ = true;
        t_espera_ = t;
        espera_por_ = por;
    }
    unsigned gamma_idx() const {
        return gamma_ == 2 ? 1u : gamma_ == 4 ? 2u : gamma_ == 8 ? 3u : 0u;
    }

    // =========================================================================
    // Lo que se ve
    // =========================================================================
    void pixel_visible(unsigned x, unsigned y, uint8_t* d) const {
        if (!mostrando()) { d[0] = d[1] = d[2] = 255; return; }   // blanco
        // La línea de memoria que va en la línea `y` del panel
        unsigned ly = y + offy_;
        if (parcial_) {
            const bool dentro = psl_ <= pel_ ? (ly >= psl_ && ly <= pel_)
                                             : (ly >= psl_ || ly <= pel_);
            if (!dentro) { d[0] = d[1] = d[2] = 255; return; }
        }
        unsigned r = ly;
        if (scroll_ && vsa_ > 0) {
            // Con ML = 1 el área fija de arriba está abajo: se cuenta desde el
            // otro extremo [§9.11.6, ejemplo 2]
            const bool ml = madctl_ & 0x10;
            const int l = ml ? int(h_) - 1 - int(ly) : int(ly);
            const int ssa = ml ? int(h_) - 1 - int(ssa_) : int(ssa_);
            int m = l;
            if (l >= int(tfa_) && l < int(tfa_ + vsa_)) {
                const int v = int(vsa_);
                m = int(tfa_) + (((l - int(tfa_)) + (ssa - int(tfa_))) % v + v) % v;
            }
            r = unsigned(ml ? int(h_) - 1 - m : m);
            if (r >= h_) r = h_ - 1;
        }
        const uint32_t p = mem_[std::size_t(r) * w_ + (x + offx_)];
        unsigned c[3] = {(p >> 12) & 63u, (p >> 6) & 63u, p & 63u};
        if (((madctl_ & 0x08) != 0) != panel_bgr_) std::swap(c[0], c[2]);
        for (unsigned& v : c) {
            if (idle_) v = (v & 32u) ? 63u : 0u;
            if (inv_)  v = 63u - v;
            v = (v << 2) | (v >> 4);
        }
        d[0] = uint8_t(c[0]);
        d[1] = uint8_t(c[1]);
        d[2] = uint8_t(c[2]);
    }

    // =========================================================================
    // Los avisos: uno por cosa
    // =========================================================================
    void avisa(const std::string& que, const std::string& texto) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("tft", (pieza() + ": " + texto).c_str());
    }
    void sobretension(const analog_net_if& n, double v) const {
        char b[200];
        std::snprintf(b, sizeof b,
                      "%.2f V en una entrada de un chip de %.1f V (maximo VDDI + 0,3 V) "
                      "[hoja, 7.1]: este modulo no lleva adaptadores de nivel", v, vddi_);
        const_cast<Tft128x160*>(this)->avisa("sobretension_" + nombre_nodo(n), b);
    }

    static constexpr uint8_t ID1 = 0x7C, ID2 = 0x89, ID3 = 0xF0;

    analog_net_if *vcc_, *gnd_, *cs_, *reset_, *ad_, *sda_, *sck_, *led_;
    const unsigned w_, h_, offx_, offy_;
    const bool   panel_bgr_;
    const double vf_luz_, r_luz_;
    std::unique_ptr<RamaDosNodos> alim_, luz_;
    int id_sda_ = -1;

    // Lo eléctrico
    double vddi_ = 0.0, i_luz_ = 0.0, brillo_ = 0.0, luz_acum_ = 0.0;
    sc_core::sc_time t_luz_ = sc_core::SC_ZERO_TIME;
    bool   alimentada_ = false;

    // La memoria, 6-6-6 bits por píxel, por filas
    std::vector<uint32_t> mem_;
    uint64_t version_ = 1;

    // Los registros
    bool sleep_ = true, disp_ = false, parcial_ = false, inv_ = false, idle_ = false,
         scroll_ = false, te_ = false, tem_ = false;
    uint8_t madctl_ = 0, colmod_ = 6, gamma_ = 1;
    unsigned xs_ = 0, xe_ = 127, ys_ = 0, ye_ = 159;
    unsigned psl_ = 0, pel_ = 159, tfa_ = 0, vsa_ = 160, bfa_ = 0, ssa_ = 0;
    uint8_t lut_[128] = {};
    bool    lut_escrita_ = false;

    // La interfaz
    bool reset_bajo_ = false, en_reset_ = false, cs_alto_ = true, sck_alto_ = false;
    sc_core::sc_time t_reset_bajo_, t_reset_alto_, t_flanco_, t_espera_;
    bool hay_reset_ = false, hay_flanco_ = false, hay_espera_ = false;
    const char* espera_por_ = "";
    unsigned bits_ = 0;
    uint8_t  byte_ = 0;
    uint8_t  cmd_ = 0;
    unsigned npar_ = 0;
    uint8_t  par_[6] = {};
    bool     escribiendo_ = false;
    unsigned col_ = 0, fila_ = 0;
    uint8_t  pix_[3] = {};
    unsigned npix_ = 0;
    // Una lectura en curso
    bool leyendo_ = false, lectura_acabada_ = false, lec_ram_ = false;
    std::vector<uint8_t> lec_;
    unsigned lec_bit_ = 0;
    unsigned vacio_ = 0;

    uint64_t n_ordenes_ = 0, n_pixeles_ = 0, n_perdidas_ = 0;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_TFT_ST7735_H
