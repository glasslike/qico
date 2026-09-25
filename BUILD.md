# Debian 12 / modern toolchain notes

The commands below are the canonical recipe (Debian 12). The same
`./autogen.sh && ./configure && make` sequence also compiled and tested on Debian 13, Ubuntu 24.04, Fedora 44, and Alpine 3.20 (musl), Raspberry Pi OS (aarch64).

## Quick build (Debian 12)

```bash
sudo apt-get install -y build-essential autoconf automake libtool pkg-config \
  flex bison libncurses-dev libperl-dev

mkdir -p ~/src
cd ~/src
git clone https://github.com/glasslike/qico.git
cd qico

./autogen.sh
./configure --prefix=/usr/local --enable-binkp
make -j"$(nproc)"
sudo make install
```



## Configure flags

Baseline (what Quick build uses): `--enable-binkp`.

Optional: `--enable-perl` (needs `libperl-dev` / `perl-devel` / `perl-dev`)
and `--enable-hydra8k`. `qcc` is built when ncurses is found.

Extended `configure` can also pin install paths and turn Perl on:

```bash
./configure --prefix=/home/map/ftn/usr \
  --bindir=/home/map/ftn/usr/bin \
  --sbindir=/home/map/ftn/usr/bin \
  --sysconfdir=/home/map/ftn/usr/etc/qico \
  --enable-binkp --enable-perl
```

Binaries: `qico` (sbin), `qcc` / `qctl` (bin).

## Other Unixes

Only package names and the make binary change. Do not cross-compile:
`configure` runs native test programs (`AC_TRY_RUN`). Leave the shipped
`src/flaglex.c` / `src/flagexp.c` alone unless you mean to regenerate them.

### Fedora (`dnf`)

```bash
sudo dnf install -y gcc make autoconf automake pkgconf flex bison \
  ncurses-devel perl perl-devel perl-ExtUtils-Embed
```

Then the same `./autogen.sh`, `./configure`, `make`.

### Alpine (`apk`)

The pkg-config package is named `pkgconfig`.

```bash
sudo apk add build-base autoconf automake pkgconfig flex bison \
  ncurses-dev m4 perl perl-dev
```



### Gentoo

Use the overlay [glasslike/qico-gentoo-overlay](https://github.com/glasslike/qico-gentoo-overlay).
It builds `net-ftn/qico` from this repository (release tags, or `master`
for the live ebuild `qico-9999`) and installs an OpenRC script, optional
xinetd files and optional systemd units.

```bash
sudo emerge --ask app-eselect/eselect-repository dev-vcs/git
sudo eselect repository add qico-gentoo-overlay git https://github.com/glasslike/qico-gentoo-overlay.git
sudo emaint sync -r qico-gentoo-overlay
echo "net-ftn/qico ~amd64" | sudo tee /etc/portage/package.accept_keywords/qico
sudo emerge --ask net-ftn/qico
```

USE flags, custom directories and the move from the older qico-xe overlay
are described in the overlay's README.

### NetBSD (`pkgsrc`)

Built in CI on NetBSD 11 (amd64) by `.github/scripts/build-netbsd.sh`,
which follows the same steps. Three things differ from Linux:

- Packages come from pkgsrc and install under `/usr/pkg`, which the base
  compiler does not search. `CPPFLAGS` and `LDFLAGS` point it there; `-R`
  records the path in the binaries so they start outside the build shell.
- Use **gmake**; the system `make` is not GNU make.
- Remove the shipped `src/flaglex.c`, `src/flagexp.c` and `src/flagexp.h`
  before compiling. flex and bison are installed, so make regenerates
  whichever of the pair looks older, and a half-regenerated pair fails to
  link (`flaglex_reset` undefined). Removing all three rebuilds them together.

NetBSD's `/bin/sh` is not bash; the commands below are plain POSIX sh.
Package installation and `gmake install` need root (`su root -c '...'`).

```sh
su root -c 'pkg_add -U pkgin git gmake autoconf automake pkgconf flex bison ncurses'

mkdir -p ~/src
cd ~/src
git clone https://github.com/glasslike/qico.git
cd qico

export CPPFLAGS="-I/usr/pkg/include -I/usr/pkg/include/ncurses"
export LDFLAGS="-L/usr/pkg/lib -Wl,-R/usr/pkg/lib"

./autogen.sh
./configure --prefix=/usr/local --enable-binkp
rm -f src/flaglex.c src/flagexp.c src/flagexp.h
gmake -j"$(sysctl -n hw.ncpu)"
su root -c 'gmake install'
```

If `pkgin` is already set up, `pkgin install` takes the same package list.
`ncurses` is only needed for `qcc`; `qico` and `qctl` use base system
libraries.

**Perl hooks on NetBSD.** pkgsrc keeps `libperl.so` in perl's own `CORE`
directory, whose name contains the perl version
(`/usr/pkg/lib/perl5/5.44.0/x86_64-netbsd-thread-multi/CORE`), not in
`/usr/pkg/lib`. Add that directory to the rpath, or `qico` fails at start
with `Shared object "libperl.so" not found`:

```sh
perl_core="$(perl -MConfig -e 'print $Config{archlib}')/CORE"
export LDFLAGS="-L/usr/pkg/lib -Wl,-R/usr/pkg/lib -Wl,-R${perl_core}"
./configure --prefix=/usr/local --enable-binkp --enable-perl
```

Such a binary only runs with that exact pkgsrc perl release; rebuild qico
after a perl update. This is why the NetBSD release zips are built without
Perl hooks.

### Other BSDs

Use **gmake** (system `make` is not GNU). OpenBSD also needs GNU
autoconf/automake selected via `AUTOCONF_VERSION` / `AUTOMAKE_VERSION`.
FreeBSD and OpenBSD were not live-tested.

## qcc

Start the daemon first, then the UI. Port and password come from your
`qico.conf` (`server` / `serverpwd`), for example:

```bash
qico -I/path/to/qico.conf -d
qcc -P 60178 -w 'your-serverpwd'
```



## systemd

See [systemd/README.md](systemd/README.md). Install `systemd/qico.service` on the host.
