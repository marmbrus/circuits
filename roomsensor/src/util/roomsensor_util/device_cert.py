"""The boards' certificates, signed by a house YubiKey: through Brayden, approved on its dashboard,
or by one plugged into this computer.

A board presents its certificate to the house's MQTT broker (brayden repo, docker/smll/mqtt) on its
TLS listener (8883), and to updates.acgt.net for OTA updates (docker/smll/auth). Both take only
certificates from one of the house's authorities. Each authority is a YubiKey holding a P-256 key
in PIV slot 9a, with its CA certificate in the same slot, and PIN and touch policy "never"
(brayden's docker/smll/yubikey/README.md). The broker and Brayden name a board by its
certificate's subject, O=acgt.net,CN=ESP_<mac>; Brayden makes its broker account when it first
connects.

A board's certificate is made like Brayden's own (server/crates/integrations/security/src/ca.rs):
an EC P-256 key, CN=ESP_<mac> and O=acgt.net, good only for client authentication, for five
years. The key is made here and goes only to the board; no copy is kept.

By default Brayden signs it (BraydenAuthority, brayden's
server/crates/integrations/security/src/board_certificates.rs): this sends Brayden the public key
and the board's name, someone signed in approves the request on the dashboard while a house
YubiKey is plugged into smll (or signs from a Mac), and Brayden answers with the certificate and
the house's authorities. With a YubiKey plugged in here instead, YubiKeyAuthority signs it locally.
"""
import base64
import datetime
import hashlib
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Callable, List, Optional

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

ORGANIZATION = "acgt.net"
VALIDITY = datetime.timedelta(days=1825)
# Brayden's BACKDATE_SECS, for clocks a little behind.
BACKDATE = datetime.timedelta(minutes=5)


@dataclass
class BoardCertificate:
    certificate: x509.Certificate
    key_pem: str
    # The authorities the board checks the broker's certificate against.
    ca_pem: str

    @property
    def certificate_pem(self) -> str:
        return self.certificate.public_bytes(serialization.Encoding.PEM).decode()


def common_name(mac: str) -> str:
    """ESP_<mac>, with the MAC as the board writes it in its topics: 12 lowercase hex digits."""
    digits = mac.replace(":", "").replace("-", "").lower()
    if len(digits) != 12 or any(c not in "0123456789abcdef" for c in digits):
        raise ValueError(f"not a MAC address: {mac!r}")
    return f"ESP_{digits}"


def certificate_builder(name: str, public_key: ec.EllipticCurvePublicKey, issuer: x509.Certificate,
                        now: datetime.datetime) -> x509.CertificateBuilder:
    subject = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, name),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, ORGANIZATION),
    ])
    return (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer.subject)
        .public_key(public_key)
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - BACKDATE)
        .not_valid_after(now + VALIDITY)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False, key_encipherment=False,
                                     data_encipherment=False, key_agreement=False, key_cert_sign=False,
                                     crl_sign=False, encipher_only=False, decipher_only=False), critical=True)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH]), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(issuer.public_key()), critical=False)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(public_key), critical=False)
    )


def mint(name: str, issuer: x509.Certificate, sign: Callable[[x509.CertificateBuilder], x509.Certificate],
         ca_pem: Optional[str] = None, now: Optional[datetime.datetime] = None) -> BoardCertificate:
    """A new key and its certificate for `name`, signed by `sign` for the authority `issuer`.

    `ca_pem` is what the board trusts for the broker's certificate; by default only `issuer`.
    """
    now = now or datetime.datetime.now(datetime.timezone.utc)
    key = ec.generate_private_key(ec.SECP256R1())
    certificate = sign(certificate_builder(name, key.public_key(), issuer, now))
    certificate.verify_directly_issued_by(issuer)
    key_pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption()).decode()
    if ca_pem is None:
        ca_pem = issuer.public_bytes(serialization.Encoding.PEM).decode()
    return BoardCertificate(certificate=certificate, key_pem=key_pem, ca_pem=ca_pem)


def authorities(ca_pem: str) -> List[x509.Certificate]:
    return x509.load_pem_x509_certificates(ca_pem.encode())


def describe(cert: x509.Certificate) -> str:
    return cert.subject.rfc4514_string()


class YubiKeyAuthority:
    """The certificate authority on a plugged-in YubiKey: the key and CA certificate in PIV slot 9a.

    Use as a context manager; it holds the YubiKey's smart card connection while open.
    """

    def __init__(self, serial: Optional[int] = None) -> None:
        self.serial = serial
        self._connection = None
        self._piv = None
        self.certificate: Optional[x509.Certificate] = None

    def __enter__(self) -> "YubiKeyAuthority":
        from ykman.device import list_all_devices
        from yubikit.core.smartcard import SmartCardConnection
        from yubikit.piv import PivSession, SLOT

        try:
            devices = list_all_devices([SmartCardConnection])
        except TypeError:  # yubikey-manager 4 lists over every connection itself
            devices = list_all_devices()
        if self.serial is not None:
            devices = [(d, info) for d, info in devices if info.serial == self.serial]
        if not devices:
            raise RuntimeError(f"YubiKey {self.serial} isn't plugged in" if self.serial else "No YubiKey is plugged in")
        if len(devices) > 1:
            serials = ", ".join(str(info.serial) for _, info in devices)
            raise RuntimeError(f"More than one YubiKey is plugged in ({serials}); pick one with --yubikey")
        device, info = devices[0]
        self.serial = info.serial
        self._connection = device.open_connection(SmartCardConnection)
        try:
            self._piv = PivSession(self._connection)
            self.certificate = self._piv.get_certificate(SLOT.AUTHENTICATION)
        except Exception as e:
            self.__exit__()
            raise RuntimeError(f"YubiKey {self.serial} has no CA certificate in PIV slot 9a ({e})") from e
        return self

    def __exit__(self, *exc) -> None:
        if self._connection is not None:
            self._connection.close()
        self._connection = None
        self._piv = None

    def sign(self, builder: x509.CertificateBuilder) -> x509.Certificate:
        from ykman.piv import sign_certificate_builder
        from yubikit.piv import SLOT

        key_type = self._piv.get_slot_metadata(SLOT.AUTHENTICATION).key_type
        return sign_certificate_builder(self._piv, SLOT.AUTHENTICATION, key_type, builder)


BRAYDEN_URL = "https://brayden.acgt.net"


def public_key_sha256(public_key: ec.EllipticCurvePublicKey) -> str:
    """The SHA-256 of the key's DER SubjectPublicKeyInfo, hex, as Brayden shows it."""
    der = public_key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return hashlib.sha256(der).hexdigest()


def fingerprint(sha256: str) -> str:
    """The first 16 hex digits in groups of four, as the dashboard shows them."""
    return " ".join(sha256[i:i + 4] for i in range(0, 16, 4))


def check_issued(name: str, key: ec.EllipticCurvePrivateKey, certificate_pem: str, chain_pem: str) -> x509.Certificate:
    """The certificate Brayden sent, once it names `name` for `key` and one of `chain_pem` signed it."""
    certificate = x509.load_pem_x509_certificate(certificate_pem.encode())
    expected = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name), x509.NameAttribute(NameOID.ORGANIZATION_NAME, ORGANIZATION)])
    if {a.oid: a.value for a in certificate.subject} != {a.oid: a.value for a in expected}:
        raise RuntimeError(f"Brayden's certificate names {describe(certificate)}, not {name}")
    if public_key_sha256(certificate.public_key()) != public_key_sha256(key.public_key()):
        raise RuntimeError("Brayden's certificate is for another key")
    for authority in authorities(chain_pem):
        try:
            certificate.verify_directly_issued_by(authority)
            return certificate
        except Exception:  # noqa: BLE001 - try the next authority
            continue
    raise RuntimeError("None of the authorities Brayden sent signed its certificate")


class BraydenAuthority:
    """Brayden, signing a board's certificate once someone approves it on the dashboard.

    From the house only: Brayden takes these requests without a sign-in from the house's addresses.
    """

    def __init__(self, url: str = BRAYDEN_URL, poll_s: float = 2.0, timeout_s: float = 660.0) -> None:
        self.url = url.rstrip("/")
        self.poll_s = poll_s
        self.timeout_s = timeout_s

    def _call(self, method: str, path: str, body: Optional[dict] = None) -> dict:
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(f"{self.url}/api/security{path}", data=data, method=method,
                                         headers={"Content-Type": "application/json", "Accept": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.loads(response.read())
        except urllib.error.HTTPError as e:
            try:
                why = json.loads(e.read()).get("error") or e.reason
            except Exception:  # noqa: BLE001
                why = e.reason
            raise RuntimeError(f"Brayden refused ({e.code}): {why}") from e
        except urllib.error.URLError as e:
            raise RuntimeError(f"Can't reach {self.url}: {e.reason}") from e

    @property
    def dashboard(self) -> str:
        return f"{self.url}/?tab=security&section=mqtt"

    def mint(self, name: str, ca_pem: Optional[str] = None, say: Callable[[str], None] = print) -> BoardCertificate:
        """A new key, and its certificate for `name` once someone approves the request.

        `ca_pem` is what the board trusts for the broker's certificate; by default the authorities
        Brayden sends.
        """
        key = ec.generate_private_key(ec.SECP256R1())
        spki = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        created = self._call("POST", "/boards/requests", {"name": name, "public_key": base64.b64encode(spki).decode()})
        sha256 = public_key_sha256(key.public_key())
        if created.get("public_key_sha256") != sha256:
            raise RuntimeError("Brayden heard another key than the one sent")
        say(f"Asked Brayden for {name}'s certificate, key {fingerprint(sha256)}.")
        say(f"Approve it at {self.dashboard} (Board certificates), with a YubiKey in smll.")
        deadline = time.time() + self.timeout_s
        while time.time() < deadline:
            answer = self._call("GET", f"/boards/requests/{created['id']}")
            state = answer.get("state")
            if state == "issued":
                chain = answer["chain"]
                certificate = check_issued(name, key, answer["certificate"], chain)
                say(f"  Approved by {answer.get('answered_by', 'someone')}.")
                key_pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                            serialization.NoEncryption()).decode()
                return BoardCertificate(certificate=certificate, key_pem=key_pem, ca_pem=ca_pem or chain)
            if state == "denied":
                raise RuntimeError(f"{answer.get('answered_by', 'Someone')} denied {name}'s certificate")
            if state == "expired":
                raise RuntimeError(f"Nobody approved {name}'s certificate in time")
            time.sleep(self.poll_s)
        raise RuntimeError(f"Nobody approved {name}'s certificate in time")

