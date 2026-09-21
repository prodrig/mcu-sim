import io
def sub(p,pares):
    s=io.open(p,encoding='utf-8').read()
    for a,b in pares:
        assert s.count(a)==1,(p,a[:70],s.count(a))
        s=s.replace(a,b)
    io.open(p,'w',encoding='utf-8').write(s); print('ok',p)

# ------------------------------------------------- doc/compilacion.md, §6.1
sub('doc/compilacion.md', [
("""De dónde sacarlo:

| Sistema | Cómo |
| :--- | :--- |
| Debian / Ubuntu | `apt install gcc-arm-none-eabi` |
| MSYS2 (MINGW64) | `pacman -S mingw-w64-x86_64-arm-none-eabi-gcc` |
| macOS | `brew install --cask gcc-arm-embedded` |
| Cualquiera | El que ya trae **STM32CubeIDE**, dentro de sus `plugins` |

La versión no importa mucho: el firmware es C bare-metal corriente y no usa nada
exótico. Aquí se compila con `arm-none-eabi-gcc 13.2`.""",
"""De dónde sacarlo:

| Sistema | Cómo |
| :--- | :--- |
| Debian / Ubuntu | `apt install gcc-arm-none-eabi` |
| MSYS2 | `pacman -S mingw-w64-x86_64-arm-none-eabi-toolchain` |
| macOS | `brew install --cask gcc-arm-embedded` |
| **Cualquiera, sin instalar nada** | El que ya trae **STM32CubeIDE** |

**En MSYS2, el grupo `-toolchain` y no el paquete `-gcc` a secas**, porque los
Makefiles de firmware necesitan además `objcopy`, que viene en `binutils`. Y si
el nombre no existe —MSYS2 los renombra de vez en cuando y está migrando de
`mingw64` a `ucrt64`—, la forma de averiguarlo no es adivinar:

```bash
pacman -Ss arm-none-eabi
```

Sirve cualquiera de los entornos: es un compilador **cruzado**, produce código
ARM, y da igual contra qué biblioteca de C de Windows se construyó él. Si acaba
en `/ucrt64/bin` estando tú en MINGW64, se usa igual con `CROSS`.

### 6.2 El atajo: el compilador que ya tienes

**Quien usa este simulador tiene STM32CubeIDE**, y CubeIDE trae su propio
`arm-none-eabi-gcc` con su `objcopy` y su `objdump`. No hay que instalar nada:
solo hay que encontrarlo.

```bash
ls -d /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin
```

Y con eso:

```bash
export CROSS=/c/ST/STM32CubeIDE_1.17.0/STM32CubeIDE/plugins/\\
com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-
${CROSS}gcc --version        # comprobar antes de lanzar nada
make test407fw
```

*(La ruta es de una instalación concreta; la tuya llevará otra versión y otro
sello de fecha. Por eso el `ls` de arriba, que la encuentra sola.)*

La versión no importa mucho: el firmware es C bare-metal corriente y no usa nada
exótico. Aquí se compila con `arm-none-eabi-gcc 13.2`, y el de CubeIDE 1.17 es
un 13.3.1."""),
])
