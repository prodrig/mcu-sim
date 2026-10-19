// =============================================================================
// sonda_red.h — Lo que tienen en común las sondas que escuchan en un puerto
//
// Una sonda de depuración que atiende a un programa del ordenador por TCP
// -OpenOCD por `remote_bitbang` (sonda_openocd.h) o por CMSIS-DAP
// (sonda_cmsis_dap.h)- se abre al arrancar la simulación, no al construirla,
// y puede imprimir lo que hace. Es todo lo que el simulador necesita saber de
// ella para montarla, sea en los pines del chip o en una pieza `Xds110`.
// =============================================================================
#ifndef STM32_VERIF_SONDA_RED_H
#define STM32_VERIF_SONDA_RED_H

namespace stm32 {

class SondaRed {
public:
    virtual ~SondaRed() = default;
    // Abre el puerto y empieza a atender (o deja de hacerlo)
    virtual void set_enabled(bool on) = 0;
    // Imprime cada conexion y lo que no entiende
    virtual void set_verbose(bool v) = 0;
    // ¿Hay un programa conectado?
    virtual bool conectado() const = 0;
    // El puerto TCP en el que escucha
    virtual unsigned puerto() const = 0;
    // "remote_bitbang" o "CMSIS-DAP", para los mensajes
    virtual const char* protocolo() const = 0;
};

} // namespace stm32
#endif // STM32_VERIF_SONDA_RED_H
