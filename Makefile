###########################################
##  Where to find the targets's sources  ##
###########################################
SRCDIR     := src
BUILDDIR   := build
DEPDIR     := $(BUILDDIR)/.deps
SOURCE_EXT  = .csm
HEADER_EXT  = .inc

####################################
##  These are the files to build  ##
####################################
TARGETS := memtest4164

                         ############################
###########################  Don't touch anything  ############################
###########################   beyond this point    ############################
                         ############################

SOURCE_FILES  = $(shell find "$(SRCDIR)/" -iname "*$(SOURCE_EXT)" | sed "s:^$(SRCDIR)/::")
SUBDIRS      := $(shell find "$(SRCDIR)/" -type d | sed "s:^$(SRCDIR)/::")
ASSEMBLER     = avra

COLOR_RESET=$(shell echo -e '\033[0m')
COLOR_RED=$(shell echo -e '\033[01;31m')
COLOR_GREEN=$(shell echo -e '\033[01;32m')
COLOR_CYAN=$(shell echo -e '\033[01;36m')
COLOR_YELLOW=$(shell echo -e '\033[01;33m')

#watch order here, SOURCE_FILES set last because the others use it
OBJ_FILES    := $(SOURCE_FILES:%$(SOURCE_EXT)=$(BUILDDIR)/%.o)
DEP_FILES    := $(SOURCE_FILES:%$(SOURCE_EXT)=$(DEPDIR)/%.d)
SOURCE_FILES := $(SOURCE_FILES:%$(SOURCE_EXT)=$(SRCDIR)/%$(SOURCE_EXT))
TARGETS      := $(TARGETS:%=$(BUILDDIR)/%.bin)

$(shell mkdir -p $(BUILDDIR))
$(shell mkdir -p $(DEPDIR))
ifneq ($(strip $(SUBDIRS)),)
	$(shell cd $(BUILDDIR) && mkdir -p $(SUBDIRS))
	$(shell cd $(DEPDIR) && mkdir -p $(SUBDIRS))
endif
ASMFLAGS = -I src
CFLAGS   = -pipe -I$(SRCDIR) -std=c++14 -Wall -Wextra -Werror -Wfatal-errors -Wno-unused-function -W -pedantic
LDFLAGS  = -lboost_signals -lboost_filesystem -lboost_system -lboost_thread -lboost_chrono
DBGFLAGS = -O0 -g

ifneq ($(DEBUG),0)
$(info $(COLOR_GREEN)[Debug mode]$(COLOR_RESET))
	CFLAGS+=$(DBGFLAGS)
endif

#.SILENT:

.PHONY: all clean cleaner
.DEFAULT: all
all: $(TARGETS)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET) Build complete"

# http://sunsite.ualberta.ca/Documentation/Gnu/make-3.79/html_chapter/make_4.html
$(DEPDIR)/%.d: $(SRCDIR)/%$(SOURCE_EXT)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET) scanning preprocess-time dependencies for $<"
	@$(CXX) $(CFLAGS) -x c++ -MM $< -MF $@ -MT "$(BUILDDIR)/$*.pp_asm"
# Next line might not be needed and lead to excessively long lines:
	@sed -Ee 's: *$$::' -e ':\\$$:;N;s:\\\n: :' -e 's: +: :g' -i $@
# next line from http://make.paulandlesley.org/autodep.html
	@cp $@ $@_
	@sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $@_ >> $@
	@rm -f $@_


# The dependency rules generated directly above dictate the dependencies for
# .pp_asm files, i.e. which .csm files are required for generating which .pp_asm
# file. This information is generated for each possible .pp_asm file (even though
# we are only going to generate one). Therefor we only need the recipe here,
# the correct set of dependencies (with the correct file at the front of the
# list) is automatically selected.
%.pp_asm:
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET) preprocessing $< / $@"
	@$(CXX) $(CFLAGS) -x c++ -std=c++17 -E -o $@ $<
# AVR assembler should support '$' as logical line-end, but AVRA does not
	@sed -i "s:\\$$:\n:g" $@
# remove comments (lines starting with #)
	@sed -i "s:^#.*$$::" $@
# Compress whitespace (lines) to at most 3 in a row
	@sed -ni '/^\s*$$/d;:b;/^\s*$$/!bn;p;n;/^\s*$$/!bn;p;n;/^\s*$$/!bn;p;:w;n;/^\s*$$/bw;bb;:n;p;n;bb' $@

%.hex %.map: %.pp_asm
# NOTE:
# AVRA is a piece of shit garbage, and most command line arguments don't work.
# E.g. for the output file argument the source mentions 'Not implemented ? B.A.'
# These are not implemented:
#   'w', wrap
#   'f', filetype
#   'o', outfile
#   'd', debugfile
#   'e', eepfile
#
# So we need to move the output file to the destination ourselves...
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)  assembling $<"
	$(ASSEMBLER) $(ASMFLAGS) -m $(^:.pp_asm=.map) $^ 2>$<.err | tail -n +13
# Colour the output for easy parsing (this is unrelated to checking for errors)
	@sed -e '#\
		/: PRAGMA directives currently ignored/d;#\
		tExit;#\
		#\
		/: Warning :/s:^:$(COLOR_YELLOW):;#\
		tResetColor;#\
		#\
		s:^:$(COLOR_RED):;#\
		tResetColor;#\
		#\
		:ResetColor;#\
		s:$$:$(COLOR_RESET):;#\
		#\
		:Exit#\
	' $<.err
# We invert the result of grep in the following expression so that having any
# lines in stderr other than the PRAGMAs cause Make to exit with an error.
	@egrep -v ': PRAGMA directives currently ignored|: Warning :' $<.err >/dev/null ; test $$? -eq 1
# Rename output (see comment above)
	@mv $<.hex $@

%.bin: %.hex
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET) Creating binary $<"
	@objcopy --input-target ihex --output-target binary $< $@

%.asm: %.bin
	@echo "$(COLOR_CYAN)[ disasm    ]$(COLOR_RESET) Disassembling $<..."
	@avr-objdump -zD --prefix-address --show-raw-insn --insn-width 4 -b binary -m avr $< > $@

disassembly: $(TARGETS:.bin=.asm)

# Simulation will generate a .VCD file
simulation: $(TARGETS)
	@echo "$(COLOR_CYAN)[ simavr    ]$(COLOR_RESET) Running simulation..."
	@objcopy --input-target binary --output-target ihex $< $(TARGETS:.bin=_simavr.hex)
#	PORTB=0x05, PORTD=0x0b, +32 = 0x25 & 0x2b
	../simavr/simavr/run_avr --trace -v -v -v -v -v --mcu atmega328p --freq 16000000 -ff $(TARGETS:.bin=_simavr.hex) \
		--vcd-trace-file $(BUILDDIR)/memtest4164.vcd                                \
		--add-vcd-trace LED=trace@0x25/0x20                                         \
		--add-vcd-trace ~WE=trace@0x25/0x04                                         \
		--add-vcd-trace ~RAS=trace@0x25/0x02                                        \
		--add-vcd-trace ~CAS=trace@0x25/0x08                                        \
		--add-vcd-trace Din=trace@0x25/0x10                                         \
		--add-vcd-trace Dout=trace@0x25/0x01                                        \
		--add-vcd-trace A0=trace@0x2b/0x01                                          \
		--add-vcd-trace A1=trace@0x2b/0x02                                          \
		--add-vcd-trace A2=trace@0x2b/0x04                                          \
		--add-vcd-trace A3=trace@0x2b/0x08                                          \
		--add-vcd-trace A4=trace@0x2b/0x10                                          \
		--add-vcd-trace A5=trace@0x2b/0x20                                          \
		--add-vcd-trace A6=trace@0x2b/0x40                                          \
		--add-vcd-trace A7=trace@0x2b/0x80

# Simulation
debug: $(TARGETS)
	@echo "$(COLOR_CYAN)[ simavr    ]$(COLOR_RESET) Debugging..."
# simavr only loads the last section in a .hex file, so we can't use the
# regular (intermediate) hex file, we have to 'generate' a single-section hex
# file from the binary
	@objcopy --input-target binary --output-target ihex $< $(TARGETS:.bin=_simavr.hex)
	@echo '#!/bin/sh\
simavr --gdb --mcu atmega328p --freq 16000000 -ff $(TARGETS:.bin=_simavr.hex) &\
#simavr --trace -v -v -v --gdb --mcu atmega328p --freq 16000000 -ff $(TARGETS:.bin=_simavr.hex) &\
SIMAVR_PID=$$!\
echo SIMAVR_PID=$${SIMAVR_PID}\
avr-gdb -ex "target remote 127.0.0.1:1234"\
kill $${SIMAVR_PID}\
wait $${SIMAVR_PID}\
	' | sed 's:\\$$::' > $(BUILDDIR)/run_simavr_gdb.sh
	@chmod +x $(BUILDDIR)/run_simavr_gdb.sh
	@./$(BUILDDIR)/run_simavr_gdb.sh || true
	@echo "Simulation closed"

listen: tty_discover
	@echo "$(COLOR_CYAN)[ running   ]$(COLOR_RESET) Listening on tty..."
	tail -f $(ARDUINO_TTY)


tty_discover:
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET) Scanning for tty..."
	@$(eval ARDUINO_TTY=$(shell lsusb | grep Arduino | sed -e 's:Bus 0*:/dev/ttyACM:' -e 's: Device.*::'))
	@echo "$(ARDUINO_TTY)" | grep "ttyACM[5-9]*" >/dev/null || (echo "${COLOR_RED}Error: tty not found. Is your arduino connected?${COLOR_RESET}" && exit 1)
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET) Found tty: $(ARDUINO_TTY)..."

upload: all tty_discover
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET) Running AVRDude"
	@avrdude -p m328p -c arduino -P $(ARDUINO_TTY) -b 115200 -D -Uflash:w:$(TARGETS):r

clean:
	@echo "$(COLOR_CYAN)[ cleaning ]$(COLOR_RESET)"
	@rm -f $(OBJ_FILES) $(DEP_FILES) $(TARGETS)


cleaner: clean
	@echo "$(COLOR_CYAN)[ cleaner-ing ]$(COLOR_RESET)"
	@rm -fr $(BUILDDIR) $(DEPDIR)


# Skip dependency calculations for cleaning
# NOTE: This *must* *NOT* have any indentation or it won't work!
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),cleaner)
-include $(DEP_FILES)
endif
endif
