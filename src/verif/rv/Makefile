# =============================================================================
# verif/rv/Makefile — Los programas de prueba del núcleo RISC-V (E1)
#
# E1 de doc/analisis_esp32_riscv.md. Genera, a partir de las fuentes, todo lo
# que el banco `testrv` lee y que se versiona en elf/ (como los .bin de
# verif/fw: el banco no necesita la cadena de RISC-V para pasar):
#
#   elf/<prueba>.elf        las pruebas propias del núcleo (propias/*.S)
#   elf/coremark_rv.elf     CoreMark, con las fuentes de ../fw/coremark
#   elf/riscv-tests.tar.xz  el subconjunto de riscv-tests que pasa el núcleo
#                           (rv32ui/um/ua/uc/mi, entorno -p), BSD-3-Clause;
#                           RVT = la carpeta isa/ de un riscv-tests compilado
#   vectores_c.inc          la tabla de expansión de las comprimidas
#                           (gen_vectores_c.py, con el ensamblador de GNU)
#   trazas_spike.inc        las huellas de las trazas de Spike de cada ELF
#                           (gen_trazas.py; SPIKE = el ejecutable)
#
#   make                    # todo menos riscv-tests y las trazas
#   make rvt RVT=~/riscv-tests/isa
#   make trazas SPIKE=~/spike/bin/spike
# =============================================================================
CROSS  ?= riscv64-unknown-elf-
CC     := $(CROSS)gcc
ARCH   := -march=rv32imac_zicsr_zifencei -mabi=ilp32
ASFLAGS := $(ARCH) -mno-relax -nostdlib -nostartfiles -Irt -Wl,--no-warn-rwx-segments
PY     ?= python3
SPIKE  ?= spike
RVT    ?=

PROPIAS := traps umodo vec irq trig cpu863 ext dbg irq_spike
ELFS    := $(PROPIAS:%=elf/%.elf) elf/coremark_rv.elf

# Las que se comparan con Spike (las demás dependen del tiempo, de la errata
# o de una extensión que Spike no tiene)
CON_SPIKE := traps umodo vec trig irq_spike coremark_rv

all: $(ELFS) vectores_c.inc

elf/%.elf: propias/%.S rt/inicio.S rt/lab.ld rt/prueba.h
	@mkdir -p elf
	$(CC) $(ASFLAGS) -T rt/lab.ld rt/inicio.S $< -o $@

# --- CoreMark ---------------------------------------------------------------
CM      := ../fw/coremark
CMFLAGS := $(ARCH) -O2 -ffreestanding -fno-builtin -std=gnu99 -mno-relax \
           -DITERATIONS=1 -DPERFORMANCE_RUN=1 \
           -DFLAGS_STR="\"-O2 -march=rv32imac\"" \
           -DCOMPILER_VERSION="\"riscv64-unknown-elf-gcc\"" \
           -specs=picolibc.specs \
           -I$(CM) -Wall -Wno-unused-parameter
CMSRC   := $(CM)/core_list_join.c $(CM)/core_matrix.c $(CM)/core_state.c \
           $(CM)/core_util.c $(CM)/ee_printf.c $(CM)/cvt.c \
           coremark/core_portme_rv.c coremark/cm_rv.c

elf/coremark_rv.elf: $(CMSRC) $(CM)/core_main.c $(CM)/core_portme.h rt/inicio.S rt/lab.ld
	@mkdir -p elf
	$(CC) $(CMFLAGS) -Dmain=coremark_main -c -o elf/core_main_rv.o $(CM)/core_main.c
	$(CC) $(CMFLAGS) -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -T rt/lab.ld \
	      -o $@ rt/inicio.S elf/core_main_rv.o $(CMSRC) -lgcc
	rm -f elf/core_main_rv.o

# --- Las comprimidas, contra el ensamblador de GNU ----------------------------
vectores_c.inc: gen_vectores_c.py
	$(PY) gen_vectores_c.py --cc $(CC) > $@

# --- riscv-tests --------------------------------------------------------------
RVT_SEL = $(filter-out %.dump,$(notdir $(wildcard $(RVT)/rv32ui-p-* $(RVT)/rv32um-p-* \
          $(RVT)/rv32ua-p-* $(RVT)/rv32uc-p-* $(RVT)/rv32mi-p-*)))
rvt:
	@test -n "$(RVT)" || { echo "make rvt RVT=<riscv-tests>/isa"; exit 1; }
	@mkdir -p elf
	tar -C $(RVT) --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
	    -cJf elf/riscv-tests.tar.xz $(RVT_SEL)

# --- Las huellas de Spike -----------------------------------------------------
trazas: $(ELFS)
	$(PY) gen_trazas.py --spike $(SPIKE) $(CON_SPIKE:%=elf/%.elf) > trazas_spike.inc

clean:
	rm -f $(ELFS)

.PHONY: all rvt trazas clean
