// =============================================================================
// canal_pty.h — Un CanalHost que es un pseudoterminal (M11 de
// doc/analisis_msp430.md)
//
// Para la eZ-FET emulada: `libmsp430` abre un puerto serie del sistema con
// boost::asio, y un pty le vale si la lista de puertos lo incluye (el parche
// de herramientas/libmsp430/, que lo toma de MSP430_PUERTO_CDC). El canal
// crea el pty, enlaza su esclavo en la ruta pedida (p. ej. /tmp/ttyEZFET) y
// pasa los bytes del maestro al CDC y al revés.
//
// «Conectado» es que haya alguien con el esclavo abierto: sin nadie, leer el
// maestro da EIO (Linux) y lo que manda el dispositivo se descarta y se cuenta,
// como en el canal TCP sin cliente. Al cerrar y abrir el puerto, el puente USB
// mueve DTR y RTS, igual que con un terminal por TCP.
//
// Solo POSIX: en Windows no hay pty y el canal dice por qué no se abre.
// =============================================================================
#ifndef STM32_PARTS_CANAL_PTY_H
#define STM32_PARTS_CANAL_PTY_H

#include <string>
#include "canal_host.h"

#if !defined(_WIN32)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace stm32 {

class CanalPty : public CanalHost {
public:
    static constexpr std::size_t MAX_SALIDA = 64 * 1024;

    explicit CanalPty(const std::string& enlace, std::size_t cola = 4096) : enlace_(enlace), max_(cola) {
#if defined(_WIN32)
        error_ = "un pty solo existe en Linux y macOS";
#else
        fd_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (fd_ < 0 || ::grantpt(fd_) != 0 || ::unlockpt(fd_) != 0) { falla("posix_openpt"); return; }
        const char* n = ::ptsname(fd_);
        if (!n) { falla("ptsname"); return; }
        esclavo_ = n;
        // El esclavo en crudo desde el principio: quien lo abra lo pondrá a su
        // gusto, pero hasta entonces que no haga eco ni traduzca nada
        const int s = ::open(n, O_RDWR | O_NOCTTY);
        if (s >= 0) {
            termios t{};
            if (::tcgetattr(s, &t) == 0) { ::cfmakeraw(&t); ::tcsetattr(s, TCSANOW, &t); }
            ::close(s);
        }
        ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);
        if (!enlace_.empty()) {
            ::unlink(enlace_.c_str());
            if (::symlink(n, enlace_.c_str()) != 0) { falla(("el enlace " + enlace_).c_str()); return; }
            enlazado_ = true;
        }
#endif
    }
    ~CanalPty() override {
#if !defined(_WIN32)
        if (enlazado_) ::unlink(enlace_.c_str());
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    bool abierto() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    const std::string& esclavo() const { return esclavo_; }
    const std::string& enlace() const { return enlace_; }

    std::size_t leer(uint8_t* b, std::size_t n) override {
        std::size_t k = 0;
        while (k < n && !entrada_.empty()) { b[k++] = entrada_.front(); entrada_.pop_front(); }
        return k;
    }
    void escribir(const uint8_t* b, std::size_t n) override {
        if (!conectado_) { n_descartados_ += n; return; }
        if (salida_.size() + n > MAX_SALIDA) { n_descartados_ += n; return; }
        salida_.insert(salida_.end(), b, b + n);
        manda();
    }
    bool conectado() const override { return conectado_; }
    std::string describir() const override {
        return "el pty " + (enlace_.empty() ? esclavo_ : enlace_ + " (" + esclavo_ + ")");
    }
    void sondear() override {
#if !defined(_WIN32)
        if (fd_ < 0) return;
        uint8_t b[512];
        for (;;) {
            if (entrada_.size() >= max_) break;                  // el resto espera en el núcleo
            const ssize_t r = ::read(fd_, b, sizeof b);
            if (r > 0) { conectado_ = true; entrada_.insert(entrada_.end(), b, b + r); continue; }
            if (r < 0 && errno == EIO) { conectado_ = false; salida_.clear(); }   // nadie al otro lado
            else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) conectado_ = true;
            break;
        }
        manda();
#endif
    }
    std::size_t pendientes() const override { return entrada_.size(); }

private:
    void falla(const char* que) {
#if !defined(_WIN32)
        error_ = std::string(que) + ": " + std::strerror(errno);
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#else
        error_ = que;
#endif
    }
    void manda() {
#if !defined(_WIN32)
        while (fd_ >= 0 && !salida_.empty()) {
            const ssize_t w = ::write(fd_, salida_.data(), salida_.size());
            if (w <= 0) break;
            salida_.erase(salida_.begin(), salida_.begin() + w);
        }
#endif
    }
    std::string enlace_, esclavo_, error_;
    std::size_t max_;
    int fd_ = -1;
    bool enlazado_ = false, conectado_ = false;
    std::deque<uint8_t> entrada_;
    std::vector<uint8_t> salida_;
};

} // namespace stm32
#endif
