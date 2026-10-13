# AIstickC

**A pocket voice assistant built into an M5StickC PLUS2.** Hold the button, say
something, read the answer on the screen. Speech recognition, an LLM reply,
Cyrillic rendered with its own font, dialogue memory and power saving — all on
a board the size of a key fob.

[![tests](https://github.com/elcrazycol/AIstickC/actions/workflows/tests.yml/badge.svg)](https://github.com/elcrazycol/AIstickC/actions/workflows/tests.yml)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-espressif32-orange)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

![Recording with a waveform](docs/images/demo-recording.gif)

*Русская версия: [README.md](README.md).*

---

## What it is

A device that does exactly one thing: **question by voice → answer as text**. No
cloud app, no subscription, no phone nearby — just the stick, Wi-Fi and two APIs.

What makes it more than a demo:

- **Its own proxy on a VPS.** `api.groq.com` is not reachable from Russia
  directly (403, geo-block), so recognition goes through a personal server: the
  stick sends raw PCM, the proxy builds the multipart request and talks to
  Whisper. The Groq key lives on the server and never reaches the firmware.
- **The answer appears as it is generated.** The model request is streamed (SSE),
  so the text types itself onto the screen instead of dropping in all at once.
- **Dialogue memory.** Keeps the last 5 exchanges and sends them as context.
- **Its own Cyrillic font.** The built-in M5GFX fonts have no Cyrillic, so the
  font is generated from JetBrains Mono by a script.
- **Model reasoning is disabled.** Otherwise every OpenCode Go model puts the
  answer into `reasoning_content` while `content` stays empty or arrives at the
  very end — and the reply takes twice as long.

## Demo

| Recording | Waiting | Answer typing in |
|---|---|---|
| ![Recording](docs/images/demo-recording.gif) | ![Waiting](docs/images/demo-waiting.gif) | ![Typing](docs/images/demo-typing.gif) |

Still screens: [idle](docs/images/screen-idle.png),
[recording](docs/images/screen-recording.png),
[waiting](docs/images/screen-waiting.png),
[answer](docs/images/screen-answer.png),
[reset panel](docs/images/screen-notice.png).

## Hardware

| Item | Value |
|---|---|
| Board | M5StickC PLUS2 |
| Chip | ESP32-PICO-V3-02 (classic ESP32, 2 cores @ 240 MHz) |
| Memory | 8 MB flash, 2 MB PSRAM |
| Microphone | SPM1423, **PDM** (not I2S) |
| Mic pins | data G34, PDM clock G0 |
| Display | 240×135 ST7789v2 |
| Buttons | BtnA (front), BtnB (side), PWR (top) |
| Sound | piezo buzzer on PWM, GPIO2 |
| Power | built-in battery, USB-C charging |

Three things the real board taught us, and they shaped the code:

1. **DMA on the classic ESP32 does not write to PSRAM.** Capture happens in 8 KB
   chunks in DRAM, then `memcpy` into PSRAM. Without it — white noise.
2. **PSRAM must be enabled by hand.** The `arduino-esp32` core drops
   `CONFIG_SPIRAM` and skips `psramInit()` unless `BOARD_HAS_PSRAM` is defined at
   build time.
3. **The PDM mic needs a warm-up.** The first ~200 ms is a DC step, not sound.
   Capture and throw it away, or a click lands at the start of every recording.

## Architecture

```
        audio (PCM)                        text
stick ───────────────> VPS proxy ────────────────> Groq Whisper
 │  HTTPS :9443             │  Groq key lives here
 │                          └─ server builds WAV + multipart
 │
 │  HTTPS, stream:true
 └────────────────────> opencode.ai/zen/go/v1 ─────> DeepSeek
        JSON, SSE stream       (x-opencode-session)
```

Why split it like this: `opencode.ai` is reachable from Russia, so the LLM goes
directly; `api.groq.com` is not, so only recognition goes through the proxy.

### Repository layout

```
aistick/
├─ src/
│  ├─ main.cpp            state machine, buttons, power saving
│  ├─ ui.cpp/.h           all drawing: screens, font, animations
│  ├─ net.cpp/.h          Wi-Fi and the shared TLS client
│  ├─ stt.cpp/.h          request to the proxy → Whisper
│  ├─ llm.cpp/.h          streamed request to OpenCode Go (SSE)
│  └─ pure/               pure modules: no Arduino, tested on a Mac
│     ├─ chat_history.h   dialogue memory with trimming
│     ├─ chunked.h        incremental chunked decoder
│     ├─ http_response.h  HTTP response parsing
│     ├─ json_extract.h   extract fields by path
│     ├─ llm_request.h    build the JSON request
│     ├─ sse.h            Server-Sent Events parsing
│     ├─ text_wrap.h      UTF-8 wrap by pixels + pagination
│     ├─ wav.h            WAV header
│     ├─ base64.h, cyrillic.h
├─ include/
│  ├─ settings.h          non-secret settings
│  ├─ config.h.example    secrets template (config.h is gitignored)
│  ├─ font_jbmono.h       generated Cyrillic font
│  └─ idle_image.h        generated idle-screen picture
├─ tools/
│  ├─ run_tests.sh        run the tests (clang++, no hardware)
│  ├─ capture.py          serial → WAV / device log
│  ├─ make_font.py        TTF → GFXfont
│  ├─ make_image.py       PNG → RGB565/1-bit
│  └─ mock_screen.py      screen emulator → PNG/GIF for the README
├─ deploy/                proxy on a VPS (see deploy/README.md)
└─ test/                  native tests
```

The point of `src/pure/` is that all logic that can be verified without hardware
lives there (JSON, SSE, chunked, text wrapping, history). The firmware only glues
those modules to the network and the display.

## Quick start

```bash
# 1. Tooling
uv tool install platformio

# 2. Secrets
cp include/config.h.example include/config.h
# fill in WIFI_SSID, WIFI_PASS (2.4 GHz — the ESP32 cannot see 5 GHz)

# 3. Proxy on a VPS (needed for recognition)
#    step by step: deploy/README.md
bash deploy/push.sh <your-domain> 9443
bash deploy/fill-config.sh <your-domain> 9443   # writes PROXY_* into config.h

# 4. Flash
pio run -e esp32dev -t upload

# 5. Device log (opens the port and resets the board itself)
python3 tools/capture.py --monitor
```

## Controls

| Action | Effect |
|---|---|
| **BtnA** (hold) | record; release to send |
| **BtnB** (tap) | previous page |
| **PWR** (tap, top) | next page |
| **BtnB** (hold 3 s) | reset session: bar → "History cleared" panel → idle |

While BtnB is held, a progress bar grows at the bottom; the reset panel stays
until you let go, so it cannot be missed.

## How it works

**Recognition.** The stick sends raw LPCM (16-bit, LE, mono, 16 kHz) to its own
proxy. The proxy builds a WAV and a multipart request and calls
`whisper-large-v3-turbo`. The firmware stays simple: no WAV header, no multipart
on the device.

**The reply.** `POST /zen/go/v1/chat/completions` with `stream:true` and
`reasoning_effort:"none"`. The response arrives as `text/event-stream`;
`delta.content` pieces are printed immediately. The chunked body is decoded
incrementally, so memory does not grow with stream length.

**Memory.** The last 5 exchanges live on the heap and go into the request as a
`messages` array. Trimming is by exchange count and character budget so the
request stays small. A sent-message counter lives in NVS and survives power-off.

**Display.** A custom monospace font (ASCII + Cyrillic + typography: dashes,
quotes, ellipsis). Text is wrapped by words and paginated. While work is in
progress, three flowing dots are animated by a separate task on the second core
(network calls block, so from the main loop the animation froze on the TLS
handshake).

## Plugins

The model itself decides when to call a tool (tool calling) — by the meaning of
the phrase, not by keywords. A plugin is an entry in the `src/plugins.cpp`
table: function name, description for the model, JSON argument schema and an
action.

There are three today:

| Plugin | Action |
|---|---|
| `set_lamp` | GPIO26: `state` = on / off / toggle |
| `show_text` | a panel with text from the model |
| `http_get` | GET a configured URL (+ optional `query`) |

When a plugin runs, a short result panel appears and the exchange is stored in
the dialogue memory tagged `[плагин]` (so the model does not think it already
answered). The `answerAfter` flag makes it also comment on the result (the
result is sent as a `role:"tool"` message).

Adding a plugin means adding a table row and rebuilding. The next step is moving
the table into LittleFS so a plugin can be dropped in as a file without
rebuilding.

## Tests

```bash
tools/run_tests.sh
```

Built with `clang++` and requiring neither PlatformIO nor hardware, so they run
anywhere. Currently **10 suites**: `chat_history`, `chunked`, `http_response`,
`json`, `llm_request`, `sse`, `text_wrap`, `wav`, `base64`, `cyrillic`. The same
run executes in CI on every push (badge at the top).

## Power

Idle power is saved in steps (thresholds in `include/settings.h`):

| After | What happens |
|---|---|
| 20 s | backlight off (returns instantly on any button) |
| 90 s | Wi-Fi off (the next question brings it back) |
| 1 h | deep sleep; the big button wakes it |

Deep sleep rather than a light idle: it saves the most, but Wi-Fi has to come up
again on wake — hence the one-hour threshold, when an active session is surely
over.

## Design decisions (deviations from the original spec)

The project started as a spec for an ESP32-S3 with an I2S microphone. Real
hardware and real APIs changed the plan:

| Spec | Reality | Why |
|---|---|---|
| ESP32-S3 | ESP32-PICO-V3-02 | that is the board at hand |
| I2S mic SPM1429 | PDM SPM1423 | same case, different interface |
| Groq directly | through a personal proxy | 403 geo-block from Russia |
| 8 s recording | 15 s | the user wanted more |
| Whole reply at once | SSE streaming | text should appear right away |
| 16 px font, 7 lines | 14 px, 5 lines | the font's tight metrics |
| Model "thinking" | `reasoning_effort:"none"` | otherwise empty replies, twice as slow |

## Roadmap

**Done:** milestones 0–2 (hardware, recognition, reply), UI with its own font,
dialogue memory, streaming, idle-screen picture and battery, power saving, the
plugin system (tool calling: GPIO, screen, HTTP).

**Remaining:**

- RTC journal: keep the last stages and errors in RTC memory so the cause of a
  reset survives a reboot (today a silent reboot is a blind spot).
- A distinct "Did not catch that" message for empty recognition.
- Auto-return to IDLE 5 s after an error.
- Cancelling a recording with a button.
- File-based plugins: move the table into LittleFS so a plugin can be dropped in
  without rebuilding the firmware.

**Not verified on hardware:** waking from deep sleep and maximum-length (15 s)
recording — both were exercised only at smaller scales.

## Credits

- **JetBrains Mono** — the font, OFL 1.1 licensed; glyphs are baked into GFXfont.
- **M5Unified / M5GFX** — board and display support.
- **Konata** — the idle-screen picture (a personal decoration, not under MIT).

## License

[MIT](LICENSE). Exceptions: the font is OFL 1.1, and the character picture is not
under MIT.
