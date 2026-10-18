/* arch/cc.h — lo que lwIP pide del compilador y de la plataforma (e4_eth)
 * Cortex-M4, little-endian, GCC, sin biblioteca de C: ni printf ni assert. */
#ifndef E4_ETH_ARCH_CC_H
#define E4_ETH_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>

#define LWIP_NO_STDINT_H      0
#define LWIP_NO_INTTYPES_H    1
#define LWIP_NO_LIMITS_H      1
#define LWIP_NO_CTYPE_H       1

#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "u"

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { e4_eth_fallo(__LINE__); } while (0)
#define LWIP_RAND()             e4_eth_azar()
#define INT_MAX 2147483647
#define SSIZE_MAX INT_MAX

void e4_eth_fallo(int linea);
uint32_t e4_eth_azar(void);

#endif
