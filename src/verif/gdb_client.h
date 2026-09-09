// =============================================================================
// gdb_client.h — Un GDB de mentira, para el banco de pruebas
//
// Habla el Remote Serial Protocol por un socket TCP contra el stub, igual que
// haría `arm-none-eabi-gdb` o el plugin de STM32CubeIDE. No es un modelo de
// nada del MCU: es la herramienta del otro lado del cable, y está aquí para
// poder comprobar el stub de punta a punta sin depender de que haya un GDB
// instalado.
//
// La única peculiaridad es que tiene que CEDER EL PASO: el stub es un proceso
// de SystemC y solo avanza cuando avanza el tiempo simulado, así que entre
// mandar y recibir hay que esperar.
// =============================================================================
#ifndef STM32_VERIF_GDB_CLIENT_H
#define STM32_VERIF_GDB_CLIENT_H

#include <systemc>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "../common/red.h"     // la unica dependencia del sistema operativo

namespace stm32 {

class GdbClient {
public:
    ~GdbClient() { desconectar(); }

    bool conectar(unsigned puerto) {
        desconectar();
        s_ = red::conecta_local(puerto);
        if (!red::valido(s_)) return false;
        rx_.clear();
        return true;
    }
    void desconectar() { red::cerrar(s_); }
    bool conectado() const { return red::valido(s_); }

    // Manda un paquete y espera la respuesta. `limite` es tiempo SIMULADO: el
    // stub solo corre cuando corre la simulacion.
    std::string pedir(const std::string& cuerpo,
                      sc_core::sc_time limite = sc_core::sc_time(20, sc_core::SC_MS)) {
        enviar(cuerpo);
        return recibir(limite);
    }
    void enviar(const std::string& cuerpo) {
        std::string p = "$" + cuerpo + "#";
        const uint8_t c = suma(cuerpo);
        p.push_back(hexd(c >> 4));
        p.push_back(hexd(c & 0xF));
        crudo(p);
    }
    void crudo(const std::string& d) {
        if (!red::valido(s_)) return;
        size_t k = 0;
        while (k < d.size()) {
            const long r = red::enviar(s_, d.data() + k, d.size() - k);
            if (r > 0) { k += size_t(r); continue; }
            if (r < 0 && red::reintentar()) {
                sc_core::wait(sc_core::sc_time(100, sc_core::SC_US));
                continue;
            }
            return;
        }
    }
    // Espera un paquete completo, cediendo el paso a la simulacion.
    std::string recibir(sc_core::sc_time limite = sc_core::sc_time(20, sc_core::SC_MS)) {
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        for (;;) {
            const std::string p = extraer();
            if (!p.empty() || visto_) { visto_ = false; return p; }
            if (sc_core::sc_time_stamp() - t0 > limite) return std::string();
            sc_core::wait(sc_core::sc_time(200, sc_core::SC_US));
            sorber();
        }
    }
    // Vacia lo que haya llegado sin esperar nada.
    void purgar() { sorber(); rx_.clear(); }

private:
    static uint8_t suma(const std::string& s) {
        unsigned c = 0;
        for (char ch : s) c += uint8_t(ch);
        return uint8_t(c);
    }
    static char hexd(unsigned v) { return char(v < 10 ? '0' + v : 'a' + v - 10); }

    void sorber() {
        if (!red::valido(s_)) return;
        char b[4096];
        for (;;) {
            const long r = red::recibir(s_, b, sizeof b);
            if (r > 0) { rx_.append(b, size_t(r)); continue; }
            if (r == 0) { desconectar(); return; }
            return;                                   // no hay nada mas ahora
        }
    }
    std::string extraer() {
        for (;;) {
            const size_t i = rx_.find('$');
            if (i == std::string::npos) { rx_.clear(); return std::string(); }
            if (i) rx_.erase(0, i);
            const size_t f = rx_.find('#');
            if (f == std::string::npos || rx_.size() < f + 3) return std::string();
            const std::string cuerpo = rx_.substr(1, f - 1);
            rx_.erase(0, f + 3);
            crudo("+");                               // acuse, como hace GDB
            visto_ = true;
            return cuerpo;
        }
    }

    red::socket_t s_ = red::invalido();
    std::string rx_;
    bool visto_ = false;
};

} // namespace stm32
#endif // STM32_VERIF_GDB_CLIENT_H
