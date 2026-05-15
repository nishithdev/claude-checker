/*
 * Claude Usage Display for ESP32 Cheap Yellow Display (CYD)
 * Board: ESP32-2432S028R  |  Display: ILI9341 320×240
 *
 * Shows your Claude.ai usage in real-time:
 *   • 5-hour session window  (rolling)
 *   • 7-day weekly limit     (all models)
 *   • 7-day Opus limit       (Opus-specific)
 * Refreshes every 30 seconds.
 *
 * ── SETUP ──────────────────────────────────────────────────
 * 1. Install libraries in Arduino IDE (Tools → Manage Libraries):
 *      - TFT_eSPI       by Bodmer
 *      - ArduinoJson    by Benoit Blanchon
 *
 * 2. Copy User_Setup.h (provided alongside this sketch) into:
 *      Arduino/libraries/TFT_eSPI/User_Setup.h
 *    (overwrite the existing file)
 *
 * 3. Get your Session Key:
 *      a. Open https://claude.ai in Chrome
 *      b. Press F12  →  Application tab  →  Cookies  →  https://claude.ai
 *      c. Find the cookie named "sessionKey"
 *      d. Copy its value (starts with sk-ant-sid01-...)
 *
 * 4. Fill in WIFI_SSID, WIFI_PASSWORD, and SESSION_KEY below.
 *
 * 5. Select board: "ESP32 Dev Module"  (or "ESP32-WROOM-DA Module")
 *    Upload speed 921600, Flash 4MB.
 *
 * ── SECURITY NOTE ──────────────────────────────────────────
 * The session key grants full access to your Claude account.
 * Keep this sketch private and never share the compiled binary.
 * Session keys expire periodically — re-extract and re-flash if
 * the display shows "Auth failed".
 * ──────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "time.h"

// ============================================================
//  USER CONFIGURATION
// ============================================================
const char* WIFI_SSID       = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD   = "YOUR_WIFI_PASSWORD";

// Paste your sessionKey cookie value here (sk-ant-sid01-...)
const char* SESSION_KEY     = "sk-ant-sid01-XXXXXXXXXXXXXXXX";

// Refresh interval in milliseconds (30 000 = 30 seconds)
const unsigned long REFRESH_MS = 30000;

// Your timezone offset from UTC in seconds (e.g. UTC+5:30 = 19800)
// Used only for displaying the local clock.
const long TZ_OFFSET_SEC    = 0;
// ============================================================

// ── TFT & layout ────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

static const int W  = 320;
static const int H  = 240;

// Palette — Claude warm theme
// C_BG      ≈ #1C1817  very dark warm charcoal
// C_HDR     ≈ #2A2420  slightly lighter warm header
// C_ACCENT  ≈ #FF8C00  Claude warm amber
// C_DIM     ≈ #8A8888  muted warm gray
// C_DIVIDER ≈ #323030  subtle warm divider
// C_BAR_BG  ≈ #201C1C  dark bar background
static const uint16_t C_BG       = 0x18C2;
static const uint16_t C_HDR      = 0x2924;
static const uint16_t C_ACCENT   = 0xFC60;
static const uint16_t C_WHITE    = TFT_WHITE;
static const uint16_t C_DIM      = 0x8C51;
static const uint16_t C_DIVIDER  = 0x3186;
static const uint16_t C_BAR_BG   = 0x2104;
static const uint16_t C_GREEN    = 0x07E0;
static const uint16_t C_YELLOW   = 0xFFE0;
static const uint16_t C_ORANGE   = 0xFD20;
static const uint16_t C_RED      = 0xF800;

// ── State ────────────────────────────────────────────────────
String g_orgId      = "";
float  g_session    = 0.0f;   // five_hour utilization %
float  g_weekly     = 0.0f;   // seven_day utilization %
String g_resetAt    = "";     // ISO 8601 reset timestamp
bool   g_hasData    = false;
unsigned long g_lastRefresh = 0;
int    g_errorCode  = 0;      // last HTTP error (0 = none)

// ── Forward declarations ─────────────────────────────────────
bool     fetchOrgId();
bool     fetchUsage();
void     drawScreen();
void     drawClaudeMascot(int cx, int cy, int r);
void     drawSection(int y, int h, const char* label,
                     float pct, const char* sub);
void     drawBar(int x, int y, int bw, int bh, float pct,
                 uint16_t color);
uint16_t usageColor(float pct);
String   timeUntil(const String& iso);
void     splashStatus(const String& msg, uint16_t color = C_WHITE);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Backlight on
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);   // Landscape: 320 wide × 240 tall
  tft.fillScreen(C_BG);

  // Splash — mascot + title
  drawClaudeMascot(W / 2, 70, 5);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("CLAUDE USAGE", W / 2, H / 2 + 8);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.drawString("ESP32 Cheap Yellow Display", W / 2, H / 2 + 30);

  // WiFi
  splashStatus("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    splashStatus("WiFi failed! Check credentials.", C_RED);
    delay(5000);
    ESP.restart();
  }
  splashStatus("WiFi OK — syncing time...");

  // NTP time (UTC base; local clock adjusted by TZ_OFFSET_SEC)
  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tmp;
  for (int i = 0; i < 20 && !getLocalTime(&tmp); i++) delay(500);

  // Fetch org ID (only needed once)
  splashStatus("Fetching org ID...");
  if (!fetchOrgId()) {
    splashStatus("Could not get org ID. Check session key.", C_RED);
    delay(5000);
    // Will retry in loop
  }

  // First data fetch
  splashStatus("Loading usage data...");
  fetchUsage();
  drawScreen();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Re-try org ID if we never got it
  if (g_orgId.isEmpty()) {
    if (millis() - g_lastRefresh > 15000) {
      fetchOrgId();
      g_lastRefresh = millis();
    }
    return;
  }

  if (millis() - g_lastRefresh >= REFRESH_MS) {
    g_lastRefresh = millis();
    if (fetchUsage()) {
      drawScreen();
    } else {
      // Redraw with error indicator but keep old data visible
      drawScreen();
    }
  }
}

// ============================================================
//  API — fetch organisation list, extract first org UUID
// ============================================================
bool fetchOrgId() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();   // Personal-use device: skip CA verify

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, "https://claude.ai/api/organizations");
  http.addHeader("Cookie",     String("sessionKey=") + SESSION_KEY);
  http.addHeader("Accept",     "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (compatible; ESP32-CYD)");

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[orgId] HTTP %d\n", code);
    g_errorCode = code;
    http.end();
    return false;
  }

  // Response is a JSON array: [ { "uuid": "...", "name": "..." }, ... ]
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("[orgId] JSON error: %s\n", err.c_str());
    return false;
  }

  // Handle both array and object-with-array shapes
  JsonVariant root = doc.as<JsonVariant>();
  JsonArray arr;
  if (root.is<JsonArray>()) {
    arr = root.as<JsonArray>();
  } else if (root["organizations"].is<JsonArray>()) {
    arr = root["organizations"].as<JsonArray>();
  }

  for (JsonObject org : arr) {
    const char* uuid = org["uuid"].as<const char*>();
    if (!uuid || strlen(uuid) < 4) uuid = org["id"].as<const char*>();
    if (uuid && strlen(uuid) > 4) {
      g_orgId = String(uuid);
      Serial.println("[orgId] Got: " + g_orgId);
      return true;
    }
  }

  Serial.println("[orgId] No org found in response");
  return false;
}

// ============================================================
//  API — fetch usage for our org
// ============================================================
bool fetchUsage() {
  if (WiFi.status() != WL_CONNECTED || g_orgId.isEmpty()) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(10000);
  String url = "https://claude.ai/api/organizations/" + g_orgId + "/usage";
  http.begin(client, url);
  http.addHeader("Cookie",     String("sessionKey=") + SESSION_KEY);
  http.addHeader("Accept",     "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (compatible; ESP32-CYD)");

  int code = http.GET();
  g_errorCode = (code == 200) ? 0 : code;

  if (code != 200) {
    Serial.printf("[usage] HTTP %d\n", code);
    http.end();
    return false;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("[usage] JSON error: %s\n", err.c_str());
    return false;
  }

  g_session = doc["five_hour"]["utilization"] | 0.0f;
  g_weekly  = doc["seven_day"]["utilization"] | 0.0f;
  g_resetAt = doc["five_hour"]["resets_at"].as<String>();
  if (g_resetAt == "null") g_resetAt = "";

  g_hasData     = true;
  g_lastRefresh = millis();
  return true;
}

// ============================================================
//  Claude mascot: amber circle face with white eyes and smile.
//  r=9  → fits the 26px header bar
//  r=24 → splash-screen hero icon
// ============================================================
// Draw Claw'd — Claude's pixel-art mascot.
// cx, cy = center of body,  u = unit pixel size
//   Header call: drawClaudeMascot(13, 13, 2)  →  20×16 px total
//   Splash call: drawClaudeMascot(W/2, 70, 5) →  50×40 px total
void drawClaudeMascot(int cx, int cy, int u) {
  const uint16_t CLAY = 0xCB08;   // terracotta coral ≈ #C86040
  const uint16_t DARK = C_BG;     // eye fill

  int bw = 8*u, bh = 6*u;
  int bx = cx - bw/2, by = cy - bh/2;

  // Body
  tft.fillRect(bx, by, bw, bh, CLAY);

  // Eyes — square dark cutouts, ≈1.5u each
  int ew = max(2, u + u/2);
  tft.fillRect(bx + u,           by + u, ew, ew, DARK);
  tft.fillRect(bx + bw - u - ew, by + u, ew, ew, DARK);

  // Arms — u×2u stubs on each side at body mid-height
  tft.fillRect(bx - u,  by + bh/2 - u, u, 2*u, CLAY);
  tft.fillRect(bx + bw, by + bh/2 - u, u, 2*u, CLAY);

  // Legs — 2u×2u at bottom, inset 1u from body edges
  tft.fillRect(bx + u,        by + bh, 2*u, 2*u, CLAY);
  tft.fillRect(bx + bw - 3*u, by + bh, 2*u, 2*u, CLAY);
}

// ============================================================
//  DISPLAY — full screen redraw
// ============================================================
void drawScreen() {
  tft.fillScreen(C_BG);

  // ── Header bar ─────────────────────────────────────────────
  tft.fillRect(0, 0, W, 26, C_HDR);
  // Small mascot in header (left side)
  drawClaudeMascot(13, 13, 2);
  tft.setTextColor(C_ACCENT, C_HDR);
  tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("CLAUDE USAGE", 28, 13);

  // Clock (top-right)
  struct tm ti;
  char clk[9] = "--:--";
  if (getLocalTime(&ti)) {
    sprintf(clk, "%02d:%02d", ti.tm_hour, ti.tm_min);
  }
  uint16_t wifiCol = (WiFi.status() == WL_CONNECTED) ? C_GREEN : C_RED;
  tft.setTextColor(wifiCol, C_HDR);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(String(clk) + "  ", W, 13);

  // ── No-data placeholder ────────────────────────────────────
  if (!g_hasData) {
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (g_errorCode == 401 || g_errorCode == 403) {
      tft.drawString("Auth failed — re-extract session key", W / 2, H / 2 - 8);
      tft.drawString("and re-flash the sketch.", W / 2, H / 2 + 8);
    } else if (g_errorCode != 0) {
      char buf[40];
      sprintf(buf, "HTTP error %d — retrying...", g_errorCode);
      tft.drawString(buf, W / 2, H / 2);
    } else {
      tft.drawString("Waiting for data...", W / 2, H / 2);
    }
    return;
  }

  // ── Three usage sections ────────────────────────────────────
  //  Divide the 214px below the header into 3 equal rows of ~71px,
  //  minus 20px footer at the bottom.
  //  y positions: 26, 97, 168  (each 71px tall)
  //  footer: 220–240

  // Compute reset sub-label
  String resetSub = "";
  if (!g_resetAt.isEmpty()) {
    String remaining = timeUntil(g_resetAt);
    resetSub = "Resets in " + remaining;
  }

  drawSection(26,  97, "5-HOUR SESSION",      g_session, resetSub.c_str());
  drawSection(123, 97, "WEEKLY (ALL MODELS)", g_weekly,  "");

  // ── Footer ─────────────────────────────────────────────────
  tft.setTextColor(C_DIVIDER, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  char foot[50];
  if (getLocalTime(&ti)) {
    sprintf(foot, "Updated %02d:%02d:%02d  \xB7  refreshes every 30s",
            ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    strcpy(foot, "Waiting for clock sync...");
  }
  tft.drawString(foot, W / 2, 230);
}

// ── One usage row ─────────────────────────────────────────────
//   y = top of the section, h = section height
void drawSection(int y, int h, const char* label, float pct,
                 const char* sub) {
  const int PAD  = 8;
  const int BAR_H = 14;

  // Label (dim, small)
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(label, PAD, y + 10);

  // Percentage (large, color-coded)
  uint16_t col = usageColor(pct);
  char buf[10];
  sprintf(buf, "%.1f%%", pct);
  tft.setTextColor(col, C_BG);
  tft.setTextSize(2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(buf, W - PAD, y + 10);

  // Bar
  drawBar(PAD, y + 22, W - PAD * 2, BAR_H, pct, col);

  // Sub-label (e.g. reset countdown)
  if (sub && strlen(sub) > 0) {
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(sub, PAD, y + 42);
  }

  // Divider line at bottom of section
  tft.drawFastHLine(PAD, y + h - 2, W - PAD * 2, C_DIVIDER);
}

// ── Filled progress bar ───────────────────────────────────────
void drawBar(int x, int y, int bw, int bh, float pct,
             uint16_t color) {
  tft.fillRoundRect(x, y, bw, bh, 3, C_BAR_BG);
  int fill = (int)((constrain(pct, 0.0f, 100.0f) / 100.0f) * (bw - 2));
  if (fill > 0) {
    tft.fillRoundRect(x + 1, y + 1, fill, bh - 2, 2, color);
  }
  tft.drawRoundRect(x, y, bw, bh, 3, C_DIVIDER);
}

// ── Pick green/yellow/orange/red by threshold ─────────────────
uint16_t usageColor(float pct) {
  if (pct < 50.0f) return C_GREEN;
  if (pct < 70.0f) return C_YELLOW;
  if (pct < 85.0f) return C_ORANGE;
  return C_RED;
}

// ── Parse ISO 8601 UTC string, return "Xh Ym" until that time ─
String timeUntil(const String& iso) {
  if (iso.isEmpty()) return "N/A";

  int yr, mo, dy, hr, mn, sc;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
             &yr, &mo, &dy, &hr, &mn, &sc) < 6) {
    // Fallback: show raw HH:MM
    return iso.length() >= 16 ? iso.substring(11, 16) : iso;
  }

  // Build epoch for the reset time (treat as UTC)
  struct tm rtm = {};
  rtm.tm_year = yr - 1900;
  rtm.tm_mon  = mo - 1;
  rtm.tm_mday = dy;
  rtm.tm_hour = hr;
  rtm.tm_min  = mn;
  rtm.tm_sec  = sc;
  // mktime treats the struct as LOCAL time, so it subtracts TZ_OFFSET_SEC
  // from the UTC input.  Add it back so both sides are true UTC epochs.
  time_t resetEpoch = mktime(&rtm) + TZ_OFFSET_SEC;
  time_t nowEpoch   = time(nullptr);  // always returns UTC epoch

  long diff = (long)(resetEpoch - nowEpoch);
  if (diff <= 60) return "< 1m";

  char buf[16];
  long h = diff / 3600;
  long m = (diff % 3600) / 60;
  if (h > 0) sprintf(buf, "%ldh %ldm", h, m);
  else        sprintf(buf, "%ldm", m);
  return String(buf);
}

// ── Status message on splash screen ──────────────────────────
void splashStatus(const String& msg, uint16_t color) {
  tft.setTextColor(color, C_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.fillRect(0, H / 2 + 24, W, 16, C_BG);
  tft.drawString(msg, W / 2, H / 2 + 32);
}
