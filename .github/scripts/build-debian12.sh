#!/bin/bash
# Build qico inside a Debian 12 userspace and pack a binary drop.
#
# GitHub Actions checks the tree out on the Ubuntu runner, then runs this
# script with the tree mounted in debian:12 (see debian-12.yml). configure
# executes test programs (AC_TRY_RUN), so the compiler and the tests must
# be the same Debian 12 system. Do not cross-compile.
#
# The architecture comes from the container, not from a configure flag.
# linux/amd64 produces Debian "amd64". linux/arm64 (QEMU on an amd64
# runner, or a native arm64 host) produces Debian "arm64"; uname -m
# inside that container is aarch64. dpkg --print-architecture is what
# lands in the zip name and in file_id.diz.
#
# On success the script leaves one zip under dist/:
#   qico-<version>-debian12-<arch>-<sha>.zip
# containing qico, qctl, qcc, file_id.diz, README, Changes, and the
# three sample configs. The zip is the Actions artifact.

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
	build-essential autoconf automake libtool pkg-config \
	flex bison libncurses-dev libperl-dev \
	git ca-certificates zip

# The runner owns the mount; git inside this container runs as root and
# otherwise refuses the checkout ("dubious ownership"). The stamp in
# qico -v comes from `git log` during configure, so git must succeed.
# git itself is not in the debian:12 image, so this is after apt-get.
git config --global --add safe.directory "$(pwd)"

# Windows checkouts rewrite autogen.sh to CRLF. dash then reads the
# shebang as "#!/bin/sh -e\r" and exits with "Illegal option -". The blob
# in git is LF; this only strips a carriage return if one is present.
tr -d '\r' < autogen.sh > /tmp/autogen.sh
cp /tmp/autogen.sh autogen.sh
rm -f /tmp/autogen.sh

# Baseline from BUILD.md, plus Perl hooks. ncurses is detected
# automatically and builds qcc. libperl-dev is installed above.
./autogen.sh
./configure --prefix=/usr/local --enable-binkp --enable-perl
# The .l/.y in the tree define flaglex_reset. The shipped flaglex.c and
# flagexp.c do not. flex and bison are installed, so make will rebuild
# whichever input looks newer. Rebuilding only the parser leaves
# flaglex_reset undefined and the link fails. Drop both outputs first
# so they are regenerated as a pair.
rm -f src/flaglex.c src/flagexp.c src/flagexp.h

make -j"$(nproc)"

for bin in src/qico src/qctl src/qcc; do
	if [ ! -x "$bin" ]; then
		echo "missing binary: $bin" >&2
		exit 1
	fi
done
# configure keeps going if the libperl probe fails. Refuse a drop
# that was asked for --enable-perl but linked without perl hooks.
if [ ! -f src/perl.o ]; then
	echo "perl hooks were not compiled (src/perl.o missing)" >&2
	exit 1
fi

# AC_INIT([qico],[version],...) — the second bracketed field.
version="$(sed -n 's/^AC_INIT(\[qico\],\[\([^]]*\)\].*/\1/p' configure.ac)"
if [ -z "$version" ]; then
	echo "could not read version from configure.ac" >&2
	exit 1
fi

sha="$(git rev-parse --short=12 HEAD)"
when="$(TZ=UTC git log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M')"
arch="$(dpkg --print-architecture)"
name="qico-${version}-debian12-${arch}-${sha}"

stage="dist/${name}"
rm -rf dist
mkdir -p "$stage"

# Stage copies, then strip those copies. The build tree stays unstripped
# so a failed link is still debuggable in the Actions log.
install -m 755 src/qico src/qctl src/qcc "$stage/"
strip --strip-unneeded "$stage/qico" "$stage/qctl" "$stage/qcc"

install -m 644 README Changes \
	qico.conf.sample qico.passwd.sample qico.substs.sample \
	"$stage/"

# Classic BBS descriptor: ASCII, at most 10 lines, 45 columns.
# Written at pack time so the version and commit stay in step with the zip.
diz="$stage/file_id.diz"
{
	printf 'Qico %s Debian 12 (%s)\n' "$version" "$arch"
	printf 'FTN mailer: BinkP, EMSI, Hydra, Janus\n'
	printf 'Binaries: qico (perl), qctl, qcc\n'
	printf 'Source %s %s UTC\n' "$sha" "$when"
	printf 'Samples: conf, passwd, substs\n'
	printf 'Read README and Changes first.\n'
} > "$diz"

line_no=0
while IFS= read -r line || [ -n "$line" ]; do
	line_no=$((line_no + 1))
	# ${#line} is characters; the file is ASCII, so that is the column count.
	if [ "${#line}" -gt 45 ]; then
		echo "file_id.diz line $line_no is ${#line} columns (max 45): $line" >&2
		exit 1
	fi
done < "$diz"
if [ "$line_no" -gt 10 ]; then
	echo "file_id.diz has $line_no lines (max 10)" >&2
	exit 1
fi

# Paths inside the archive are relative to dist/, so unzip yields one directory.
( cd dist && zip -r -9 "${name}.zip" "$name" )

echo "packed dist/${name}.zip"
# Each staged binary must actually start. -v prints the ident and then
# the usage text, then exits 0, before qctl/qcc try to reach the daemon.
# A missing library or a bad link fails the job here.
for bin in qico qctl qcc; do
	echo "---- ${bin} -v ----"
	"$stage/$bin" -v
done
