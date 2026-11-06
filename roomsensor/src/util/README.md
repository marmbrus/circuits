RoomSensor Serial Utilities
===========================

Python utilities to interact with the RoomSensor ESP32S3 over the serial console for programming, debugging, and testing.

Quickstart
----------

1) Create and activate a virtual environment

```bash
python3 -m venv .venv
source .venv/bin/activate
```

2) Install package (editable) and dev deps

```bash
pip install -e .
```

3) Run tests

```bash
pytest -q
```

Port selection
--------------

By default the library auto-selects the first serial port whose name or description contains "usbserial". You can override with environment variable `ROOMSENSOR_SERIAL_PORT` or by passing `port="/dev/tty.usbserial-XXXX"` to the class.

Notes
-----

- On connect, the library performs a hardware reset using RTS/DTR to place the device in a known state.
- All output is captured in an in-memory buffer and also logged to the console for debugging.


Board certificates
------------------

Each board has a certificate of its own, which it presents to the house's MQTT broker (brayden
repo, `docker/smll/mqtt`, TLS listener on 8883) and to `updates.acgt.net` for OTA updates
(`docker/smll/auth`). Both take only certificates from one of the house's authorities, each a
YubiKey holding the CA's key in PIV slot 9a (brayden's `docker/smll/yubikey/README.md`).

```bash
python provision.py --program --device-cert
```

reads the board's name from its console (`device_cert status`) and makes an EC P-256 key for it.
It sends Brayden the public key and the name (`POST https://brayden.acgt.net/api/security/boards/requests`,
from the house only), and prints the key's fingerprint and the dashboard's link. On the Security
tab's MQTT subtab, under Board certificates, check the fingerprint matches and approve: that takes
the Google sign-in and a house YubiKey plugged into smll (or signing from a Mac). Brayden signs the
certificate, and `provision.py`, which asks every 2 s for up to 11 minutes, writes the key, the
certificate and the house's authorities to the board's NVS (namespace `device_cert`). The key isn't
kept anywhere else. The certificate has `CN=ESP_<mac>` (the station MAC as in the `sensor/<mac>/`
topics) and `O=acgt.net`, is good for client authentication only, and lasts five years, like
Brayden's own. The board uses it from its next boot.

- `--yubikey [<serial>]` signs with a YubiKey plugged into this computer instead (`pip install -e
  '.[yubikey]'`, which needs `swig` and `libpcsclite-dev` to build `pyscard`); the serial picks one
  when more than one is plugged in. The board then trusts only that YubiKey's CA for the broker.
- `--brayden <url>` asks another Brayden (default `https://brayden.acgt.net`).
- `--mqtt-ca <file>` sets the authorities the board checks the broker's certificate against,
  instead of the ones Brayden sends. (`updates.acgt.net` has a public certificate, which the board
  checks with ESP-IDF's bundle.)

OTA always goes to `updates.acgt.net`, which refuses a board without a certificate, so a board
needs one before it can update itself. `deploy_ota.sh` uploads there (smll's `/home/brayden/updates`,
which needs the `brayden` group).

For MQTT, set the broker to the TLS listener: `"mqtt_broker": "mqtts://mqtt.acgt.net:8883"` in the
`wifi` namespace. The host name must be one in the broker's certificate (`mqtt.acgt.net`, `smll`,
`10.10.0.197`). With an `mqtt://` broker the certificate isn't used for MQTT. The broker and Brayden
name the board `O=acgt.net,CN=ESP_<mac>`, and Brayden makes its broker account, limited to
`sensor/<mac>/#`, the first time it connects.

`device_cert status` on the console shows the board's name and its certificate, and
`device_cert erase` removes it.
