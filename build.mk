# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023 Amol Surati
# vim: set noet ts=4 sts=4 sw=4:

# DIR is the sub-directory to process.
# SRCDIR is for prereq and makefile-includes.
SRCDIR := $(DIR)
OBJDIR := $(DIR)

OBJS :=
SUBDIRS :=

# Default target for this makefile

# Using only $(OBJDIR) on the prereq side will cause make to search for
# $(VPATH)/$(OBJDIR) which it will find because VPATH is the SRC_PATH and the
# source does contain the DIR.
# about the | symbol: check order-only prerequisites
all: | $(CURDIR)/$(OBJDIR)

include $(SRCDIR)/Makefile

OBJS := $(sort $(OBJS))
SUBDIRS := $(sort $(SUBDIRS))

ifneq ($(OBJS),)
DEPS := $(addsuffix .d,$(basename $(OBJS)))
DEPS := $(addprefix $(OBJDIR)/,$(DEPS))
OBJS := $(addprefix $(OBJDIR)/,$(OBJS))
all: $(OBJS)
endif
ifneq ($(SUBDIRS),)
SUBDIRS := $(addprefix $(OBJDIR)/,$(SUBDIRS))
all: | $(SUBDIRS)
endif

$(SUBDIRS):
	$(MAKE) $(BUILD)=$@

# make can find the prereqs because of VPATH := $(SRC_PATH)
$(OBJDIR)/%.S.o: $(SRCDIR)/%.S
	$(CC) $(CFLAGS) $< -o $@

$(OBJDIR)/%.c.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $< -o $@
	$(AR) $(ARFLAGS) $(BIN_AR) $@

$(CURDIR)/$(OBJDIR):
	mkdir -p $@

-include $(DEPS)

.PHONY: $(SUBDIRS)
