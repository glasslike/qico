# Running qico under systemd (production: user `map`, node 2:203/910)

Unit files in this directory are for Debian / Raspberry Pi OS (`systemd`).

| File | Purpose |
|------|---------|
| `qico.target` | **Enable this** — starts daemon + BinkP inbound together |
| `qico.service` | Outbound queue / originate (`qico -f`) |
| `qico-binkp.socket` | Listen TCP **24554** |
| `qico-binkp@.service` | Per-connection answer (`qico -abinkp`) — do not enable directly |
| `qico.service.d/lab-user.conf` | Lab VM only (`User=osboxes`) — **not** for production |

Paths baked into the units:

| Role | Path |
|------|------|
| Binary | `/home/map/ftn/usr/bin/qico` |
| Config | `/home/map/ftn/usr/etc/qico/qico.conf` |
| Spool | `/home/map/ftn/fido` |

## Production install

Stop **binkd** first so TCP 24554 is free.

```bash
sudo cp systemd/qico.target \
        systemd/qico.service \
        systemd/qico-binkp.socket \
        systemd/qico-binkp@.service \
        /etc/systemd/system/
# Do NOT install lab-user.conf on production
sudo systemctl daemon-reload
sudo systemctl enable --now qico.target
```

Verify:

```bash
systemctl status qico.target qico.service qico-binkp.socket
ss -ltnp | grep -E '24554|60178'
```

## Everyday commands

```bash
sudo systemctl status qico.target
sudo systemctl start qico.target
sudo systemctl stop qico.target      # stops daemon + closes 24554
sudo systemctl restart qico.target

sudo systemctl reload qico.service   # SIGHUP → reread config
sudo journalctl -u qico -u 'qico-binkp@*' -f
```

## UI / control (after the target is up)

```bash
/home/map/ftn/usr/bin/qcc -P 60178 -w 'your-serverpwd'
/home/map/ftn/usr/bin/qctl -P 60178 -w 'your-serverpwd' -o
```

Port and password come from `server` / `serverpwd` in
`/home/map/ftn/usr/etc/qico/qico.conf` (UI control, not BinkP link passwords).

## Design notes

- One **target**, two **roles**: originate daemon vs inetd-style BinkP answer.
  qico cannot do both in a single `-d`/`-f` process (unlike binkd).
- Daemon uses **`-f` (foreground)** so systemd tracks it (`Type=simple`).
- `ExecStartPre=... -t` refuses to start if the config is invalid.
- Reload uses `SIGHUP` on `qico.service` (no `qctl` password in unit files).
- `PartOf=qico.target` makes `stop qico.target` tear down both pieces.
