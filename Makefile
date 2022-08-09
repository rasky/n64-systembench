SOURCE_DIR=src
BUILD_DIR=build
include $(N64_INST)/include/n64.mk

all: n64-systembench.z64
.PHONY: all

OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/rsp_bench.o

hello.z64: N64_ROM_TITLE="SysBenchmark"

$(BUILD_DIR)/n64-systembench.elf: $(OBJS)

clean:
	rm -f $(BUILD_DIR)/* *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
