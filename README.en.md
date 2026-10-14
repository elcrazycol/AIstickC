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
[reset panel](docs/images/screen-notice.png),
[plugin panel](docs/images/screen-plugins.png),
[settings](docs/images/screen-settings.png),
[dialogue and answer](docs/images/screen-dialog.png).

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
│  ├─ plugins.cpp/.h      plugins: script loading and execution
│  ├─ script_host.cpp/.h  whitelist of functions for scripts
│  ├─ repo.cpp/.h         repositories: manifests, install, versions
│  ├─ webfetch.cpp/.h     https requests (shared by plugins and repositories)
│  ├─ hold.cpp/.h         button hold: counting and the bottom bar
│  ├─ chat_settings.cpp/.h  memory and answer-limit choice (stored in NVS)
│  └─ pure/               pure modules: no Arduino, tested on a Mac
│     ├─ chat_history.h   dialogue memory with trimming
│     ├─ chunked.h        incremental chunked decoder
│     ├─ chat_options.h   setting steps and the "auto" picking
│     ├─ hold_button.h    button hold counting (events and percentages)
│     ├─ http_response.h  HTTP response parsing
│     ├─ json_extract.h   extract fields by path
│     ├─ llm_request.h    build the JSON request
│     ├─ plugin_meta.h    script header → tool description
│     ├─ plugin_repo.h    repository manifest parsing
│     ├─ script.h         the plugin language interpreter
│     ├─ sse.h            Server-Sent Events parsing
│     ├─ text_wrap.h      UTF-8 wrap by pixels + pagination
│     ├─ tool_call.h      tools for the model and its call parsing
│     ├─ wav.h            WAV header
│     ├─ weather_text.h   Russian weather text assembly
│     ├─ base64.h, cyrillic.h
├─ include/
│  ├─ settings.h          non-secret settings
│  ├─ config.h.example    secrets template (config.h is gitignored)
│  ├─ font_jbmono.h       generated Cyrillic font
│  └─ idle_image.h        generated idle-screen picture
├─ data/
│  └─ plugins/*.ais       plugins (uploaded to LittleFS)
├─ partitions.csv         partition table: 3 MB app, 4 MB LittleFS
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

# 5. Plugins: flash the starting set into the filesystem
#    (after that, install and update straight from the stick — shake it)
pio run -e esp32dev -t uploadfs

# 6. Device log (opens the port and resets the board itself)
python3 tools/capture.py --monitor
```

## Controls

| Action | Effect |
|---|---|
| **BtnA** (tap) | wake the screen, nothing more |
| **BtnA** (hold 0.2 s) | record; release to send |
| **BtnB** (tap, while recording) | cancel: recording stops, nothing is sent |
| **BtnB** (tap) | previous page |
| **PWR** (tap, top) | next page |
| **BtnB** (hold 1.5 s) | from the answer screen — back to the idle screen; the dialogue is kept |
| **BtnB** (hold 3 s) | on the idle screen — reset the dialogue: bar → "History cleared" panel |
| **Shake** | settings menu (first item — plugins) |

Paging also works while an answer is still being written. A finished answer
wraps around; one still in progress does not jump from start to end. The screen
does not move on its own: as new text arrives, only the page count changes.

The BtnA threshold is tiny: recording starts almost immediately, but an
ordinary click does not raise it — only a confident hold does. Any press wakes
the screen.

While BtnB is held, a progress bar grows at the bottom, so the countdown is
visible. The reset panel stays until you let go, so it cannot be missed.

Resetting the dialogue only works on the idle screen: from the answer screen the
button simply goes back, so memory cannot be wiped by accident while reading.

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
quotes, ellipsis). Text is wrapped by words and paginated. The `x/y` counter
lives in the status bar, to the left of the battery and Wi-Fi, so it takes no
text line. While an answer is being written the screen does not flip by itself:
the page stays where you left it and only the page count grows. Finished pages
can be flipped through without waiting for the end. While work is in
progress, three flowing dots are animated by a separate task on the second core
(network calls block, so from the main loop the animation froze on the TLS
handshake).

## Plugins

The model itself decides when to call a tool (tool calling) — by the meaning of
the phrase, not by keywords.

A plugin is a **script** in a tiny built-in language: an `.ais` file in
`/plugins` on LittleFS. Installing or removing a plugin needs no firmware
rebuild.

### Settings menu

Shake the stick to open the menu: **Plugins** · **Dialogue and answer** ·
**Reset settings**. Inside the dialogue submenu there are three items, the
answer length first, because the token limit follows it:

| Item | Values |
|---|---|
| Answer length | `кратко` · `обычно` · `подробно` |
| Dialogue memory | `авто` · 1 · 3 · 5 · 8 · 12 exchanges |
| Answer limit | `авто` · 150 · 300 · 600 · 1200 · 2400 tokens |

Navigation: **PWR** — forward, **BtnB** (tap) — back, **BtnA** (tap) — enter an
item or switch its value, **BtnB** (hold 1.5 s) — one level up. The choice lives
in NVS and survives power-off.

**Answer length** is a request to the model plus a token ceiling sized for it.
The screen shows 5 lines of ~29 characters, so a page is about 145 characters,
and the model produces roughly 6 characters per token (measured on the device).
The ceiling is therefore computed straight from the page count:

| Level | What we ask | Ceiling | Result (measured) |
|---|---|---|---|
| `кратко` | 2-3 sentences | 97 tokens | ~3 screens |
| `обычно` | up to 10 sentences | 121 tokens | ~5 screens |
| `подробно` | up to 16 sentences | 242 tokens | ~10 screens |

The measurements showed the key fact: **the model writes up to the ceiling**.
While the ceiling is below what it wants to say, truncation is unavoidable, and
wording does not cure it — the vague "3-4 screens of text" was read as "write a
lot" and produced 20+ screens at a 1200 limit.

So truncation is cured by trimming, not by prompting: if an answer still hits
the ceiling, the stick cuts the unfinished phrase back to the last sentence end.
No half-word stub is ever left on screen, and the length stays exactly what was
chosen. If the cut lands inside a code sample, we cut before the sample instead
of leaving `func main() { fmt.`.

**"Авто" for the limit** means "follow the answer length" — the row shows the
number that came out of it (`авто 600`). It can also be pinned manually.

**"Авто" for memory** is measured: as many exchanges as fit the character
budget at the current average exchange size. Short replies keep more exchanges,
long answers fewer, while the request stays the same size. While the dialogue is
empty the factory 5 exchanges are used.

**"Reset settings"** returns everything to factory: length "кратко", memory and
limit to "авто".

The menu does not hang forever: on the same power-saving timer (20 s without a
press) the stick returns to the idle screen and turns the screen off. Buttons
refresh the countdown, so navigating does not close the menu.

### Plugin panel

### Plugin panel

**Shake the stick** — the settings menu opens, the first item is "Plugins". The
panel lists what the repository offers, marked `стоит` (installed) or `обновить`
(update available), plus installed plugins that are no longer in the repository.

The mark on the right of a name tells what the stick has:

| Mark | Meaning |
|---|---|
| 🟢 green dot | installed, nothing to update |
| 🟠 orange dot in a ring | installed, but the repository has a newer version |
| nothing | not installed |

| Button | Action |
|---|---|
| **BtnA** (tap) | install / remove the selected plugin |
| **PWR** (tap) | next item |
| **BtnB** (tap) | previous item |
| **BtnB** (hold 1.5 s) | back: panel → settings, settings → idle screen (same progress bar as the history reset) |

Manifest addresses are listed in `config.h` (comma-separated):

```c
#define PLUGIN_REPOS "https://raw.githubusercontent.com/<user>/<repo>/<branch>/index.txt"
```

The manifest is cached in LittleFS, so with no network the panel still shows the
last good list instead of nothing. A broken script cannot brick the stick: it
simply fails to load.

### Your own repository

Put the script files next to the manifest. The manifest uses the same blank-line
separated blocks with `key = value` lines:

```
name = switch
description = Turn the switch on, off or toggle it
params = channel=1|2|3|all, state*=on|off|toggle
file = switch.ais
version = 1
```

| Key | Meaning |
|---|---|
| `name` | function name for the model, latin |
| `description` | what it does; the model decides by this |
| `params` | argument description; `*` marks a required one |
| `file` | script file name next to the manifest |
| `version` | number; if the repository has a higher one, the panel shows `обновить` |

A ready example is the
[aistick-plugins](https://github.com/elcrazycol/aistick-plugins) repository.

### Script format

Metadata goes into the leading `#` comment header, the body follows:

```
# name: weather
# description: Get today's weather in a city. If no city is given, call without the city parameter.
# params: city

if city == "" {
  ask("Which city?")
} else {
  raw = http("GET", "https://wttr.in/" + city + "?format=%l|%C|%t|%f|%w|%h")
  answer(weather_text(raw))
}
```

Arguments arrive as variables (`city`, `state`, `channel`). The language is
deliberately tiny: numbers, strings, `if`/`else`, `while` with a step cap, host
calls. A script has no file access, no arbitrary pins and no secrets — only a
whitelist of functions is available:

| Function | Purpose |
|---|---|
| `show(text)` | short panel on the stick screen |
| `answer(text)` | long answer, like the model's |
| `ask(text)` | clarifying question; the next utterance goes to the plugin |
| `http(method, url[, body])` | https request |
| `proxy_post(path, body)` | POST to our proxy with `X-Auth` (the secret stays hidden) |
| `gpio(pin, value)` · `gpio_get(pin)` | whitelisted pins only |
| `json(text, path)` | extract a field: `state` or `a/[0]/b` |
| `len` · `str` · `num` · `contains` · `log` · `sleep` | small helpers |

A script runs in its own task with stack and step limits, so a hanging plugin
fails instead of taking the stick down.

The Tuya switch is controlled **through the VPS proxy**: the stick sends
`POST /tuya` with the shared `X-Auth`, and the server talks to the Tuya cloud.
Tuya secrets stay on the server — the firmware has none. Setup is described in
[deploy/README.md](deploy/README.md).

A plugin can be **two-stage**. `weather` works like this: if no city was given,
a "Which city?" panel appears, and the next utterance is not sent to the model —
it goes to the plugin. Weather comes from `wttr.in` (no key), and the answer is
assembled into Russian: the condition is translated with a dictionary, and the
wind arrow, which the font lacks, is stripped.

When a plugin runs, a short result panel appears and disappears back to the idle
screen by itself after a couple of seconds. The exchange is deliberately **not**
stored in the dialogue memory: otherwise the model starts copying the note as
text instead of calling the tool.

## Tests

```bash
tools/run_tests.sh
```

Built with `clang++` and requiring neither PlatformIO nor hardware, so they run
anywhere. Currently **13 suites**: `chat_history`, `chunked`, `http_response`,
`json`, `llm_request`, `plugin_file`, `sse`, `text_wrap`, `tool_call`, `wav`,
`weather_text`, `base64`, `cyrillic`. The same
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
dialogue memory, streaming, idle-screen picture and battery, power saving,
plugin system v2: scripts in LittleFS, a plugin panel opened by shaking, install
and update from repositories — no rebuild and no cable.

**Remaining:**

- RTC journal: keep the last stages and errors in RTC memory so the cause of a
  reset survives a reboot (today a silent reboot is a blind spot).
- A distinct "Did not catch that" message for empty recognition.
- Auto-return to IDLE 5 s after an error.
- Cancelling a recording with a button.

**Not verified on hardware:** waking from deep sleep and maximum-length (15 s)
recording — both were exercised only at smaller scales.

## Credits

- **JetBrains Mono** — the font, OFL 1.1 licensed; glyphs are baked into GFXfont.
- **M5Unified / M5GFX** — board and display support.
- **Konata** — the idle-screen picture (a personal decoration, not under MIT).

## License

[MIT](LICENSE). Exceptions: the font is OFL 1.1, and the character picture is not
under MIT.
