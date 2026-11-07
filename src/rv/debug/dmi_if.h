// =============================================================================
// dmi_if.h — La interfaz del Debug Module: leer y escribir sus registros
//
// E2 de doc/analisis_esp32_riscv.md, §3.6. El DM de [Debug Spec 0.13.2] se
// ve desde fuera como un puñado de registros de 32 bits en un espacio de
// direcciones de `abits` bits (el DMI). Hay tres maneras de llegar a él, y
// las tres acaban en esta interfaz:
//
//   * una llamada de función (G1, el stub interno);
//   * el DTM JTAG por los pines (G2' con el stub de pines propio, y G2 con
//     OpenOCD por remote_bitbang o CMSIS-DAP);
//   * el USB-Serial-JTAG de Espressif (E7).
//
// `false` en una operación quiere decir que no ha llegado (un DTM ocupado que
// no se recupera, un pin suelto): no es un error del DM, que los dice en sus
// propios registros (cmderr, sberror).
// =============================================================================
#ifndef STM32_RV_DEBUG_DMI_IF_H
#define STM32_RV_DEBUG_DMI_IF_H

#include <cstdint>

namespace stm32 {

class dmi_if {
public:
    virtual ~dmi_if() = default;
    virtual bool dmi_leer(uint32_t dir, uint32_t& v) = 0;
    virtual bool dmi_escribir(uint32_t dir, uint32_t v) = 0;
};

// Los registros del DM [Debug Spec 0.13.2, tabla 3.8]
namespace dm {
enum : uint32_t {
    DATA0 = 0x04, DMCONTROL = 0x10, DMSTATUS = 0x11, HARTINFO = 0x12, HALTSUM1 = 0x13,
    HAWINDOWSEL = 0x14, HAWINDOW = 0x15, ABSTRACTCS = 0x16, COMMAND = 0x17,
    ABSTRACTAUTO = 0x18, CONFSTRPTR0 = 0x19, NEXTDM = 0x1D, PROGBUF0 = 0x20,
    AUTHDATA = 0x30, HALTSUM2 = 0x34, HALTSUM3 = 0x35, SBADDRESS3 = 0x37, SBCS = 0x38,
    SBADDRESS0 = 0x39, SBADDRESS1 = 0x3A, SBADDRESS2 = 0x3B, SBDATA0 = 0x3C,
    SBDATA1 = 0x3D, SBDATA2 = 0x3E, SBDATA3 = 0x3F, HALTSUM0 = 0x40,
};
// dmcontrol
enum : uint32_t {
    HALTREQ = 1u << 31, RESUMEREQ = 1u << 30, HARTRESET = 1u << 29, ACKHAVERESET = 1u << 28,
    HASEL = 1u << 26, SETRESETHALTREQ = 1u << 3, CLRRESETHALTREQ = 1u << 2,
    NDMRESET = 1u << 1, DMACTIVE = 1u << 0,
};
// dmstatus
enum : uint32_t {
    IMPEBREAK = 1u << 22, ALLHAVERESET = 1u << 19, ANYHAVERESET = 1u << 18,
    ALLRESUMEACK = 1u << 17, ANYRESUMEACK = 1u << 16, ALLNONEXISTENT = 1u << 15,
    ANYNONEXISTENT = 1u << 14, ALLUNAVAIL = 1u << 13, ANYUNAVAIL = 1u << 12,
    ALLRUNNING = 1u << 11, ANYRUNNING = 1u << 10, ALLHALTED = 1u << 9, ANYHALTED = 1u << 8,
    AUTHENTICATED = 1u << 7, HASRESETHALTREQ = 1u << 5,
};
// abstractcs y command
enum : uint32_t {
    AC_BUSY = 1u << 12,
    CMDERR_NINGUNO = 0, CMDERR_OCUPADO = 1, CMDERR_NO_SOPORTADO = 2, CMDERR_EXCEPCION = 3,
    CMDERR_PARADA = 4, CMDERR_BUS = 5, CMDERR_OTRO = 7,
    AR_TRANSFER = 1u << 17, AR_WRITE = 1u << 16, AR_POSTEXEC = 1u << 18,
    AR_POSTINC = 1u << 19, AR_SIZE32 = 2u << 20,
};
// sbcs
enum : uint32_t {
    SB_BUSYERROR = 1u << 22, SB_BUSY = 1u << 21, SB_READONADDR = 1u << 20,
    SB_AUTOINC = 1u << 16, SB_READONDATA = 1u << 15,
};
} // namespace dm

} // namespace stm32
#endif // STM32_RV_DEBUG_DMI_IF_H
