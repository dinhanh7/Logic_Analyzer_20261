#!/usr/bin/env bash
# Source this script, or use scripts/run-* wrappers.
RP2350_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RP2350_ROOT
export PATH="$RP2350_ROOT/.local/bin:$PATH"
export LD_LIBRARY_PATH="$RP2350_ROOT/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$RP2350_ROOT/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
if [[ -d "$RP2350_ROOT/.deps/root/usr" ]]; then
    RP2350_DEPS="$RP2350_ROOT/.deps/root/usr"
    export PATH="$RP2350_DEPS/bin:$PATH"
    export LD_LIBRARY_PATH="$RP2350_DEPS/lib/x86_64-linux-gnu:$RP2350_DEPS/lib:$LD_LIBRARY_PATH"
    export PKG_CONFIG_PATH="$RP2350_DEPS/lib/x86_64-linux-gnu/pkgconfig:$RP2350_DEPS/share/pkgconfig:$PKG_CONFIG_PATH"
    export CPATH="$RP2350_DEPS/include:$RP2350_DEPS/include/x86_64-linux-gnu${CPATH:+:$CPATH}"
    export LIBRARY_PATH="$RP2350_DEPS/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export PERL5LIB="$RP2350_DEPS/share/autoconf:$RP2350_DEPS/share/automake-1.16${PERL5LIB:+:$PERL5LIB}"
    export AUTOCONF="$RP2350_DEPS/bin/autoconf" AUTOHEADER="$RP2350_DEPS/bin/autoheader" AUTOM4TE="$RP2350_DEPS/bin/autom4te"
    export ACLOCAL_PATH="$RP2350_DEPS/share/aclocal:/usr/share/aclocal"
    export AUTOM4TE_CFG="$RP2350_DEPS/share/autoconf/autom4te.cfg"
    export AUTOMAKE_LIBDIR="$RP2350_DEPS/share/automake-1.16"
    export CMAKE_PREFIX_PATH="$RP2350_DEPS${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi
