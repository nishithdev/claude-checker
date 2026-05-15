# Claude Checker — ESP32 Cheap Yellow Display

Displays your Claude.ai usage limits live on an ESP32-2432S028R (Cheap Yellow Display).

- **5-hour rolling session** usage + countdown to reset
- **7-day all-models** weekly usage
- **7-day Opus / Opus 4** weekly usage
- Auto-refreshes every 30 seconds

![CYD display showing Claude usage bars](https://raw.githubusercontent.com/nishithdev/claude-checker/main/docs/preview.png)

---

## Quickest start — Web Flasher (no IDE needed)

1. Open [`web-flasher/index.html`](web-flasher/index.html) in **Chrome or Edge**
2. Enter your WiFi credentials and Claude session key on the **Configure** tab
3. Click **Flash** → **Connect Device** → select your CYD's COM port
4. Click **Patch & Flash** — done in ~30 seconds

The firmware is pre-compiled and embedded in the HTML file.

### Getting your session key

1. Open [claude.ai](https://claude.ai) in Chrome and log in
2. Press `F12` → **Application** tab → Storage → Cookies → `https://claude.ai`
3. Find the cookie named `sessionKey` → double-click Value → `Ctrl+A` → Copy

Session keys expire periodically. When the display shows *"Auth failed"*, grab a fresh key and re-flash.

---

## Alternative — Arduino IDE (hardcoded credentials)

1. Install libraries via Library Manager: **TFT_eSPI** (Bodmer), **ArduinoJson** (Blanchon v6)
2. Copy `firmware/claude_usage_cyd/User_Setup.h` → `Arduino/libraries/TFT_eSPI/User_Setup.h`
3. Open `firmware/claude_usage_cyd/claude_usage_cyd.ino`, fill in your WiFi + session key at the top
4. Board: **ESP32 Dev Module** · Flash: 4MB · Upload speed: 921600 → Upload

---

## Hardware

- **Board:** ESP32-2432S028R (Cheap Yellow Display / CYD)
- **Display:** ILI9341 2.8" 320×240 TFT
- **Connection:** USB-C (same cable used for flashing and power)

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| No COM port shown | Install [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) |
| Connect fails | Hold BOOT button while clicking Connect |
| Auth failed on display | Session key expired — get a new one and re-flash |
| WiFi failed | CYD only supports 2.4 GHz networks |
| All values show 0 | Flash the latest version (API field names updated) |

---

## License

MIT
