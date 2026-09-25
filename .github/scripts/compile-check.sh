#!/bin/bash
# Compile qico on Debian 12 and stop there. No stripping, no staging,
# no zip: this is the per-commit gate that answers one question, does
# the tree still build, and it has to answer it fast.
#
# The release drop is built by build-debian12.sh. The configure flags
# here match it, so a break caught by this check is a break in the
# drop too. Anything about packaging belongs in that script, not here.
#
# Run by .github/workflows/compile-check.yml inside debian:12 on the
# runner's own architecture. configure executes test programs
# (AC_TRY_RUN), so this is a native build; do not cross-compile.

set -euo pipefail

# One banner per phase. The whole script is a single Actions step, so
# without these the apt, configure, and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

export DEBIAN_FRONTEND=noninteractive

step "Install Debian 12 build packages"
apt-get update
apt-get install -y --no-install-recommends \
	build-essential autoconf automake libtool pkg-config \
	flex bison libncurses-dev libperl-dev \
	git ca-certificates

# The runner owns the mount; git inside this container runs as root and
# otherwise refuses the checkout ("dubious ownership"). configure reads
# `git log` for the source stamp, so git must succeed.
# git itself is not in the debian:12 image, so this is after apt-get.
step "Trust the mounted git checkout"
git config --global --add safe.directory "$(pwd)"

# Windows checkouts rewrite autogen.sh to CRLF. dash then reads the
# shebang as "#!/bin/sh -e\r" and exits with "Illegal option -". The blob
# in git is LF; this only strips a carriage return if one is present.
step "Normalize autogen.sh to LF"
tr -d '\r' < autogen.sh > /tmp/autogen.sh
cp /tmp/autogen.sh autogen.sh
rm -f /tmp/autogen.sh

# Same flags as the release drop: a check that built fewer features
# would let a break through.
step "Generate configure and Makefiles"
./autogen.sh
step "Configure with BinkP and Perl"
./configure --prefix=/usr/local --enable-binkp --enable-perl

# The .l/.y in the tree define flaglex_reset. The shipped flaglex.c and
# flagexp.c do not. flex and bison are installed, so make will rebuild
# whichever input looks newer. Rebuilding only the parser leaves
# flaglex_reset undefined and the link fails. Drop both outputs first
# so they are regenerated as a pair.
step "Regenerate lexer and parser together"
rm -f src/flaglex.c src/flagexp.c src/flagexp.h

step "Compile"
make -j"$(nproc)"

step "Check that qico, qctl, qcc, and perl.o exist"
for bin in src/qico src/qctl src/qcc; do
	if [ ! -x "$bin" ]; then
		echo "missing binary: $bin" >&2
		exit 1
	fi
done
# configure keeps going if the libperl probe fails. A build that quietly
# dropped the perl hooks is a break worth reporting.
if [ ! -f src/perl.o ]; then
	echo "perl hooks were not compiled (src/perl.o missing)" >&2
	exit 1
fi

# The binaries must also start. -v prints the ident and then the usage
# text, then exits 0, before qctl/qcc try to reach the daemon. This is
# still a build check: no config files, no sessions.
for bin in qico qctl qcc; do
	step "Run ${bin} -v"
	"src/$bin" -v
done
