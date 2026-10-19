#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# comprueba_vectores.py — vuelve a calcular TODOS los vectores de `cryp.vec`,
# `hash.vec`, y los del MSP432E4 (`cryp2.vec`, `sha2.vec` y `crc.vec`), con DOS
# implementaciones independientes, y los compara con lo que dice el fichero.
#
# QUÉ PROBLEMA RESUELVE. Los vectores son la única red de seguridad que tiene
# un acelerador criptográfico modelado: si están mal, un modelo roto pasa las
# pruebas y el alumno se encuentra con que su PC no sabe descifrar lo que su
# placa cifró. Copiar los números de un documento a mano es exactamente la
# clase de paso que introduce erratas invisibles, y de hecho **este proyecto ya
# pilló una**: la lectura automática del apéndice F del SP 800-38A devolvió mal
# el contador inicial de CTR y seis de los doce bloques de CTR-AES192/256. Se
# vio porque las dos implementaciones decían otra cosa. [fase 0, punto 7]
#
# NO TIENE NINGÚN NÚMERO DENTRO. Los números están en los `.vec`, con su
# procedencia; este fichero solo sabe recalcular y comparar.
#
# MOTORES
#   AES, DES y TDES : OpenSSL (por línea de órdenes) y pycryptodome
#   AES-GCM         : pycryptodome y `cryptography` (openssl enc no hace AEAD)
#   MD5, SHA-1, SHA-224, SHA-256 y sus HMAC: hashlib/hmac y `openssl dgst`
#   CRC             : crccheck y un CRC bit a bit de aquí (el CRC-32, además,
#                     con zlib); la suma de Internet, con dos sumas de aquí
# Un motor que no esté instalado se salta con aviso; si para un caso queda
# UNO SOLO, el caso se marca como no confirmado y el programa termina en 1.
# Un vector con una sola fuente es justo lo que este fichero existe para
# impedir.
#
# USO
#   python3 comprueba_vectores.py            # todo menos los casos lentos
#   python3 comprueba_vectores.py --lentos   # también el del millón de letras
#   python3 comprueba_vectores.py --detalle  # una línea por caso
# =============================================================================
import binascii
import hashlib
import hmac
import io
import os
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))

# --- Lectura de los ficheros -------------------------------------------------

def lee(nombre):
    """Devuelve la lista de casos; cada caso es un dict de campo -> valor."""
    ruta = os.path.join(AQUI, nombre)
    casos, actual = [], None
    for n, linea in enumerate(io.open(ruta, encoding="utf-8"), 1):
        linea = linea.rstrip("\n")
        if not linea.strip() or linea.lstrip().startswith("#"):
            continue
        if linea.strip() == "[caso]":
            actual = {"_fichero": nombre, "_linea": n}
            casos.append(actual)
            continue
        if actual is None or "=" not in linea:
            raise SystemExit("%s:%d: línea suelta fuera de un [caso]: %r"
                             % (nombre, n, linea))
        clave, valor = linea.split("=", 1)
        actual[clave.strip()] = valor.strip()
    return casos


def hx(s):
    return binascii.unhexlify(s) if s else b""


# --- Motores de cifrado ------------------------------------------------------

ALG_OSSL = {"aes-ecb": "aes-%d-ecb", "aes-cbc": "aes-%d-cbc", "aes-ctr": "aes-%d-ctr",
            "aes-cfb": "aes-%d-cfb", "des-ecb": "des-ecb", "des-cbc": "des-cbc",
            "des-cfb": "des-cfb", "tdes-ecb": "des-ede3", "tdes-cbc": "des-ede3-cbc",
            "tdes-cfb": "des-ede3-cfb"}


def openssl_cifra(alg, clave, iv, datos, descifra=False):
    nombre = ALG_OSSL[alg]
    if "%d" in nombre:
        nombre = nombre % (len(clave) * 4)        # la clave viene en hex
    orden = ["openssl", "enc", "-" + nombre, "-provider", "legacy",
             "-provider", "default", "-K", clave, "-nopad",
             "-d" if descifra else "-e"]
    if iv:
        orden += ["-iv", iv]
    p = subprocess.run(orden, input=hx(datos), capture_output=True)
    if p.returncode:
        return None
    return binascii.hexlify(p.stdout).decode()


def pycrypto_cifra(alg, clave, iv, datos, descifra=False):
    try:
        from Crypto.Cipher import AES, DES, DES3
    except ImportError:
        return None
    k, i, d = hx(clave), hx(iv), hx(datos)
    if alg.startswith("aes"):
        mod, modos = AES, {"aes-ecb": AES.MODE_ECB, "aes-cbc": AES.MODE_CBC,
                           "aes-ctr": AES.MODE_CTR, "aes-cfb": AES.MODE_CFB}
    elif alg.startswith("tdes"):
        mod, modos = DES3, {"tdes-ecb": DES3.MODE_ECB, "tdes-cbc": DES3.MODE_CBC,
                            "tdes-cfb": DES3.MODE_CFB}
    else:
        mod, modos = DES, {"des-ecb": DES.MODE_ECB, "des-cbc": DES.MODE_CBC,
                           "des-cfb": DES.MODE_CFB}
    modo = modos[alg]
    if alg.endswith("cfb"):
        # CFB del bloque entero (128 en AES, 64 en DES), que es el del E4
        c = mod.new(k, modo, i, segment_size=8 * len(i))
    elif alg == "aes-ctr":
        from Crypto.Util import Counter
        # El CTR del CRYP cuenta sobre los 128 bits enteros del vector inicial,
        # que es lo que dice el SP 800-38A y lo que hace `openssl aes-*-ctr`.
        c = mod.new(k, modo, counter=Counter.new(128,
                    initial_value=int.from_bytes(i, "big")))
    elif i:
        c = mod.new(k, modo, i)
    else:
        c = mod.new(k, modo)
    salida = c.decrypt(d) if descifra else c.encrypt(d)
    return binascii.hexlify(salida).decode()


# --- Motores de resumen ------------------------------------------------------

def py_resume(alg, clave, mensaje):
    base = alg[5:] if alg.startswith("hmac-") else alg
    if not alg.startswith("hmac-"):
        return hashlib.new(base, mensaje).hexdigest()
    return hmac.new(clave, mensaje, base).hexdigest()


def openssl_resume(alg, clave, mensaje):
    corto = alg[5:] if alg.startswith("hmac-") else alg
    orden = ["openssl", "dgst", "-" + corto]
    if alg.startswith("hmac"):
        orden += ["-mac", "HMAC", "-macopt",
                  "hexkey:" + binascii.hexlify(clave).decode()]
    p = subprocess.run(orden, input=mensaje, capture_output=True)
    if p.returncode:
        return None
    texto = p.stdout.decode().strip()
    return texto.split("=")[-1].strip() if "=" in texto else texto.split()[-1]


# --- Comprobación ------------------------------------------------------------

class Cuenta:
    def __init__(self):
        self.ok = self.fallo = self.saltado = self.flojo = 0
        self.motores = set()


def confronta(caso, esperado, resultados, cuenta, detalle, que):
    """`resultados` es una lista de (motor, valor|None)."""
    vivos = [(m, v) for m, v in resultados if v is not None]
    for m, _ in vivos:
        cuenta.motores.add(m)
    discrepan = [(m, v) for m, v in vivos if v != esperado]
    etiqueta = "%s/%s" % (caso["id"], que)
    if not vivos:
        print("  [SIN MOTOR] %-28s no se ha podido recalcular" % etiqueta)
        cuenta.fallo += 1
        return
    if discrepan:
        print("  [FALLO   ] %-28s" % etiqueta)
        print("             esperado  %s" % esperado)
        for m, v in discrepan:
            print("             %-9s %s" % (m, v))
        cuenta.fallo += 1
        return
    if len(vivos) < 2:
        print("  [UN MOTOR] %-28s solo lo confirma %s; hace falta un segundo"
              % (etiqueta, vivos[0][0]))
        cuenta.flojo += 1
        return
    cuenta.ok += 1
    if detalle:
        print("  [ok      ] %-28s %s  (%s)"
              % (etiqueta, esperado[:32] + ("…" if len(esperado) > 32 else ""),
                 ", ".join(m for m, _ in vivos)))


def comprueba_cryp(casos, cuenta, detalle):
    for c in casos:
        alg, clave, iv = c["algoritmo"], c["clave"], c.get("iv", "")
        ent, sal = c["entrada"], c["salida"]
        if alg not in ALG_OSSL:
            raise SystemExit("%s: algoritmo desconocido %r" % (c["id"], alg))
        if len(ent) != len(sal):
            print("  [FALLO   ] %s: entrada y salida no miden lo mismo" % c["id"])
            cuenta.fallo += 1
            continue
        confronta(c, sal, [("openssl", openssl_cifra(alg, clave, iv, ent)),
                           ("pycrypto", pycrypto_cifra(alg, clave, iv, ent))],
                  cuenta, detalle, "cifrar")
        # Y la vuelta, que es la mitad que se olvida: en ECB y CBC descifrar
        # necesita preparar la clave, y en CTR es la misma operación.
        descifra = alg.endswith("ctr")
        confronta(c, ent, [("openssl", openssl_cifra(alg, clave, iv, sal, not descifra)),
                           ("pycrypto", pycrypto_cifra(alg, clave, iv, sal, not descifra))],
                  cuenta, detalle, "descifrar")


# --- GCM ---------------------------------------------------------------------

def pycrypto_gcm(clave, iv, aad, datos):
    try:
        from Crypto.Cipher import AES
    except ImportError:
        return None
    c = AES.new(hx(clave), AES.MODE_GCM, nonce=hx(iv))
    c.update(hx(aad))
    ct, tag = c.encrypt_and_digest(hx(datos))
    return binascii.hexlify(ct + tag).decode()


def cryptography_gcm(clave, iv, aad, datos):
    try:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    except ImportError:
        return None
    r = AESGCM(hx(clave)).encrypt(hx(iv), hx(datos), hx(aad) or None)
    return binascii.hexlify(r).decode()


def comprueba_cryp2(casos, cuenta, detalle):
    gcm = [c for c in casos if c["algoritmo"] == "aes-gcm"]
    comprueba_cryp([c for c in casos if c["algoritmo"] != "aes-gcm"], cuenta, detalle)
    for c in gcm:
        esperado = c["salida"] + c["tag"]
        args = (c["clave"], c["iv"], c.get("aad", ""), c["entrada"])
        confronta(c, esperado, [("pycrypto", pycrypto_gcm(*args)),
                                ("cryptography", cryptography_gcm(*args))],
                  cuenta, detalle, "cifrar+tag")


# --- CRC ---------------------------------------------------------------------

# Rocksoft: ancho, polinomio, inicial, refleja la entrada, refleja la salida, xor
CRC_PARAM = {
    "crc32":        (32, 0x04C11DB7, 0xFFFFFFFF, True,  True,  0xFFFFFFFF),
    "crc32c":       (32, 0x1EDC6F41, 0xFFFFFFFF, True,  True,  0xFFFFFFFF),
    "crc16-ccitt":  (16, 0x1021,     0xFFFF,     False, False, 0),
    "crc16-xmodem": (16, 0x1021,     0,          False, False, 0),
    "crc16-arc":    (16, 0x8005,     0,          True,  True,  0),
    "crc16-umts":   (16, 0x8005,     0,          False, False, 0),
}


def crc_bit_a_bit(alg, datos):
    if alg == "inet":
        return None
    w, poli, ini, rin, rout, xo = CRC_PARAM[alg]
    msk, top = (1 << w) - 1, 1 << (w - 1)
    c = ini
    for b in datos:
        if rin:
            b = int("{:08b}".format(b)[::-1], 2)
        c ^= b << (w - 8)
        for _ in range(8):
            c = ((c << 1) ^ poli) & msk if c & top else (c << 1) & msk
    if rout:
        c = int("{:0{}b}".format(c, w)[::-1], 2)
    return "%0*x" % (w // 4, c ^ xo)


def crc_crccheck(alg, datos):
    try:
        from crccheck import crc
    except ImportError:
        return None
    clase = {"crc32": "Crc32", "crc32c": "Crc32c", "crc16-ccitt": "Crc16CcittFalse",
             "crc16-xmodem": "Crc16Xmodem", "crc16-arc": "Crc16Arc",
             "crc16-umts": "Crc16Buypass"}.get(alg)
    if clase is None:
        return None
    w = CRC_PARAM[alg][0]
    return "%0*x" % (w // 4, getattr(crc, clase).calc(datos))


def crc_zlib(alg, datos):
    import zlib
    return "%08x" % zlib.crc32(datos) if alg == "crc32" else None


def inet_suma(datos):
    s = 0
    for i in range(0, len(datos), 2):
        s += (datos[i] << 8) | (datos[i + 1] if i + 1 < len(datos) else 0)
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return "%04x" % (~s & 0xFFFF)


def inet_struct(datos):
    import struct
    d = datos + (b"\0" if len(datos) % 2 else b"")
    s = sum(struct.unpack(">%dH" % (len(d) // 2), d))
    s = (s % 0xFFFF) or (0xFFFF if s else 0)     # el complemento a uno, por módulo
    return "%04x" % (~s & 0xFFFF)


def comprueba_crc(casos, cuenta, detalle):
    for c in casos:
        alg, m = c["algoritmo"], hx(c["mensaje"])
        if alg == "inet":
            r = [("suma", inet_suma(m)), ("struct", inet_struct(m))]
        else:
            r = [("crccheck", crc_crccheck(alg, m)), ("bit-a-bit", crc_bit_a_bit(alg, m)),
                 ("zlib", crc_zlib(alg, m))]
        confronta(c, c["salida"], r, cuenta, detalle, "crc")


def comprueba_hash(casos, cuenta, detalle, lentos):
    for c in casos:
        if c.get("lento") == "si" and not lentos:
            cuenta.saltado += 1
            if detalle:
                print("  [saltado ] %-28s (lento; usa --lentos)" % c["id"])
            continue
        mensaje = hx(c["mensaje"]) * int(c.get("repite", 1))
        clave = hx(c.get("clave", ""))
        # El campo `texto` es informativo, pero si está tiene que cuadrar: un
        # comentario que miente es peor que no tenerlo.
        t = c.get("texto")
        if t is not None and t.startswith('"') and t.endswith('"'):
            if t[1:-1].encode() != hx(c["mensaje"]):
                print("  [FALLO   ] %s: el campo `texto` no coincide con `mensaje`"
                      % c["id"])
                cuenta.fallo += 1
                continue
        confronta(c, c["salida"],
                  [("hashlib", py_resume(c["algoritmo"], clave, mensaje)),
                   ("openssl", openssl_resume(c["algoritmo"], clave, mensaje))],
                  cuenta, detalle, "resumir")


def main():
    lentos = "--lentos" in sys.argv
    detalle = "--detalle" in sys.argv
    cuenta = Cuenta()

    cryp = lee("cryp.vec")
    hsh = lee("hash.vec")
    cryp2 = lee("cryp2.vec")
    sha2 = lee("sha2.vec")
    crc = lee("crc.vec")

    # Los identificadores tienen que ser únicos: un duplicado haría que una
    # prueba del banco tapara a otra sin que nadie se enterase.
    ids = [c["id"] for c in cryp + hsh + cryp2 + sha2 + crc]
    if len(ids) != len(set(ids)):
        repes = sorted({i for i in ids if ids.count(i) > 1})
        raise SystemExit("identificadores repetidos: %s" % ", ".join(repes))

    print("cryp.vec: %d casos" % len(cryp))
    comprueba_cryp(cryp, cuenta, detalle)
    print("hash.vec: %d casos" % len(hsh))
    comprueba_hash(hsh, cuenta, detalle, lentos)
    print("cryp2.vec: %d casos" % len(cryp2))
    comprueba_cryp2(cryp2, cuenta, detalle)
    print("sha2.vec: %d casos" % len(sha2))
    comprueba_hash(sha2, cuenta, detalle, lentos)
    print("crc.vec: %d casos" % len(crc))
    comprueba_crc(crc, cuenta, detalle)

    print()
    print("RESULTADO  %d comprobaciones con dos motores, %d con uno solo, "
          "%d fallos, %d saltadas"
          % (cuenta.ok, cuenta.flojo, cuenta.fallo, cuenta.saltado))
    print("Motores    %s" % ", ".join(sorted(cuenta.motores)))
    if cuenta.flojo:
        print("AVISO: hay casos confirmados por un solo motor. Instala lo que "
              "falte (pip install pycryptodome cryptography crccheck) y vuelve a pasar.")
    return 1 if (cuenta.fallo or cuenta.flojo) else 0


if __name__ == "__main__":
    sys.exit(main())
