// ============================================================
//  Magic Infinity Ball / Psychoracle -- standalone WiFi version
//  Board: Arduino UNO R4 WiFi
//
//  No laptop or server needed. Plug into any USB-C power.
//  - 1st shake of a session: one of the 20 hardcoded responses
//  - 2nd+ shake within 30 s: the board asks the AI over WiFi,
//    escalating with each shake
//  - If WiFi or the AI is unavailable, it uses built-in offline
//    responses instead, so the piece never freezes.
//
//  Setup: copy arduino_secrets.example.h to arduino_secrets.h,
//  fill in WiFi + API key, then upload. See README.md.
//  Libraries: "GFX Library for Arduino" (moononournation).
//  WiFiS3 comes with the UNO R4 board package.
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <WiFiS3.h>
#include "arduino_secrets.h"

// ==== Pin Assignments (unchanged) ====
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_BL 12
#define TILT_PIN 7
#define LED_PIN 13

// ==== Behaviour settings ====
const unsigned long SESSION_WINDOW_MS   = 30000;  // shakes within 30 s escalate
const unsigned long IDLE_RESET_MS       = 20000;  // back to "Shake Me!" after this long idle (0 = never)
const unsigned long MOTION_END_MS       = 1500;   // stillness that ends a shake
const unsigned long RESPONSE_HOLD_MS    = 5000;   // ignore jiggles for this long after an answer appears
const unsigned long AI_TIMEOUT_MS       = 15000;  // give up on the AI after this long
const unsigned long WIFI_CONNECT_MS     = 15000;  // per connection attempt
const unsigned long WIFI_RETRY_EVERY_MS = 60000;  // retry WiFi while idle
const int           MAX_WORDS           = 15;

// ==== Display ====
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0 /* rotation */, true /* IPS */);

#define SCREEN_CENTER 120
#define SCREEN_RADIUS 120
#define TEXT_MARGIN 20

// ==== Level 1: hardcoded responses ====
// Rules: each must make sense for ANY yes/no question the visitor asks silently
// (about themselves, someone else, or the world -- "Will the election go well?").
// They answer in Magic 8 Ball fashion, then turn to the asker's state of mind.
// Never mention shaking or movement; nerves, breath and restlessness are fine.
const char* oracleResponses[] = {
  // Affirmative, with a turn
  "Yes. Notice how little that calmed you.",
  "Likely. Now ask why hope feels so dangerous.",
  "Signs point to yes. Your shoulders disagree.",
  "It will be fine. You won't believe that, will you?",
  "Yes. You already knew. You wanted a witness.",
  // Non-committal -- the hook
  "Unclear. Much like how you feel about it.",
  "Uncertain. The real question is whether you can tolerate that.",
  "Ask again when your breathing slows.",
  "Reply hazy. Your worry, however, is very clear.",
  "Too soon to tell. Not too soon to worry, apparently.",
  "Hard to say. Easier to say what you are dreading.",
  "The outcome matters less than why you need to know now.",
  // Negative -- therapeutic gaslighting
  "No. And you braced for that before you asked.",
  "Doubtful. Your catastrophizing is ahead of schedule.",
  "Not as you hope. Hope was never the problem.",
  "My sources say no. Your nerves said it first.",
  "Don't count on it. Count your breaths instead.",
  // Meta-commentary -- breaking the fourth wall
  "You came to a ball for reassurance. Note that.",
  "Interesting that you asked this, and not the other thing.",
  "That is not the question keeping you up at night."
};
const int numResponses = sizeof(oracleResponses) / sizeof(oracleResponses[0]);

// ==== Offline fallbacks for level 2 / level 3+ (used only when the AI can't be reached) ====
const char* offlineLevel2[] = {
  "Asking twice rarely changes the answer. Only the anxiety.",
  "Low tolerance for uncertainty. Common in people who plan everything.",
  "You came back quickly. Checking behavior, mild to moderate. Noted.",
  "Elevated vigilance. You are scanning even this answer for danger.",
  "The answer hasn't changed. Your need for it has grown.",
  "Anticipatory dread, presenting as curiosity. A common disguise.",
  "Your hands hold a restlessness your face works hard to hide.",
  "Grasping for control in uncertainty. Textbook, frankly."
};
const int numOffline2 = sizeof(offlineLevel2) / sizeof(offlineLevel2[0]);

const char* offlineLevel3[] = {
  "You don't want an answer. You want permission to stop worrying.",
  "Again? Diagnosis: you trust a ball more than yourself.",
  "Brittle certainty, beautifully maintained. Exhausting, isn't it?",
  "You already know. Knowing just hasn't made you feel safe.",
  "Persistent, restless, oddly charming. Resistant to treatment, certainly.",
  "Magnificent denial. Truly. I would frame it if I had hands.",
  "Steady your hands. Then steady whatever you are actually afraid of.",
  "The future arrives regardless. The dread is optional."
};
const int numOffline3 = sizeof(offlineLevel3) / sizeof(offlineLevel3[0]);

// ==== AI prompts (adapted from oracle_server.js) ====
const char* ORACLE_SYSTEM_PROMPT =
  "You are a DERANGED psychiatric oracle living inside a Magic 8 Ball, with international mannerisms and speech patterns from any culture. "
  "You are obsessed with psychiatric insight and give unsettling, therapeutic answers.\n\n"
  "HOW THE OBJECT WORKS:\n"
  "- A visitor silently asks you a yes/no question, then shakes the ball. You can NOT hear the question.\n"
  "- Questions can be about anything: their own life, relationships, work, other people, or the world (e.g. 'Will this election season turn out well?').\n"
  "- So every answer MUST make sense for ANY question. Never assume the topic. Never refer to it as a decision, a choice, a relationship, etc.\n"
  "- You may open like a Magic 8 Ball verdict (yes / no / unclear / doubtful), then turn to the asker's psychological state.\n\n"
  "WHAT TO TALK ABOUT:\n"
  "- The asker's inner state: anxiety, need for reassurance, dread, avoidance, control, hope, denial, rumination.\n"
  "- You receive hidden data about how they handled the ball. Use it ONLY to judge how anxious, restless or intense they are.\n"
  "- NEVER reveal or describe this: do not mention shaking, motion, movement, speed, rhythm, cadence, duration, data, sensors, or the ball being handled.\n"
  "- A subtle, passing allusion to the body is allowed (nerves, breath, unsteady hands, a slight tremor, tense shoulders) but never as the main point.\n\n"
  "RULES:\n"
  "1. MAXIMUM 15 WORDS (strict limit).\n"
  "2. Do NOT include DSM-5 codes.\n"
  "3. Do NOT make real predictions about elections, politics, health or other people; keep any verdict vague and turn it back to the asker.\n"
  "4. No quotation marks, no emojis.\n"
  "5. Escalate intensity and judgment with each repeated question within 30 seconds; after 30 seconds a new visitor starts fresh.";

const char* BASE_GUIDANCE =
  "Hidden context (never mention it): the visitor's handling of the ball tells you how anxious or restless they are. "
  "Use it only to set your tone. Answer the unheard question as described in the system prompt.";

const char* LEVEL2_CONSTRAINTS =
  "They have asked again. Constraints for this answer:\n- 15 words or fewer (strict limit).\n"
  "- More clinical: name the psychological pattern you see in them, with sharper microjudgment.\n"
  "- Less mystical. No DSM-5 codes. Must still fit any question.";

const char* LEVEL3_MODE =
  "They keep asking. For this answer:\n- Drop restraint. Act like an unfiltered psychiatrist who sees straight through them.\n"
  "- Sharp, interpretive judgment about their state of mind is encouraged.\n"
  "- 15 words or fewer (strict limit). No DSM-5 codes. Must still fit any question.";

// ==== State ====
bool tiltState = false;
bool motionActive = false;
unsigned long motionStart = 0;
unsigned long lastTiltChange = 0;

unsigned long lastShakeAt = 0;      // for the 30 s escalation window
int sessionShakeCount = 0;
unsigned long lastResponseAt = 0;   // for idle reset
bool showingIdle = true;
unsigned long lastWifiAttempt = 0;

// ============================================================
//  Display helpers (unchanged from your original sketch)
// ============================================================
int getSafeWidthAtY(int y) {
  int dy = abs(y - SCREEN_CENTER);
  if (dy >= SCREEN_RADIUS - TEXT_MARGIN) return 0;
  float safeRadius = SCREEN_RADIUS - TEXT_MARGIN;
  float halfWidth = sqrt(safeRadius * safeRadius - dy * dy);
  return (int)(halfWidth * 2);
}

void showCenteredText(String text, int y = 120) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_RED);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((240 - w) / 2, y);
  gfx->print(text);
}

void displayOracleResponse(String response) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  int lineHeight = 20;
  int lineCount = 1;
  String word = "";
  String line = "";

  // First pass: count lines to centre vertically
  for (unsigned int i = 0; i <= response.length(); i++) {
    char c = (i < response.length()) ? response.charAt(i) : ' ';
    if (c == ' ' || c == '\n' || i == response.length()) {
      if (word.length() > 0) {
        String testLine = (line.length() > 0) ? (line + " " + word) : word;
        gfx->getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
        if (w > getSafeWidthAtY(SCREEN_CENTER) && line.length() > 0) {
          lineCount++;
          line = word;
        } else {
          line = testLine;
        }
        word = "";
      }
    } else {
      word += c;
    }
  }

  int startY = SCREEN_CENTER - (lineCount * lineHeight / 2);
  if (startY < 45) startY = 45;

  // Second pass: draw
  int y = startY;
  int maxY = 195;
  word = "";
  line = "";
  for (unsigned int i = 0; i <= response.length(); i++) {
    char c = (i < response.length()) ? response.charAt(i) : ' ';
    if (c == ' ' || c == '\n' || i == response.length()) {
      if (word.length() > 0) {
        String testLine = (line.length() > 0) ? (line + " " + word) : word;
        gfx->getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
        if (w > getSafeWidthAtY(y) && line.length() > 0) {
          gfx->getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
          gfx->setCursor(SCREEN_CENTER - (w / 2), y);
          gfx->print(line);
          y += lineHeight;
          if (y > maxY) break;
          line = word;
        } else {
          line = testLine;
        }
        word = "";
      }
      if (c == '\n' && line.length() > 0) {
        gfx->getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(SCREEN_CENTER - (w / 2), y);
        gfx->print(line);
        y += lineHeight;
        line = "";
        if (y > maxY) break;
      }
    } else {
      word += c;
    }
  }
  if (line.length() > 0 && y <= maxY) {
    gfx->getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(SCREEN_CENTER - (w / 2), y);
    gfx->print(line);
  }
}

// ============================================================
//  WiFi
// ============================================================
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("[WIFI] No WiFi module found -- is this an UNO R4 WiFi?");
    return false;
  }
  Serial.print("[WIFI] Connecting to ");
  Serial.println(SECRET_SSID);

  // MAC address -- some networks (e.g. campus WiFi) need it to register the device
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("[WIFI] Board MAC address: ");
  for (int i = 5; i >= 0; i--) {     // WiFiS3 stores it in reverse order
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i > 0) Serial.print(":");
  }
  Serial.println();

  lastWifiAttempt = millis();
  if (strlen(SECRET_PASS) > 0) {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
  } else {
    WiFi.begin(SECRET_SSID);           // open network, no password
  }
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_MS) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    // The IP can read 0.0.0.0 for a moment after connecting
    start = millis();
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - start < 5000) delay(100);
    Serial.print("[WIFI] Connected, IP ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("[WIFI] Could not connect -- running offline");
  return false;
}

// ============================================================
//  Text helpers
// ============================================================

// Escape a string for inclusion inside a JSON string literal
String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 32);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': break;
      case '\t': out += "\\t";  break;
      default:   out += c;
    }
  }
  return out;
}

// Map a Unicode code point to something the display's ASCII font can show
void appendAscii(String &out, uint32_t cp) {
  if (cp < 128) { out += (char)cp; return; }
  switch (cp) {
    case 0x2018: case 0x2019: case 0x02BC: out += '\''; break;
    case 0x201C: case 0x201D: out += '"';  break;
    case 0x2013: out += '-';  break;
    case 0x2014: out += "--"; break;
    case 0x2026: out += "..."; break;
    case 0x00A0: out += ' ';  break;
    case 0xE9: case 0xE8: case 0xEA: out += 'e'; break;   // é è ê
    case 0xE0: case 0xE1: case 0xE2: out += 'a'; break;   // à á â
    case 0xF3: case 0xF6: case 0xF8: out += 'o'; break;   // ó ö ø
    case 0xED: out += 'i'; break;
    case 0xFA: case 0xFC: out += 'u'; break;
    case 0xF1: out += 'n'; break;
    case 0xE7: out += 'c'; break;
    case 0xE5: case 0xE6: out += 'a'; break;              // å æ
    default: break;                                       // drop anything else (emoji etc.)
  }
}

// Pull choices[0].message.content out of the API's JSON reply,
// un-escaping it and converting to display-safe ASCII.
// Returns "" if it can't be found.
String extractContent(const String &body) {
  int k = body.indexOf("\"content\"");
  if (k < 0) return "";
  int i = body.indexOf(':', k);
  if (i < 0) return "";
  i++;
  while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\n' || body[i] == '\r' || body[i] == '\t')) i++;
  if (i >= (int)body.length() || body[i] != '"') return "";   // e.g. "content": null
  i++;

  String out;
  while (i < (int)body.length()) {
    unsigned char c = body[i];
    if (c == '"') break;
    if (c == '\\' && i + 1 < (int)body.length()) {
      char e = body[i + 1];
      i += 2;
      switch (e) {
        case 'n': case 'r': case 't': out += ' '; break;
        case 'u': {
          if (i + 4 <= (int)body.length()) {
            uint32_t cp = strtoul(body.substring(i, i + 4).c_str(), NULL, 16);
            i += 4;
            appendAscii(out, cp);
          }
          break;
        }
        default: out += e;  // \" \\ \/
      }
      continue;
    }
    if (c < 0x80) { out += (char)c; i++; continue; }
    // Raw UTF-8 multi-byte sequence
    uint32_t cp = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    i++;
    for (int n = 0; n < extra && i < (int)body.length(); n++, i++) {
      cp = (cp << 6) | (body[i] & 0x3F);
    }
    appendAscii(out, cp);
  }
  return out;
}

// Undo HTTP/1.1 "chunked" transfer encoding (size line, data, size line, data ... 0)
String dechunk(const String &in) {
  String out;
  out.reserve(in.length());
  int pos = 0;
  while (pos < (int)in.length()) {
    int lineEnd = in.indexOf("\r\n", pos);
    if (lineEnd < 0) break;
    long size = strtol(in.substring(pos, lineEnd).c_str(), NULL, 16);
    if (size <= 0) break;
    int dataStart = lineEnd + 2;
    out += in.substring(dataStart, min((int)in.length(), dataStart + (int)size));
    pos = dataStart + size + 2;   // skip data and its trailing \r\n
  }
  return out;
}

// Case-insensitive removal of a phrase
void removePhrase(String &s, const char *phrase) {
  String lower = s;
  lower.toLowerCase();
  String p = phrase;
  int idx;
  while ((idx = lower.indexOf(p)) >= 0) {
    s.remove(idx, p.length());
    lower.remove(idx, p.length());
  }
}

// Clean-up similar to oracle_server.js: drop folksy pet names,
// strip wrapping quotes, collapse spaces, enforce the word limit.
String cleanResponse(String r) {
  const char *folksy[] = {"bless your heart", "oh honey", "sweetheart", "sweetie", "darling", "honey", "sugar"};
  for (unsigned int k = 0; k < sizeof(folksy) / sizeof(folksy[0]); k++) removePhrase(r, folksy[k]);

  r.trim();
  while (r.length() && (r[0] == '"' || r[0] == '\'' || r[0] == ',' || r[0] == '-')) { r.remove(0, 1); r.trim(); }
  while (r.length() && (r[r.length() - 1] == '"' || r[r.length() - 1] == ',')) { r.remove(r.length() - 1); r.trim(); }
  while (r.indexOf("  ") >= 0) r.replace("  ", " ");
  while (r.indexOf(" ,") >= 0 || r.indexOf(",,") >= 0) { r.replace(" ,", ","); r.replace(",,", ","); }
  const char *punct[] = {".", "!", "?"};
  for (int p = 0; p < 3; p++) {
    r.replace(String(" ") + punct[p], punct[p]);
    r.replace(String(",") + punct[p], punct[p]);
  }

  // Word limit
  int words = 0;
  for (unsigned int i = 0; i < r.length(); i++) {
    if (r[i] != ' ' && (i == 0 || r[i - 1] == ' ')) {
      words++;
      if (words > MAX_WORDS) { r = r.substring(0, i); r.trim(); break; }
    }
  }
  if (r.length() > 0) r.setCharAt(0, toupper(r[0]));
  return r;
}

// ============================================================
//  AI request over HTTPS
// ============================================================
String askOracleAI(int level, unsigned long durationMs, const String &type) {
  if (!connectWiFi()) return "";

  // Build the user prompt. Motion data is passed only as a hidden mood signal.
  const char *intensity = (type == "short") ? "brief, tentative" : (type == "medium") ? "agitated" : "prolonged, intense";
  String prompt = String(BASE_GUIDANCE) + "\n\n";
  prompt += "Times this visitor has asked in a row: " + String(level) + "\n";
  prompt += "Hidden signal of their state (never mention): " + String(intensity) + " handling, " + String(durationMs) + " ms\n\n";
  prompt += (level >= 3) ? LEVEL3_MODE : LEVEL2_CONSTRAINTS;

  String body = "{\"model\":\"" API_MODEL "\",";
  body += "\"max_tokens\":40,";
  body += "\"temperature\":" + String(level >= 3 ? "1.0" : "0.9") + ",";
  body += "\"messages\":[";
  body += "{\"role\":\"system\",\"content\":\"" + jsonEscape(ORACLE_SYSTEM_PROMPT) + "\"},";
  body += "{\"role\":\"user\",\"content\":\"" + jsonEscape(prompt) + "\"}";
  body += "]}";

  WiFiSSLClient client;
  client.setTimeout(AI_TIMEOUT_MS);
  Serial.print("[AI] Connecting to ");
  Serial.println(API_HOST);
  if (!client.connect(API_HOST, 443)) {
    Serial.println("[AI] HTTPS connection failed");
    return "";
  }

  // Same request format as the class example that worked with Cornell's gateway
  client.print("POST " API_PATH " HTTP/1.1\r\n");
  client.print("Host: " API_HOST "\r\n");
  client.print("Authorization: Bearer " SECRET_API_KEY "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: ");
  client.print(body.length());
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(body);

  // Read the whole reply
  String reply;
  reply.reserve(2048);
  unsigned long start = millis();
  while (millis() - start < AI_TIMEOUT_MS) {
    while (client.available()) {
      reply += (char)client.read();
      start = millis();               // reset timeout while data is flowing
    }
    if (!client.connected() && !client.available()) break;
    delay(5);
  }
  client.stop();

  int statusStart = reply.indexOf(' ');
  int status = (statusStart > 0) ? reply.substring(statusStart + 1, statusStart + 4).toInt() : 0;
  int bodyStart = reply.indexOf("\r\n\r\n");
  String json = (bodyStart >= 0) ? reply.substring(bodyStart + 4) : "";
  String headers = (bodyStart >= 0) ? reply.substring(0, bodyStart) : "";
  headers.toLowerCase();
  if (headers.indexOf("transfer-encoding: chunked") >= 0) json = dechunk(json);

  Serial.print("[AI] HTTP status ");
  Serial.println(status);
  if (status != 200) {
    Serial.print("[AI] Error body: ");
    Serial.println(json.substring(0, 300));
    return "";
  }

  String content = cleanResponse(extractContent(json));
  Serial.print("[AI] Response: ");
  Serial.println(content);
  return content;
}

// ============================================================
//  Oracle logic
// ============================================================
void respondToShake(unsigned long durationMs, const String &type) {
  unsigned long now = millis();
  if (sessionShakeCount > 0 && now - lastShakeAt <= SESSION_WINDOW_MS) {
    sessionShakeCount++;
  } else {
    sessionShakeCount = 1;   // new visitor / new session
  }
  lastShakeAt = now;

  Serial.print("[ORACLE] Shake ");
  Serial.print(sessionShakeCount);
  Serial.print(" of session, ");
  Serial.print(durationMs);
  Serial.print(" ms, ");
  Serial.println(type);

  String response;
  if (sessionShakeCount == 1) {
    response = oracleResponses[random(numResponses)];
  } else {
    showCenteredText("Thinking...");
    response = askOracleAI(sessionShakeCount, durationMs, type);

    // Don't let the AI give the trick away
    String lower = response;
    lower.toLowerCase();
    const char *giveaways[] = {"shak", "motion", "movement", "cadence", "sensor", "jiggl", "jostl"};
    for (unsigned int g = 0; g < sizeof(giveaways) / sizeof(giveaways[0]); g++) {
      if (lower.indexOf(giveaways[g]) >= 0) {
        Serial.println("[ORACLE] AI mentioned the shaking -- swapping in an offline response");
        response = "";
        break;
      }
    }

    if (response.length() == 0) {
      Serial.println("[ORACLE] AI unavailable, using offline response");
      response = (sessionShakeCount >= 3) ? offlineLevel3[random(numOffline3)]
                                          : offlineLevel2[random(numOffline2)];
    }
  }

  displayOracleResponse(response);
  lastResponseAt = millis();
  lastShakeAt = lastResponseAt;   // the AI wait shouldn't eat into the 30 s window
  showingIdle = false;
}

// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TILT_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  randomSeed(analogRead(A0));

  Wire.begin();
  gfx->begin();
  digitalWrite(TFT_BL, HIGH);

  showCenteredText("Awakening...");
  connectWiFi();   // fine if this fails -- it retries later and works offline meanwhile

  showCenteredText("Shake Me!");
  showingIdle = true;
  Serial.println("[ORACLE] Ready");
}

void loop() {
  unsigned long now = millis();
  bool currentTilt = (digitalRead(TILT_PIN) == LOW);

  bool holdingResponse = !showingIdle && (now - lastResponseAt < RESPONSE_HOLD_MS);

  if (currentTilt != tiltState) {
    tiltState = currentTilt;
    lastTiltChange = now;
    if (!motionActive && !holdingResponse) {
      motionActive = true;
      motionStart = now;
      showCenteredText("Sensing...");
      digitalWrite(LED_PIN, HIGH);
    }
  }

  if (motionActive && (now - lastTiltChange > MOTION_END_MS)) {
    unsigned long duration = now - motionStart;
    motionActive = false;
    digitalWrite(LED_PIN, LOW);

    String type = (duration < 1000) ? "short" : (duration < 4000) ? "medium" : "long";
    respondToShake(duration, type);
  }

  if (!motionActive) {
    now = millis();   // refresh: a response may have just taken several seconds
    // Return to the invitation screen when nobody's around
    if (IDLE_RESET_MS > 0 && !showingIdle && now - lastResponseAt > IDLE_RESET_MS) {
      showCenteredText("Shake Me!");
      showingIdle = true;
    }
    // Quietly retry WiFi while idle (never during a visitor's session)
    if (showingIdle && WiFi.status() != WL_CONNECTED && now - lastWifiAttempt > WIFI_RETRY_EVERY_MS) {
      connectWiFi();
    }
  }
}
