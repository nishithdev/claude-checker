# Love Slides

A romantic 10-slide message display for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028R)**. Touch the screen to flip through love messages. Optionally show a personal photo on the final slide. No coding required — everything is customized through a browser-based web flasher.

---

## What you need

- ESP32-2432S028R (CYD) board
- USB-A to USB-B (or USB Micro) cable
- Google Chrome or Microsoft Edge (Web Serial API required)
- macOS, Windows, or Linux

---

## Quick start (no build required)

The repo ships with a pre-compiled binary already embedded in the web flasher.

1. **Open the flasher** — double-click `start.command` (macOS) or open `web-flasher/index.html` directly in Chrome/Edge.
2. **Connect your CYD** via USB.
3. **Enter your 10 messages** in the text fields (up to 127 characters each).
4. *(Optional)* **Upload a photo** — any JPEG, it gets embedded into the binary.
5. Click **Flash** and wait ~30 seconds.
6. The device reboots automatically and shows your slideshow.

> **Touch left half** of the screen → previous slide  
> **Touch right half** → next slide

---

## Adding photos via SD card

Insert a MicroSD card with any of the following files at the root:

| File | Purpose |
|---|---|
| `photo.jpg` | Full-screen photo shown on the final (slide 11) |
| `slide1.jpg` … `slide10.jpg` | Per-slide background photos with the message overlaid |

The device will detect the SD card on boot and use those photos automatically. Standard baseline JPEG only — progressive JPEGs are not supported.

---

## Customizing messages later

Open the web flasher again → enter new messages → click Flash.  
Always use the **original unmodified template binary** when re-flashing messages. The flasher patches a fresh copy each time.

---

## Building from source (optional)

Only needed if you modify the firmware code.

### Prerequisites

- Arduino IDE 2.x **or** `arduino-cli` installed
- Libraries (auto-installed by the build script):
  - `TFT_eSPI` (Bodmer)
  - `TJpg_Decoder`
  - ESP32 board core (`esp32:esp32`)

### Build & embed (macOS)

Double-click **`build_and_embed.command`** in the project root. It will:

1. Locate `arduino-cli` (installs ESP32 core and libraries if missing)
2. Patch `User_Setup.h` into the TFT_eSPI library
3. Compile the sketch at `firmware/love_slides_cyd/love_slides_cyd.ino`
4. Embed the resulting binary into `web-flasher/index.html`

Then use `start.command` to launch the flasher and flash your device.

### Manual embed (all platforms)

```bash
./embed_firmware.sh firmware/love_slides_cyd/build/esp32.esp32.esp32/love_slides_cyd.ino.merged.bin
```

---

## Project structure

```
love-slides/
├── firmware/
│   └── love_slides_cyd/
│       ├── love_slides_cyd.ino   # Main firmware
│       └── User_Setup.h          # TFT_eSPI pin config for CYD
├── web-flasher/
│   └── index.html                # Self-contained browser flasher
├── filtered-images/              # Sample slide images
├── build_and_embed.command       # macOS: compile + embed in one click
├── start.command                 # macOS: launch local flasher server
└── embed_firmware.sh             # Embed a .bin into the HTML flasher
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Serial port not showing in flasher | Use Chrome or Edge; try a different USB cable (data cable, not charge-only) |
| Flash fails partway through | Hold the BOOT button on the CYD while clicking Flash |
| Screen stays black | Check that the USB cable delivers power; try re-flashing |
| SD card not detected | Format as FAT32; use a card ≤32 GB |
| JPEG not showing | Re-export as baseline JPEG (not progressive) at 240×320 or larger |
| Messages show defaults after flash | Make sure you're patching the original unmodified template binary |
