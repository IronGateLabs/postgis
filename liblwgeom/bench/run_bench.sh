#!/bin/bash
# ============================================================================
# PostGIS CPU vs SIMD vs GPU Benchmark Runner
# ============================================================================
#
# Compares scalar (plain C), NEON SIMD, and Metal GPU backends for
# ECI/ECEF coordinate transform operations:
#   - Uniform Z-rotation (ptarray_rotate_z)
#   - Per-point M-epoch Z-rotation (ptarray_rotate_z_m_epoch)
#   - Radian conversion (ptarray_rad_convert)
#
# Usage:
#   ./run_bench.sh           # Build and run, formatted table output
#   ./run_bench.sh --csv     # Build and run, CSV output
#   ./run_bench.sh --validate  # Build and run validation checks
#
# Prerequisites:
#   - liblwgeom must be built (run `make` in the PostGIS root first)
#   - On macOS with Apple Silicon, Metal GPU benchmarks run automatically
#   - NEON SIMD is auto-detected on ARM64 platforms
#
# Interpreting results:
#   Median (us)   - Median wall-clock time across 8 measured iterations
#   Min/Max (us)  - Range of measured times (lower is better)
#   Mpts/sec      - Throughput in million points per second (higher is better)
#
#   The GPU backend includes transfer overhead (CPU<->GPU memory copies).
#   At low point counts, GPU overhead dominates; expect crossover at ~5K-50K
#   points depending on hardware.
#
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBLWGEOM_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
POSTGIS_DIR="$(cd "$LIBLWGEOM_DIR/.." && pwd)"

BENCH_BIN="$SCRIPT_DIR/bench_metal"

# Check that liblwgeom is built
if [[ ! -f "$LIBLWGEOM_DIR/.libs/liblwgeom.a" ]] && [[ ! -f "$LIBLWGEOM_DIR/.libs/liblwgeom.dylib" ]]; then
	echo "Error: liblwgeom not built. Run 'make' in $POSTGIS_DIR first." >&2
	exit 1
fi

# Build the benchmark if needed or if source is newer
if [[ ! -f "$BENCH_BIN" ]] || [[ "$SCRIPT_DIR/bench_metal.c" -nt "$BENCH_BIN" ]]; then
	echo "Building bench_metal..."
	make -C "$SCRIPT_DIR" bench_metal
	echo ""
fi

# Run the benchmark, passing through all arguments
# Set DYLD_LIBRARY_PATH so the dynamic linker finds liblwgeom
export DYLD_LIBRARY_PATH="$LIBLWGEOM_DIR/.libs:${DYLD_LIBRARY_PATH:-}"

exec "$BENCH_BIN" "$@"
