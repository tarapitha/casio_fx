# Out-of-tree Makefile for casio_sx / casio_rx / cas2pbm / prg2cas
# Place this Makefile in the project root.
# Usage:
#   make                # creates build/ and does the build there
# or
#   mkdir build
#   cd build
#   make -f ../Makefile

# Toolchain
CC	?= gcc
CFLAGS	?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -MMD -MP
LDFLAGS	?=

TOP          := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRC_DIR      := $(TOP)/src
BUILD_DIR    ?= $(TOP)/build
VERSION_FILE := $(TOP)/VERSION
VERSION_STR  := $(shell cat $(VERSION_FILE))

CFLAGS	+= -DCASIO_FX_VERSION=\"$(VERSION_STR)\"

casio_tx_SRCS := casio_tx_fsm.c casio_tx_transport.c
casio_rx_SRCS := casio_rx_fsm.c casio_rx_transport.c
cas2pbm_SRCS  := cas2pbm.c
prg2cas_SRCS  := prg2cas.c

casio_tx_SRCS := $(addprefix $(SRC_DIR)/,$(casio_tx_SRCS))
casio_rx_SRCS := $(addprefix $(SRC_DIR)/,$(casio_rx_SRCS))
cas2pbm_SRCS  := $(addprefix $(SRC_DIR)/,$(cas2pbm_SRCS))
prg2cas_SRCS  := $(addprefix $(SRC_DIR)/,$(prg2cas_SRCS))

TARGETS := casio_tx casio_rx cas2pbm prg2cas

.PHONY: all clean _build_all

# Default: ensure build dir exists and delegate to a make inside it.
all: | $(BUILD_DIR)
	$(MAKE) -C $(BUILD_DIR) -f $(TOP)/Makefile _build_all

# Create build dir if missing
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --------------------------------------------------------------------
# Simple top-level clean: remove the entire build/ directory (safe,
# because build/ is temporary and not tracked in git)
# --------------------------------------------------------------------
clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
	  printf "Removing build artifacts in %s\n" "$(BUILD_DIR)"; \
	  rm -rf "$(BUILD_DIR)"/*; \
	else \
	  printf "No build directory to process: %s\n" "$(BUILD_DIR)"; \
	fi

#
# === The following section is the "in-build" behavior.
# If make is executed with CWD == BUILD_DIR (i.e. via "make -C build -f ../Makefile"),
# the Makefile will fall through to the real compile rules below.
#

# Decide whether we are running inside the build dir
ifeq ($(abspath $(CURDIR)),$(abspath $(BUILD_DIR)))

casio_tx_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(casio_tx_SRCS))
casio_rx_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(casio_rx_SRCS))
cas2pbm_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(cas2pbm_SRCS))
prg2cas_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(prg2cas_SRCS))

ALL_OBJS := $(casio_tx_OBJS) $(casio_rx_OBJS) $(cas2pbm_OBJS) $(prg2cas_OBJS)
ALL_DEPS := $(ALL_OBJS:.o=.d)

casio_tx: $(casio_tx_OBJS)
	$(CC) $^ -o $(BUILD_DIR)/casio_tx $(LDFLAGS)

casio_rx: $(casio_rx_OBJS)
	$(CC) $^ -o $(BUILD_DIR)/casio_rx $(LDFLAGS)

cas2pbm: $(cas2pbm_OBJS)
	$(CC) $^ -o $(BUILD_DIR)/cas2pbm $(LDFLAGS)

prg2cas: $(prg2cas_OBJS)
	$(CC) $^ -o $(BUILD_DIR)/prg2cas $(LDFLAGS)

# phony helper: invoked by top-level make -> recursive call into this Makefile
_build_all: casio_tx casio_rx cas2pbm prg2cas
	@echo "Built executables in $(BUILD_DIR)"

# Pattern rule: compile TOP/xxx.c -> BUILD_DIR/xxx.o
# Order-only prerequisite | $(TOP) not needed but kept to show intent
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(VERSION_FILE)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Include generated dependency files if present
-include $(ALL_DEPS)

endif
