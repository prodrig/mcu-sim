Reglas de exhaustividad (obligatorias)
Usa exclusivamente las fuentes de este cuaderno. 
No inventes ni completes con conocimiento externo.
Prohibido remitir a otro documento: nunca escribas "véase el manual de referencia", "consulte el datasheet" o similar. Reproduce el dato en el propio informe.
Incluye siempre los valores concretos: direcciones base, offsets de registros, valores de reset, codificaciones binarias, fórmulas, límites, frecuencias máximas, latencias y número de ciclos cuando estén documentados.
Si un dato necesario no aparece en las fuentes, escríbelo explícitamente como ⚠ NO DISPONIBLE EN LAS FUENTES: <descripción del dato>. Nunca lo omitas en silencio ni lo sustituyas por una generalidad.

Registros: documenta cada registro con una tabla Markdown con columnas: Offset | Nombre | Bits | Campo | Acceso (rw/r/w/rc_w0/rc_w1/rs/rt_w...) | Valor de reset | Descripción y efectos laterales. Incluye los efectos laterales de lectura/escritura (bits que se limpian al leer, escrituras con clave, registros sombra, protecciones de escritura) porque el modelo debe reproducirlos.

Instrucciones de la CPU: para cada instrucción documenta: sintaxis en ensamblador, codificación (16/32 bits, campos de la codificación), operandos y tipos de datos soportados, pseudocódigo completo de la operación, efecto sobre los flags de APSR, comportamiento con operandos no alineados, excepciones que puede generar, y ciclos de ejecución si están documentados. Usa bloques de código para el pseudocódigo.

Buses: describe las transacciones a nivel de protocolo (fases de dirección y datos, tipos de transferencia, ráfagas, tamaños, señales de respuesta y error, arbitraje), no solo a nivel funcional, porque el modelo SystemC debe reproducir el comportamiento de las transacciones.

No resumas ni selecciones "lo más importante": el criterio es la completitud, no la brevedad. Un capítulo largo es correcto; un capítulo incompleto no lo es.

Al final de cada tabla o afirmación clave, cita la fuente entre corchetes: [documento, sección].

Formato de salida

Markdown puro (.md): encabezados jerárquicos #/##/###, tablas Markdown, bloques de código para pseudocódigo y secuencias, listas solo cuando aporten claridad.
Números en hexadecimal con prefijo 0x para direcciones y valores de registros.

Cada capítulo empieza con un párrafo de "Implicaciones para el modelo SystemC" que resuma qué comportamiento observable debe reproducir el modelo.
Estructura del informe

