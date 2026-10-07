CC ?= cc
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
CPPFLAGS += -Iinclude -Isrc

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)
LIB = build/libmd12.a

.PHONY: all lib check cli cross m7check clean kernel spice selftest

all: lib cli

lib: $(LIB)

build/%.o: src/%.c include/md12.h src/internal.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

build:
	mkdir -p build

# The tables are committed, so building the library needs no Python.
kernel:
	python3 tools/gen_kernel.py src/kernel.c

# The filter prototypes come from the netlists, checked against ngspice.
spice:
	tools/spice/run.sh
	python3 tools/gen_filters.py src/proto.c

build/test_md12: tests/test_md12.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra $< $(LIB) -lm -o $@

check: build/test_md12
	./build/test_md12

cli: build/md12 build/probe

# The owner test kit's analysis, run on the library itself.
selftest: cli
	python3 tools/kit/selftest.py

build/probe: tools/probe.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra tools/probe.c $(LIB) -lm -o $@

build/md12: tools/md12.c tools/wav.c tools/wav.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra tools/md12.c tools/wav.c $(LIB) -lm -o $@

# Proves the library builds for a bare Cortex-M7 with a single-precision FPU.
cross:
	mkdir -p build/cross
	for f in $(SRC); do arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 \
	    -mfloat-abi=hard -O2 $(WARN) -fsingle-precision-constant $(CPPFLAGS) -c $$f \
	    -o build/cross/$$(basename $$f .c).o || exit 1; done
	arm-none-eabi-size build/cross/*.o

# The whole test suite on a Cortex-M7 with a single-precision FPU, under QEMU.
M7 = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard
build/m7/test.elf: $(SRC) tests/test_md12.c tests/m7/boot.c tests/m7/m7.ld include/md12.h src/internal.h
	mkdir -p build/m7
	arm-none-eabi-gcc $(M7) -O2 -std=c99 $(CPPFLAGS) -Dmain=test_main -nostartfiles \
	    --specs=rdimon.specs -Wl,--no-warn-rwx-segments -T tests/m7/m7.ld $(SRC) tests/test_md12.c tests/m7/boot.c -lm -o $@

m7check: build/m7/test.elf
	qemu-system-arm -M mps2-an500 -nographic -monitor none -serial none \
	    -semihosting-config enable=on,target=native -kernel build/m7/test.elf

clean:
	rm -rf build
