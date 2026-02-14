CC = gcc
CFLAGS = -O2 -Wall -I.. -I../.. -std=gnu11
LDFLAGS = -L.. -llwgeom -lm -lrt

# Use the acceleration flags from the build
ACCEL_CFLAGS = $(shell grep 'CFLAGS' ../../GNUmakefile 2>/dev/null | head -1 | sed 's/.*=//')

bench_accel: bench_accel.c ../liblwgeom.la
	$(CC) $(CFLAGS) $(ACCEL_CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: run run-csv run-validate clean

run: bench_accel
	./bench_accel

run-csv: bench_accel
	./bench_accel --csv

run-validate: bench_accel
	./bench_accel --validate

clean:
	rm -f bench_accel
