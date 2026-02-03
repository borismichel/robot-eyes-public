# DeskBuddy

Expressive robot eyes for ESP32-S3 with AMOLED display, inspired by Anki's Cozmo robot.

**Target Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)

---

## Features

### Expressions & Animation
- **30 emotion presets** with smooth transitions (0.08s-0.35s)
- **Core**: Neutral, Happy, Sad, Surprised, Angry, Sleepy, Scared, Confused, Focused, Joyful
- **Special shapes**: Heart (Love), Star (Dizzy), Spiral (Dazed), Crescents (Joy)
- **Personality**: Curious, Thinking, Mischievous, Bored, Smug, Dreamy, Skeptical
- **Asymmetric**: Suspicious, Confused, Wink for added character
- **Idle behavior**: Random gaze scanning, micro-movements, natural blinking

### Interaction
| Input | Response |
|-------|----------|
| Tap | Cycle expressions |
| Hold 2s | "Petting" mode (happy + sound) |
| 2-finger tap | Open settings menu |
| Pick up | Scared expression |
| Shake | Confused + sound |
| Tilt | Eyes drift toward gravity |
| Face down | Hiding/shy expression |
| Loud noise | Grumpy/irritated |

### Time & Mood
- **Internal clock** with 12H/24H format
- **Mood shifts** based on time of day:
  - Morning (6am-12pm): Energetic, faster blinks
  - Afternoon: Balanced baseline
  - Evening (6pm-10pm): Relaxed, slower movements
  - Night (10pm-6am): Sleepy, heavy lids
- **Sleep cycle**: After 30 min inactivity → yawn → drowsy → sleep with breathing animation

### Pomodoro Timer
- Classic technique: Work → Short Break → repeat → Long Break
- **Visual**: Large countdown + 16px progress bar frame (depletes clockwise)
- **Audio**: Optional tick sound in last 60 seconds
- **Configurable**: Work (1-60 min), breaks (1-60 min), sessions (1-8)

### WiFi & Remote Control
- **Setup**: Connect to `DeskBuddy-Setup` AP, configure via captive portal
- **Access**: `http://deskbuddy.local` or device IP
- **Web UI tabs**: Dashboard, Display, Audio, Time, WiFi, Pomodoro, Expressions
- **Dashboard**: Status, WiFi, IP, Pomodoro state, Time, Uptime
- **Expression preview**: Click any of 30 expressions to preview live
- **Factory reset**: Hold BOOT button 5+ seconds

---

## Getting Started

### Hardware
- Waveshare ESP32-S3-Touch-AMOLED-1.8
  - 368×448 AMOLED display
  - Capacitive touch + QMI8658 IMU
  - ES8311 audio codec + speaker
  - 16MB Flash, 8MB PSRAM

### Build & Flash
```bash
# Build firmware
pio run

# Upload firmware
pio run -t upload

# Upload audio files (required)
pio run -t uploadfs

# Monitor serial
pio device monitor
```

### WiFi Setup
1. On first boot, connect to WiFi `DeskBuddy-Setup` (password: `deskbuddy`)
2. Browser opens setup page (or go to `http://192.168.4.1`)
3. Select your network and enter password
4. Device reboots and connects automatically
5. Access web UI at `http://deskbuddy.local`

---

## Usage

### On-Device Settings
Open with 2-finger tap, swipe up/down to navigate:

**Main Menu** → Pomodoro | Settings | Exit

**Pomodoro**: Start/Stop, Work duration, Short break, Long break, Sessions, Ticking, Back

**Settings**: Volume, Brightness, Mic Gain, Mic Threshold, Eye Color (8 presets), Time, Time Format, Back

### Web Interface

| Tab | Controls |
|-----|----------|
| Dashboard | Status cards, quick volume/brightness sliders |
| Display | Brightness, eye color picker |
| Audio | Volume, mic gain, mic threshold |
| Time | Hour/minute, 12H/24H toggle |
| WiFi | Status, scan networks, connect, forget |
| Pomodoro | Start/stop, all duration settings |
| Expressions | Grid of 30 buttons for live preview |

### REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | WiFi, pomodoro, time, uptime |
| `/api/settings` | GET/POST | All device settings |
| `/api/expression` | POST | Preview expression (index: 0-29) |
| `/api/pomodoro/start` | POST | Start timer |
| `/api/pomodoro/stop` | POST | Stop timer |
| `/api/wifi/scan` | GET | Available networks |
| `/api/wifi/connect` | POST | Connect (ssid, password) |
| `/api/wifi/forget` | POST | Clear credentials |
| `/api/time` | GET/POST | Device clock |

---

## Project Structure

```
src/
├── main.cpp                 # Application loop
├── eyes/                    # Parametric eye rendering
├── behavior/                # Expressions, idle, sleep, mood
├── input/                   # IMU and audio handlers
├── audio/                   # MP3 playback
├── ui/                      # Settings menu, pomodoro
├── network/                 # WiFi, web server, captive portal
└── animation/               # Tweening utilities

data/                        # Audio files (happy, confused, yawn, tick)
lib/                         # Waveshare GFX, ES8311 driver
```

---

## Development

### Adding Expressions

1. Add enum to `Expression` in `expressions.h`
2. Create preset in `ExpressionPresets` namespace
3. Add to `getExpressionShape()` and `getExpressionName()` switches

```cpp
inline EyeShape myExpression() {
    EyeShape s;
    s.height = 0.8f;
    s.outerCornerY = 0.2f;
    return s;
}
```

### Eye Shape Parameters

| Parameter | Description |
|-----------|-------------|
| `width`, `height` | Size multipliers (0.5-1.5) |
| `cornerRadius` | Roundness (0.0-2.0) |
| `offsetX/Y` | Gaze direction (-1.0 to 1.0) |
| `topLid`, `bottomLid` | Eyelid closure (0.0-1.0) |
| `innerCornerY`, `outerCornerY` | Corner offsets |
| `openness` | Overall eye openness (blink) |
| `topCurve`, `bottomCurve` | Edge curves for crescents |
| `stretch`, `squash` | Shape distortion |

### Display Rotation Note

The display is physically rotated 90° CCW:
- Buffer X (0-336) → Screen vertical
- Buffer Y (0-416) → Screen horizontal
- Eyes positioned side-by-side via different buffer Y values

---

## Technical Details

- **Rendering**: 30fps software per-pixel evaluation, RGB565 framebuffer
- **Optimization**: Dirty-rect clearing, partial screen blit, shape-aware bounds
- **Processing**: Display on Core 1, audio on Core 0
- **Storage**: Settings persisted via Preferences, audio via LittleFS

### Dependencies
- `lvgl/lvgl@^8.4.0` - Display driver
- `earlephilhower/ESP8266Audio@^1.9.7` - MP3 decoding
- GFX Library for Arduino (Waveshare)
- ES8311 codec driver

---

## License

This project is provided for educational and personal use.

## Acknowledgments

- Inspired by Anki's Cozmo robot
- Built for Waveshare ESP32-S3-Touch-AMOLED-1.8
