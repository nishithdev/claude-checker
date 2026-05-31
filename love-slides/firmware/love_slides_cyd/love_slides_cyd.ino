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

// XPT2046 touch IRQ — goes LOW when screen is pressed (input-only GPIO)
#define T_IRQ 36

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

// ── Helpers ──────────────────────────────────────────────────────────────────
bool isTouched() { return digitalRead(T_IRQ) == LOW; }

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

  // Check if still unpatched (marker bytes still present)
  if ((uint8_t)SLIDES[idx][0] == 0xAA && (uint8_t)SLIDES[idx][1] == 0xBB) {
    tft.setTextColor(TFT_RED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("Messages not set!", W/2, H/2 - 10);
    tft.drawString("Use the Web Flasher to enter your messages.", W/2, H/2 + 6);
    return;
  }

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

  // ── Message text ──
  const char* msg = SLIDES[idx];
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

// ── Splash screen ─────────────────────────────────────────────────────────────
void drawSplash() {
  tft.fillScreen(C_BG);

  // Scattered small hearts
  drawHeart( 30,  30,  8, C_LINE);
  drawHeart(290,  30,  8, C_LINE);
  drawHeart( 30, 210,  8, C_LINE);
  drawHeart(290, 210,  8, C_LINE);
  drawHeart(160,  18,  6, C_LINE);

  // Big central heart
  drawHeart(W/2, 85, 36, C_HEART);

  // Title
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_PINK, C_BG);
  tft.setTextSize(2);
  tft.drawString("A message for you", W/2, 140);

  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.drawString("touch to begin", W/2, 164);

  // Bottom hearts row
  drawHeartRow(220, 7, C_LINE);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(T_IRQ, INPUT);

  tft.init();
  tft.setRotation(1);   // landscape

  drawSplash();

  // Wait for the first touch before showing slide 1
  while (!isTouched()) delay(30);
  delay(350);  // debounce

  g_slide = 0;
  drawSlide(g_slide);
}

void loop() {
  if (isTouched() && millis() - g_lastTouch > 450) {
    g_lastTouch = millis();
    g_slide = (g_slide + 1) % 10;
    drawSlide(g_slide);
  }
  delay(20);
}
