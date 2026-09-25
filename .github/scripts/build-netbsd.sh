#!/bin/sh
# Build qico inside a NetBSD virtual machine and pack a binary drop.
#
# GitHub Actions checks the tree out on the Ubuntu runner, then boots a
# real NetBSD under QEMU and runs this script there (see netbsd.yml).
# configure executes test programs (AC_TRY_RUN), so the compiler and the
# tests must be the same NetBSD system. Do not cross-compile.
#
# NetBSD's /bin/sh is not bash: this file is POSIX sh, and the build uses
# gmake because the system make is not GNU make.
#
# On success the script leaves one zip under dist/:
#   qico-<version>-netbsd<major>-<arch>-<sha>.zip
# containing qico, qctl, qcc, file_id.diz, requirements.txt, README,
# Changes, LICENSE, and the three sample configs. systemd/ is left
# out: NetBSD uses rc.d, so those units would be dead weight here.
# The zip is the Actions artifact and a release asset.

set -eu

# One banner per phase. The whole script is a single Actions step, so
# without these the pkg_add, configure, and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

step "Install NetBSD build packages"
# pkgin is listed first because the base image may only have pkg_add.
# ncurses gives qcc, perl backs --enable-perl, zip packs the drop.
/usr/sbin/pkg_add -U pkgin git gmake autoconf automake pkgconf \
	flex bison ncurses perl zip

# The host owns the synced tree; git in the VM runs as another user and
# would otherwise refuse it ("dubious ownership"). The stamp in qico -v
# comes from `git log` during configure, so git must succeed.
step "Trust the synced git checkout"
git config --global --add safe.directory "$(pwd)"

# Windows checkouts rewrite autogen.sh to CRLF. sh then reads the shebang
# as "#!/bin/sh -e\r" and fails. The blob in git is LF; this only strips a
# carriage return if one is present.
step "Normalize autogen.sh to LF"
tr -d '\r' < autogen.sh > /tmp/autogen.sh
cp /tmp/autogen.sh autogen.sh
rm -f /tmp/autogen.sh
chmod +x autogen.sh

# pkgsrc installs under /usr/pkg, which the base compiler does not search.
# The ncurses headers sit in their own subdirectory, and -R records the
# library path in the binaries so they still start outside this build.
CPPFLAGS="-I/usr/pkg/include -I/usr/pkg/include/ncurses"
LDFLAGS="-L/usr/pkg/lib -Wl,-R/usr/pkg/lib"
export CPPFLAGS LDFLAGS

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

# System make is not GNU make; the Makefiles automake writes need gmake.
step "Compile"
gmake -j"$(sysctl -n hw.ncpu)"

step "Check that qico, qctl, qcc, and perl.o exist"
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
# uname -r is "11.0"; the drop is named after the major release only.
# uname -m is the NetBSD architecture name, amd64 on x86_64.
release="$(uname -r | cut -d. -f1)"
arch="$(uname -m)"
name="qico-${version}-netbsd${release}-${arch}-${sha}"

stage="dist/${name}"
rm -rf dist
mkdir -p "$stage"

# Stage copies, then strip those copies. The build tree stays unstripped
# so a failed link is still debuggable in the Actions log.
step "Stage stripped binaries, docs, and samples"
install -m 755 src/qico src/qctl src/qcc "$stage/"
strip "$stage/qico" "$stage/qctl" "$stage/qcc"

install -m 644 README Changes LICENSE \
	qico.conf.sample qico.passwd.sample qico.substs.sample \
	"$stage/"

# The dynamic libraries each binary was linked against, read from the ELF
# NEEDED entries rather than from the package list: this is what the target
# host actually has to provide. Most of these live under /usr/pkg, so the
# list doubles as the pkgsrc shopping list.
step "Write requirements.txt"
needed() {
	readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p'
}
{
	printf 'Runtime libraries for this build (NetBSD %s, %s).\n' "$release" "$arch"
	printf 'Install them on the target host before running.\n'
	for bin in qico qctl qcc; do
		printf '\n%s:\n' "$bin"
		needed "$stage/$bin" | sed 's/^/  /'
	done
} > "$stage/requirements.txt"

# The compiler that produced the binaries above, as "gcc 12.4.0". The last
# line of -v is "gcc version 12.4.0 (NetBSD ...)" and names the real
# compiler; --version would say "cc", since cc is the gcc driver here.
cc_bin="${CC:-cc}"
cc_ident="$("$cc_bin" -v 2>&1 | sed -n 's/^\(.*\) version \([0-9][^ ]*\).*/\1 \2/p' | tail -n 1)"
if [ -z "$cc_ident" ]; then
	cc_ident="unknown"
fi

# Classic BBS descriptor: ASCII, at most 10 lines, 45 columns.
# Written at pack time so the version and commit stay in step with the zip.
# Built here with LF so the width check below counts real columns; the
# file itself is converted to CRLF afterwards, as a DIZ is a DOS text file.
step "Write file_id.diz"
diz="$stage/file_id.diz"
diz_lf="$stage/.file_id.diz.lf"
{
	printf 'qico %s, netbsd %s (%s)\n' "$version" "$release" "$arch"
	printf '\n'
	printf 'FTN mailer: BinkP, ifcico, modem\n'
	printf 'Binaries: qico (perl), qctl, qcc\n'
	printf 'Source %s %s UTC\n' "$sha" "$when"
	printf 'Compiler: %s\n' "$cc_ident"
} > "$diz_lf"

line_no=0
while IFS= read -r line || [ -n "$line" ]; do
	line_no=$((line_no + 1))
	# ${#line} is characters; the file is ASCII, so that is the column count.
	if [ "${#line}" -gt 45 ]; then
		echo "file_id.diz line $line_no is ${#line} columns (max 45): $line" >&2
		exit 1
	fi
done < "$diz_lf"
if [ "$line_no" -gt 10 ]; then
	echo "file_id.diz has $line_no lines (max 10)" >&2
	exit 1
fi

# awk, not sed: a "\r" in a sed replacement is not portable across seds.
awk '{ printf "%s\r\n", $0 }' "$diz_lf" > "$diz"
rm -f "$diz_lf"

# Paths inside the archive are relative to dist/, so unzip yields one directory.
step "Pack ${name}.zip"
( cd dist && zip -r -9 "${name}.zip" "$name" )

echo "packed dist/${name}.zip"
# Each staged binary must actually start. -v prints the ident and then
# the usage text, then exits 0, before qctl/qcc try to reach the daemon.
# A missing library or a bad link fails the job here.
for bin in qico qctl qcc; do
	step "Run ${bin} -v"
	"$stage/$bin" -v
done
