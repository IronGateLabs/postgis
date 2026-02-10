#!/usr/bin/env bash
set -e
CLANG_FULL_VER=`clang --version`
echo $CLANG_FULL_VER
echo `cat /etc/os-release`
export CLANG_VER=`clang --version | perl -lne 'print $1 if /version (\d+)/'`
echo "Clang major version is: $CLANG_VER"

# Enable address sanitizer for memory safety checks
CFLAGS_ASAN="-g3 -O1 -mtune=generic -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address"
LDFLAGS_ASAN="-fsanitize=address"

# Sanitizer options: don't stop on leaks (expected in postgres binaries)
export ASAN_OPTIONS=halt_on_error=false,detect_leaks=1,leak_check_at_exit=0,exitcode=0
export LSAN_OPTIONS=exitcode=0

# Run postgres preloading sanitizer libs
LD_PRELOAD=/usr/lib/clang/${CLANG_VER}/lib/linux/libclang_rt.asan-x86_64.so

/usr/local/pgsql/bin/pg_ctl -c -o '-F' -l /tmp/logfile start

# Build with Clang and ASan flags
./autogen.sh
./configure CC=clang CFLAGS="${CFLAGS_ASAN}" LDFLAGS="${LDFLAGS_ASAN}"
bash ./ci/github/logbt -- make -j
bash ./ci/github/logbt -- make check RUNTESTFLAGS=--verbose
