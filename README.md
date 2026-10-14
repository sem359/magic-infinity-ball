# Magic Infinity Ball (Psychoracle)

An interactive object: a Magic 8 Ball possessed by a deranged psychiatric oracle. Visitors silently ask a yes/no question and shake the ball. The round screen answers, and the answers get sharper and more personal each time the same visitor asks again.

Created by Sofia Mattson, Cornell University (AI Embodiment, Fall 2025).

**Day-to-day operation for gallery staff:** see [`GALLERY_SETUP.txt`](GALLERY_SETUP.txt). You only need this README if the code has to be re-uploaded, for example to change the WiFi network.

---

## How it works

- **First shake:** one of 20 pre-written answers, shown immediately.
- **Shaking again within 30 seconds:** the board asks an AI model over WiFi for a new answer that escalates with each shake.
- **No WiFi, or the AI is unreachable:** it falls back to built-in answers, so visitors never see an error.
- **After 20 seconds idle:** it returns to "Shake Me!" for the next visitor.

Everything runs on the Arduino itself. No computer is needed, only USB-C power.

## Hardware

| Part | Notes |
|---|---|
| Arduino UNO R4 **WiFi** | The R4 Minima will not work (no WiFi) |
| GC9A01 1.28" round TFT display (240×240) | SPI |
| Tilt / vibration switch | Digital |
| USB-C cable + 5V wall charger | Power |

### Wiring

| Display pin | Arduino pin |
|---|---|
| CS | 10 |
| DC | 9 |
| RST | 8 |
| BL (backlight) | 12 |
| SCL / SCK | 13 |
| SDA / MOSI | 11 |
| VCC | 3.3V or 5V |
| GND | GND |

| Tilt sensor pin | Arduino pin |
|---|---|
| Signal | 7 |
| VCC | 5V |
| GND | GND |

## Re-uploading the code

1. **Install the Arduino IDE** from [arduino.cc/en/software](https://www.arduino.cc/en/software).
2. **Add the board:** Tools → Board → Boards Manager → install **Arduino UNO R4 Boards**.
3. **Add the display library:** Tools → Manage Libraries → install **GFX Library for Arduino** (by moononournation).
4. **Download this repository:** green **Code** button → **Download ZIP**, then unzip it.
5. **Create the secrets file.** In the `AR4_magicinfinity_wifi` folder, make a copy of `arduino_secrets.example.h` and name it **`arduino_secrets.h`**. Fill in:
   - the WiFi network name and password (use `""` for an open network)
   - an OpenAI API key (from [platform.openai.com](https://platform.openai.com) → API keys)

   The artist's own key is not included in this repository. Contact the artist, or use your own key and set a monthly budget limit on the account.
6. **Open** `AR4_magicinfinity_wifi/AR4_magicinfinity_wifi.ino` in the Arduino IDE.
7. **Plug in the board**, choose **Tools → Board → Arduino UNO R4 WiFi** and the matching **Port**, then click **Upload** (→).
8. **Check it:** open Tools → Serial Monitor at **115200** baud and shake the ball twice. You should see `[WIFI] Connected` and, on the second shake, `[AI] HTTP status 200`.

### Troubleshooting

| Serial Monitor shows | Meaning / fix |
|---|---|
| `Could not connect` (WiFi) | Wrong name/password, a 5 GHz-only network, or a network with a login page. Try a phone hotspot to rule this out. |
| `HTTPS connection failed` | Update the board's WiFi firmware: Tools → Firmware Updater. |
| `HTTP status 401` | API key is wrong or revoked. |
| `HTTP status 429` | The API account has no credit or has hit its limit. |
| `No WiFi module found` | The board is not an UNO R4 WiFi. |

In all of these cases the ball keeps working with its built-in answers.

## Adjusting the piece

Settings near the top of the `.ino` file:

- `SESSION_WINDOW_MS`: how long repeated shakes count as the same visitor (default 30 s)
- `IDLE_RESET_MS`: how long before it returns to "Shake Me!" (default 20 s)
- `oracleResponses`, `offlineLevel2`, `offlineLevel3`: the pre-written answers
- `ORACLE_SYSTEM_PROMPT`: the AI's personality and rules
