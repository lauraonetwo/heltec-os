# Validation

## Automated checks

Compile with the pinned Heltec board package and libraries, then run:

```text
python -m pip install -r scripts/requirements.txt
python scripts/validate_build.py build/output
python -m unittest discover -s scripts -p "test_*.py"
g++ -std=c++17 -Wall -Wextra -Werror tests/ota_protocol_test.cpp -o build/ota-protocol-tests
build/ota-protocol-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/morse_test.cpp -o build/morse-tests
build/morse-tests
node tests/messages_page_test.js
python scripts/ota_release.py sign --tag v1.2.0
python scripts/ota_release.py verify build/release
```

The validator inspects the generated partition binary, target chip and image size. Tests exercise
rejection of an oversized image, a different chip, a substituted partition table and damaged metadata.
Signed-release tests reject modified manifests, binaries, signatures, chips, board/version identities,
wrong trust keys and mismatched tags. Native C++ tests exercise the same parser, numeric version
comparison, redirect allowlist and image-header checks used by the firmware. These checks do not
emulate the radio, PRG button, OLED or flash power-loss behavior.

## Hardware acceptance checklist

Perform these checks after the initial USB installation. They require a physical board and router;
a successful compile alone does not establish that they have passed.

- Boot: `hi user!`, followed by `WiFi`, `Messages`, `Update system`, `About` in a scrolling menu.
- Messages: perform the [phone/board/LED checks](messages.md#hardware-acceptance-checks).
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
- About: verify version 1.2.0, Heltec V3.2 / ESP32-S3 and 8 MB flash.
- Update system: with no release, expect `No OTA release yet`. With the same version published,
  expect `System is up to date`. Offline checks must report `Connect WiFi first`.
- New release: publish a signed higher version and verify it is displayed. Back is selected initially;
  tap to select Install update, then hold and release. No installation occurs just by entering the page.
- Cancellation: hold PRG during check/download. The UI must remain responsive, and the running slot
  must remain selected. Download timeout or router loss must also leave the current slot selected.
- Invalid assets: using a test device/repository setup, try a modified image, wrong signature,
  missing signature, wrong board, oversized image, wrong certificate and an HTTP redirect. Every
  case must fail without switching the boot slot. Do not replace a real published release to test this.
- Success: install a signed higher version, check progress, restart, About's version and preserved
  WiFi credentials. Wait ten seconds, then reset again to confirm the new firmware remains selected.
- Rollback: on a test device, reset the newly installed firmware before ten-second health confirmation;
  the previous valid application should boot. Also test a build that fails the startup health check.
- Power interruption: on a test device, interrupt during download/write and during first boot before
  confirmation, then verify recovery. A desktop compile cannot establish these hardware behaviors.

## Current verification scope

The existing 1.0.0 USB firmware and WiFi setup were reported working on the user's board.
For 1.1.0, desktop checks cover protocol parsing, release signatures, binary identity and partition
layout. The end-to-end radio/download/flash/reboot/rollback checklist above still requires a board.
No board is flashed and no public release is published by the local development commands.

The pinned Heltec core's `sdkconfig` enables `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`
and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. `OtaUpdater.cpp` refuses to compile without the
certificate bundle and bootloader rollback support. The linked firmware must export a strong
`verifyRollbackLater` symbol so Arduino does not confirm the image before the health check. GitHub Actions must be run after the updated
files are pushed. Signed release publication additionally requires the repository signing secret.

## Verified local build: 2026-10-17

- Arduino CLI 1.5.1 / Heltec core 3.3.8 / pinned libraries: compilation passed.
- Final 1.1.0 application image: 1,190,128 bytes of 2,097,152 bytes per OTA slot.
- Compiler static RAM: 49,244 of 327,680 bytes.
- All 20 Python tests and the native C++ protocol tests passed.
- Real 1.1.0 release assets were signed and successfully verified with the existing project key.
- The linked ELF exports a strong `verifyRollbackLater` override, preserving deferred health confirmation.
- Compiled source copies match the final workspace files, excluding Arduino's generated #line directives.
- The generated partition binary is byte-for-byte identical to the user's previous 1.0.0 GitHub artifact.
- Workflow YAML parsed successfully. GitHub workflows and physical OTA/rollback have not been run here.

The packaged local release is `build/heltec-os-v1.1.0-ota.zip`; it contains only the application,
signed manifest and signature. Extract it and attach the three files individually to a GitHub
Release if using manual publication. No private key is in the archive.

## Verified local build: 2026-10-18 (1.2.0)

- Arduino CLI 1.5.1 / Heltec core 3.3.8 / pinned libraries: final compilation passed.
- Application image: 1,208,128 bytes of 2,097,152 bytes per OTA slot; partition validation passed.
- Compiler static RAM: 49,788 of 327,680 bytes.
- Compiled source copies match the workspace, excluding generated `#line` directives.
- All 20 Python release/validator tests and the native OTA protocol tests passed.
- Native Morse tests cover the alphabet/digits, button-duration boundaries, invalid and oversized
  input, symbol/letter/word pulse spacing, draft editing, held-button pauses and clock rollover.
- The embedded browser script passes syntax and simulated DOM/network tests for received messages,
  successful sends, busy-state rejection without draft loss, input validation, light stop and expired sessions.
- Both workflow YAML files parse. They include the new Morse and browser-script tests.
- No hardware was flashed or release published. Real phone/browser layout, WiFi delivery, PRG timing
  and LED playback still require the acceptance checks in `messages.md`; browser automation could
  not start in this desktop environment, so the browser tests above are simulated.
