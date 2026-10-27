# qctl — control tool for qico

`qctl` sends one command to an already running qico daemon and exits. It does not receive mail or place the call itself. The daemon must already be up (`qico -d` or `qico -f`). Leaving `qctl` leaves the mailer running.

This is the same control socket `qcc` uses. The behavior below follows the actual sources.

## Starting

```
qctl [-P port] [-a host] [-w password] <command> [node ...] [files ...]
```

| Option | Meaning |
|---|---|
| `-P port` | daemon port. Default is the `qicoui` service or port **60178** |
| `-a host` | host. Default is `localhost` |
| `-w password` | password. `-w-` prompts for the password in the terminal |
| `-h` | short option help |
| `-v` | version |

If the daemon requires a password and none is given, `qctl` exits. The first reply from the daemon must arrive within a few seconds.

Only one command is sent. If several command options are given, the last one is used.

A node address is `zone:net/node[.point]`, the same string the daemon parses. `qctl` does not fill in a missing zone or net the way the `qcc` address line does.

## Commands that need no node

| Option | Action |
|---|---|
| `-q` | stop the daemon |
| `-Q` | rescan the outbound |
| `-R` | reread the config. Open sessions keep running. A config that does not parse makes the daemon exit |
| `-o` | print the queue |

`-o` prints a table: address, mail size, file size, tries, and the same flag letters as the `qcc` queue.

## Poll, kill, info

| Option | Action |
|---|---|
| `-p[flavor]` | create a poll for each following node, then rescan |
| `-K` | remove each following node's outbound as if it had already been sent |
| `-f` | nodelist info for one node. Printed fields: address, station, place, sysop, phone, flags, speed, and work time |

Several nodes may follow `-p` and `-K`. `-f` uses one node.

The flavor letter is optional and sticks to `-p`:

| Letter | Flavor file |
|---|---|
| `n` | normal, `.flo`. Also used when the letter is omitted: the daemon's `pollflavor`, whose default is `n` |
| `c` | crash, `.clo` |
| `d` | direct, `.dlo` |
| `h` | hold, `.hlo` |
| `i` | immediate, `.ilo` |

`qctl -p 2:5020/104` polls with `pollflavor`. `qctl -pi 2:5020/104` is an immediate poll. After a poll the daemon clears the immediate flag on that node if it was set.

## File request and send

```
qctl -r node file ...
qctl -s[flavor] [-k] node file ...
```

`-r` requests those file names from `node`. Names with a space are passed as one argument; the daemon strips a surrounding pair of quotes.

`-s` attaches each file to `node` in the flavor given after `-s`. The letters are the same as for `-p`. Omitted, the flavor is again `pollflavor`.

Each name is checked on the machine where `qctl` runs: it must be a regular readable file. A relative name is turned into a full path. The daemon later opens that same path, so a remote `qctl -a` only works when the daemon can read it. A name that fails the check is skipped.

`-k` together with `-s` marks the file to be removed after a successful transfer. If the file is not writable, it is still attached and a warning says it will not be removed.

## Node status

```
qctl -x letters node
```

A capital letter sets the flag, a small letter clears it. Several letters can be written in one `-x`.

| Letter | Meaning |
|---|---|
| `U` / `u` | undialable / clear it and reset the try counter |
| `W` / `w` | pause everything / clear every pause |
| `I` / `i` | immediate / clear immediate |

This is not the timed hold. Hold for N minutes is the `h` key in `qcc`.

## Hangup

`-H tty` asks the daemon to hang up the session whose lock name is `tty`. The daemon sends SIGHUP to the pid in that lock file.

The check that requires a node for other commands also runs for hangup. `qctl -H ttyS0` alone prints the usage text and does not send the request. A following word satisfies the check and is not used as the tty name; the tty is the argument of `-H`.
