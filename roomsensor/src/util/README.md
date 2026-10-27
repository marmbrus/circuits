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


MQTT client certificates
------------------------

The house's MQTT broker (brayden repo, `docker/smll/mqtt`) has a TLS listener on 8883 that only
takes clients with a certificate from one of the house's authorities. Each authority is a YubiKey
holding the CA's key in PIV slot 9a (brayden's `docker/smll/yubikey/README.md`). With one of them
plugged into the computer provisioning the board:

```bash
python provision.py --program --mqtt-cert
```

reads the board's name from its console (`mqtt_tls status`), makes an EC P-256 key and a
certificate for it signed by the YubiKey, and writes the key, the certificate and the authorities
to the board's NVS (namespace `mqtt_tls`). The key isn't kept anywhere else. The certificate has
`CN=ESP_<mac>` (the station MAC as in the `sensor/<mac>/` topics) and `O=acgt.net`, is good for
client authentication only, and lasts five years, like Brayden's own.

- `--yubikey <serial>` picks the YubiKey when more than one is plugged in.
- `--mqtt-ca <file>` sets the authorities the board checks the broker's certificate against. By
  default it's only the YubiKey's own CA certificate. To trust every house authority (for example
  when the other YubiKey might have signed the broker's certificate), copy
  `~/.brayden/config/security/mqtt/broker/ca.pem` from smll.

To connect with it, set the broker to the TLS listener, e.g. `"mqtt_broker": "mqtts://mqtt.acgt.net:8883"`
in the `wifi` namespace. The host name must be one in the broker's certificate (`mqtt.acgt.net`,
`smll`, `10.10.0.197`). The broker names the board `O=acgt.net,CN=ESP_<mac>`. It connects without an
account but can't publish or subscribe until it has a certificate account on Brayden's Security tab.

With an `mqtt://` broker the certificate is kept but not used. `mqtt_tls status` on the console shows
the board's name and its certificate, and `mqtt_tls erase` removes it.
