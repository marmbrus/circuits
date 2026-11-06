"""The certificate a board gets, signed by a software stand-in for the YubiKey's CA."""
import datetime

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

from roomsensor_util import device_cert


@pytest.fixture(scope="module")
def authority():
    """A CA like a YubiKey's (brayden's remake_ca.py): P-256, CN=<name>, OU=YubiKey <serial>, O=acgt.net."""
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, "TestYubiKey"),
        x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, "YubiKey 12345678"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "acgt.net"),
    ])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (
        x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=20 * 365))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .sign(key, hashes.SHA256())
    )
    return cert, lambda builder: builder.sign(key, hashes.SHA256())


def test_common_name():
    assert device_cert.common_name("AA:BB:CC:00:11:22") == "ESP_aabbcc001122"
    assert device_cert.common_name("aabbcc001122") == "ESP_aabbcc001122"
    with pytest.raises(ValueError):
        device_cert.common_name("aabbcc00112")
    with pytest.raises(ValueError):
        device_cert.common_name("aabbcc00112g")


def test_mint(authority):
    ca, sign = authority
    now = datetime.datetime(2026, 10, 27, tzinfo=datetime.timezone.utc)
    board = device_cert.mint("ESP_aabbcc001122", ca, sign, now=now)
    cert = board.certificate

    # How the broker writes the subject (use_subject_as_username), which names its account
    assert cert.subject.rfc4514_string() == "O=acgt.net,CN=ESP_aabbcc001122"
    assert cert.issuer == ca.subject
    cert.verify_directly_issued_by(ca)

    assert cert.not_valid_before_utc == now - datetime.timedelta(minutes=5)
    assert cert.not_valid_after_utc == now + datetime.timedelta(days=1825)
    assert cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca is False
    assert cert.extensions.get_extension_for_class(x509.KeyUsage).value.digital_signature
    assert list(cert.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value) == [ExtendedKeyUsageOID.CLIENT_AUTH]
    assert (cert.extensions.get_extension_for_class(x509.AuthorityKeyIdentifier).value.key_identifier
            == ca.extensions.get_extension_for_class(x509.SubjectKeyIdentifier).value.digest)

    key = serialization.load_pem_private_key(board.key_pem.encode(), password=None)
    assert isinstance(key.curve, ec.SECP256R1)
    assert key.public_key() == cert.public_key()

    # By default the board trusts only the authority that signed it
    assert device_cert.authorities(board.ca_pem) == [ca]
    assert x509.load_pem_x509_certificate(board.certificate_pem.encode()) == cert


def test_mint_makes_a_new_key_each_time(authority):
    ca, sign = authority
    first = device_cert.mint("ESP_aabbcc001122", ca, sign)
    second = device_cert.mint("ESP_aabbcc001122", ca, sign)
    assert first.key_pem != second.key_pem
    assert first.certificate.serial_number != second.certificate.serial_number


def test_mint_keeps_a_given_bundle(authority):
    ca, sign = authority
    bundle = ca.public_bytes(serialization.Encoding.PEM).decode() * 2
    board = device_cert.mint("ESP_aabbcc001122", ca, sign, ca_pem=bundle)
    assert board.ca_pem == bundle


class FakeBrayden:
    """Brayden's board certificate requests (brayden's board_certificates.rs), served on localhost:
    each request is answered as `answer` says once asked about `polls` times."""

    def __init__(self, ca, sign, answer="issued", name_on_cert=None, polls=2):
        import base64
        import http.server
        import json
        import threading
        from cryptography.hazmat.primitives.serialization import load_der_public_key

        fake = self
        self.requests = []
        ca_pem = ca.public_bytes(serialization.Encoding.PEM).decode()

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, status, body):
                data = json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                if self.path != "/api/security/boards/requests":
                    return self.reply(404, {"error": "no such route"})
                if not body["name"].startswith("ESP_"):
                    return self.reply(400, {"error": "not a board's name"})
                key = load_der_public_key(base64.b64decode(body["public_key"]))
                fake.requests.append({"name": body["name"], "key": key, "polls": 0})
                self.reply(200, {"id": str(len(fake.requests) - 1), "name": body["name"], "state": "pending",
                                 "public_key_sha256": device_cert.public_key_sha256(key), "expires_at": "later"})

            def do_GET(self):
                request = fake.requests[int(self.path.rsplit("/", 1)[1])]
                request["polls"] += 1
                if request["polls"] < polls:
                    return self.reply(200, {"state": "pending"})
                if answer != "issued":
                    return self.reply(200, {"state": answer, "answered_by": "m@example.com"})
                now = datetime.datetime.now(datetime.timezone.utc)
                cert = sign(device_cert.certificate_builder(name_on_cert or request["name"], request["key"], ca, now))
                self.reply(200, {"state": "issued", "answered_by": "m@example.com", "chain": ca_pem,
                                 "certificate": cert.public_bytes(serialization.Encoding.PEM).decode()})

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()


def test_brayden_signs_once_approved(authority):
    ca, sign = authority
    brayden = FakeBrayden(ca, sign)
    said = []
    try:
        board = device_cert.BraydenAuthority(brayden.url, poll_s=0.01).mint("ESP_aabbcc001122", say=said.append)
    finally:
        brayden.close()
    cert = board.certificate
    assert cert.subject.rfc4514_string() == "O=acgt.net,CN=ESP_aabbcc001122"
    cert.verify_directly_issued_by(ca)
    # The key made here, sent only as its public half
    key = serialization.load_pem_private_key(board.key_pem.encode(), password=None)
    assert device_cert.public_key_sha256(key.public_key()) == device_cert.public_key_sha256(cert.public_key())
    assert device_cert.public_key_sha256(brayden.requests[0]["key"]) == device_cert.public_key_sha256(key.public_key())
    # The board trusts the authorities Brayden sent, for the broker
    assert device_cert.authorities(board.ca_pem) == [ca]
    # What to look for on the dashboard
    fingerprint = device_cert.fingerprint(device_cert.public_key_sha256(key.public_key()))
    assert any(fingerprint in line for line in said)
    assert any("/?tab=security&section=mqtt" in line for line in said)


def test_brayden_refusals(authority):
    ca, sign = authority
    for answer, says in (("denied", "denied"), ("expired", "in time")):
        brayden = FakeBrayden(ca, sign, answer=answer)
        try:
            with pytest.raises(RuntimeError, match=says):
                device_cert.BraydenAuthority(brayden.url, poll_s=0.01).mint("ESP_aabbcc001122", say=lambda _: None)
        finally:
            brayden.close()
    brayden = FakeBrayden(ca, sign, name_on_cert="ESP_000000000000")
    try:
        with pytest.raises(RuntimeError, match="names"):
            device_cert.BraydenAuthority(brayden.url, poll_s=0.01).mint("ESP_aabbcc001122", say=lambda _: None)
        with pytest.raises(RuntimeError, match="400.*not a board's name"):
            device_cert.BraydenAuthority(brayden.url, poll_s=0.01).mint("board", say=lambda _: None)
    finally:
        brayden.close()


def test_check_issued_wants_our_key_and_their_authority(authority):
    ca, sign = authority
    now = datetime.datetime.now(datetime.timezone.utc)
    ours = ec.generate_private_key(ec.SECP256R1())
    ca_pem = ca.public_bytes(serialization.Encoding.PEM).decode()
    other = sign(device_cert.certificate_builder("ESP_aabbcc001122", ec.generate_private_key(ec.SECP256R1()).public_key(), ca, now))
    with pytest.raises(RuntimeError, match="another key"):
        device_cert.check_issued("ESP_aabbcc001122", ours, other.public_bytes(serialization.Encoding.PEM).decode(), ca_pem)
    stranger_key = ec.generate_private_key(ec.SECP256R1())
    stranger = device_cert.certificate_builder("ESP_aabbcc001122", ours.public_key(), ca, now).sign(stranger_key, hashes.SHA256())
    with pytest.raises(RuntimeError, match="None of the authorities"):
        device_cert.check_issued("ESP_aabbcc001122", ours, stranger.public_bytes(serialization.Encoding.PEM).decode(), ca_pem)
