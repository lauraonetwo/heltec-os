import json
from pathlib import Path
import tempfile
import unittest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
import ota_release as ota
from test_validate_build import image_header, partition_binary

class SignedReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        cls.public = cls.key.public_key()
        cls.image = bytes(image_header()) + ota.BOARD.encode() + b'\0' + (
            f'HELTEC_OS_IMAGE_V1|{ota.BOARD}|1.2.0|{ota.key_id(cls.public)}|').encode()

    def assets(self, image=None, version='1.2.0'):
        image = self.image if image is None else image
        manifest = ota.manifest_bytes(version, image)
        return manifest, self.key.sign(manifest, padding.PKCS1v15(), hashes.SHA256()), image

    def test_valid_release(self):
        self.assertEqual(ota.verify_assets(*self.assets(), self.public), '1.2.0')

    def test_modified_firmware(self):
        manifest, signature, image = self.assets()
        with self.assertRaises(ValueError): ota.verify_assets(manifest, signature, image + b'X', self.public)

    def test_modified_manifest(self):
        manifest, signature, image = self.assets()
        with self.assertRaises(InvalidSignature): ota.verify_assets(manifest.replace(b'1.2.0', b'1.3.0'), signature, image, self.public)

    def test_bad_signature(self):
        manifest, signature, image = self.assets()
        with self.assertRaises(InvalidSignature): ota.verify_assets(manifest, bytes(384), image, self.public)

    def test_wrong_board_image(self):
        bad = self.image.replace(ota.BOARD.encode(), b'other-board')
        with self.assertRaises(ValueError): ota.verify_assets(*self.assets(bad), self.public)

    def test_wrong_version_image(self):
        with self.assertRaises(ValueError): ota.verify_assets(*self.assets(version='1.3.0'), self.public)

    def test_wrong_chip(self):
        bad=bytearray(self.image); bad[12]=0
        with self.assertRaises(ValueError): ota.verify_assets(*self.assets(bytes(bad)), self.public)

    def test_wrong_trust_key(self):
        other = rsa.generate_private_key(public_exponent=65537, key_size=3072).public_key()
        with self.assertRaises(InvalidSignature): ota.verify_assets(*self.assets(), other)

    def test_version_boundaries(self):
        for value in ('01.2.0', '1.2', '1.2.0-beta', '65536.0.0', '-1.0.0'):
            self.assertFalse(ota.valid_version(value))
        self.assertTrue(ota.valid_version('65535.65535.65535'))

    def test_signing_refuses_tag_mismatch(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'manifest.json').write_text(json.dumps({'version':'1.2.0'}))
            with self.assertRaises(ValueError): ota.sign_build(root,root/'release','v9.0.0',b'',self.public)

    def test_sign_and_verify_pipeline(self):
        import hashlib
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'manifest.json').write_text(json.dumps({'version':'1.2.0','board':ota.BOARD,'size':len(self.image),
                'sha256':hashlib.sha256(self.image).hexdigest(),'ota_enabled':True}))
            (root/'heltec-os.bin').write_bytes(self.image)
            (root/'HeltecOS.ino.partitions.bin').write_bytes(partition_binary())
            private=self.key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption())
            ota.sign_build(root,root/'release','v1.2.0',private,self.public)
            out=root/'release'
            self.assertEqual(ota.verify_assets((out/'manifest.txt').read_bytes(),(out/'manifest.sig').read_bytes(),
                                              (out/'heltec-os.bin').read_bytes(),self.public),'1.2.0')

    def test_existing_key_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            key=Path(folder)/'private.pem'; key.write_bytes(b'keep')
            with self.assertRaises(ValueError): ota.create_key(key,Path(folder)/'trust.h')
            self.assertEqual(key.read_bytes(),b'keep')

if __name__ == '__main__': unittest.main()
