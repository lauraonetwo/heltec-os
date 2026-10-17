# OTA status and next stage

Version 1.0.0 deliberately implements the first stage discussed for Heltec OS.
**Update system is a status screen; it does not download, verify or install firmware.**
There is no update URL, GitHub credential, private signing key, or unsigned update endpoint in the firmware.

## Flash layout

| Partition | Offset | Size | Purpose |
| --- | --- | --- | --- |
| nvs | 0x9000 | 0x5000 | Device preferences and saved WiFi network |
| otadata | 0xE000 | 0x2000 | OTA boot selection metadata |
| app0 | 0x10000 | 0x200000 | OTA application slot 0 (2 MiB) |
| app1 | 0x210000 | 0x200000 | OTA application slot 1 (2 MiB) |
| spiffs | 0x410000 | 0x300000 | Reserved filesystem space, currently unused |

The remaining flash is unallocated. The build validator reads the generated partition binary
and rejects a different table, overlapping partitions, an incorrect ESP32 chip ID or an application
larger than either slot. The OLED's partition message only confirms that an alternate slot exists.
It does not mean signed OTA or rollback has been implemented.

## Requirements before enabling Update system installation

1. Configure the actual public GitHub repository and release asset URLs.
2. Provision a trusted public signing key in the initial firmware. Keep the private key outside
   the repository and protect the signing process independently from public release uploads.
3. Authenticate HTTPS connections, including GitHub's asset redirect hosts, with verified
   certificate chains and a valid clock. Never use `setInsecure()` as a production workaround.
4. Verify a signed manifest binding the version, board identifier, image size and SHA-256.
   Verify the image digest and signature before switching the boot partition.
5. Check the ESP32-S3 image header and Heltec V3.2 target, application size, release version,
   and downgrade policy. Write only to the inactive application slot.
6. Ask the user to confirm the specific available version on the OLED before installation.
   Report progress and network, signature, compatibility and flash-write failures.
7. Test interrupted downloads and power loss. Add startup health confirmation and bootloader
   rollback support before claiming automatic recovery from a bad application.

The current workflow produces development binaries and an **unsigned** manifest. A SHA-256 checksum
alone does not authenticate a release. These files must not be treated as ready-to-install OTA updates.
Until the signed updater is implemented and tested, install subsequent versions by USB.
