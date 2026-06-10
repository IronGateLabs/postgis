CC = gcc
CFLAGS = -O2 -Wall -I.. -I../.. -std=gnu11
LDFLAGS = -L../.libs -llwgeom -lm

# Use the acceleration flags from the build
ACCEL_CFLAGS = $(shell grep 'CFLAGS' ../../GNUmakefile 2>/dev/null | head -1 | sed 's/.*=//')

# macOS: link Metal and Foundation frameworks, no -lrt
ifeq ($(shell uname),Darwin)
  LDFLAGS += -framework Metal -framework Foundation
else
  LDFLAGS += -lrt
endif

bench_accel: bench_accel.c ../liblwgeom.la
	$(CC) $(CFLAGS) $(ACCEL_CFLAGS) -o $@ $< $(LDFLAGS)

bench_metal: bench_metal.c ../liblwgeom.la
	$(CC) $(CFLAGS) $(ACCEL_CFLAGS) -o $@ $< $(LDFLAGS)

all: bench_accel bench_metal

.PHONY: run run-csv run-validate run-metal run-metal-csv run-metal-validate clean all

run: bench_accel
	./bench_accel

run-csv: bench_accel
	./bench_accel --csv

run-validate: bench_accel
	./bench_accel --validate

run-metal: bench_metal
	./bench_metal

run-metal-csv: bench_metal
	./bench_metal --csv

run-metal-validate: bench_metal
	./bench_metal --validate

clean:
	rm -f bench_accel bench_metal
