import hashlib
import struct
import unittest
from validate_build import EXPECTED_PARTITIONS, validate_image, validate_partitions


def partition_binary(entries=EXPECTED_PARTITIONS):
    data = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, sub, start, size,
                                name.encode(), 0) for name, kind, sub, start, size in entries)
    return data + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()


def image_header(chip=9):
    data = bytearray(24)
    data[0] = 0xE9
    data[3] = 0x30
    struct.pack_into("<H", data, 12, chip)
    return data


class BuildValidationTests(unittest.TestCase):
    def test_expected_partitions(self):
        self.assertEqual(validate_partitions(partition_binary()), 2 * 1024 * 1024)

    def test_substituted_layout(self):
        changed = list(EXPECTED_PARTITIONS)
        changed[3] = ("app1", 0, 0x11, 0x210000, 0x100000)
        with self.assertRaises(ValueError):
            validate_partitions(partition_binary(changed))

    def test_corrupt_table(self):
        data = bytearray(partition_binary())
        data[5] ^= 1
        with self.assertRaises(ValueError):
            validate_partitions(data)

    def test_missing_table_checksum(self):
        with self.assertRaises(ValueError):
            validate_partitions(partition_binary()[:-32])

    def test_target_chip(self):
        validate_image(image_header(), 0x200000)
        with self.assertRaises(ValueError):
            validate_image(image_header(chip=0), 0x200000)

    def test_oversized_image(self):
        with self.assertRaises(ValueError):
            validate_image(image_header() + bytes(0x200000), 0x200000)

    def test_wrong_flash_size(self):
        data = image_header()
        data[3] = 0x20
        with self.assertRaises(ValueError):
            validate_image(data, 0x200000)

    def test_truncated_image(self):
        with self.assertRaises(ValueError):
            validate_image(b"\xe9", 0x200000)


if __name__ == "__main__":
    unittest.main()
