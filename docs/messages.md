# Phone and board messages

Heltec OS 1.3.0 supports **Messages** over your existing WiFi or a **Device hotspot**. The phone and board share a local
conversation, and every accepted message is also emitted in Morse using the onboard LED
(`LED_BUILTIN`, GPIO 35 on the pinned Heltec V3 board). No extra wires, headers, antenna
or cloud account are needed for this feature. It uses WiFi; the LoRa radio is not used.

## Choose the connection mode

Open **WiFi > Connection mode**. Tap PRG to move, then hold for at least 0.7 seconds and
release to select **WiFi network** or **Device hotspot**. The asterisk marks the active mode.
The choice is saved for the next startup. Switching modes retains the draft, message history
and router credentials; it disconnects the phone, so reconnect it to the selected network.
Only one messaging mode runs at a time. A storage failure is shown as **Settings not saved**
or **Mode not saved**; in that case the current mode works but may not survive a restart.

### Device hotspot: iPhone without a router or Internet

1. Choose **WiFi > Connection mode > Device hotspot**.
2. The OLED shows a network such as **HeltecChat-A1B2** and its **Key**.
3. On the iPhone, open **Settings > Wi-Fi**, select that network and enter the displayed Key.
   A no-Internet warning is expected: stay connected to this network.
4. Tap PRG once on the board's Phone access screen to see the address and message access Code.
5. Open **Safari** and type **http://192.168.4.1/** explicitly. No app or automatic captive-portal
   window is required. Enter the **Code** from the OLED to open the conversation.
6. Send from Safari or from **Messages > Compose**. Both directions and the Morse LED work offline.

The hotspot has a generated 12-character WiFi password, stored on the board for future sessions.
It supports two connected clients and stays active until you change modes or turn off the board;
it does not expire after five minutes. **Messages > Phone access** shows the network/Key and
address/Code on two pages; tap PRG to switch pages, hold to return. **WiFi > Connection status**
also shows the hotspot details and number of connected clients.

The **Key** joins the WiFi network; the separate **Code** opens the message page and changes
on each restart. The board does not provide Internet access or relay the phone's mobile data.
**Update system** explains that updates require WiFi network mode with Internet.

### WiFi network: use your usual router

1. Choose **WiFi > Connection mode > WiFi network**. The board reconnects to the saved router.
   If none is saved, use **WiFi > Scan networks** to configure one.
2. Connect your phone to the same local network. Guest/client isolation can prevent access.
3. On the board open **Messages > Phone access**.
4. Enter `http://` followed by the displayed IP address in your phone browser.
   This is the board's router-assigned address, not the temporary `192.168.4.1` setup address.
5. Enter the eight-character **Code** from the OLED. The code changes at each restart.

The phone remembers its session until the board restarts or the browser clears its cookies.
Messages use local HTTP, so use a trusted WiFi network. The code restricts access but does
not encrypt network traffic. Do not expose the web server through router port forwarding.
The message page is not available through the temporary WiFi password setup access point.

## Send from the phone

Type 1-80 characters and press **Send to board + light**. Supported characters are A-Z,
0-9 and spaces. Lowercase becomes uppercase and repeated spaces are collapsed. Accented
letters, punctuation, emoji and line breaks are rejected with an explanation.

The text is added immediately to the conversation and **Messages > Inbox** on the OLED.
It also starts LED playback. The browser polls approximately every 1.5 seconds while open;
this is not a background phone notification service. On the board, tap in Inbox to read the
next text page and then the next older message. Hold PRG to return.

## Send from the board

Open **Messages > Compose**. PRG has special controls in this screen only:

| Gesture (duration before release) | Result |
| --- | --- |
| Less than 350 ms | Add a dot |
| 350-1799 ms | Add a dash |
| Pause 1.2 seconds without pressing | Finish and decode the current letter |
| Hold at least 1.8 seconds and release | Finish the letter and open draft actions |

The OLED shows the draft, current marks and the action for the held button. In draft actions,
normal menu controls apply: tap to move, hold at least 0.7 seconds and release to select.

- **Continue writing** returns to Morse input.
- **Add space** adds a word separator and returns to input.
- **Send + light** adds the message to the shared history and starts LED playback.
- **Delete last** removes the last character and returns to input.
- **Clear draft** clears the draft and returns to input.
- **Back to Messages** leaves the editor, keeping the unsent draft until restart.

Example: write `SOS` as `...`, wait 1.2 seconds, `---`, wait 1.2 seconds, `...`, wait.
Hold PRG for 1.8 seconds, select **Send + light**, and confirm. An open authenticated phone
page will show **Board → phone** on its next refresh.

An unknown Morse sequence is discarded with a notice. The phone page includes an alphabet
reference. For digits: 0 `-----`, 1 `.----`, 2 `..---`, 3 `...--`, 4 `....-`, 5 `.....`,
6 `-....`, 7 `--...`, 8 `---..`, 9 `----.`.

## LED and storage

The unit duration is 120 ms: dot 1 unit, dash 3, gap between marks 1, letters 3, words 7.
Playback runs without delay loops; menus and WiFi remain usable. Only one message can flash
at a time. A new send is rejected while busy, preserving the draft. Use **Messages > Stop light**
or the phone's **Stop light** button to stop early. Stopping the light retains the message.
OTA checks/installation stop light playback and temporarily reject new messages.

The last 12 messages are held in RAM; the oldest is replaced when full. Restart clears the
history and draft. Offline board messages can still flash and remain available for the phone
when WiFi reconnects, until overwritten or restarted. "Sent" means stored on the board, not
that a person has read it; LED playback does not confirm reception by another device.

## Install or publish

The source version is **1.3.0**. Compile/upload by USB with full-flash erase disabled, or commit
and push all changes and publish tag **v1.3.0** using the existing signing secret/workflow.
Devices with the HTTPS transmit-buffer fix can install the signed release via **Update system**.
An original 1.1.0 device affected by `HTTP_CLIENT: Out of buffer` needs the fixed firmware by USB.

## Hardware acceptance checks

These checks require a real board and phone; compilation/native tests do not prove them:

1. Confirm WiFi setup still works; the temporary access point must reject `/messages` and API access.
2. Pair with the OLED code. Wrong codes and API requests without a session must be rejected.
3. Send `hello 123` from the phone; verify `HELLO 123` in Inbox and light playback.
4. Compose `SOS`, a space and a digit on PRG; check letter separation and both directions of history.
5. Send 80 characters and page through every character on OLED. Send 13 messages, stopping the
   light between them, and confirm the history retains the latest 12.
6. Try another send during playback: no message/draft should disappear. Stop light from both interfaces.
7. Navigate menus, check OTA and disconnect/reconnect WiFi during use; the board must stay responsive.
8. Restart: LED starts off, history clears, old phone sessions fail and a new code works.
9. Select Device hotspot with the router off. Join from iPhone, open Safari at the displayed
   address, and repeat both messaging directions and LED checks without Internet.
10. Leave the hotspot running for more than five minutes; it must remain available. Restart:
    the hotspot mode, network name and Key must return; only the message Code changes.
11. Switch to WiFi network: the hotspot must disappear and the saved router reconnect. Rejoin
    the router on the phone and use the board's router-assigned IP. Existing messages remain.
12. In hotspot mode, scanning must ask for WiFi network mode and Update system must explain
    that Internet is required. Forget network must not stop the hotspot. After switching back,
    configure a protected router network and check setup success, failure, retry and cancellation.
