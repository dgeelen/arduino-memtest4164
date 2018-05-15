###########################################
##  Where to find the targets's sources  ##
###########################################
SRCDIR     := src
BUILDDIR   := build
DEPDIR     := $(BUILDDIR)/.deps
ASM_SOURCE_EXT  = .csm
ASM_HEADER_EXT  = .inc
CPP_SOURCE_EXT  = .cpp
CPP_HEADER_EXT  = .hpp

####################################
##  These are the files to build  ##
####################################
ASM_TARGETS := memtest4164

                         ############################
###########################  Don't touch anything  ############################
###########################   beyond this point    ############################
                         ############################

CPP_SOURCE_FILES  = $(shell find "$(SRCDIR)/" -iname "*$(CPP_SOURCE_EXT)" | sed "s:^$(SRCDIR)/::")
ASM_SOURCE_FILES  = $(shell find "$(SRCDIR)/" -iname "*$(ASM_SOURCE_EXT)" | sed "s:^$(SRCDIR)/::")
SUBDIRS      := $(shell find "$(SRCDIR)/" -type d | sed "s:^$(SRCDIR)/::")
ASSEMBLER     = avra

COLOR_RESET=$(shell echo -e '\033[0m')
COLOR_RED=$(shell echo -e '\033[01;31m')
COLOR_GREEN=$(shell echo -e '\033[01;32m')
COLOR_CYAN=$(shell echo -e '\033[01;36m')
COLOR_YELLOW=$(shell echo -e '\033[01;33m')

#watch order here, CPP_SOURCE_FILES set last because the others use it
CPP_OBJ_FILES     := $(CPP_SOURCE_FILES:%.cpp=$(BUILDDIR)/%.o)
CPP_DEP_FILES     := $(CPP_SOURCE_FILES:%=$(DEPDIR)/%.d)
CPP_SOURCE_FILES  := $(CPP_SOURCE_FILES:%$(CPP_SOURCE_EXT)=$(SRCDIR)/%$(CPP_SOURCE_EXT))
CPP_TARGETS       := $(CPP_TARGETS:%=$(BUILDDIR)/%)
ASM_TARGETS       := $(ASM_TARGETS:%=$(BUILDDIR)/%)
ASM_DEP_FILES     := $(ASM_SOURCE_FILES:%=$(DEPDIR)/%.d)
DEP_FILES          = $(CPP_DEP_FILES) $(ASM_DEP_FILES)
TARGETS            = $(CPP_TARGETS) $(ASM_TARGETS)
GENERATED_TARGETS := $(GENERATED_TARGETS:%=$(BUILDDIR)/%)
CPP_TARGET_DEPS   := $(CPP_TARGETS:$(BUILDDIR)/%=$(DEPDIR)/%_cpp_tgt.d)
ASM_TARGET_DEPS   := $(ASM_TARGETS:$(BUILDDIR)/%=$(DEPDIR)/%_asm_tgt.d)
TARGET_DEPS       := $(CPP_TARGET_DEPS) $(ASM_TARGET_DEPS)
# Also here, ASM_TARGETS is special
ASM_TARGETS       := $(ASM_TARGETS:%=%.bin)

$(shell mkdir -p $(BUILDDIR))
$(shell mkdir -p $(DEPDIR))
ifneq ($(strip $(SUBDIRS)),)
	$(shell cd $(BUILDDIR) && mkdir -p $(SUBDIRS))
	$(shell cd $(DEPDIR) && mkdir -p $(SUBDIRS))
endif
INCLUDE_DIRS = -I $(SRCDIR) -I $(BUILDDIR)
ASMFLAGS = $(INCLUDE_DIRS)
CFLAGS   = -pipe $(INCLUDE_DIRS) -pedantic -Wall -Wextra -Werror -Wfatal-errors -Wno-unused-function
ifneq ($(DEBUG),0)
$(info $(COLOR_GREEN)[Debug mode]$(COLOR_RESET))
	CFLAGS+=$(DBGFLAGS)
endif
CXXFLAGS = $(CFLAGS) -std=c++17
LDFLAGS  = -lpng
DBGFLAGS = -O0 -g

#.SILENT:

.PHONY: all clean cleaner
.DEFAULT: all
all: $(TARGET_DEPS) $(TARGETS)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Build complete"

# http://sunsite.ualberta.ca/Documentation/Gnu/make-3.79/html_chapter/make_4.html
$(DEPDIR)/%$(ASM_SOURCE_EXT).d: $(SRCDIR)/%$(ASM_SOURCE_EXT) $(GENERATED_TARGETS)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Scanning preprocess-time dependencies for $<"
	@$(CXX) $(CXXFLAGS) -x c++ -MM $< -MF $@ -MT "$(BUILDDIR)/$*.pp_asm $@"
# Next line might not be needed and lead to excessively long lines:
	@sed -Ee 's: *$$::' -e ':\\$$:;N;s:\\\n: :' -e 's: +: :g' -i $@
# next line from http://make.paulandlesley.org/autodep.html
	@cp $@ $@_
	@sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $@_ >> $@
	@rm -f $@_

# http://sunsite.ualberta.ca/Documentation/Gnu/make-3.79/html_chapter/make_4.html
$(DEPDIR)/%$(CPP_SOURCE_EXT).d: $(SRCDIR)/%$(CPP_SOURCE_EXT) $(GENERATED_TARGETS)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Scanning compile-time dependencies for $<"
	@$(CXX) $(CXXFLAGS) -x c++ -MM $< -MF $@ -MT "$(BUILDDIR)/$*.o $@"
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
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Preprocessing $< / $@"
	@$(CXX) $(CXXFLAGS) -x c++ -E -o $@ $<
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
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Assembling $<"
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
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   Creating binary $<"
	@objcopy --input-target ihex --output-target binary $< $@

%.asm: %.bin
	@echo "$(COLOR_CYAN)[ disasm    ]$(COLOR_RESET)   Disassembling $<..."
	@avr-objdump -zD --prefix-address --show-raw-insn --insn-width 4 -b binary -m avr $< > $@

$(DEPDIR)/%_asm_tgt.d: $(DEPDIR)/%$(ASM_SOURCE_EXT).d
# Nothing to do here, the preprocessing step is basically like linking?
	@touch $@

################################################################################


$(DEPDIR)/%_cpp_tgt.d: $(DEPDIR)/%$(CPP_SOURCE_EXT).d
	@echo "$(COLOR_CYAN)[ linking   ]$(COLOR_RESET)   Scanning link-time dependencies for $*"
#	     For all deps,        back-ref .o to the source files,                   back-ref dependent files to source files,  put on new lines,                 if exists,      output,               sort && rename back to output .o,                                                 put back on one line,     make these .o files dependencies of the final output.
	@sed "$(DEPDIR)/$*$(CPP_SOURCE_EXT).d" -Ee 's~(^[^.]+)\.o:~$(SRCDIR)/\1$(CPP_SOURCE_EXT)~' -e 's~( [^.]+)[^ ]+~\1$(CPP_SOURCE_EXT)~g' -e 's: +:\n:g' | while read l ; do [ -f "$${l}" ] && echo "$${l}" ; done | sort -u | sed -Ee 's:^($(SRCDIR)/)([^.]+)($(CPP_SOURCE_EXT)):$(BUILDDIR)/\2.o:' | sed ':a;N;s:\n: :g;ta' | (echo -n "$(BUILDDIR)/$*: " ; cat) > $@


$(BUILDDIR)/%.o: $(SRCDIR)/%$(CPP_SOURCE_EXT)
	@echo "$(COLOR_CYAN)[ compiling ]$(COLOR_RESET)   $<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

%:
	@echo '$(COLOR_CYAN)[ linking   ]$(COLOR_RESET)   $@'
	@$(CXX) $(LDFLAGS) $^ -o $@


################################################################################



disassembly: $(ASM_TARGETS:.bin=.asm)

# Simulation will generate a .VCD file
simulation: $(ASM_TARGETS)
	@echo "$(COLOR_CYAN)[ simavr    ]$(COLOR_RESET)   Running simulation..."
	@objcopy --input-target binary --output-target ihex $< $(ASM_TARGETS:.bin=_simavr.hex)
#	PORTB=0x05, PORTC=0x08, PORTD=0x0b, +32 --> 0x25, 0x28, 0x2b
	simavr -v --mcu atmega328p --freq 16000000 -ff $(TARGETS:.bin=_simavr.hex)    \
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
debug: $(ASM_TARGETS)
	@echo "$(COLOR_CYAN)[ simavr    ]$(COLOR_RESET)   Debugging..."
# simavr only loads the last section in a .hex file, so we can't use the
# regular (intermediate) hex file, we have to 'generate' a single-section hex
# file from the binary
	@objcopy --input-target binary --output-target ihex $< $(ASM_TARGETS:.bin=_simavr.hex)
	@echo '#!/bin/sh\
simavr --gdb --mcu atmega328p --freq 16000000 -ff $(ASM_TARGETS:.bin=_simavr.hex) &\
#simavr --trace -v -v -v --gdb --mcu atmega328p --freq 16000000 -ff $(ASM_TARGETS:.bin=_simavr.hex) &\
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
	@echo "$(COLOR_CYAN)[ running   ]$(COLOR_RESET)   Listening on tty..."
	@tail -f $(ARDUINO_TTY)


tty_discover:
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET)   Scanning for tty..."
	@$(eval ARDUINO_TTY=$(shell lsusb | grep Arduino | sed -e 's:Bus 0*:/dev/ttyACM:' -e 's: Device.*::'))
	@echo "$(ARDUINO_TTY)" | grep "ttyACM[5-9]*" >/dev/null || (echo "${COLOR_RED}Error: tty not found. Is your arduino connected?${COLOR_RESET}" && exit 1)
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET)   Found tty: $(ARDUINO_TTY)..."

upload: all tty_discover
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET)   Running AVRDude"
	@avrdude -p m328p -c arduino -P $(ARDUINO_TTY) -b 115200 -D -Uflash:w:$(ASM_TARGETS):r

clean:
	@echo "$(COLOR_CYAN)[ cleaning ]$(COLOR_RESET)"
	@rm -f $(OBJ_FILES) $(DEP_FILES) $(TARGET_DEPS) $(TARGETS)


cleaner: clean
	@echo "$(COLOR_CYAN)[ cleaner-ing ]$(COLOR_RESET)"
	@rm -fr $(BUILDDIR) $(DEPDIR)


# Skip dependency calculations for cleaning
# NOTE: This *must* *NOT* have any indentation or it won't work!
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),cleaner)
-include $(DEP_FILES)
-include $(TARGET_DEPS)
endif
endif
