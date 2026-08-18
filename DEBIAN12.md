# Debian 12 / modern toolchain notes

This fork of [kosfango/qico](https://github.com/kosfango/qico) (qico 0.59.1)
includes build and BinkP fixes validated on Debian 12 (bookworm).

## Changes vs upstream `master`

1. **`configure.ac`** — add `-fcommon` so GCC 10+ can link legacy common symbols.
2. **`src/Makefile.am`** — move `@PERLLDFLAGS@` / `@QCCLIBS@` into `*_LDADD`
   so Perl and ncurses are actually linked with modern `ld`.
3. **`src/md5q.h`** — fix `UINT4` detection for 64-bit (BinkP MD5 auth).

Sample configs (`qico.conf.sample`, `qico.passwd.sample`, `qico.substs.sample`)
are unchanged from upstream.

## Quick build (Debian 12)

```bash
sudo apt-get install -y build-essential autoconf automake libtool pkg-config \
  flex bison libncurses-dev libperl-dev
./autogen.sh
./configure --prefix=/usr/local --enable-binkp
make -j"$(nproc)"
sudo make install
```

Binaries: `qico` (sbin), `qcc` / `qctl` (bin).

## qcc

Start the daemon first, then the UI. Port and password come from your
`qico.conf` (`server` / `serverpwd`), for example:

```bash
qico -I/path/to/qico.conf -d
qcc -P 60178 -w 'your-serverpwd'
```
