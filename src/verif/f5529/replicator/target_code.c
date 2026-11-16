/* target_code.c — mcu-sim: el programa que el Replicator430 escribe en el
 * objetivo, transcrito de Target_Code_CCS.s43 del Replicator430 de TI (el
 * mismo de Target_Code.txt): para el perro, P1.0 de salida y cambia P1.0 (y
 * P3.0) en un bucle de espera. 0xFF00-0xFF11 y el vector de reset. */
const unsigned int eprom[19] = {   /* Target_Code.h dice [10]: el tamaño de verdad es 19 */
    0x4031, 0x0300, 0x40B2, 0x5A80, 0x0120, 0xD3D2, 0x0022, 0xD3E2,
    0x0032, 0x403F, 0x9C40, 0x831F, 0x23FE, 0xE3D2, 0x0021, 0xE3E2,
    0x0031, 0x3FF7,
    0xFF00
};
const unsigned long eprom_address[10] = {0x0000FF00, 0x0000FFFE};
const unsigned long eprom_length_of_sections[10] = {0x00000012, 0x00000001};
const unsigned long eprom_sections = 0x0002;
