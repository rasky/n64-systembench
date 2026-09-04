SOURCE_DIR=src
BUILD_DIR=build
ROM_NAME=n64-systembench
CFLAGS+=-Wall -Wstrict-prototypes
include $(N64_INST)/include/n64.mk

all: postbuild
.PHONY: all

OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/rdp_lowlevel.o $(BUILD_DIR)/rsp_bench.o

n64-systembench.z64: N64_ROM_TITLE="SysBenchmark"

postbuild: n64-systembench.z64
	$(N64_INST)/bin/ed64romconfig --savetype flashram $<

$(BUILD_DIR)/n64-systembench.elf: $(OBJS)

clean:
	rm -f $(BUILD_DIR)/* *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
