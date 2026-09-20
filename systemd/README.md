# Running qico under systemd

Unit files in this directory are for Debian / Raspberry Pi OS (`systemd`).

| File | Purpose |
|------|---------|
| `qico.target` | **Enable this** — starts daemon + BinkP and ifcico inbound together |
| `qico.service` | Outbound queue / originate (`qico -f`) |
| `qico-binkp.socket` | Listen TCP **24554** |
| `qico-binkp@.service` | Per-connection BinkP answer (`qico -abinkp`) — do not enable directly |
| `qico-ifc.socket` | Listen TCP **60179** |
| `qico-ifc@.service` | Per-connection ifcico answer (`qico -aauto`) — do not enable directly |

Paths baked into the units:

| Role | Path |
|------|------|
| Binary | `/home/map/ftn/usr/bin/qico` |
| Config | `/home/map/ftn/usr/etc/qico/qico.conf` |
| Spool | `/home/map/ftn/fido` |

## Production install

Stop **binkd** first so TCP 24554 is free. Port 60179 must also be free
(no leftover ifcico/inetd listener).

```bash
sudo cp systemd/qico.target \
        systemd/qico.service \
        systemd/qico-binkp.socket \
        systemd/qico-binkp@.service \
        systemd/qico-ifc.socket \
        systemd/qico-ifc@.service \
        /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now qico.target
```

To keep **binkd** as the boot default, do not enable the target.
Stop binkd first, then `systemctl start qico.target` (all three roles).
`systemctl stop qico.target` frees 24554 and 60179 again.

Verify:

```bash
systemctl status qico.target qico.service qico-binkp.socket qico-ifc.socket
ss -ltnp | grep -E '24554|60179|60178'
```

## Everyday commands

```bash
sudo systemctl status qico.target
sudo systemctl start qico.target
sudo systemctl stop qico.target      # stops daemon + closes 24554 and 60179
sudo systemctl restart qico.target

sudo systemctl reload qico.service   # SIGHUP → reread config
sudo journalctl -u qico -u 'qico-binkp@*' -u 'qico-ifc@*' -f
```

## UI / control (after the target is up)

```bash
/home/map/ftn/usr/bin/qcc -P 60178 -w 'your-serverpwd'
/home/map/ftn/usr/bin/qctl -P 60178 -w 'your-serverpwd' -o
```

Port and password come from `server` / `serverpwd` in
`/home/map/ftn/usr/etc/qico/qico.conf` (UI control, not session passwords).

## Design notes

- One **target**, three **roles**: originate daemon, inetd-style BinkP
  answer, inetd-style ifcico answer. qico cannot listen on 24554/60179
  inside a single `-d`/`-f` process (unlike binkd).
- Daemon uses **`-f` (foreground)** so systemd tracks it (`Type=simple`).
- `ExecStartPre=... -t` refuses to start if the config is invalid.
- Reload uses `SIGHUP` on `qico.service` (no `qctl` password in unit files).
- `PartOf=qico.target` makes `stop qico.target` tear down all pieces.
