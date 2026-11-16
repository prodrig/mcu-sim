/* target_code.c — mcu-sim: el programa que el Replicator430Xv2 escribe en el
 * objetivo, transcrito de Target_Code_5xx_CCS.s43 del Replicator430Xv2 de TI
 * (el mismo de Target_Code_5xx.txt): para el perro, P1.0 de salida y la
 * cambia en un bucle de espera (el LED1 de la F5529LP). 0xFC00-0xFC1B y el
 * vector de reset. */
const unsigned int eprom[15] = {   /* Target_Code.h dice [10]: el tamaño de verdad es 15 */
    0x4031, 0x0280, 0x40B2, 0x5A80, 0x015C, 0xD3D2, 0x0204, 0xE3D2,
    0x0202, 0x403F, 0xC350, 0x831F, 0x23FE, 0x3FF9,
    0xFC00
};
const unsigned long eprom_address[10] = {0x0000FC00, 0x0000FFFE};
const unsigned long eprom_length_of_sections[10] = {0x0000000E, 0x00000001};
const unsigned long eprom_sections = 0x0002;
