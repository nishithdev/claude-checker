/*
 * Claude Usage Display — Flashable Edition
 * ESP32 Cheap Yellow Display (CYD / ESP32-2432S028R)
 *
 * This variant uses BINARY-PATCHABLE credential fields so the
 * Web Flasher (index.html) can inject your WiFi / session key
 * directly into the compiled .bin — no IDE edits needed after
 * the first build.
 *
 * ── FIRST-TIME SETUP ──────────────────────────────────────
 * 1. Install libraries: TFT_eSPI (Bodmer), ArduinoJson (Blanchon)
 * 2. Copy User_Setup.h → Arduino/libraries/TFT_eSPI/User_Setup.h
 * 3. In Arduino IDE: Sketch → Export Compiled Binary
 *    Save the resulting .ino.bin as  claude_template.bin
 * 4. Open index.html in Chrome / Edge
 * 5. Upload claude_template.bin, enter credentials, click Flash
 *
 * ── UPDATING CREDENTIALS LATER ────────────────────────────
 * Open index.html → upload the ORIGINAL claude_template.bin
 * (keep it safe!) → enter new session key → Flash again.
 *
 * ── BINARY PATCH FORMAT ────────────────────────────────────
 * Three static global arrays embed 8-byte magic markers in the
 * .data section of the binary.  The web flasher finds each marker
 * and overwrites the entire fixed-size field with your value.
 *
 *   WF_SSID[64]:  marker 0xAA 0xBB 0xCC 0xDD S S I D  (+56 nulls)
 *   WF_PASS[64]:  marker 0xAA 0xBB 0xCC 0xDD P A S S  (+56 nulls)
 *   WF_SESS[300]: marker 0xAA 0xBB 0xCC 0xDD S E S S  (+292 nulls)
 *
 * IMPORTANT: always patch the ORIGINAL unmodified .bin — not a
 * previously-patched one (the markers are overwritten after patching).
 * ──────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "time.h"

// ============================================================
//  PATCHABLE CREDENTIAL FIELDS
//  Do NOT edit these manually — use the Web Flasher instead.
//  Marker = 0xAA 0xBB 0xCC 0xDD + 4-char tag.
//  Remaining bytes are zero (C++ zero-initialises rest of array).
// ============================================================
static char WF_SSID[64]  = {'\xAA','\xBB','\xCC','\xDD','S','S','I','D'};
static char WF_PASS[64]  = {'\xAA','\xBB','\xCC','\xDD','P','A','S','S'};
static char WF_SESS[300] = {'\xAA','\xBB','\xCC','\xDD','S','E','S','S'};

// Convenience pointers used by the rest of the code
static const char* WIFI_SSID     = WF_SSID;
static const char* WIFI_PASSWORD = WF_PASS;
static const char* SESSION_KEY   = WF_SESS;

// ============================================================
//  NON-PATCHABLE SETTINGS  (edit here before first compile)
// ============================================================
const unsigned long REFRESH_MS  = 30000;  // 30-second refresh
const long          TZ_OFFSET_SEC = 0;    // e.g. UTC+5:30 = 19800
// ============================================================

// ── TFT ─────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
static const int W = 320, H = 240;

// Palette — Claude warm theme
// C_BG      ≈ #1C1817  very dark warm charcoal
// C_HDR     ≈ #2A2420  slightly lighter warm header
// C_ACCENT  ≈ #FF8C00  Claude warm amber
// C_DIM     ≈ #8A8888  muted warm gray
// C_DIVIDER ≈ #323030  subtle warm divider
// C_BAR_BG  ≈ #201C1C  dark bar background
static const uint16_t C_BG      = 0x18C2;
static const uint16_t C_HDR     = 0x2924;
static const uint16_t C_ACCENT  = 0xFC60;
static const uint16_t C_DIM     = 0x8C51;
static const uint16_t C_DIVIDER = 0x3186;
static const uint16_t C_BAR_BG  = 0x2104;
static const uint16_t C_GREEN   = 0x07E0;
static const uint16_t C_YELLOW  = 0xFFE0;
static const uint16_t C_ORANGE  = 0xFD20;
static const uint16_t C_RED     = 0xF800;

// ── State ────────────────────────────────────────────────────
String g_orgId   = "";
float  g_session = 0, g_weekly = 0, g_opus = 0;
String g_resetAt = "";
bool   g_hasData = false;
unsigned long g_lastRefresh = 0;
int    g_errorCode = 0;

// ── Forward declarations ─────────────────────────────────────
bool     fetchOrgId();
bool     fetchUsage();
void     drawScreen();
void     drawClaudeMascot(int cx, int cy, int r);
void     drawSection(int y, int h, const char* label, float pct, const char* sub);
void     drawBar(int x, int y, int bw, int bh, float pct, uint16_t color);
uint16_t usageColor(float pct);
String   timeUntil(const String& iso);
void     splashStatus(const String& msg, uint16_t color = TFT_WHITE);

// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);

  // Splash mascot + title
  drawClaudeMascot(W/2, 70, 5);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("CLAUDE USAGE", W/2, H/2 + 8);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.drawString("ESP32 CYD  -  Flashable Edition", W/2, H/2 + 30);

  // Sanity check: if credentials are still markers, warn
  if ((uint8_t)WF_SSID[0] == 0xAA && (uint8_t)WF_SSID[1] == 0xBB) {
    splashStatus("Not configured! Use Web Flasher.", TFT_RED);
    delay(5000);
  }

  splashStatus("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) {
    splashStatus("WiFi failed! Re-flash with correct SSID.", C_RED);
    delay(5000); ESP.restart();
  }
  splashStatus("WiFi OK — syncing time...");

  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tmp;
  for (int i = 0; i < 20 && !getLocalTime(&tmp); i++) delay(500);

  splashStatus("Fetching org ID...");
  if (!fetchOrgId()) {
    splashStatus("Org fetch failed. Check session key.", C_RED);
    delay(5000);
  }

  splashStatus("Loading usage data...");
  fetchUsage();
  drawScreen();
}

void loop() {
  if (g_orgId.isEmpty()) {
    if (millis() - g_lastRefresh > 15000) { fetchOrgId(); g_lastRefresh = millis(); }
    return;
  }
  if (millis() - g_lastRefresh >= REFRESH_MS) {
    g_lastRefresh = millis();
    fetchUsage();
    drawScreen();
  }
}

// ============================================================
bool fetchOrgId() {
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.println("[DBG] fetchOrgId: connecting...");
  Serial.print("[DBG] SessionKey prefix: ");
  Serial.println(String(SESSION_KEY).substring(0, 20) + "...");
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(10000);
  http.begin(client, "https://claude.ai/api/organizations");
  http.addHeader("Cookie",     String("sessionKey=") + SESSION_KEY);
  http.addHeader("Accept",     "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (compatible; ESP32-CYD)");
  int code = http.GET();
  Serial.print("[DBG] fetchOrgId HTTP code: "); Serial.println(code);
  if (code != 200) {
    g_errorCode = code;
    Serial.print("[DBG] Error payload: "); Serial.println(http.getString().substring(0,200));
    http.end(); return false;
  }

  String body = http.getString();
  http.end();
  Serial.print("[DBG] Response (first 300): "); Serial.println(body.substring(0, 300));

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[DBG] JSON parse error: "); Serial.println(err.c_str());
    return false;
  }

  JsonVariant root = doc.as<JsonVariant>();
  JsonArray arr = root.is<JsonArray>() ? root.as<JsonArray>()
                : root["organizations"].as<JsonArray>();
  Serial.print("[DBG] Array size: "); Serial.println(arr.size());
  for (JsonObject org : arr) {
    const char* uuid = org["uuid"].as<const char*>();
    if (!uuid) uuid = org["id"].as<const char*>();
    Serial.print("[DBG] Org uuid/id: "); Serial.println(uuid ? uuid : "(null)");
    if (uuid && strlen(uuid) > 4) { g_orgId = String(uuid); return true; }
  }
  Serial.println("[DBG] No valid org UUID found in response");
  return false;
}

bool fetchUsage() {
  if (WiFi.status() != WL_CONNECTED || g_orgId.isEmpty()) return false;
  Serial.println("[DBG] fetchUsage: connecting...");
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(10000);
  http.begin(client, "https://claude.ai/api/organizations/" + g_orgId + "/usage");
  http.addHeader("Cookie",     String("sessionKey=") + SESSION_KEY);
  http.addHeader("Accept",     "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (compatible; ESP32-CYD)");
  int code = http.GET();
  Serial.print("[DBG] fetchUsage HTTP code: "); Serial.println(code);
  g_errorCode = (code == 200) ? 0 : code;
  if (code != 200) {
    Serial.println(http.getString().substring(0,200));
    http.end(); return false;
  }

  String body = http.getString();
  http.end();
  Serial.print("[DBG] Usage response (first 300): "); Serial.println(body.substring(0, 300));

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) { return false; }

  g_session = doc["five_hour"]["utilization"]      | 0.0f;
  g_weekly  = doc["seven_day"]["utilization"]      | 0.0f;
  // seven_day_opus may be null; fall back to seven_day_omelette (Opus 4 codename)
  if (!doc["seven_day_opus"].isNull())
    g_opus = doc["seven_day_opus"]["utilization"] | 0.0f;
  else
    g_opus = doc["seven_day_omelette"]["utilization"] | 0.0f;
  g_resetAt = doc["five_hour"]["resets_at"].as<String>();
  if (g_resetAt == "null") g_resetAt = "";
  Serial.printf("[DBG] session=%.1f weekly=%.1f opus=%.1f\n", g_session, g_weekly, g_opus);
  g_hasData = true; g_lastRefresh = millis();
  return true;
}

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

void drawScreen() {
  tft.fillScreen(C_BG);
  // Header bar
  tft.fillRect(0, 0, W, 26, C_HDR);
  // Small mascot in header (left side)
  drawClaudeMascot(13, 13, 2);
  // Title
  tft.setTextColor(C_ACCENT, C_HDR); tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("CLAUDE USAGE", 28, 13);

  struct tm ti; char clk[9] = "--:--";
  if (getLocalTime(&ti)) sprintf(clk, "%02d:%02d", ti.tm_hour, ti.tm_min);
  tft.setTextColor((WiFi.status()==WL_CONNECTED)?C_GREEN:C_RED, C_HDR);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(String(clk)+"  ", W, 13);

  if (!g_hasData) {
    tft.setTextColor(C_DIM, C_BG); tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
    if (g_errorCode==401||g_errorCode==403)
      tft.drawString("Auth failed - re-flash with new session key", W/2, H/2);
    else if (g_errorCode)
      { char b[40]; sprintf(b,"HTTP error %d - retrying...",g_errorCode); tft.drawString(b,W/2,H/2); }
    else tft.drawString("Waiting for data...", W/2, H/2);
    return;
  }

  String resetSub = g_resetAt.isEmpty() ? "" : "Resets in " + timeUntil(g_resetAt);
  drawSection(26,  71, "5-HOUR SESSION",      g_session, resetSub.c_str());
  drawSection(97,  71, "WEEKLY (ALL MODELS)", g_weekly,  "");
  drawSection(168, 52, "WEEKLY OPUS/OPU4",    g_opus,    "");

  tft.setTextColor(C_DIVIDER, C_BG); tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  char foot[50];
  if (getLocalTime(&ti)) sprintf(foot, "Updated %02d:%02d:%02d  -  refreshes every 30s",
                                  ti.tm_hour, ti.tm_min, ti.tm_sec);
  tft.drawString(foot, W/2, 230);
}

void drawSection(int y, int h, const char* label, float pct, const char* sub) {
  const int PAD=8, BAR_H=14;
  tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM); tft.drawString(label, PAD, y+10);
  uint16_t col = usageColor(pct);
  char buf[10]; sprintf(buf, "%.1f%%", pct);
  tft.setTextColor(col, C_BG); tft.setTextSize(2);
  tft.setTextDatum(MR_DATUM); tft.drawString(buf, W-PAD, y+10);
  drawBar(PAD, y+22, W-PAD*2, BAR_H, pct, col);
  if (sub && strlen(sub)) {
    tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM); tft.drawString(sub, PAD, y+42);
  }
  tft.drawFastHLine(PAD, y+h-2, W-PAD*2, C_DIVIDER);
}

void drawBar(int x, int y, int bw, int bh, float pct, uint16_t color) {
  tft.fillRoundRect(x, y, bw, bh, 3, C_BAR_BG);
  int fill = (int)((constrain(pct,0,100)/100.0f)*(bw-2));
  if (fill>0) tft.fillRoundRect(x+1, y+1, fill, bh-2, 2, color);
  tft.drawRoundRect(x, y, bw, bh, 3, C_DIVIDER);
}

uint16_t usageColor(float p) {
  return p<50?C_GREEN : p<70?C_YELLOW : p<85?C_ORANGE : C_RED;
}

String timeUntil(const String& iso) {
  if (iso.isEmpty()) return "N/A";
  int yr,mo,dy,hr,mn,sc;
  if (sscanf(iso.c_str(),"%d-%d-%dT%d:%d:%d",&yr,&mo,&dy,&hr,&mn,&sc)<6)
    return iso.length()>=16 ? iso.substring(11,16) : iso;
  struct tm rtm={}; rtm.tm_year=yr-1900; rtm.tm_mon=mo-1;
  rtm.tm_mday=dy; rtm.tm_hour=hr; rtm.tm_min=mn; rtm.tm_sec=sc;
  time_t resetEpoch = mktime(&rtm) + TZ_OFFSET_SEC;
  time_t nowEpoch   = time(nullptr);
  long diff = (long)(resetEpoch - nowEpoch);
  if (diff<=60) return "< 1m";
  char buf[16];
  long h=diff/3600, m=(diff%3600)/60;
  if (h>0) sprintf(buf,"%ldh %ldm",h,m); else sprintf(buf,"%ldm",m);
  return String(buf);
}

void splashStatus(const String& msg, uint16_t color) {
  tft.setTextColor(color, C_BG); tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.fillRect(0, H/2+24, W, 16, C_BG);
  tft.drawString(msg, W/2, H/2+32);
}
