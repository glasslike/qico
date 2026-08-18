# Running qico under systemd

## Install (Debian 12+)

```bash
sudo cp systemd/qico.service /etc/systemd/system/qico.service
sudo systemctl daemon-reload
sudo systemctl enable --now qico.service
```

The unit assumes:

- binaries in `/home/map/ftn/usr/bin`
- config at `/home/map/ftn/usr/etc/qico/qico.conf`
- spool under `/home/map/ftn/fido`
- service user/group `map` (change `User=` / `Group=` if needed)

## Commands

```bash
sudo systemctl status qico
sudo systemctl restart qico
sudo systemctl reload qico    # SIGHUP — reread config
sudo systemctl stop qico
sudo journalctl -u qico -f
```

## UI

After the service is running:

```bash
qcc -P <server-port> -w '<serverpwd>'
```

Port and password come from `server` / `serverpwd` in your `qico.conf`
(not from this repository).

## Design

- Starts with **`-f` (foreground)** and `Type=simple` so systemd tracks the
  main process (preferred over classic `-d` + forking).
- `ExecStartPre=... -t` refuses to start if the config fails validation.
- Reload uses `SIGHUP` (no UI password in the unit file).
