"""The MQTT client certificate a board gets, signed by a software stand-in for the YubiKey's CA."""
import datetime

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

from roomsensor_util import mqtt_cert


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
    assert mqtt_cert.common_name("AA:BB:CC:00:11:22") == "ESP_aabbcc001122"
    assert mqtt_cert.common_name("aabbcc001122") == "ESP_aabbcc001122"
    with pytest.raises(ValueError):
        mqtt_cert.common_name("aabbcc00112")
    with pytest.raises(ValueError):
        mqtt_cert.common_name("aabbcc00112g")


def test_mint(authority):
    ca, sign = authority
    now = datetime.datetime(2026, 10, 27, tzinfo=datetime.timezone.utc)
    board = mqtt_cert.mint("ESP_aabbcc001122", ca, sign, now=now)
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
    assert mqtt_cert.authorities(board.ca_pem) == [ca]
    assert x509.load_pem_x509_certificate(board.certificate_pem.encode()) == cert


def test_mint_makes_a_new_key_each_time(authority):
    ca, sign = authority
    first = mqtt_cert.mint("ESP_aabbcc001122", ca, sign)
    second = mqtt_cert.mint("ESP_aabbcc001122", ca, sign)
    assert first.key_pem != second.key_pem
    assert first.certificate.serial_number != second.certificate.serial_number


def test_mint_keeps_a_given_bundle(authority):
    ca, sign = authority
    bundle = ca.public_bytes(serialization.Encoding.PEM).decode() * 2
    board = mqtt_cert.mint("ESP_aabbcc001122", ca, sign, ca_pem=bundle)
    assert board.ca_pem == bundle
