# qcc — qico control center

`qcc` is an ncurses client for an already running qico daemon. It does not receive mail or place calls itself. It connects to the daemon socket, shows the queue, the log, and the sessions, and sends commands to the daemon. Leaving `qcc` leaves the mailer running.

The behavior below follows the actual sources.

## Starting

```
qcc [-P port] [-a host] [-w password] [-B path] [-n] [-h] [-v]
```

| Option | Meaning |
|---|---|
| `-P port` | daemon port. Default is the `qicoui` service or port **60178** |
| `-a host` | host. Default is `localhost` |
| `-w password` | password. `-w-` prompts for the password in the terminal |
| `-B path` | today's sessions from this binlog. The reader is built by default. `./configure --disable-qcc-binlog` leaves `-B` and the `l`/`L` keys out of `qcc`. The mailer's own binlog writer is unchanged |
| `-n` | silent mode, no beep |
| `-h` | short option help |
| `-v` | version |

If the daemon requires a password and none is given, `qcc` exits immediately. After a successful login the client registers for events and starts receiving the queue, the log, and session updates.

## Screen

Five bands, top to bottom:

1. **Header** — `[index/total]`, window name (`master` or a line), session title, clock on the right.
2. **Upper window** (about 10 lines; shorter on a small terminal) — the queue, a session card, or the chat transcript. On the session log, the card of the selected row.
3. **Lower window** — the log. The `master` log only appends. A line window keeps the last **256** lines and can scroll them. On the session log, the list covers the master log.
4. **Status line** — the daemon's current phase (`Waiting N…`, protocol stage, and similar). On the session log, totals of the sessions loaded for today.
5. **Key hint** and the `qcc-…` version. The hint changes with the window.

Letters in the bottom hint are often drawn in uppercase. The program is case-sensitive. The tables below and the **F1** help give the actual case. On master and on a line, **F1** is printed into that window's log. On the session log, **F1** replaces the list.

Windows are slots. **Master** (the daemon queue) is its own window. Each modem line and each IP session is a window. Chat is an extra window derived from a line: the line name is rewritten with a `CHT…` or `IP…` prefix.

## Switching windows

| Key | Action |
|---|---|
| `1`…`9` | line window with that number. Header `[1/…]` is key `1` |
| `0` or `` ` `` | back to master |
| **Tab** | next window; after the last one, back to master |
| **Right** | next window; stops on the last one |
| **Left** | previous window; stops on master |
| **F10**, `q` | quit qcc |
| **F1** | short help. Into the log, or into the session-log list when that view is open |
| **Ctrl+L** | redraw the screen |

In a queue row, `>` is the cursor and `=` on the left means a live session already exists for that address. A bold row means the node is being dialed.

Queue columns: address, netmail size, file size (including freq), try count, flags. An empty queue is shown as `* Empty queue *`.

## Queue flags

Letters from left to right. A dot in a letter's place means the flag is off.

| Letter | Meaning |
|---|---|
| `N` | normal, ordinary poll |
| `H` | hold, will not be called |
| `D` | direct |
| `C` | crash |
| `I` | immediate, call now |
| `R` | a file request is queued |
| `U` | undialable, the node is excluded from dialing |
| `d` | dialing in progress |
| `r` | paused while the queue holds only a freq |
| `x` | paused while the queue holds only arcmail |
| `w` | operator pause on everything, including a timed hold |

`N`, `H`, `D`, `C`, and `R` come from outbound files. The keyboard changes `I`, `U`, and `w`.

## Master: the queue

| Key | Action |
|---|---|
| **Up / Down** | move the cursor through the queue |
| **PgUp / PgDn** | one page |
| **Home / End** | first / last row. On the session log they do nothing |
| `l` / `L` | open or close today's session log. Master only. See [Session log](#session-log) |
| `r` | rescan the outbound |
| `R` | reread the config. Open sessions keep running |
| **Space** | reset the dial delay: the daemon looks for the next call immediately |
| `p` | poll. Asks for an address |
| `f` / `F` | nodelist info. `F` uses the node under the cursor, `f` asks for an address. On the session log these keys are the failed-only filter instead |
| `e` / `E` | file request. `E` uses the current node, `e` asks for an address |
| `s` / `S` | attach a file to the outbound. `S` uses the current node and flavor normal |
| `k` / `K` | remove a node's outbound as if it had already been sent |
| `I` / `i` | set / clear immediate |
| `U` / `u` | set undialable / clear it and reset the try counter |
| `W` / `w` | pause everything / clear every pause (`w`, `r`, and `x`) |
| `h` | hold for N minutes (asks for the number). Cleared with `w` |

The bottom hint draws hold as `H` and poll as `P`. The keys that work are lowercase `h` and `p`.

### Address input

The address is completed from the primary AKA the daemon sends at connect time. With AKA `2:203/910`:

| Typed | Result |
|---|---|
| `.99` | `2:203/910.99` |
| `.1` | `2:203/910.1` |
| `910.3` | `2:203/910.3` |
| `201/127` | `2:201/127` |
| `2:5030/1374` | `2:5030/1374` |

A field written as zero keeps the corresponding part of the AKA. With AKA `2:203/910`, both `0` and `2:203/0` become `2:203/910`. The uplink `2:203/0` cannot be entered in this line.

Allowed characters are digits, `:`, `/`, `.`, space, `@`, and the flavor letters `i`, `c`, `d`, `h`, `n`, `y`. A number above 32767 is rejected. An empty **Enter** cancels the command.

### Poll (`p`)

A flavor letter may be included in the address:

| Letter | Flavor |
|---|---|
| `n` | normal. Used when no letter is typed |
| `c` | crash |
| `d` | direct |
| `h` | hold |
| `i` | immediate |

Example: `i201/127` with AKA `2:203/910` is an immediate poll of node `2:201/127`. `d5030/1374` is a direct poll of node `2:5030/1374`.

### File request (`e` / `E`)

After the address, the prompt is `File(s) for request:`. Several names are separated by spaces. A name that contains a space is wrapped in quotes. For `e` the flavor letter must stay `n`: type the address without `c`, `d`, `h`, or `i`. Any other letter cancels the command.

### Send (`s` / `S`)

The prompt asks for a full file path. The file must be readable on the machine where the daemon runs. If the file is not readable, a warning is written to the log and the command is still sent. Spaces in the name are replaced with `?`. For `s` the flavor comes from the letter in the address (`n`, `c`, `d`, `h`, `i`). For `S` the flavor is always normal and the address is the node under the cursor.

### Kill (`k` / `K`)

Confirmation is the letter `y` in the same input line.

- `K` asks about the node under the cursor; answer `y`.
- `k` takes the address together with `y`, for example `y.99` for `2:203/910.99` or `y201/127` for `2:201/127`.

The daemon walks the outbound files the same way it does after a successful transfer: deletion, `^`-file handling, and the other "file was sent" actions.

### Info (`f` / `F`)

Taken from the nodelist only, without substs. The log receives the address, station, location, sysop, phone, flags, work time, and speed.

## Session log

`l` or `L` on the master window toggles it. It is not another slot: **Tab** still moves between master and the line windows. The header name becomes `Session log`. The queue stays in memory and comes back when you leave.

`qcc` reads the binlog itself. The daemon is not asked, and nothing is added to the wire protocol. The path comes from `-B`, not from the daemon's `binlog` keyword. `qcc -a` on another machine sees the history only when that path is visible there. Without `-B`, `l`/`L` beeps and the master log receives `session history needs -B path`. The file is not opened.

Nothing is read until you first open the screen with `l`/`L`. The list is the local calendar day, from local midnight through now, oldest first, and at most **4096** sessions. A short tail (a binlog record still being written) is left until the next check.

While the view is open and the current window is master, the same once-a-second tick that redraws the clock checks the file size. An unchanged size does not reread the records. Growth appends only the new tail. A shorter or empty file, from rotation or from the daemon repairing the tail, reloads the day. Moving to a line window hides the list and stops the check; the rows already loaded stay. Coming back to master checks the size and reads what was appended in the meantime. Closing with `l`/`L` stops the check as well. Opening it again reads only that new tail, unless the file shrank. If the cursor was on the last row, it follows sessions that were just appended.

### List

| Column | Meaning |
|---|---|
| Start Time | local `DD/MM HH:MM` |
| DPLA | four flags. An unset flag is a space |
| Address | the point is printed only when it is not zero. All four fields zero is `Unresolved`. The column is as wide as the longest address now loaded, and never narrower than the word Address |
| Rcv, Snt | received, then sent. Always two columns |
| Info | whatever width is left |

DPLA, left to right:

| Letter | Meaning |
|---|---|
| `I` or `O` | inbound or outbound |
| `P` | a password was used |
| `L` | the node was listed |
| `A` | the session did not finish cleanly. Success is a space, and `A` is red |

The address is cyan. The cursor row keeps the highlight bar, so that row is not recolored. A line wider than the window replaces its last visible character with `.`.

Sizes are shown as b/k/G/M so they stay easy to read.

Info joins the non-empty fields with `"; "`. A failed session puts the result first, then peer, system, location, and sysop. A successful session skips the result.

### Card

The upper window shows the details of the session under the cursor.

### Keys

| Key | Action |
|---|---|
| **F1** | replace the list with this screen's help. An arrow, or **F1** again, brings the list back |
| `l` / `L` | leave the session log |
| **Up / Down** | previous or next row that is still shown |
| **PgUp / PgDn** | a page of shown rows |
| `Z` / `z` | hide sessions with no bytes either way. Press again to show them |
| `F` / `f` | failed sessions only. Not nodelist info on this screen |
| `q` | quit qcc |

`Z` and `F` may both be on: failed sessions that moved some traffic. Either limit stays until `qcc` accepts a change in the file, which is a new session, a shorter file, or the next local day. Leaving the screen, or moving to a line, does not clear them. An unfinished tail does not clear them either. The status totals still count every loaded session, including hidden rows.

## Line window

Before the handshake the upper window is empty and the lower window is the line log. After EMSI or Binkp the upper window is the session card:

- system name and location, sysop;
- speed and flags, phone;
- addresses;
- `[pwd]` — the session had a password;
- `[lst]` — the node is in the nodelist;
- session duration.

Below that are two progress blocks: receive on the left, send on the right. Each shows file N of M, the name, cps, a bar for the current file, and a bar for the total with an estimate of the time left.

| Key | Action |
|---|---|
| **Up / Down** | scroll the log by 1 line |
| **Ins / Del** | scroll the log by 2 lines |
| **PgUp / PgDn** | scroll the log by one page |
| **Home / End** | start / end of the buffer (256 lines) |
| `h` | hang up. The session receives an operator hangup. A plain modem line also gets SIGHUP sent to the process recorded in the port lock file. Binkp and ifcico receive the session hangup |
| `X` | skip the file currently being received. Active session only |
| `R` | suspend the current file. Active session only |
| `c` | open chat when the protocol supports it (Hydra, Zmodem, Binkp) and a session is already up |
| **F8** | drop this window from the screen. The session continues; the window comes back with the next event from that line |
| **Space** | same as on master: reset the daemon's dial delay |
| `q` | quit qcc |

While there is no session, `X`, `R`, and `c` are dimmed in the hint and beep. The hint draws hangup and chat as `H` and `C`; the keys that work are lowercase `h` and `c`.

## Chat

`c` on a line opens a separate window. The upper half is the remote transcript, with a blinking `_` at the end. The lower half is what you type. An incoming bell is shown as `*` and produces a beep and a screen flash.

| Key | Action |
|---|---|
| ordinary characters | sent to the chat immediately and echoed below |
| **Enter** | newline |
| **Backspace** | backspace |
| **F2** | send a whole string (the `Text:` editor) |
| **F4** | send a bell |
| **Esc**, **F8** | close the chat |

**Tab** in chat switches slots the same way it does in every other window.

If the chat window is the first slot (key `1`, header `[1/…]`, with no other line present), typed characters are not sent: the chat handler starts at the second slot. The usual order — a line window first, then a separate chat window — does not hit that path.

## Input line

The same editor is used for addresses, file names, and hold minutes. While it is open, the bottom hint changes.

| Key | Action |
|---|---|
| **Ins** | insert / overwrite |
| **Left / Right**, **Home / End** | move the cursor |
| **Backspace**, **Del** | delete a character |
| **Up / Down** | history, up to 50 lines. Addresses, free text, and numbers are stored separately |
| **PgUp** | oldest line of this kind |
| **PgDn** | clear the field |
| **Enter** | accept |
| **Esc**, **F8** | cancel |

Field length depends on the mode: an address is up to 40 characters, hold minutes are up to 5 digits, and a file name or chat text is a long line.
