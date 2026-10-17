# Validation

## Automated checks

Compile with the pinned Heltec board package and libraries, then run:

```text
python scripts/validate_build.py build/output
python -m unittest discover -s scripts -p "test_*.py"
```

The validator inspects the generated partition binary, target chip and image size. Tests exercise
rejection of an oversized image, a different chip, a substituted partition table and damaged metadata.
These checks do not emulate the radio, PRG button, OLED or NVS power-loss behavior.

## Hardware acceptance checklist

Perform these checks after the initial USB installation. They require a physical board and router;
a successful compile alone does not establish that they have passed.

- Boot: `hi user!`, followed by exactly `WiFi`, `Update system`, `About`.
- Button: short presses advance one item; a 0.7-second hold selects only on release. Holding for
  several seconds does not repeat. A button held during normal software startup causes no false selection.
- Layout: all three main entries fit the OLED; lists scroll and Back remains reachable.
- Scan: cancel a scan, rescan, try an empty scan, duplicates and more than 20 nearby networks.
  Long SSIDs are shortened; protected networks have a `*` marker.
- Open WiFi: select a 2.4 GHz open network and check the acquired IP address.
- Protected WiFi: select the network, join the temporary AP with the displayed key, open
  `http://192.168.4.1/`, enter the password and check that the AP closes on success.
- Retry: submit a wrong password. After 20 seconds, the OLED reports failure and setup permits
  another attempt. The previous saved network remains stored until a new connection succeeds.
- Cancellation: hold PRG during setup/connection. The temporary AP closes and the saved network
  is restored when available. Leaving setup open for five minutes also closes it.
- Storage: restart after a successful connection and confirm automatic reconnection. Save a different
  network and verify it replaces the first. Cancel Forget, then confirm Forget and verify the next
  restart does not reconnect to the removed network.
- Form safety: use an SSID containing quotes or `<` and `&`; the full SSID must display as text.
  An invalid setup token must be rejected, and requests through the router-side IP must be rejected.
- Disconnection: turn off and restore the router; confirm automatic reconnection and responsive controls.
- About: verify version 1.0.0, Heltec V3.2 / ESP32-S3 and 8 MB flash.
- Update system: verify `OTA not enabled yet` and `OTA partition ready`; no firmware is downloaded.

## Current verification scope

Verified locally on 2026-10-17 with Arduino CLI 1.5.1, Heltec core 3.3.8 and the pinned libraries:

- Compilation passed for `Heltec-esp32:esp32:heltec_wifi_lora_32_V3`.
- Final application binary: 979,360 bytes; each OTA slot: 2,097,152 bytes.
- Static RAM reported by the compiler: 48,332 of 327,680 bytes.
- Generated ESP32-S3 / 8 MB image header and compiled partition table passed validation.
- All eight build-validator unit tests passed.
- Main menu labels and footer text fit the actual OLED font metrics; the footer uses y=51
  to keep descenders inside the 64-pixel screen.

GitHub Actions must run after the files are pushed to a repository. Physical-board behavior is
pending the checklist above; this change does not flash a board or publish a release.
