"""MQTT client certificates for the boards, signed by the YubiKey plugged into this computer.

The house's MQTT broker (brayden repo, docker/smll/mqtt) takes clients on its TLS listener (8883)
that have a certificate from one of the house's authorities. Each authority is a YubiKey holding
a P-256 key in PIV slot 9a, with its CA certificate in the same slot, and PIN and touch policy
"never" (brayden's docker/smll/yubikey/README.md). The broker names a client by its certificate's
subject, O=acgt.net,CN=ESP_<mac>, which needs a certificate account on Brayden's Security tab
before it may publish or subscribe.

A board's certificate is made like Brayden's own (server/crates/integrations/security/src/ca.rs):
an EC P-256 key, CN=ESP_<mac> and O=acgt.net, good only for client authentication, for five
years. The key is made here and goes only to the board; no copy is kept.
"""
import datetime
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
