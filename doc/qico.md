# qico — FTN mailer

`qico` is an FTN mailer. It originates calls from the outbound, answers incoming sessions, and keeps a control socket for `qcc` and `qctl`. It speaks EMSI and, when Binkp is compiled in, Binkp, over a modem or TCP/IP.

The behavior below follows the actual sources. The configuration language is `qico.conf.sample`.

## Starting

```
qico [-d | -f | -a type | -c[NIA] | -n | -t | -T] [-b] [-i host] [-I file] [node] [line]
```

| Option | Meaning |
|---|---|
| `-d` | daemon. Forks to the background and originates from the outbound |
| `-f` | the same loop, in the foreground |
| `-a type` | answer one incoming session. `auto`, `emsi` (also `**emsi` or `**EMSI_INQC816`), and, when built in, `binkp` |
| `-c[NIA]` | call `node` now and exit. See [One call](#one-call) |
| `-i host` | TCP/IP call to `host`. `node` is required. Does not go through the daemon queue |
| `-b` | use Binkp for `-c` and `-i`. Absent when Binkp was not compiled in. Without `-b` the call is EMSI |
| `-n` | compile the nodelist index and exit |
| `-t` | read the config and exit. Prints `Config file is OK`, or exits with an error |
| `-T` | the same check, with verbose config output |
| `-I file` | config file. A stock build uses `/usr/local/etc/qico.conf`; `--with-config` or `--sysconfdir` changes that path |
| `-h` | short option help |
| `-v` | version |

With no mode and no `-i`, `qico` prints the option help and exits.

Every mode reads the config first. A bad file is reported on stderr and, when `masterlog` names a file, appended there. The process then exits. `address` is required. At least one outbound directory must be set.

## Daemon

`qico -d` is the process that places outbound calls. `qico -f` is that same process left in the foreground: `-f` only skips the fork. A second daemon exits with `another daemon exists`.

The daemon scans the outbound, dials when a node may be called, and listens for `qcc` and `qctl`. It does not answer the phone and it does not accept incoming IP itself. An incoming session is a separate `qico -a`, started by getty, mgetty, or inetd.

Modem origination is on only when the config has at least one `port` line. With no `port` lines the daemon does not open a serial port; IP calls from `subst` hosts still go out. Incoming analog (`qico -a`) does not use `port`.

## Answer

`qico -a type` handles the session already connected to its standard I/O and then exits.

| `type` | Session |
|---|---|
| `auto` | try EMSI. Any type other than `emsi` and `binkp` does the same |
| `emsi`, `**emsi`, `**EMSI_INQC816` | EMSI, without the initial exchange |
| `binkp` | Binkp. Only when Binkp is compiled in |

## One call

`-c` and `-i` place one call from this process and exit. They are not a request to a running daemon. To queue a call for the daemon, use `qctl -p` or `qcc`.

```
qico -c[NIA] zone:net/node[.point] [line]
qico -i host -b zone:net/node[.point]
```

The letters after `-c` may be combined:

| Letter | Effect |
|---|---|
| `N` | normal call. Replaces any flags already set |
| `I` | immediate: do not check the node's work time |
| `A` | any free port: do not check `cancall` |

`line` is optional and is counted from 1. Omit it and qico picks a line.

`-i host` calls that host over TCP. The node address is still required. `-b` selects Binkp; without it the call is EMSI. The hostname is not read from `subst` for this command.

## Nodelist and the config check

`qico -n` rebuilds the nodelist index and exits.

`qico -t` and `qico -T` only load the config. `-T` is the verbose form. A config that cannot be parsed, or a config with no outbound, exits with an error instead of `Config file is OK`.

Keywords, `if` blocks, and includes are described in `qico.conf.sample`. Two time forms from that file: `Txy` is UTC, a numeric range such as `01:20-20:15` is local time.
