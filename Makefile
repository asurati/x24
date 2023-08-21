# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023 Amol Surati
# vim: set noet ts=4 sts=4 sw=4:

# cd /path/to/outdir
# make -f path/to/Makefile [all|c|r|rd]
# Later, make

# Accept only 'all', 'c', 'r' 'v' as MAKECMDGOALS
T = $(filter-out all c r v,$(MAKECMDGOALS))
ifneq ($(T),)
$(error Unsupported goal(s))
endif

all:

ifeq ($(MAKELEVEL),0)
MAKEFLAGS += -rR
MAKEFLAGS += --no-print-directory

THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
SRC_PATH := $(realpath $(dir $(THIS_MAKEFILE)))

# CURDIR is the path where object files are built.
# SRCPATH and CURDIR must be different.
ifneq ($(CURDIR),$(SRC_PATH))
MAKEFLAGS += --include-dir=$(SRC_PATH)
else
$(error In-tree builds not supported)
endif

VPATH := $(SRC_PATH)
export VPATH SRC_PATH

all c r v:
	$(MAKE) -f $(THIS_MAKEFILE) $(MAKECMDGOALS)
endif # ifeq ($(MAKELEVEL),0)







# From here on, $(CURDIR) doesn't change.
# $(CURDIR) is the location where the output files will be stored.

ifeq ($(MAKELEVEL),1)
# Created inside $(CURDIR)/
BIN := x24
BIN_AR := x24.a

# valgrind flags
VFLAGS := --leak-check=full --show-leak-kinds=all --track-origins=yes
VFLAGS += --verbose --log-file=/tmp/$(BIN).valgrind.txt

CC := clang
LD := ld
AR := ar
OC := objcopy
OD := objdump
CPP := clang-cpp

ifneq ($(CROSS),)
CC := $(CROSS)-$(CC)
AR := $(CROSS)-$(AR)
OC := $(CROSS)-$(OC)
OD := $(CROSS)-$(OD)
CPP := $(CROSS)-$(CPP)
endif

RM := rm --one-file-system --preserve-root=all

INC := -I$(SRC_PATH)

ARFLAGS += --thin -r -cvsP

# ASAN:
# CFLAGS += -fno-omit-frame-pointer -fno-optimize-sibling-calls
# CFLAGS += -fsanitize=address
# Build: add to $(CC) link/build cmd -fsanitize=address -static-libsan

# c11 for uchar.h
XFLAGS += $(INC) -std=c11 -MMD -MP
CPPFLAGS += -P -x assembler-with-cpp $(XFLAGS)
CFLAGS += -c -O0 -g -pedantic-errors $(XFLAGS) -Werror -Wfatal-errors
CFLAGS += -Wall -Wextra -Wshadow -Wpedantic -Wcast-align
CFLAGS += -fno-common -fno-exceptions -fno-unwind-tables
CFLAGS += -fno-asynchronous-unwind-tables -fsigned-char

# These exports are needed by the BUILD command.
export CC CPP AR CFLAGS CPPFLAGS ARFLAGS BIN_AR

# Building commands
BUILD := -f $(SRC_PATH)/build.mk DIR
export BUILD

DIRS := src
###############################################################################
all: $(BIN)
	@:

$(BIN): $(BIN_AR)
	$(CC) $^ -o $@

$(BIN_AR): $(DIRS)
	@:

$(DIRS): OUT_MK
	$(MAKE) $(BUILD)=$@

OUT_MK:
	@[ -f Makefile ] || { echo "include $(SRC_PATH)/Makefile" >> Makefile; }

c:
	if [ -e ./$(BIN) ]; then $(RM) ./$(BIN); fi
	if [ -e ./$(BIN_AR) ]; then $(RM) ./$(BIN_AR); fi
	if [ -d ./src ]; then $(RM) -r ./src; fi

r:
	./$(BIN) /tmp/1.c

v:
	valgrind $(VFLAGS) ./$(BIN) /tmp/1.c

endif # ifeq ($(MAKELEVEL),1)

.PHONY: OUT_MK r c $(DIRS) all
