// ============================================================
//  arduino_secrets.example.h
//
//  1. Make a copy of this file named  arduino_secrets.h
//     (same folder as the .ino file).
//  2. Fill in the values below.
//  3. Never upload arduino_secrets.h anywhere public --
//     it contains your WiFi password and API key.
// ============================================================

// --- WiFi ---
// 2.4 GHz network only. For an open network with no password, use "".
// Networks that require a login / "accept terms" web page will NOT work.
#define SECRET_SSID   "YourWiFiName"
#define SECRET_PASS   "YourWiFiPassword"

// --- AI service (OpenAI) ---
// Create a key at platform.openai.com -> API keys.
// Set a monthly budget limit on the account to cap costs.
#define SECRET_API_KEY  "sk-your-openai-key"
#define API_HOST        "api.openai.com"
#define API_PATH        "/v1/chat/completions"
#define API_MODEL       "gpt-4o-mini"

// Any OpenAI-compatible chat completions service also works,
// e.g. Cornell's AI gateway:
// #define API_HOST   "api.ai.it.cornell.edu"
// #define API_MODEL  "openai.gpt-4o-mini"
