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



### BSD

Use **gmake** (system `make` is not GNU). OpenBSD also needs GNU
autoconf/automake selected via `AUTOCONF_VERSION` / `AUTOMAKE_VERSION`.
This tree was not live-tested on BSD.

## qcc

Start the daemon first, then the UI. Port and password come from your
`qico.conf` (`server` / `serverpwd`), for example:

```bash
qico -I/path/to/qico.conf -d
qcc -P 60178 -w 'your-serverpwd'
```



## systemd

See [systemd/README.md](systemd/README.md). Install `systemd/qico.service` on the host.
