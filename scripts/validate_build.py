"""Validate compiled Heltec OS artifacts and create an unsigned development manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 0x5000),
    ("otadata", 1, 0, 0xE000, 0x2000),
    ("app0", 0, 0x10, 0x10000, 0x200000),
    ("app1", 0, 0x11, 0x210000, 0x200000),
    ("spiffs", 1, 0x82, 0x410000, 0x300000),
]


def validate_partitions(data):
    entries = []
    checksum_found = False
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            raise ValueError("Truncated partition entry")
        if entry[:2] == b"\xeb\xeb":
            if hashlib.md5(data[:offset]).digest() != entry[16:32]:
                raise ValueError("Invalid partition-table checksum")
            checksum_found = True
            break
        if entry == b"\xff" * 32:
            break
        magic, kind, subtype, start, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if magic != 0x50AA or flags != 0:
            raise ValueError("Unexpected partition magic or flags")
        entries.append((label.rstrip(b"\0").decode("ascii"), kind, subtype, start, size))
    if not checksum_found or entries != EXPECTED_PARTITIONS:
        raise ValueError("Compiled partition table does not match the two-slot Heltec OS layout")
    return min(entry[4] for entry in entries if entry[1] == 0)


def validate_image(image, slot_size):
    if len(image) < 24 or image[0] != 0xE9:
        raise ValueError("Not an ESP application image")
    if struct.unpack_from("<H", image, 12)[0] != 9:
        raise ValueError("Image is not for ESP32-S3")
    if image[3] >> 4 != 3:
        raise ValueError("Image is not configured for 8 MB flash")
    if len(image) > slot_size:
        raise ValueError(f"Image is {len(image)} bytes; OTA slot permits {slot_size}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    slot_size = validate_partitions((args.output / "HeltecOS.ino.partitions.bin").read_bytes())
    image = (args.output / "HeltecOS.ino.bin").read_bytes()
    validate_image(image, slot_size)
    root = Path(__file__).resolve().parents[1]
    config = (root / "firmware/HeltecOS/Config.h").read_text(encoding="utf-8-sig")
    version = re.search(r'#define HELTEC_OS_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"', config).group(1)
    trust = (root / "firmware/HeltecOS/OtaTrust.h").read_text(encoding="utf-8-sig")
    key_id = re.search(r'HELTEC_OS_SIGNING_KEY_ID "([0-9a-f]{64})"', trust).group(1)
    marker = f"HELTEC_OS_IMAGE_V1|heltec-wifi-lora-32-v3.2|{version}|{key_id}|".encode()
    if marker not in image:
        raise ValueError("Compiled image does not match the source version and public OTA key")
    manifest = {
        "version": version,
        "board": "heltec-wifi-lora-32-v3.2",
        "chip": "esp32s3",
        "file": "heltec-os.bin",
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "ota_enabled": True,
        "signed": False,
        "signing_key_sha256": key_id,
    }
    (args.output / "heltec-os.bin").write_bytes(image)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Verified ESP32-S3 / 8 MB image: {len(image):,} of {slot_size:,} bytes per OTA slot.")
    print("Verified compiled partition table. Wrote unsigned development manifest.")


if __name__ == "__main__":
    main()
