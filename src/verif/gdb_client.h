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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace stm32 {

class GdbClient {
public:
    ~GdbClient() { desconectar(); }

    bool conectar(unsigned puerto) {
        desconectar();
        s_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s_ < 0) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(uint16_t(puerto));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(s_, (sockaddr*)&a, sizeof a) < 0) { desconectar(); return false; }
        ::fcntl(s_, F_SETFL, O_NONBLOCK);
        int uno = 1;
        ::setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno);
        rx_.clear();
        return true;
    }
    void desconectar() { if (s_ >= 0) { ::close(s_); s_ = -1; } }
    bool conectado() const { return s_ >= 0; }

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
        if (s_ < 0) return;
        size_t k = 0;
        while (k < d.size()) {
            const ssize_t r = ::send(s_, d.data() + k, d.size() - k, MSG_NOSIGNAL);
            if (r > 0) { k += size_t(r); continue; }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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
        if (s_ < 0) return;
        char b[4096];
        for (;;) {
            const ssize_t r = ::recv(s_, b, sizeof b, 0);
            if (r > 0) { rx_.append(b, size_t(r)); continue; }
            if (r == 0) { desconectar(); return; }
            return;                                   // EAGAIN
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

    int s_ = -1;
    std::string rx_;
    bool visto_ = false;
};

} // namespace stm32
#endif // STM32_VERIF_GDB_CLIENT_H
