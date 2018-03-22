###########################################
##  Where to find the targets's sources  ##
###########################################
SRCDIR     := src
DEPDIR     := .deps
BUILDDIR   := build
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
	@echo "$(COLOR_CYAN)[ done ]$(COLOR_RESET)"

# http://sunsite.ualberta.ca/Documentation/Gnu/make-3.79/html_chapter/make_4.html
$(DEPDIR)/%.d: $(SRCDIR)/%$(SOURCE_EXT)
	@echo "$(COLOR_CYAN)[ scanning preprocess-time dependencies ]$(COLOR_RESET) $<"
	@$(CXX) $(CFLAGS) -x c++ -MM $< -MF $@ -MT "$(BUILDDIR)/$*.asm"
# Next line might not be needed and lead to excessively long lines:
	@sed -Ee 's: *$$::' -e ':\\$$:;N;s:\\\n: :' -e 's: +: :g' -i $@
# next line from http://make.paulandlesley.org/autodep.html
	@cp $@ $@_
	@sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $@_ >> $@
	@rm -f $@_


# The dependency rules generated directly above dictate the dependencies for
# .asm files, i.e. which .csm files are required for generating which .asm
# file. This information is generated for each possible .asm file (even though
# we are only going to generate one). Therefor we only need the recipe here,
# the correct set of dependencies (with the correct file at the front of the
# list) is automatically selected.
%.asm:
	@echo "$(COLOR_CYAN)[ preprocessing ]$(COLOR_RESET) $<"
	@$(CXX) $(CFLAGS) -x c++ -E -o $@ $<
# AVR assembler should support '$' as logical line-end, but AVRA does not
	@sed -i "s:\\$$:\n:g" $@
# remove comments (lines starting with #)
	@sed -i "s:^#.*$$::" $@
# Compress whitespace (lines) to at most 3 in a row
	@sed -ni '/^\s*$$/d;:b;/^\s*$$/!bn;p;n;/^\s*$$/!bn;p;n;/^\s*$$/!bn;p;:w;n;/^\s*$$/bw;bb;:n;p;n;bb' $@

%.hex: %.asm
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
	@echo "$(COLOR_CYAN)[ assembling ]$(COLOR_RESET) $<"
	@$(ASSEMBLER) $(ASMFLAGS) $^ 2>$<.err | tail -n +13
# We invert the result of grep in the following expression so that having any
# lines in stderr other than the PRAGMAs cause Make to exit with an error.
	@sed -e 's:^:$(COLOR_RED):' -e 's:$$:$(COLOR_RESET):' $<.err \
		| grep -v 'PRAGMA directives currently ignored' \
		; test $$? -eq 1

%: $(BUILDDIR)/%.hex
	@echo "$(COLOR_CYAN)[ hex2bin ]$(COLOR_RESET) $<"
	@objcopy --input-target ihex --output-target binary $< $@

upload:
	@echo "$(COLOR_CYAN)[ uploading ]$(COLOR_RESET) $<"
# avrdude -p m168 -P /dev/ttyUSB0 -c stk500v1 -b 19200 -F -u -U flash:w:<YOUR FILE>.s.hex

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
