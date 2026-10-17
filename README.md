# Heltec OS

Experimental firmware for **Heltec WiFi LoRa 32 V3.2 (ESP32-S3, 8 MB flash)**.
Version **1.0.0** implements the first stage of the project: an English OLED interface,
one-button navigation, WiFi setup from a phone, and a partition layout prepared for OTA.

## Main menu

- **WiFi**: scan and select nearby networks, view connection status, or forget the saved network.
- **Update system**: show the installed version and OTA partition availability. Downloading and installing updates is **not enabled in this version**.
- **About**: show the version, board, processor and flash size.

Startup displays `hi user!`. Change `USER_NAME` in `firmware/HeltecOS/Config.h` to customize it.

## Controls

| Action | Result |
| --- | --- |
| Tap PRG | Move to the next option |
| Hold PRG for at least 0.7 seconds, then release | Select the highlighted option |
| Hold PRG on a detail screen | Go back |
| Hold PRG while scanning, connecting or setting up WiFi | Cancel |
| Select Back in a list | Return to the previous menu |
| RST | Restart |

Actions occur when PRG is released. A held button does not repeat. Use PRG after startup;
holding it during reset can enter the ESP32 bootloader instead.

## Entering a WiFi password

1. Open **WiFi > Scan networks**. Tap PRG to highlight your network, then hold and release to select it.
2. For a password-protected network, the OLED displays a temporary network such as `Heltec-A1B2`
   and a randomly generated **Key**. This key belongs to the Heltec setup network.
3. On your phone or computer, join that temporary network using the displayed key.
   Choose to stay connected if the phone reports that it has no Internet access.
4. Open **http://192.168.4.1/** in a browser. Some phones open the page automatically.
5. Check the selected network name on the page, enter **your router's WiFi password**, and press **Connect**.
6. Check **WiFi > Connection status** on the OLED. After a successful connection, the temporary network closes.
   Your phone can return to its usual WiFi network.

The device remembers the last successfully configured network and reconnects after restart.
You do not put credentials into source code or commit them to GitHub. Selecting a protected
network again opens setup so you can replace a changed or incorrect password. An open network
connects directly without a password. The firmware retains the previous saved credentials
when a new attempt fails; setup remains open for retry until cancelled or timed out.

The setup network closes after five minutes or when you hold PRG to cancel. The device uses a
single radio: your phone can briefly lose the setup connection while it tries the router's channel.
If the OLED reports failure, rejoin the displayed setup network and reload the page.

Supported: visible 2.4 GHz open and WPA/WPA2/WPA3 Personal networks. At most 20 networks are listed;
`*` marks a protected network. Hidden SSIDs, enterprise WiFi, WEP, router captive-portal login,
and 5 GHz-only networks are not supported. Long names are shortened on the OLED; the full selected
name appears on the setup page. OLED fonts have limited Unicode coverage.

**WiFi > Forget network** asks you to choose `Forget network` and confirm with a hold.
`Cancel` is selected initially. Credentials are stored in ESP32 NVS; this build does not enable
flash encryption, so storage is not protected from physical flash extraction. The local setup
page uses HTTP inside the temporary password-protected WiFi network, with no cloud service.

## Build and first USB installation

Install these versions using Arduino IDE's Boards Manager and Library Manager:

| Component | Version |
| --- | --- |
| Heltec ESP32 Series Dev-boards (`Heltec-esp32:esp32`) | 3.3.8 |
| Heltec ESP32 Dev-Boards library | 2.1.6 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit BusIO | 1.17.4 |
| heltec-eink-modules | 4.6.0 |

Add this Boards Manager URL in Preferences:

```text
https://resource.heltec.cn/download/package_heltec_esp32_index.json
```

1. Open `firmware/HeltecOS/HeltecOS.ino` in Arduino IDE. Keep the accompanying headers and
   `partitions.csv` in the same folder.
2. Select **Heltec WiFi LoRa 32(V3)** and the COM port belonging to your board.
   The pinned board package configures 8 MB flash for this board.
3. Leave **Erase All Flash Before Sketch Upload** disabled.
4. Click **Verify**. The pinned package automatically uses the sketch's `partitions.csv`;
   no custom partition menu entry needs to be created.
5. Click **Upload** for the first installation by USB. This installs the application and its
   two-slot OTA partition table. Future USB uploads should retain that same table.
6. Open **Update system** and check for `OTA partition ready`.

Do not copy just the application binary to an arbitrary flash offset for the first installation.
Use Arduino IDE/CLI so the bootloader and partition table are installed together.

### Command line

With Arduino CLI **1.5.1**, the dependencies above, and Python 3 installed, run from the repository root:

```powershell
arduino-cli compile --fqbn Heltec-esp32:esp32:heltec_wifi_lora_32_V3 --build-path build/compile --output-dir build/output firmware/HeltecOS
python scripts/validate_build.py build/output
```

If Arduino CLI is not on PATH, Windows Arduino IDE normally bundles it at:
`%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`.
Use `--config-file "%USERPROFILE%\.arduinoIDE\arduino-cli.yaml"` if you need the IDE's configured directories.

The validator checks the **compiled** partition table, ESP32-S3 image header and 2 MB size limit,
then writes `build/output/heltec-os.bin` and `build/output/manifest.json`.
The manifest's SHA-256 is an integrity checksum, not a digital signature.
The Arduino size summary may show the board package's default application limit; the validator
enforces the actual 2 MiB limit of this project's custom OTA slots. Build files are ignored by Git.

GitHub Actions repeats compilation and validation on pushes, pull requests and manual runs,
using the pinned versions. Its artifacts are USB development builds, not signed OTA releases.
The workflow does not publish a release or install anything on the device.

## Project files

- `firmware/HeltecOS/`: firmware and the two-slot OTA partition table.
- `scripts/validate_build.py`: build checks and application manifest generation.
- `.github/workflows/build.yml`: automated compile and artifact checks.
- `docs/ota.md`: current OTA status and requirements for signed updates.
- `docs/testing.md`: hardware acceptance checks and validation limits.

Hardware references: [Heltec V3 documentation](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/)
and [Espressif WiFi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html).
