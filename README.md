# DeskBuddy

Expressive robot eyes for ESP32-S3 with AMOLED display, voice assistant, and MCP integration. Inspired by Anki's Cozmo robot.

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
| Tap | Cycle expressions (or start/stop listening when assistant active) |
| Hold 2s | "Petting" mode (happy + sound) |
| 2-finger tap | Open settings menu |
| Pick up | Scared expression |
| Shake | Confused + sound |
| Tilt | Eyes drift toward gravity |
| Face down | Hiding/shy expression |
| Loud noise | Grumpy/irritated |

### Voice Assistant
- **LLM**: Claude (Sonnet 4) or OpenAI (GPT-4o), user-configurable
- **Speech-to-Text**: OpenAI Whisper (streaming 16kHz mono)
- **Text-to-Speech**: OpenAI TTS
- **Wake word**: ESP-SR local detection ("Hey Buddy"), no cloud required
- **Tap-to-Listen**: Single tap starts listening, second tap stops and processes
- **STT Language**: Configurable Whisper language hint (ISO 639-1) for improved accuracy
- **Tool use**: LLM can control device tools + external MCP server tools
- **Full-duplex audio**: Simultaneous TTS output and STT input via ES8311 codec

### MCP Integration
- **MCP Server** (port 3001): Exposes DeskBuddy tools to external Claude instances via SSE transport
- **MCP Client**: Connects to external MCP servers for additional tool discovery (up to 8 servers, 16 tools each)
- **14 device tools** available via both LLM and MCP:
  - `set_expression` - Change facial expression (18 named expressions)
  - `set_timer` / `cancel_timer` - Countdown timer with on-screen progress
  - `start_pomodoro` / `stop_pomodoro` - Productivity timer
  - `get_device_info` - Device status (expression, WiFi, timers, volume, brightness, eye color)
  - `play_sound` - Audio feedback (happy, sad, alert, confirm, error)
  - `set_reminder` / `cancel_reminder` / `list_reminders` - Timed reminders
  - `start_breathing` - Guided breathing exercise (box or nadi shodhana)
  - `set_volume` / `set_brightness` / `set_eye_color` - Device settings control

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
- **Sleep cycle**: After 30 min inactivity, yawn, drowsy, sleep with breathing animation

### Productivity Tools

#### Pomodoro Timer
- Classic technique: Work, Short Break, repeat, Long Break
- **Visual**: Large countdown + 16px progress bar frame (depletes clockwise)
- **Audio**: Optional tick sound in last 60 seconds
- **Configurable**: Work (1-60 min), breaks (1-60 min), sessions (1-8)

#### Countdown Timer
- Standalone timer with on-screen countdown and progress display
- Celebration animation when done
- Controllable via web UI, voice assistant, or MCP

#### Timed Reminders
- Up to 20 reminders with hour:minute trigger time
- Message displayed on screen in large text (max 48 chars) + alert sound
- One-shot or recurring (daily)
- Snooze (5 min) or dismiss via touch
- Auto-snooze after 60 seconds of no interaction
- Persisted to NVS across reboots
- Manageable via web UI, voice assistant, or MCP

### Breathing Exercises
- **Box breathing**: 5-5-5-5 pattern (inhale, hold, exhale, hold), 3 cycles = 60 seconds
- **Nadi Shodhana**: Alternate nostril breathing, 6-phase cycle with asymmetric eye animation, 3 full cycles = 72 seconds
- **Visual**: Progress bar fills/empties with breath, phase text overlay
- **Scheduled reminders**: Configurable interval (1-8 hours), active hours
- **Post-exercise**: Content (3s), Relaxed (60s) calm-down animation
- **Access**: Settings menu, web UI, or MCP/voice (`start_breathing` with `exercise` param)

### WiFi & Remote Control
- **First boot**: Setup screen with "Configure WiFi" or "Use Offline" options
- **Setup**: Connect to `DeskBuddy-Setup` AP (password: `deskbuddy`), configure via web
- **Offline mode**: Eyes show normally, AP runs silently for optional web access
- **Access**: `http://deskbuddy.local` or `http://192.168.4.1` (AP mode)
- **Web UI tabs**: Dashboard, Productivity, Mindfulness, Assistant, Settings, Expressions
- **Dashboard**: Status, WiFi, IP, Current Mood, Time, Uptime
- **Expression preview**: Click any of 32 expressions to preview live, current mood indicator
- **Audio test**: Test speaker output from web UI
- **Disable WiFi**: Completely turn off WiFi from web UI or device settings
- **Factory reset**: Hold BOOT button 5+ seconds
- **OTA updates**: Drag-and-drop firmware upload with rollback support

---

## Getting Started

### Hardware
- Waveshare ESP32-S3-Touch-AMOLED-1.8
  - 368x448 AMOLED display
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

**Disable WiFi**: To completely turn off WiFi (no AP, no network), use the device settings menu (Settings > WiFi > tap to toggle) or the web UI (Settings tab > WiFi > Disable WiFi).

---

## Architecture

DeskBuddy uses a modular class-based architecture. Each module follows the `begin()`/`update()` lifecycle pattern and owns its own state.

### System Diagram
```
main.cpp (orchestrator, ~1,464 lines)
  │
  ├── Rendering Pipeline
  │   ├── EyeRenderer         - Per-pixel eye shape rasterization
  │   ├── EyeAnimator         - Gaze, shape building, tweening
  │   ├── ScreenCompositor    - Framebuffer, dirty rects, progress bars, blits
  │   └── TextRenderer        - Shared font & drawing utilities
  │
  ├── Behavior Layer
  │   ├── ExpressionManager   - Priority-based expression control (8 levels)
  │   ├── BlinkController     - Blink state machine (single/double)
  │   ├── MicroExpressionSystem - Idle personality (winks, glances, squints)
  │   ├── ReactiveAnimations  - Timed reactions (joy, love, irritated, IMU)
  │   ├── MoodDrift           - Time-of-day mood shifting
  │   ├── IdleBehavior        - Autonomous gaze scanning
  │   └── SleepBehavior       - Inactivity-driven sleep cycle
  │
  ├── UI Layer
  │   ├── SettingsMenu        - On-device hierarchical menu
  │   ├── PomodoroTimer       - Work/break state machine
  │   ├── CountdownTimer      - Standalone timer
  │   ├── TimerVisuals        - Timer expression effects & sounds
  │   ├── ReminderManager     - Timed message reminders
  │   └── BreathingExercise   - Box & nadi shodhana exercises
  │
  ├── Input Layer
  │   ├── Touch (I2C FT5x06)  - Tap, hold, pet gestures
  │   ├── ImuHandler          - Tilt, shake, orientation
  │   └── AudioHandler        - Ambient noise level
  │
  ├── Network Layer
  │   ├── NetworkSetup        - WiFi lifecycle, NTP, captive portal
  │   ├── WiFiManager         - Multi-network, AP mode, connection state
  │   ├── WebServerManager    - REST API + web UI
  │   ├── CaptivePortal       - First-boot setup
  │   └── OtaManager          - Firmware updates
  │
  └── Voice Assistant
      ├── Assistant            - STT → LLM → TTS orchestrator
      ├── LLMClient           - Claude / OpenAI API
      ├── STTClient            - Whisper streaming
      ├── TTSClient            - OpenAI TTS
      ├── WakeWord             - ESP-SR local detection
      ├── VoiceInput           - Ring buffer microphone capture
      ├── MCPServer            - SSE transport (port 3001)
      ├── MCPClient            - External tool discovery
      └── DeviceTools          - 14 tool definitions + callbacks
```

### Source Tree
```
src/
├── main.cpp                        # Orchestrator: frame loop, input, callbacks
├── eyes/
│   ├── eye_shape.h                 # EyeShape struct (parametric)
│   ├── eye_renderer.h/.cpp         # Per-pixel rasterizer (RGB565)
│   ├── eye_animator.h/.cpp         # Gaze + shape compositor with tweening
│   └── eye_params.h                # Rendering constants
├── display/
│   ├── screen_compositor.h/.cpp    # Framebuffer, dirty rects, progress bars, blits
│   └── display_driver.h/.cpp       # SH8601 AMOLED + LVGL init
├── animation/
│   ├── tweener.h/.cpp              # EyeShapeTweener smooth interpolation
│   └── blink_controller.h/.cpp     # Blink state machine
├── behavior/
│   ├── expressions.h               # Expression enum (32 presets) + shapes
│   ├── expression_manager.h/.cpp   # Priority stack with token system
│   ├── micro_expressions.h/.cpp    # Idle personality (wink, glance, squint)
│   ├── reactive_animations.h/.cpp  # Timed reactions (joy, love, IMU, etc.)
│   ├── idle_behavior.h/.cpp        # Autonomous gaze + blink scheduling
│   ├── sleep_behavior.h/.cpp       # Inactivity → yawn → sleep
│   ├── mood_drift.h/.cpp           # Gradual mood transitions
│   ├── time_mood.h                 # Time-of-day mood modifiers
│   └── breathing_exercise.h/.cpp   # Box + nadi shodhana exercises
├── input/
│   ├── imu_handler.h/.cpp          # QMI8658 IMU: tilt, shake, orientation
│   └── audio_handler.h/.cpp        # Ambient noise detection
├── audio/
│   ├── audio_player.h/.cpp         # MP3 playback (FreeRTOS task)
│   ├── audio_output_duplex.h/.cpp  # Sample rate conversion
│   └── i2s_duplex.h/.cpp           # ES8311 I2S full-duplex driver
├── ui/
│   ├── settings_menu.h/.cpp        # Hierarchical on-device menu
│   ├── text_renderer.h/.cpp        # Shared font & drawing (90° rotation)
│   ├── pomodoro.h/.cpp             # Pomodoro state machine
│   ├── countdown_timer.h/.cpp      # Countdown state machine
│   ├── timer_visuals.h/.cpp        # Timer expression effects & tick sounds
│   └── reminder_manager.h/.cpp     # Timed reminders with NVS persistence
├── network/
│   ├── network_setup.h/.cpp        # WiFi lifecycle orchestration
│   ├── wifi_manager.h/.cpp         # Multi-network WiFi + NTP
│   ├── web_server.h/.cpp           # REST API endpoints (~1,720 lines)
│   ├── web_ui_html.h               # Web UI HTML/CSS/JS (~2,600 lines)
│   ├── captive_portal.h/.cpp       # First-boot AP setup
│   └── ota_manager.h/.cpp          # OTA firmware updates
└── assistant/
    ├── assistant.h/.cpp            # Voice pipeline orchestrator
    ├── llm_client.h/.cpp           # Claude / OpenAI LLM
    ├── stt_client.h/.cpp           # Whisper STT
    ├── tts_client.h/.cpp           # OpenAI TTS
    ├── wake_word.h/.cpp            # ESP-SR wake word
    ├── voice_input.h/.cpp          # Mic ring buffer
    ├── mcp_server.h/.cpp           # MCP SSE server
    ├── mcp_client.h/.cpp           # External MCP client
    ├── device_tools.h/.cpp         # Tool definitions + callbacks
    └── http_helpers.h              # Shared secure client factory

data/                               # Audio files (happy, confused, yawn, tick, etc.)
lib/                                # Waveshare GFX, ES8311 driver, Adafruit BusIO
include/                            # version.h, pin_config.h
```

### Key Design Patterns

**Priority Expression Stack**: `ExpressionManager` replaces 8 `expressionBefore*` globals with a token-based priority system. Subsystems call `request(expr, priority)` and get a token. On `release(token)`, the expression auto-reverts to the next highest active priority.

**Dirty Rect Optimization**: `ScreenCompositor` tracks previous and current eye bounding boxes, clearing and blitting only changed regions for 30fps with minimal bus traffic.

**begin()/update() Lifecycle**: Every module initializes with `begin(deps...)` in `setup()` and runs its state machine via `update()` in `loop()`.

---

## Usage

### On-Device Settings
Open with 2-finger tap, swipe up/down to navigate:

**Main Menu**: Pomodoro | Mindfulness | Settings | Exit

**Pomodoro**: Start/Stop, Work duration, Short break, Long break, Sessions, Ticking, Back

**Settings**: Volume, Brightness, Mic Gain, Mic Threshold, Eye Color (8 presets), Time, Time Format, Timezone, WiFi (on/off), Back

**Mindfulness**: Breathe Now, Schedule (on/off), Interval (1-8 hours), Sound (on/off), Back

### Web Interface

| Tab | Contents |
|-----|----------|
| Dashboard | Status cards, current mood, quick volume/brightness sliders |
| Productivity | Reminders, Countdown Timer, Pomodoro Timer |
| Mindfulness | Breathing exercises (Box/Nadi Shodhana), settings and schedule |
| Assistant | LLM provider (Claude/OpenAI), API keys, voice settings, push-to-talk, MCP servers |
| Settings | Display, Audio, Time, WiFi, System (OTA, restart, rollback) |
| Expressions | Current mood indicator, grid of 32 buttons for live preview |

### REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | WiFi, pomodoro, time, uptime, currentMood |
| `/api/settings` | GET/POST | All device settings (incl. breathing schedule) |
| `/api/expression` | POST | Preview expression (index: 0-31) |
| `/api/audio/test` | POST | Play test sound |
| `/api/pomodoro/start` | POST | Start Pomodoro timer |
| `/api/pomodoro/stop` | POST | Stop Pomodoro timer |
| `/api/timer/start` | POST | Start countdown timer |
| `/api/timer/stop` | POST | Stop countdown timer |
| `/api/reminders` | GET | List all reminders |
| `/api/reminders` | POST | Add reminder (hour, minute, message, recurring) |
| `/api/reminders/delete` | POST | Delete reminder by index |
| `/api/breathing/start` | POST | Start breathing exercise |
| `/api/assistant/status` | GET | Voice assistant status |
| `/api/assistant/clear` | POST | Clear conversation history |
| `/api/assistant/settings` | GET/POST | LLM provider, API keys, voice config |
| `/api/mcp/servers` | GET/POST | Manage MCP client connections |
| `/api/mcp/discover` | POST | Discover MCP server tools |
| `/api/wifi/scan` | GET | Available networks |
| `/api/wifi/connect` | POST | Connect (ssid, password) |
| `/api/wifi/forget` | POST | Clear credentials |
| `/api/wifi/disable` | POST | Disable WiFi completely |
| `/api/time` | GET/POST | Device clock |
| `/api/system/info` | GET | Firmware version, memory stats |
| `/api/ota/upload` | POST | Upload firmware binary |
| `/api/ota/status` | GET | OTA progress |
| `/api/ota/cancel` | POST | Cancel OTA upload |
| `/api/system/restart` | POST | Restart device |
| `/api/system/rollback` | POST | Rollback to previous firmware |

### MCP Server

DeskBuddy exposes an MCP server on port 3001 using HTTP+SSE transport. Connect from Claude Desktop or any MCP client using `mcp-remote`:

```json
{
  "mcpServers": {
    "deskbuddy": {
      "command": "npx",
      "args": ["mcp-remote", "http://DEVICE_IP:3001/sse"]
    }
  }
}
```

**Available tools:** `set_expression`, `set_timer`, `cancel_timer`, `start_pomodoro`, `stop_pomodoro`, `get_device_info`, `play_sound`, `set_reminder`, `cancel_reminder`, `list_reminders`, `start_breathing`, `set_volume`, `set_brightness`, `set_eye_color`

---

## Development

### Adding Expressions

1. Add enum to `Expression` in `behavior/expressions.h`
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

The display is physically rotated 90 degrees CCW:
- Buffer X (0-336) maps to screen vertical
- Buffer Y (0-416) maps to screen horizontal
- Eyes positioned side-by-side via different buffer Y values

---

## Technical Details

- **Rendering**: 30fps software per-pixel evaluation, RGB565 framebuffer in PSRAM
- **Optimization**: Dirty-rect clearing, partial screen blit, shape-aware bounds
- **Processing**: Display on Core 1, audio decoding on Core 0, MCP server on dedicated task
- **Storage**: Settings persisted via Preferences (NVS), audio via LittleFS
- **Memory**: ~20% RAM, ~26% Flash (v2.0.0)

### Dependencies
- `lvgl/lvgl@^8.4.0` - Display driver
- `earlephilhower/ESP8266Audio@^1.9.7` - MP3 decoding
- `bblanchon/ArduinoJson@^7.0.0` - JSON parsing
- `links2004/WebSockets@^2.4.1` - WebSocket support
- GFX Library for Arduino (Waveshare)
- ES8311 codec driver

---

## License

This project is provided for educational and personal use.

## Acknowledgments

- Inspired by Anki's Cozmo robot
- Built for Waveshare ESP32-S3-Touch-AMOLED-1.8
