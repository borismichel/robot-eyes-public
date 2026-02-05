# DeskBuddy

Expressive robot eyes for ESP32-S3 with AMOLED display, inspired by Anki's Cozmo robot.

**Target Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)

---

## Features

### Expressions & Animation
- **32 emotion presets** with smooth transitions (0.08s-0.35s)
- **Core**: Neutral, Happy, Sad, Surprised, Angry, Sleepy, Scared, Confused, Focused, Joyful
- **Special shapes**: Heart (Love), Star (Dizzy), Spiral (Dazed), Crescents (Joy)
- **Personality**: Curious, Thinking, Mischievous, Bored, Smug, Dreamy, Skeptical
- **Asymmetric**: Suspicious, Confused, Wink for added character
- **Breathing**: BreathingPrompt, Relaxed for mindfulness exercises
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
- **NTP time sync**: Automatic time synchronization when WiFi is connected
- **Timezone support**: Configurable GMT offset (-12 to +14 hours)
- **12H/24H format**: Choose your preferred time display
- **Fallback clock**: Internal millis-based clock when offline
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

### Breathing Exercise
- **Box breathing**: 5-5-5-5 pattern (inhale, hold, exhale, hold)
- **3 cycles** = 60 seconds total
- **Visual**: Progress bar fills/empties with breath, phase text overlay (IN/HOLD/OUT)
- **Scheduled reminders**: Configurable interval (1-8 hours), active hours
- **Post-exercise**: Content (3s) → Relaxed (60s) calm-down animation
- **Access**: Settings menu → Mindfulness, or web UI Productivity tab

### WiFi & Remote Control
- **First boot**: Setup screen with "Configure WiFi" or "Use Offline" options
- **Setup**: Connect to `DeskBuddy-Setup` AP (password: `deskbuddy`), configure via web
- **Offline mode**: Eyes show normally, AP runs silently for optional web access
- **Access**: `http://deskbuddy.local` or `http://192.168.4.1` (AP mode)
- **Web UI tabs**: Dashboard, Display, Audio, Time, WiFi, Pomodoro, Expressions
- **Dashboard**: Status, WiFi, IP, Current Mood, Time, Uptime
- **Expression preview**: Click any of 30 expressions to preview live, current mood indicator
- **Audio test**: Test speaker output from web UI
- **Disable WiFi**: Completely turn off WiFi from web UI or device settings
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
1. On first boot, device shows WiFi info screen with SSID, password, and IP address
2. Connect phone/computer to WiFi `DeskBuddy-Setup` (password: `deskbuddy`)
3. Device detects your connection and shows choice screen:
   - **Configure WiFi**: Tap top half to proceed with WiFi setup
   - **Use Offline**: Tap bottom half to use without network (AP stays on for later config)
4. Open `http://192.168.4.1` in browser
5. Select your network and enter password
6. Device connects automatically, access web UI at `http://deskbuddy.local`

**Offline Mode**: If you choose "Use Offline", eyes display normally but the AP remains running silently. You can still configure settings via web at any time by connecting to the AP.

**Disable WiFi**: To completely turn off WiFi (no AP, no network), use the device settings menu (Settings → WiFi → tap to toggle) or the web UI (WiFi tab → Disable WiFi).

---

## Usage

### On-Device Settings
Open with 2-finger tap, swipe up/down to navigate:

**Main Menu** → Pomodoro | Settings | Mindfulness | Exit

**Pomodoro**: Start/Stop, Work duration, Short break, Long break, Sessions, Ticking, Back

**Settings**: Volume, Brightness, Mic Gain, Mic Threshold, Eye Color (8 presets), Time, Time Format, Timezone, WiFi (on/off), Back

**Mindfulness**: Breathe Now, Schedule (on/off), Interval (1-8 hours), Sound (on/off), Back

### Web Interface

| Tab | Controls |
|-----|----------|
| Dashboard | Status cards, current mood, quick volume/brightness sliders |
| Display | Brightness, eye color picker |
| Audio | Volume, mic gain, mic threshold, test audio button |
| Time | Hour/minute, 12H/24H toggle, timezone (GMT offset) |
| WiFi | Status, scan networks, connect, forget, NTP sync status |
| Productivity | Pomodoro settings, Breathing exercise settings |
| Expressions | Current mood indicator, grid of 32 buttons for live preview |
| System | Firmware version, OTA updates, restart, rollback |

### REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | WiFi, pomodoro, time, uptime, currentMood |
| `/api/settings` | GET/POST | All device settings (incl. breathing schedule) |
| `/api/expression` | POST | Preview expression (index: 0-31) |
| `/api/audio/test` | POST | Play test sound |
| `/api/pomodoro/start` | POST | Start timer |
| `/api/pomodoro/stop` | POST | Stop timer |
| `/api/breathing/start` | POST | Start breathing exercise |
| `/api/wifi/scan` | GET | Available networks |
| `/api/wifi/connect` | POST | Connect (ssid, password) |
| `/api/wifi/forget` | POST | Clear credentials |
| `/api/wifi/disable` | POST | Disable WiFi completely |
| `/api/time` | GET/POST | Device clock |
| `/api/system/info` | GET | Firmware version, memory stats |
| `/api/ota/upload` | POST | Upload firmware binary |
| `/api/system/restart` | POST | Restart device |
| `/api/system/rollback` | POST | Rollback to previous firmware |

---

## Project Structure

```
src/
├── main.cpp                 # Application loop
├── eyes/                    # Parametric eye rendering
├── behavior/                # Expressions, idle, sleep, mood, breathing
├── input/                   # IMU and audio handlers
├── audio/                   # MP3 playback
├── ui/                      # Settings menu, pomodoro
├── network/                 # WiFi, web server, captive portal, OTA
└── animation/               # Tweening utilities

data/                        # Audio files (happy, confused, yawn, tick, breathe_reminder)
lib/                         # Waveshare GFX, ES8311 driver
include/                     # version.h, pin_config.h
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
