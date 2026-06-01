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

// XPT2046 touch — IRQ on input-only GPIO, bit-bang SPI on VSPI pins
#define T_IRQ  36
#define T_CLK  25
#define T_DIN  32
#define T_OUT  39
// TOUCH_CS (33) is defined in User_Setup.h

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
  "Every moment with you is a gift I never want to return.",
  "You are my favorite notification.",
  "I fall a little more in love with you every single day.",
  "Home is wherever I am with you.",
  "You make ordinary days feel extraordinary.",
  "Loving you is the best thing that ever happened to me.",
  "You had me at hello, and you still have me every day since.",
  "I choose you. Over and over, without pause, without a doubt.",
  "Thank you for being exactly who you are.",
  "You are my today and all of my tomorrows."
};

// ── Display ──────────────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
static const int W = 320, H = 240;

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
int           g_slide    = 0;
unsigned long g_lastTouch = 0;

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
  for (int i = 0; i < 4; i++) { sum += touchRaw(0x90); delayMicroseconds(200); }
  return (sum / 4) < 2048;
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

// ── Boot animation ────────────────────────────────────────────────────────────
void animateSplash() {
  tft.fillScreen(C_BG);

  // Phase 1: central heart grows in, overshoots, settles
  static const uint8_t kIntro[] = {5, 13, 21, 29, 37, 44, 40, 36};
  int prevR = 0;
  for (int i = 0; i < (int)(sizeof(kIntro) / sizeof(kIntro[0])); i++) {
    int s = kIntro[i];
    int clearR = max(s, prevR) + 6;
    tft.fillRect(W/2 - clearR, 85 - clearR, 2 * clearR, 2 * clearR, C_BG);
    drawHeart(W/2, 85, s, C_HEART);
    prevR = s;
    delay(65);
  }

  // Phase 2: corner accent hearts pop in
  delay(80);
  drawHeart( 30,  30, 8, C_LINE); delay(80);
  drawHeart(290,  30, 8, C_LINE); delay(80);
  drawHeart( 30, 210, 8, C_LINE); delay(80);
  drawHeart(290, 210, 8, C_LINE); delay(80);
  drawHeart(160,  18, 6, C_LINE); delay(80);

  // Phase 3: title types in character by character
  static const char kTitle[] = "A message for you";
  tft.setTextSize(2);
  int titleX = (W - tft.textWidth(kTitle)) / 2;
  int titleY = 132;
  char buf[sizeof(kTitle)] = {};
  for (int i = 0; kTitle[i]; i++) {
    buf[i] = kTitle[i];
    tft.fillRect(0, titleY, W, 18, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_PINK, C_BG);
    tft.drawString(buf, titleX, titleY);
    delay(55);
  }

  // Phase 4: bottom heart row appears left to right
  delay(120);
  int spacing = W / 7;
  for (int i = 0; i < 7; i++) {
    drawHeart(spacing / 2 + i * spacing, 220, 5, C_LINE);
    delay(70);
  }

  // Touch hint
  delay(100);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawString("touch to begin", W/2, 164);

  // Idle heartbeat loop — keeps animating until the screen is touched
  // Clears only the heart bounding box per frame so nothing else flickers
  static const int8_t kBeat[] = {38, 41, 38, 36};
  int beatR = 36;
  while (!isTouched()) {
    for (int b = 0; b < 4 && !isTouched(); b++) {
      int s = kBeat[b];
      int clearR = max(s, beatR) + 4;
      tft.fillRect(W/2 - clearR, 85 - clearR, 2 * clearR, 2 * clearR, C_BG);
      drawHeart(W/2, 85, s, C_HEART);
      beatR = s;
      delay(55);
    }
    // Rest between beats (~60 BPM)
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
  tft.setRotation(1);   // landscape

  animateSplash();
  delay(350);  // debounce after touch

  g_slide = 0;
  drawSlide(g_slide);
}

void loop() {
  if (isTouched() && millis() - g_lastTouch > 450) {
    g_lastTouch = millis();
    if (touchIsLeft()) {
      g_slide = (g_slide - 1 + 10) % 10;
    } else {
      g_slide = (g_slide + 1) % 10;
    }
    drawSlide(g_slide);
  }
  delay(20);
}
