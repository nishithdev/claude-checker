/*
 * Love Slides — CYD Edition
 * ESP32 Cheap Yellow Display (CYD / ESP32-2432S028R)
 *
 * Displays 10 custom love messages as slides.
 * Touch the screen to advance to the next slide.
 * Messages are patched into the binary by the Web Flasher — no IDE needed.
 *
 * ── FIRST-TIME SETUP ──────────────────────────────────────
 * 1. Install library: TFT_eSPI (Bodmer) via Arduino Library Manager
 * 2. Copy User_Setup.h → Arduino/libraries/TFT_eSPI/User_Setup.h
 * 3. In Arduino IDE: Sketch → Export Compiled Binary
 *    Save the resulting .ino.bin as  love_slides_template.bin
 * 4. Open web-flasher/index.html in Chrome or Edge
 * 5. Upload love_slides_template.bin, type your 10 messages, click Flash
 *
 * ── UPDATING MESSAGES LATER ───────────────────────────────
 * Open index.html → upload the ORIGINAL love_slides_template.bin
 * → enter new messages → Flash again.
 * Always patch the ORIGINAL unmodified .bin (keep it safe!).
 *
 * ── BINARY PATCH FORMAT ───────────────────────────────────
 * Each of the 10 message slots is a 128-byte array.
 * The first 8 bytes are a magic marker the Web Flasher searches for:
 *
 *   MSG_01[128]: marker 0xAA 0xBB 0xCC 0xDD M S 0 1  (+120 nulls)
 *   MSG_02[128]: marker 0xAA 0xBB 0xCC 0xDD M S 0 2  (+120 nulls)
 *   … through …
 *   MSG_10[128]: marker 0xAA 0xBB 0xCC 0xDD M S 1 0  (+120 nulls)
 *
 * Max message length: 127 characters (null-terminated).
 */

#include <TFT_eSPI.h>
#include <SD.h>
#include <TJpg_Decoder.h>

// XPT2046 touch — IRQ on input-only GPIO, bit-bang SPI on VSPI pins
#define T_IRQ  36
#define T_CLK  25
#define T_DIN  32
#define T_OUT  39
// TOUCH_CS (33) is defined in User_Setup.h

// SD card — standard VSPI pins on CYD
#define SD_CS  5

// ── Patchable message slots ──────────────────────────────────────────────────
// Do NOT edit these manually — use the Web Flasher instead.
static char MSG_01[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','1'};
static char MSG_02[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','2'};
static char MSG_03[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','3'};
static char MSG_04[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','4'};
static char MSG_05[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','5'};
static char MSG_06[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','6'};
static char MSG_07[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','7'};
static char MSG_08[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','8'};
static char MSG_09[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','0','9'};
static char MSG_10[128] = {'\xAA','\xBB','\xCC','\xDD','M','S','1','0'};

const char* SLIDES[10] = {
  MSG_01, MSG_02, MSG_03, MSG_04, MSG_05,
  MSG_06, MSG_07, MSG_08, MSG_09, MSG_10
};

static const char* DEFAULTS[10] = {
  "Remember our first offical date",
  "First family function",
  "I fall a little more in love with you every single day.",
  "Home is wherever I am with you.",
  "You make ordinary days feel extraordinary.",
  "Loving you is the best thing that ever happened to me.",
  "A day is not enough to show all the love for you",
  "I choose you. Over and over, without pause, without a doubt.",
  "Thank you for being exactly who you are.",
  "Day 180 together. Each one better than the last. You are my home, my adventure, and my calm. Happy 180, Preethi."
};

// ── Photo slot ───────────────────────────────────────────────────────────────
// Full-screen 240×320 RGB565 image patched in by the Web Flasher (portrait).
// Marker byte [7] is '1' (unpatched) or '2' (patched).
#define IMG_W     240
#define IMG_H     320
#define IMG_BYTES (IMG_W * IMG_H * 2)

// PROGMEM keeps this in flash (153 KB is too large for DRAM).
// volatile read in isImgPatched prevents the compiler constant-folding the check.
static const uint8_t IMG_DATA[IMG_BYTES + 8] PROGMEM = {
  '\xAA','\xBB','\xCC','\xDD','I','M','G','1'
};

bool isImgPatched() {
  const volatile uint8_t* p = IMG_DATA;
  return p[7] == '2';
}

// ── Display ──────────────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
static const int W = 240, H = 320;

// Romantic color palette (RGB565)
// C_BG      ≈ #0D0012  deep dark plum
// C_HEART   ≈ #E8143C  vivid red
// C_PINK    ≈ #FF6EB4  warm pink
// C_GOLD    ≈ #FFD060  soft gold
// C_DIM     ≈ #886688  muted mauve
static const uint16_t C_BG    = 0x0802;
static const uint16_t C_HEART = 0xE027;
static const uint16_t C_PINK  = 0xFB76;
static const uint16_t C_GOLD  = 0xFEA0;
static const uint16_t C_DIM   = 0x8B4D;
static const uint16_t C_LINE  = 0x4812;

// ── State ────────────────────────────────────────────────────────────────────
int           g_slide       = 0;
unsigned long g_lastTouch   = 0;
bool          g_hasSDPhoto  = false;
bool          g_sdMounted   = false;
int           g_totalSlides = 10;  // becomes 11 when a photo is available

// ── Touch ────────────────────────────────────────────────────────────────────
void touchSetup() {
  pinMode(T_CLK,    OUTPUT); digitalWrite(T_CLK, LOW);
  pinMode(T_DIN,    OUTPUT);
  pinMode(T_OUT,    INPUT);
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  pinMode(T_IRQ,    INPUT);
}

bool isTouched() { return digitalRead(T_IRQ) == LOW; }

// Bit-bang SPI read from XPT2046 — bypasses hardware SPI to avoid bus conflicts.
// cmd 0x91 = Y-channel (maps to screen X in landscape), 0xD1 = X-channel.
static uint16_t touchRaw(uint8_t cmd) {
  digitalWrite(TOUCH_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_DIN, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH); delayMicroseconds(1);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(1);
  }
  uint16_t v = 0;
  for (int i = 15; i >= 0; i--) {
    digitalWrite(T_CLK, HIGH); delayMicroseconds(1);
    if (digitalRead(T_OUT)) v |= (1 << i);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(1);
  }
  digitalWrite(TOUCH_CS, HIGH);
  return (v >> 3) & 0xFFF;
}

// Screen X in landscape comes from XPT2046 Y-channel.
// Must use PD=00 (command 0x90) so PENIRQ is re-enabled after each conversion —
// any other PD setting disables T_IRQ, breaking all subsequent touches.
// High raw value → left side of screen. Average 4 reads for stability.
static bool touchIsLeft() {
  delay(2);
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) { sum += touchRaw(0xD0); delayMicroseconds(200); }
  return (sum / 4) > 2048;
}

// Draw a pixel-art heart centered at (cx, cy) with outer radius r
void drawHeart(int cx, int cy, int r, uint16_t col) {
  int rc = (r * 3) / 4;           // circle radius
  int ox = r / 2;                  // horizontal offset of circle centers
  int oy = r / 5;                  // vertical offset of circle centers above cy
  tft.fillCircle(cx - ox, cy - oy, rc, col);
  tft.fillCircle(cx + ox, cy - oy, rc, col);
  // Triangle fills in the bottom V of the heart
  tft.fillTriangle(cx - r, cy - oy + 2,
                   cx + r, cy - oy + 2,
                   cx,     cy + r,
                   col);
}

// Word-wrap text into maxW pixels wide, textSize sz, returns bottom Y reached
int drawWrapped(const char* text, int x, int y, int maxW,
                uint16_t color, uint8_t sz) {
  tft.setTextSize(sz);
  tft.setTextColor(color, C_BG);
  tft.setTextDatum(TL_DATUM);

  const int lineH = sz * 10 + 2;
  int curX = x, curY = y;

  char line[128] = "";
  char word[64]  = "";
  int  li = 0, wi = 0;
  const char* p = text;

  // Flush accumulated word into line, drawing if line would overflow
  auto flush = [&](bool forceNewline) {
    if (wi == 0 && !forceNewline) return;
    word[wi] = '\0';

    if (wi > 0) {
      // Build candidate line
      char candidate[128];
      if (li > 0) {
        snprintf(candidate, sizeof(candidate), "%s %s", line, word);
      } else {
        strncpy(candidate, word, sizeof(candidate)-1);
        candidate[sizeof(candidate)-1] = '\0';
      }

      if (tft.textWidth(candidate) > maxW && li > 0) {
        // Draw current line, start fresh with this word
        tft.drawString(line, x, curY);
        curY += lineH;
        strncpy(line, word, sizeof(line)-1);
        li = wi;
      } else {
        strncpy(line, candidate, sizeof(line)-1);
        li = strlen(line);
      }
      wi = 0;
    }

    if (forceNewline && li > 0) {
      tft.drawString(line, x, curY);
      curY += lineH;
      line[0] = '\0';
      li = 0;
    }
  };

  while (*p) {
    char c = *p++;
    if (c == ' ' || c == '\n') {
      flush(c == '\n');
    } else if (wi < (int)sizeof(word) - 1) {
      word[wi++] = c;
    }
  }
  flush(false);

  // Draw any remaining line
  if (li > 0) {
    tft.drawString(line, x, curY);
    curY += lineH;
  }

  return curY;
}

// Draw small decorative hearts along the top/bottom edge
void drawHeartRow(int y, int count, uint16_t col) {
  int spacing = W / count;
  for (int i = 0; i < count; i++) {
    int cx = spacing / 2 + i * spacing;
    drawHeart(cx, y, 5, col);
  }
}

// ── Slide renderer ────────────────────────────────────────────────────────────
void drawSlide(int idx) {
  if (idx == 10) { drawPhotoSlide(); return; }

  // Per-slide photo: /slide1.jpg … /slide10.jpg on SD card
  if (g_sdMounted) {
    char fname[16];
    snprintf(fname, sizeof(fname), "/slide%d.jpg", idx + 1);
    if (SD.exists(fname)) { drawPhotoSlideMsg(idx); return; }
  }

  tft.fillScreen(C_BG);

  // ── Header: big heart + slide counter ──
  drawHeart(W/2, 36, 22, C_HEART);

  // Slide dots
  int dotW = 8, dotGap = 4;
  int totalDotW = 10 * (dotW + dotGap) - dotGap;
  int dotX = (W - totalDotW) / 2;
  for (int i = 0; i < 10; i++) {
    uint16_t col = (i == idx) ? C_PINK : C_LINE;
    tft.fillCircle(dotX + i * (dotW + dotGap) + dotW/2, 72, dotW/2, col);
  }

  // Divider
  tft.drawFastHLine(16, 83, W - 32, C_LINE);

  // Use default message if this slot hasn't been patched via the Web Flasher
  bool unpatched = ((uint8_t)SLIDES[idx][0] == 0xAA && (uint8_t)SLIDES[idx][1] == 0xBB);
  const char* msg = unpatched ? DEFAULTS[idx] : SLIDES[idx];
  int textY = 93;
  int bottomY = drawWrapped(msg, 16, textY, W - 32, TFT_WHITE, 2);

  // ── Footer hint ──
  const int footerY = H - 18;
  tft.drawFastHLine(16, footerY - 6, W - 32, C_LINE);

  tft.setTextColor(C_DIM, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  if (idx < 9) {
    tft.drawString("~ touch to continue ~", W/2, footerY + 2);
  } else {
    // Last slide
    tft.setTextColor(C_PINK, C_BG);
    tft.drawString("~ with all my love ~", W/2, footerY + 2);
  }
}

// ── SD card JPEG ─────────────────────────────────────────────────────────────
// Dedicated VSPI instance for the SD card (CYD: SCK=18, MISO=19, MOSI=23, CS=5)
static SPIClass g_sdSPI(VSPI);

static bool jpegCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* data) {
  tft.pushImage(x, y, w, h, data);
  return true;
}

// Show a small status line at the bottom of the screen during boot.
static void sdStatus(const char* msg, uint16_t col = 0xC618) {
  tft.fillRect(0, H - 16, W, 16, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(col, C_BG);
  tft.setTextSize(1);
  tft.drawString(msg, W/2, H - 8);
}

// Display /photo.jpg from SD full-screen, scaled to fit 320×240.
// Returns true if the photo was rendered successfully.
static bool trySDPhoto() {
  sdStatus("Looking for SD card...");

  // Explicitly init VSPI with CYD's SD pins — do not rely on SPI global defaults
  g_sdSPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, g_sdSPI, 4000000)) {
    sdStatus("SD card not found", 0xF800);
    delay(1200);
    return false;
  }
  g_sdMounted = true;  // SD stays mounted for per-slide photos

  if (!SD.exists("/photo.jpg")) {
    sdStatus("No photo.jpg — slide photos still active", 0xFFE0);
    delay(800);
    return false;
  }

  sdStatus("Loading photo...");

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegCallback);

  uint16_t jw = 0, jh = 0;
  TJpgDec.getSdJpgSize(&jw, &jh, "/photo.jpg");
  if (jw == 0 || jh == 0) {
    sdStatus("JPEG decode failed (try baseline JPEG)", 0xF800);
    delay(1500);
    SD.end();
    return false;
  }

  // Largest power-of-2 scale where both dimensions fit within the display
  uint8_t scale = 1;
  while (scale < 8 && (jw / scale > (uint16_t)W || jh / scale > (uint16_t)H)) scale *= 2;
  TJpgDec.setJpgScale(scale);

  // Center the (possibly smaller-than-screen) scaled image
  int32_t ox = ((int32_t)W - jw / scale) / 2;
  int32_t oy = ((int32_t)H - jh / scale) / 2;
  tft.fillScreen(C_BG);
  TJpgDec.drawSdJpg(ox, oy, "/photo.jpg");
  return true;
}

// ── Photo slide (slide 11) ────────────────────────────────────────────────────
static void drawPhotoSlide() {
  tft.fillScreen(C_BG);

  if (g_hasSDPhoto) {
    uint16_t jw = 0, jh = 0;
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(jpegCallback);
    TJpgDec.getSdJpgSize(&jw, &jh, "/photo.jpg");
    if (jw > 0) {
      // Fill mode: largest scale where the image still covers the full screen
      // (crop the overflow edges rather than leaving black bars)
      uint8_t scale = 1;
      while (scale * 2 <= 8
             && (jw / (scale * 2) >= (uint16_t)W)
             && (jh / (scale * 2) >= (uint16_t)H)) {
        scale *= 2;
      }
      TJpgDec.setJpgScale(scale);
      // Centre the oversized image — negative offsets crop symmetrically
      int32_t ox = ((int32_t)W - jw / scale) / 2;
      int32_t oy = ((int32_t)H - jh / scale) / 2;
      TJpgDec.drawSdJpg(ox, oy, "/photo.jpg");
    }
  } else {
    tft.pushImage(0, 0, IMG_W, IMG_H,
      (uint16_t*)(const_cast<uint8_t*>(IMG_DATA) + 8));
  }

  // Text floats over the photo — C_BG background on each glyph keeps it readable
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_PINK, C_BG);
  tft.setTextSize(2);
  tft.drawString("with all my love", W/2, 296);

  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.drawString("~ always, forever ~", W/2, 312);
}

// ── Per-slide photo + message ─────────────────────────────────────────────────
// Renders /slideN.jpg full-screen with the message text overlaid on top.
static void drawPhotoSlideMsg(int idx) {
  tft.fillScreen(C_BG);

  char fname[16];
  snprintf(fname, sizeof(fname), "/slide%d.jpg", idx + 1);

  // Fill-mode JPEG display (same as drawPhotoSlide)
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegCallback);
  uint16_t jw = 0, jh = 0;
  TJpgDec.getSdJpgSize(&jw, &jh, fname);
  if (jw > 0) {
    uint8_t scale = 1;
    while (scale * 2 <= 8
           && jw / (scale * 2) >= (uint16_t)W
           && jh / (scale * 2) >= (uint16_t)H) scale *= 2;
    TJpgDec.setJpgScale(scale);
    int32_t ox = ((int32_t)W - jw / scale) / 2;
    int32_t oy = ((int32_t)H - jh / scale) / 2;
    TJpgDec.drawSdJpg(ox, oy, fname);
  }

  // Slides 2, 6, 7 (0-indexed: 1, 5, 6) show text at the bottom
  static const bool kTextBottom[10] = {false, true, false, false, false,
                                        true,  true, false, true,  false};
  bool textBottom = kTextBottom[idx];

  // Dark header bar: heart + slide dots
  tft.fillRect(0, 0, W, 66, C_BG);
  drawHeart(W/2, 20, 15, C_HEART);

  int dotW = 8, dotGap = 4;
  int totalDotW = 10 * (dotW + dotGap) - dotGap;
  int dotX = (W - totalDotW) / 2;
  for (int i = 0; i < 10; i++)
    tft.fillCircle(dotX + i*(dotW+dotGap) + dotW/2, 48, dotW/2,
                   (i == idx) ? C_PINK : C_LINE);
  tft.drawFastHLine(10, 58, W - 20, C_LINE);

  bool unpatched = ((uint8_t)SLIDES[idx][0] == 0xAA && (uint8_t)SLIDES[idx][1] == 0xBB);
  const char* msg = unpatched ? DEFAULTS[idx] : SLIDES[idx];

  if (textBottom) {
    // Text floats over the photo near the bottom — same per-glyph C_BG background, no tray
    drawWrapped(msg, 10, 185, W - 20, TFT_WHITE, 2);
  } else {
    // Text floats just below the header bar
    drawWrapped(msg, 10, 70, W - 20, TFT_WHITE, 2);
  }

  // Footer
  const int footerY = H - 18;
  if (!textBottom) tft.drawFastHLine(10, footerY - 6, W - 20, C_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  if (idx < 9) {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("~ touch to continue ~", W/2, footerY + 2);
  } else {
    tft.setTextColor(C_PINK, C_BG);
    tft.drawString("~ with all my love ~", W/2, footerY + 2);
  }
}

// Redraw a photo rectangle — used to "erase" the heart between idle beat frames
static void restorePhoto(int x, int y, int w, int h) {
  const uint16_t* img = (const uint16_t*)(IMG_DATA + 8);
  for (int row = 0; row < h; row++) {
    int iy = y + row;
    if (iy < 0 || iy >= IMG_H) continue;
    int ix = max(0, x);
    int iw = min(w + min(0, x), IMG_W - ix);
    if (iw <= 0) continue;
    tft.pushImage(ix, iy, iw, 1, (uint16_t*)(img + (size_t)iy * IMG_W + ix));
  }
}

// Draw gradient fade from photo → C_BG over `fadeH` rows starting at `y0`,
// then solid C_BG from `y0+fadeH` to bottom.
static void drawPhotoGradient(int y0, int fadeH) {
  uint16_t buf[W];
  const uint16_t* img = (const uint16_t*)(IMG_DATA + 8);
  uint8_t br = (C_BG >> 11) & 0x1F;
  uint8_t bg = (C_BG >> 5)  & 0x3F;
  uint8_t bb =  C_BG        & 0x1F;
  for (int row = 0; row < fadeH; row++) {
    int iy = y0 + row;
    uint8_t a = (uint8_t)((row * 255) / fadeH);
    const uint16_t* src = img + (size_t)iy * IMG_W;
    for (int x = 0; x < W; x++) {
      uint16_t p = src[x];
      uint8_t pr = (p >> 11) & 0x1F;
      uint8_t pg = (p >> 5)  & 0x3F;
      uint8_t pb =  p        & 0x1F;
      buf[x] = (uint16_t)(
        (((pr*(255-a) + br*a + 127) >> 8) << 11) |
        (((pg*(255-a) + bg*a + 127) >> 8) << 5)  |
         ((pb*(255-a) + bb*a + 127) >> 8)
      );
    }
    tft.pushImage(0, iy, W, 1, buf);
  }
  tft.fillRect(0, y0 + fadeH, W, H - (y0 + fadeH), C_BG);
}

// ── Boot animation ────────────────────────────────────────────────────────────
void animateSplash() {
  tft.fillScreen(C_BG);

  // Priority: SD card JPEG > binary-patched image > heart-only
  g_hasSDPhoto          = trySDPhoto();
  bool hasBinPhoto      = !g_hasSDPhoto && isImgPatched();
  int  idleHeartY       = 85;

  // ── SD card photo path ──────────────────────────────────────────────────────
  if (g_hasSDPhoto) {
    const int STRIP_Y = 264;   // solid dark strip starts here (portrait 320px)
    const int HEART_Y = 276;   // heart centre inside the strip
    idleHeartY = HEART_Y;

    // Photo is already on screen (drawn by trySDPhoto).
    // Draw a pink accent line + solid strip over the bottom.
    delay(80);
    tft.fillRect(0, STRIP_Y, W, H - STRIP_Y, C_BG);
    tft.drawFastHLine(0, STRIP_Y, W, C_PINK);

    // Heart pulses in inside the strip
    static const uint8_t kSP[] = {2, 5, 7, 9, 8, 6};
    int prevR = 0;
    for (int i = 0; i < (int)(sizeof(kSP) / sizeof(kSP[0])); i++) {
      int s = kSP[i];
      int cr = max(s, prevR) + 4;
      tft.fillRect(W/2 - cr, HEART_Y - cr, 2*cr, 2*cr, C_BG);
      drawHeart(W/2, HEART_Y, s, C_HEART);
      prevR = s;
      delay(70);
    }

    // "Hey Preethi," types in
    static const char kName[] = "Hey Preethi,";
    tft.setTextSize(2);
    int nameX = (W - tft.textWidth(kName)) / 2;
    int nameY = HEART_Y + 16;
    char nbuf[sizeof(kName)] = {};
    for (int i = 0; kName[i]; i++) {
      nbuf[i] = kName[i];
      tft.fillRect(0, nameY, W, 18, C_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_PINK, C_BG);
      tft.drawString(nbuf, nameX, nameY);
      delay(55);
    }
    delay(80);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextSize(1);
    tft.drawString("touch to begin", W/2, nameY + 18);

  // ── Binary flash photo path ─────────────────────────────────────────────────
  } else if (hasBinPhoto) {
    const int HEART_Y = 90;
    const int GRAD_Y  = 260;
    const int SOLID_Y = 276;
    idleHeartY = HEART_Y;

    // Sweep binary photo in top-to-bottom
    for (int row = 0; row < IMG_H; row += 16) {
      int rows = min(16, IMG_H - row);
      tft.pushImage(0, row, IMG_W, rows,
        (uint16_t*)(const_cast<uint8_t*>(IMG_DATA) + 8 + row * IMG_W * 2));
      delay(6);
    }
    delay(60);
    drawPhotoGradient(GRAD_Y, SOLID_Y - GRAD_Y);

    // Heart pulses in over photo (restores photo pixels each frame)
    static const uint8_t kIP[] = {5, 13, 21, 29, 37, 44, 40, 36};
    int prevR = 0;
    for (int i = 0; i < (int)(sizeof(kIP) / sizeof(kIP[0])); i++) {
      int s = kIP[i]; int clearR = max(s, prevR) + 6;
      restorePhoto(W/2 - clearR, HEART_Y - clearR, 2*clearR, 2*clearR);
      drawHeart(W/2, HEART_Y, s, C_HEART);
      prevR = s; delay(65);
    }

    static const char kName[] = "Hey Preethi,";
    tft.setTextSize(2);
    int nameX = (W - tft.textWidth(kName)) / 2;
    int nameY = SOLID_Y + 2;
    char nbuf[sizeof(kName)] = {};
    for (int i = 0; kName[i]; i++) {
      nbuf[i] = kName[i];
      tft.fillRect(0, nameY, W, 18, C_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_PINK, C_BG);
      tft.drawString(nbuf, nameX, nameY);
      delay(55);
    }
    delay(80);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextSize(1);
    tft.drawString("touch to begin", W/2, nameY + 20);

  // ── Heart-only path ─────────────────────────────────────────────────────────
  } else {
    idleHeartY = 130;

    static const uint8_t kIntro[] = {5, 13, 21, 29, 37, 44, 40, 36};
    int prevR = 0;
    for (int i = 0; i < (int)(sizeof(kIntro) / sizeof(kIntro[0])); i++) {
      int s = kIntro[i]; int clearR = max(s, prevR) + 6;
      tft.fillRect(W/2 - clearR, 130 - clearR, 2*clearR, 2*clearR, C_BG);
      drawHeart(W/2, 130, s, C_HEART);
      prevR = s; delay(65);
    }

    delay(80);
    drawHeart( 25,  25, 8, C_LINE); delay(80);
    drawHeart(215,  25, 8, C_LINE); delay(80);
    drawHeart( 25, 295, 8, C_LINE); delay(80);
    drawHeart(215, 295, 8, C_LINE); delay(80);
    drawHeart(120,  16, 6, C_LINE); delay(80);

    static const char kTitle[] = "Hey Preethi,";
    tft.setTextSize(2);
    int titleX = (W - tft.textWidth(kTitle)) / 2;
    int titleY = 200;
    char tbuf[sizeof(kTitle)] = {};
    for (int i = 0; kTitle[i]; i++) {
      tbuf[i] = kTitle[i];
      tft.fillRect(0, titleY, W, 18, C_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_PINK, C_BG);
      tft.drawString(tbuf, titleX, titleY);
      delay(55);
    }

    delay(120);
    int spc = W / 7;
    for (int i = 0; i < 7; i++) { drawHeart(spc/2 + i*spc, 290, 5, C_LINE); delay(70); }
    delay(100);
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("touch to begin", W/2, 240);
  }

  // ── Idle heartbeat — all paths ──────────────────────────────────────────────
  static const int8_t kBeatBig[]  = {38, 41, 38, 36};
  static const int8_t kBeatSmall[] = {6, 8, 6, 5};
  const int8_t* kBeat = g_hasSDPhoto ? kBeatSmall : kBeatBig;
  int beatR           = g_hasSDPhoto ? 6 : 36;

  while (!isTouched()) {
    for (int b = 0; b < 4 && !isTouched(); b++) {
      int s = kBeat[b];
      int clearR = max(s, beatR) + 4;
      if (hasBinPhoto) {
        restorePhoto(W/2 - clearR, idleHeartY - clearR, 2*clearR, 2*clearR);
      } else {
        tft.fillRect(W/2 - clearR, idleHeartY - clearR, 2*clearR, 2*clearR, C_BG);
      }
      drawHeart(W/2, idleHeartY, s, C_HEART);
      beatR = s;
      delay(55);
    }
    unsigned long t = millis();
    while (!isTouched() && millis() - t < 700) delay(20);
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  touchSetup();

  tft.init();
  tft.setRotation(0);   // portrait

  animateSplash();
  delay(350);  // debounce after touch

  g_totalSlides = (g_hasSDPhoto || isImgPatched()) ? 11 : 10;
  g_slide = 0;
  drawSlide(g_slide);
}

void loop() {
  if (isTouched() && millis() - g_lastTouch > 450) {
    g_lastTouch = millis();
    if (touchIsLeft()) {
      g_slide = (g_slide - 1 + g_totalSlides) % g_totalSlides;
    } else {
      g_slide = (g_slide + 1) % g_totalSlides;
    }
    drawSlide(g_slide);
  }
  delay(20);
}
