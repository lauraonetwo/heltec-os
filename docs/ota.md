# Signed OTA updates

Heltec OS 1.2.0 updates from the public repository **lauraonetwo/heltec-os**.
The board needs Internet access over its saved WiFi network. It does not need a GitHub token.
The firmware, menus and setup instructions are in English.

## 1. Install the OTA-capable firmware once by USB

Open `firmware/HeltecOS/HeltecOS.ino`, select **Heltec WiFi LoRa 32(V3)** and upload using the
pinned Heltec 3.3.8 package. Keep `partitions.csv` unchanged and leave full-flash erase disabled.
The previous WiFi credentials remain in the same NVS partition. You can build directly in Arduino
IDE; the private signing key and Python packages are not needed to compile firmware.

Both local and GitHub builds include `OtaTrust.h`, which contains only a **public** RSA-3072 key.
Do not replace that public key after installing this version unless you intend to provision devices
again by USB. A normal version update retains the same trust key.

The old development ZIP is version 1.0.0 with `ota_enabled: false`. It is not a signed OTA release
and cannot install this initial OTA-capable version by itself.

## 2. Configure the signing secret once

The existing private signing key is stored locally at:

```text
.local/ota-signing-private.pem
```

The `.local/` directory and PEM files are ignored by Git. **Never commit this file, attach it to a
release, or put its contents in a public issue, workflow file or screenshot.** Keep a private backup;
losing this key means installed devices cannot accept newly signed releases without reprovisioning.

In the repository's **Settings > Secrets and variables > Actions**, add a **repository secret**:

- Name: `OTA_SIGNING_PRIVATE_KEY`
- Value: the entire contents of `.local/ota-signing-private.pem`, including the BEGIN/END lines.

This is the signing key for your project, not a GitHub account password or token. Keep repository
write access restricted: someone able to change workflows using this secret can sign updates.
For stronger separation, retain the key offline and use the manual signing route below instead.
The workflow does not overwrite an existing release; use a new version tag for every new build.

## 3. Publish a release

Commit and push the source, including `OtaTrust.h`, `OtaProtocol.h`, `OtaUpdater.cpp`, the new
scripts, tests and workflows. Do not add `.local/` or `build/`.

After configuring the secret, tag the commit containing version 1.2.0:

```text
git tag v1.2.0
git push origin v1.2.0
```

The **Publish signed OTA release** workflow compiles, tests, validates the actual binary and
partition table, signs the manifest, verifies it, and publishes these three assets:

```text
heltec-os.bin
manifest.txt
manifest.sig
```

`heltec-os.bin` is the application only. Never substitute `HeltecOS.ino.merged.bin`, a bootloader,
a partition-table binary or the CI artifact ZIP. The signer rejects the old 1.0.0 build and
binaries whose embedded board/version/public-key identity does not match the release.

If a tag workflow failed before creating a release, rerun it after fixing setup, or use
**Actions > Publish signed OTA release > Run workflow** with that existing tag. The tag must
match `HELTEC_OS_VERSION` in `Config.h` exactly. All three assets are uploaded as part of release
creation. A temporary missing asset while publication is in progress results in an error on the
device; retry once publication has completed. Nothing is installed unless verification succeeds.

## 4. Publish later changes

1. Make and test the code changes.
2. Change `HELTEC_OS_VERSION` in `firmware/HeltecOS/Config.h`, for example from `1.2.0` to `1.2.1`.
3. Commit and push the changes.
4. Create and push the matching new tag, for example `v1.2.1`.
5. Wait for the signed release workflow to succeed.
6. On the Heltec, open **Update system**. When it shows 1.2.1, tap PRG to select **Install update**,
   then hold PRG for at least 0.7 seconds and release to confirm.

Only stable `MAJOR.MINOR.PATCH` versions are supported; each numeric component is at most 65535.
Equal or older versions are not installed. Publish a higher stable version as GitHub's latest release;
do not mark development/pre-release builds as latest. Editing release descriptions is harmless, but
replacing a release's firmware or signature can cause checks to fail. Publish a new version instead.

## Manual local signing and publication

Use the pinned dependencies and compile as described in the README. Then run from the repository root:

```text
python -m pip install -r scripts/requirements.txt
python scripts/validate_build.py build/output
python scripts/ota_release.py sign --tag v1.2.0
python scripts/ota_release.py verify build/release
```

The signing command defaults to the ignored local key. It writes the same three OTA assets into
`build/release/`. Create a GitHub Release for the matching tag, attach those three files, and publish
it as the latest stable release. You do not need a GitHub signing secret when signing locally.
The project already has its trust key: do not run `keygen` again. The tool refuses to overwrite it.

## What the device verifies

- HTTPS certificate chains and host names using the full trusted certificate bundle in the pinned
  ESP32 core. Redirects are limited to this repository's releases and GitHub's release asset hosts.
- The system clock is synchronized by NTP before HTTPS if no valid clock is available.
- A bounded, canonical text manifest and an RSA-3072 PKCS#1 v1.5 / SHA-256 signature over its exact bytes.
- The expected Heltec V3.2 board, a newer numeric version, an exact version-specific asset URL,
  the signed image size and SHA-256, and a matching embedded board/version/key identity.
- The ESP32-S3 image header, 8 MB flash configuration, and the two 2 MiB application slots.
- ESP-IDF's image validation before selecting the new boot partition.

The worker performs network and flash operations separately from OLED/button handling. Downloads
have timeouts and can be cancelled. On a cancelled, incomplete or invalid download the inactive
slot may contain partial data, but the current boot partition is not changed.
The private key is never included in the firmware. `manifest.json` in a normal CI artifact is
unsigned development metadata; it is not the OTA manifest.

## Recovery and limits

The pinned Heltec package enables bootloader rollback. After an OTA boot, the new firmware confirms
itself after ten seconds only if the OLED initialized, NVS opened, and the flash size is correct.
WiFi is not required for this startup check. A reset/crash before confirmation causes the bootloader
to try the previous valid application. This is a basic startup health check, not a guarantee against
all runtime bugs. Failures after confirmation require a later update or USB recovery.

Power loss before boot selection leaves the previous application selected. Keep power stable during
finalization and restart. Neither the partition table nor the bootloader is changed over OTA. USB
installation is the recovery route if the device cannot boot, the layout is wrong or the trust key
must change. Physical-board power-loss/rollback tests remain necessary before relying on this in
an unattended deployment. Secure Boot and flash encryption are not enabled by this project; the
signature is verified by the running application, not a hardware-enforced trust chain.

## Messages

| Message | Meaning / action |
| --- | --- |
| Connect WiFi first | Configure WiFi or wait for reconnection |
| Clock sync failed | Internet/NTP is unavailable; retry after fixing the connection |
| No OTA release yet | No published latest release with manifest.txt exists |
| System is up to date | The signed latest release is equal to or older than this firmware |
| Signature unavailable / rejected | Assets are missing, altered, or signed with a different key |
| HTTPS check failed | DNS, TLS, network, redirect or HTTP response failed; no insecure fallback is used |
| Wrong OTA layout | Install the supplied partition layout by USB |
| Firmware rejected | Download, SHA-256, or embedded image identity verification failed |
| Update cancelled | Current firmware remains selected |

Implementation reference: [Espressif OTA and rollback](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html).
