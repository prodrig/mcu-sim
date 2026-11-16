MSP430 Replicator Project Readme.txt

For more information please reference:
"MSP430 Programming Via the JTAG Interface User's Guide"-
Lit number: SLAU320 (previously SLAA149), available on www.ti.com

Software Package Version:   1.22 (Rev.W)
Last Change:                07/03/17


Version history:       

Vers.| Date     |Author| Comments
0.00 | 08/29/02 | ALB2 | Initial .zip Release
0.01 | 01/  /03 | ALB2 | Modifications were made to Replicator.C removing uneccesary code lines for general-purpose
                         Flash programming. These line have been commented out and still remain within the file.
0.02 | 08/  /03 | RAJU | Modifications were made to Filemaker.exe to handle byte strings in the object code file. 
                         Please note that the fill options in the compiler must be used to allow the filemaker 
                         handle the bytes correctly. The earlier version handles object codes as word strings only.
0.03 | 09/  /05 | JDI  | Project was converted to IAR Embedded Workbench Ver. 3.21.
                         JTAGFunc.c was modified to support F2xxx devices; all affected lines are marked 'F2xxx'.
                         Added support in Replicator.c for 2xx device info segment erase for B, C & D.
                         Software delay funtion "Delay()" converted to hardware using Timer_A: "MsDelay()"
1.00 | 01/  /06 | STO  | Project was adapted for SpyBiWire devices. Two versions of the project was created:
                         1)for 4Wire JTAG (also for SpyBiWire capable devices), placed in "\Replicator Project & Sources"; 
                         2)for 2Wire JTAG (SpyBiWire devices), placed in "\Replicator Project & Sources for SpyBiWire".
                         The project can be compiled with IAR Embedded Workbench Ver. 3.40B [Kickstart].
1.01 | 01/  /06 | STO  | Project was adapted for 430X CPU (e.g. MSP430F4619). This version is placed in 
                         "\Replicator Project & Sources for MSP430X".
                         Modifications were made to Filemaker.exe to handle both upper- and lower-case address formats
                         in .txt-files. Previous FileMaker.exe versions support only address specifiers in upper-case.
1.02 | 09/  /07 | WLUT | Project was updated to future funcionality of PSAVerify(). Some minor bugs and incompatibilities were fixed.
                         Additionally Filemaker.exe was replaced by an open source tool srec_cat.exe due to lack of 430X CPU compatibility. 
                         For usage see provided slaa149.pdf
1.03 | 01/  /08 | WLUT | Project was updated with latest device feature list (Devices.c).
                         Added delay to JTAG fuse check sequence to assure a TMS low phase of at least 5us.
1.04 | 08/  /08 | WLUT | New projects have been added for both CCE and IAR environments to support Flash programming via JTAG for
                         the MSP430Xv2 architecture which is implemented in the MSP430F5xx microcontroller families.
                         The projects were built and tested under CCEssentials v3 and IAR EW430 v4.11B
1.05 | 10/  /08 | WLUT | Fixed VerifyPSA_430Xv2() in module JTAGfunc430Xv2.c. The previous version was driving StartAddr-2 on the devices' address bus.
                         This can cause security violations in the 5xx system. The fixed version of the algorithm is now operating only within the
                         specified memory range which is to be verified.
                         Added correct Spy-Bi-Wire entry sequence for MSP430Xv2 architecture to the GetDevice() routine when configured
                         for Spy-Bi-Wire communication.
1.06 | 10/10/09 | FB   | Added support for new replicator hardware. If you have an old hardware please use the old software version 1.05. 
1.07 | 12/15/09 | FB   | The Erase FLASH does not work probably with the old constants, regarding this fact new constants were added. 
                         Also a new section which shows how to perform an erase check over the whole memory was added.
1.08 | 04/19/10 | FB   | Changed device ID pointer to an un-scrambled 20bit value.
                         Deleted DeviceIdPointer = ((DeviceIdPointer & 0xFFFF) << 4 )  + (DeviceIdPointer >> 16 ).
1.09 | 11/17/10 | FB   | Fixed blow JTAG fuse function. Removed support files for old F149 Replicator. 
1.10 | 04/14/11 | FB   | Added Handling for LPMx.5 wakeup 
                         Added Handling for Password protection and unlock
                         Added handling for FRAM Memory 
                         Added handling for Memory Protection Unit
                         Fixed and changed Magic Pattern function
1.11 | 09/05/11 | RL   | Removed SRecord binaries from the slau320 package due to licensing reasons. SRecord can be downloaded from 
                         http://sourceforge.net/projects/srecord/. Refer to the MSP430 Programming Via the JTAG Interface User's Guide 
                         #SLAU320 for information on how to convert TI-txt files into other formats.
1.12 | 07/09/12 | RL   | Added quick start guide to the Replicator package.
                         Changed the structure of the software package. CCS and IAR projects now use the same source code files in
                         slau320\Replicator430, slau320\Replicator430X and slau320\Replicator430Xv2
                         Updated commentaries in all source code files
                         Included DoxyGen documentation for the Replicator projects in slau320\_doc\html
                         Added Config430.h, Config430X.h and Config430Xv2.h to centralize all user input necessary for the initial setup in one file.
                         Added fuse blow via SBW for the Replicator430 project
1.13 | 10/24/12 | RL   | Removed hard-coded RAM start address for funclet loading - RAM start address can now be specified in the config file. (Replicator430Xv2 only)
1.14 | 11/26/12 | RL   | Fixed erase main memory flow in Replicator430Xv2.c - EraseFlash() and EraseCheck() now use main memory boundaries specified in Config430Xv2.h
1.15 | 02/11/13 | RL   | Added new devices to Replicator430 (tsDeviceFeatures in Devices430.c)
1.16 | 03/14/13 | RL   | Added support for MSP430G2x44, Xenon (MSP430G2955) and MSP430TCH5E
                         Added example code to unlock and erase InfoA memory
                         Added example code to unlock and read BSL memory area for 5xx/6xx devices
1.17 | 07/05/13 | RL   | Added support for MSP430i20xx family (4-wire JTAG only)
1.18 | 02/12/14 | RL   | Fixed SetPC function in Replicator430
                         Enhanced ReadMemQuick_430Xv2 and ExecutePor_430Xv2 functions in Replicator430Xv2 to improve FR59xx FRAM robustness
1.19 | 02/12/14 | RL/FB| Added support for i2040 in 2-wire Spy-Bi-Wire
                         Added Replicator430FR project for FRAM devices
1.20 | 06/24/15 | RL   | Fixed segment erase for MSP430ixx devices
                         Fixed EraseFRAM_430Xv2 function which would previously time out when erasing long memory areas
                         Changed FCTL handling to set LOCK bit after write/erase in flash
1.21 | 02/22/16 | RL   | Added workaround in ExecutePOR_430Xv2 function for Replicator430Xv2
1.22 | 07/03/17 | FB   | Added disable write protection for MSP430FR6047 and MSP430FR5994 Faimly devices
