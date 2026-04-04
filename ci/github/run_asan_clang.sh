#!/usr/bin/env bash
set -e
CLANG_FULL_VER=$(clang --version)
echo "$CLANG_FULL_VER"
echo "$(cat /etc/os-release)"
export CLANG_VER=$(clang --version | perl -lne 'print $1 if /version (\d+)/')
echo "Clang major version is: $CLANG_VER"

# Locate the ASan runtime library. Required for -fsanitize=address linking.
ASAN_RT=$(find /usr/lib/clang/ -name 'libclang_rt.asan-x86_64.so' 2>/dev/null | head -1)
if [[ -z "$ASAN_RT" ]]; then
	# Try the llvm lib path (Debian/Ubuntu package layout)
	ASAN_RT=$(find /usr/lib/llvm-*/lib/clang/ -name 'libclang_rt.asan-x86_64.so' 2>/dev/null | head -1)
fi
if [[ -z "$ASAN_RT" ]]; then
	echo "WARNING: ASan runtime not found, attempting to install..."
	apt-get update -qq && apt-get install -y --no-install-recommends libclang-rt-${CLANG_VER}-dev 2>/dev/null || true
	ASAN_RT=$(find /usr/lib/clang/ /usr/lib/llvm-*/ -name 'libclang_rt.asan-x86_64.so' 2>/dev/null | head -1)
fi
if [[ -z "$ASAN_RT" ]]; then
<<<<<<< HEAD
	echo "ERROR: Cannot find ASan runtime library. Skipping ASan build." >&2
	echo "Install libclang-rt-${CLANG_VER}-dev or equivalent package." >&2
=======
	echo "ERROR: Cannot find ASan runtime library. Skipping ASan build."
	echo "Install libclang-rt-${CLANG_VER}-dev or equivalent package."
>>>>>>> e5e27cd75 (Fix ASan CI: locate runtime library dynamically and install if missing)
	exit 0
fi
echo "ASan runtime: $ASAN_RT"
ASAN_RT_DIR=$(dirname "$ASAN_RT")

# Enable address sanitizer for memory safety checks
CFLAGS_ASAN="-g3 -O1 -mtune=generic -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address"
LDFLAGS_ASAN="-fsanitize=address -L${ASAN_RT_DIR} -Wl,-rpath,${ASAN_RT_DIR}"

# Sanitizer options: don't stop on leaks (expected in postgres binaries)
export ASAN_OPTIONS=halt_on_error=false,detect_leaks=1,leak_check_at_exit=0,exitcode=0
export LSAN_OPTIONS=exitcode=0

# Preload ASan runtime for postgres child processes
export LD_PRELOAD="$ASAN_RT"

/usr/local/pgsql/bin/pg_ctl -c -o '-F' -l /tmp/logfile start

# Build with Clang and ASan flags
./autogen.sh
./configure CC=clang CFLAGS="${CFLAGS_ASAN}" LDFLAGS="${LDFLAGS_ASAN}"
bash ./ci/github/logbt -- make -j
bash ./ci/github/logbt -- make check RUNTESTFLAGS=--verbose
