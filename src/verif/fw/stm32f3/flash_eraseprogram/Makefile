# FLASH_EraseProgram de STM32CubeF3, sin tocar: borra las paginas 16 a 63 con
# HAL_FLASHEx_Erase, las programa con HAL_FLASH_Program (palabras de 32 bits,
# que la HAL parte en dos medias palabras), las comprueba y enciende LD3 (PE9)
# si todo cuadra, LD4 (PE8) si no y LD5 (PE10) si el FPEC da error. Fase S2
NOMBRE := FLASH_EraseProgram
EJ     := ../sdk/ejemplos/FLASH_EraseProgram
include ../ejemplo_st.mk
