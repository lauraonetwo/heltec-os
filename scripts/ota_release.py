"""Create the OTA trust key and sign/verify Heltec OS release assets. Never prints private keys."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from validate_build import validate_image, validate_partitions

ROOT = Path(__file__).resolve().parents[1]
BOARD = 'heltec-wifi-lora-32-v3.2'
REPOSITORY = 'lauraonetwo/heltec-os'
KEY_PATH = ROOT / '.local/ota-signing-private.pem'
TRUST_PATH = ROOT / 'firmware/HeltecOS/OtaTrust.h'
VERSION_RE = r'(?:0|[1-9][0-9]{0,4})\.(?:0|[1-9][0-9]{0,4})\.(?:0|[1-9][0-9]{0,4})'


def valid_version(version):
    return bool(re.fullmatch(VERSION_RE, version)) and all(int(p) <= 65535 for p in version.split('.'))


def key_id(public):
    der = public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return hashlib.sha256(der).hexdigest()


def trusted_public(path=TRUST_PATH):
    source = Path(path).read_text(encoding='utf-8-sig')
    match = re.search(r'-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----', source, re.S)
    if not match:
        raise ValueError('Trusted public key is missing')
    public = serialization.load_pem_public_key(match.group().encode())
    if not isinstance(public, rsa.RSAPublicKey) or public.key_size != 3072:
        raise ValueError('The release key must be RSA-3072')
    fingerprint = re.search(r'HELTEC_OS_SIGNING_KEY_ID "([0-9a-f]{64})"', source)
    if not fingerprint or fingerprint[1] != key_id(public):
        raise ValueError('Public key fingerprint does not match OtaTrust.h')
    return public


def create_key(private_path=KEY_PATH, trust_path=TRUST_PATH):
    private_path, trust_path = Path(private_path), Path(trust_path)
    if private_path.exists() or trust_path.exists():
        raise ValueError('Trust already exists. Keep the original signing key; do not replace the key used by installed devices.')
    private = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    private_path.parent.mkdir(parents=True, exist_ok=True)
    trust_path.parent.mkdir(parents=True, exist_ok=True)
    data = private.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    # Exclusive creation prevents accidental replacement of an existing signing identity.
    fd = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as handle:
        handle.write(data)
    public = private.public_key()
    pem = public.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    trust_path.write_text('#pragma once\n// Public OTA trust anchor. Never put a private key in this file.\n'
        f'#define HELTEC_OS_SIGNING_KEY_ID "{key_id(public)}"\n'
        f'namespace OtaTrust {{\ninline constexpr char PUBLIC_KEY[] = R"KEY({pem})KEY";\n}}\n', encoding='utf-8')
    print(f'Private signing key saved to {private_path} (ignored by Git).')
    print(f'Public trust anchor saved to {trust_path}.')
    print(f'Public key SHA-256: {key_id(public)}')


def manifest_bytes(version, image):
    if not valid_version(version):
        raise ValueError('Use a stable version MAJOR.MINOR.PATCH with components <= 65535')
    return (f'HELTEC-OS-OTA-V1\nboard={BOARD}\nversion={version}\nsize={len(image)}\n'
            f'sha256={hashlib.sha256(image).hexdigest()}\n'
            f'url=https://github.com/{REPOSITORY}/releases/download/v{version}/heltec-os.bin\n').encode('ascii')


def verify_assets(manifest, signature, image, public):
    if len(manifest) > 512 or len(signature) != 384:
        raise ValueError('Invalid manifest or signature size')
    public.verify(signature, manifest, padding.PKCS1v15(), hashes.SHA256())
    text = manifest.decode('ascii')
    lines = text.splitlines()
    if len(lines) != 6 or not lines[2].startswith('version='):
        raise ValueError('Invalid manifest format')
    version = lines[2][8:]
    if manifest != manifest_bytes(version, image):
        raise ValueError('Manifest does not match the firmware, board or repository')
    validate_image(image, 0x200000)
    marker = f'HELTEC_OS_IMAGE_V1|{BOARD}|{version}|{key_id(public)}|'.encode()
    if marker not in image:
        raise ValueError('Firmware identity does not match the version, board and trusted key')
    return version


def sign_build(build_dir, output_dir, tag, private_data, public):
    build_dir, output_dir = Path(build_dir), Path(output_dir)
    metadata = json.loads((build_dir / 'manifest.json').read_text(encoding='utf-8-sig'))
    version = metadata['version']
    if tag != 'v' + version or not valid_version(version):
        raise ValueError('The tag must match the firmware version exactly')
    image = (build_dir / 'heltec-os.bin').read_bytes()
    validate_image(image, validate_partitions((build_dir / 'HeltecOS.ino.partitions.bin').read_bytes()))
    if metadata.get('sha256') != hashlib.sha256(image).hexdigest() or metadata.get('size') != len(image):
        raise ValueError('Build metadata does not match the binary; run validate_build.py again')
    if metadata.get('board') != BOARD or metadata.get('ota_enabled') is not True:
        raise ValueError('This is not an OTA-enabled Heltec OS build')
    private = serialization.load_pem_private_key(private_data, password=None)
    if not isinstance(private, rsa.RSAPrivateKey) or private.key_size != 3072 or key_id(private.public_key()) != key_id(public):
        raise ValueError('Signing key does not match the public key installed in the firmware')
    manifest = manifest_bytes(version, image)
    signature = private.sign(manifest, padding.PKCS1v15(), hashes.SHA256())
    verify_assets(manifest, signature, image, public)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / 'heltec-os.bin').write_bytes(image)
    (output_dir / 'manifest.txt').write_bytes(manifest)
    (output_dir / 'manifest.sig').write_bytes(signature)
    print(f'Created and verified signed release {tag}: {len(image):,} bytes.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('keygen', help='Initial project setup only; refuses to replace existing trust')
    sign = sub.add_parser('sign')
    sign.add_argument('--build-dir', type=Path, default=ROOT / 'build/output')
    sign.add_argument('--output-dir', type=Path, default=ROOT / 'build/release')
    sign.add_argument('--tag', required=True)
    source = sign.add_mutually_exclusive_group()
    source.add_argument('--key', type=Path, default=KEY_PATH)
    source.add_argument('--key-env', help='Environment variable containing the private PEM key')
    verify = sub.add_parser('verify')
    verify.add_argument('directory', type=Path)
    args = parser.parse_args()
    if args.command == 'keygen':
        create_key()
    elif args.command == 'sign':
        if args.key_env:
            raw = os.environ.get(args.key_env)
            if not raw:
                raise ValueError(f'Missing signing secret: {args.key_env}')
            data = raw.encode()
        else:
            data = args.key.read_bytes()
        sign_build(args.build_dir, args.output_dir, args.tag, data, trusted_public())
    else:
        folder = args.directory
        version = verify_assets((folder / 'manifest.txt').read_bytes(), (folder / 'manifest.sig').read_bytes(),
                                (folder / 'heltec-os.bin').read_bytes(), trusted_public())
        print(f'Valid signed Heltec OS release v{version}.')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'OTA release failed: {error}', file=sys.stderr)
        sys.exit(1)
