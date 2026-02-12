# DeskBuddy Firmware Changelog

All notable changes to DeskBuddy firmware are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
Versioning: [Semantic Versioning](https://semver.org/)

---

## [2.0.0] - 2026-02-10

### Architecture Rewrite

Complete modular decomposition of the firmware. `main.cpp` reduced from 3,216 to 1,464 lines (55% reduction). 155+ global variables replaced by 8 self-contained modules following `begin()`/`update()` lifecycle.

### Added — New Modules

- **ExpressionManager** (`src/behavior/expression_manager.h/.cpp`)
  - Token-based priority system with 8 levels (Base → Sleep)
  - `request(expr, priority)` returns token, `release(token)` auto-reverts
  - Replaces 8 `expressionBefore*` globals and ad-hoc state management
- **BlinkController** (`src/animation/blink_controller.h/.cpp`)
  - Self-contained blink state machine with configurable rate
  - Absorbs `isBlinking`, `blinkProgress`, `blinkCount`, `blinkSpeed` globals
- **MicroExpressionSystem** (`src/behavior/micro_expressions.h/.cpp`)
  - Personality idle behaviors: squint, wink, wide-eye, eyebrow raise, glance
  - Absorbs `microExprActive`, `winkProgress`, `triggerRandomMicroExpression()` (~200 lines)
- **ReactiveAnimations** (`src/behavior/reactive_animations.h/.cpp`)
  - Unified timed reactions: IMU events, irritated, love, joy, orientation, breathing
  - `TimedReaction` struct replaces ~12 independent timer globals
- **EyeAnimator** (`src/eyes/eye_animator.h/.cpp`)
  - Composites final eye shapes from expression base + gaze + blink + micro-expressions
  - Owns tweeners, gaze smoothing, touch tracking, breathing overrides
- **ScreenCompositor** (`src/display/screen_compositor.h/.cpp`)
  - PSRAM framebuffer management, dirty rect optimization, all blit operations
  - Perimeter progress bars (pomodoro + breathing), sleep bars, safe-center blit
  - Absorbs `eyeBuffer`, `EyePosition`, `prevLeftRect`/`prevRightRect`, render mode tracking
- **TimerVisuals** (`src/ui/timer_visuals.h/.cpp`)
  - Pomodoro/countdown expression side-effects and tick sound logic
  - Absorbs `lastPomodoroState`, `lastCountdownState`, state change handlers
- **NetworkSetup** (`src/network/network_setup.h/.cpp`)
  - WiFi lifecycle: enable/disable, NTP sync, captive portal, first-boot flow, timezone
  - Absorbs `wifiWasEnabled`, `wifiWasConnected`, `lastGmtOffsetHours` globals

### Added — Shared Utilities

- **TextRenderer** (`src/ui/text_renderer.h/.cpp`)
  - Shared font (5x7 bitmap, 44 glyphs) and drawing functions
  - `drawText`, `drawCenteredText`, `drawWrappedText`, `drawLargeDigit`, `clearBuffer`
  - 90° CCW rotation encapsulated — callers use logical coordinates
  - Eliminates 360 lines of duplicated rendering across 3 files
- **HttpHelpers** (`src/assistant/http_helpers.h`)
  - `createSecureClient()` factory used by LLM, STT, TTS, and MCP clients
- **Web UI HTML** (`src/network/web_ui_html.h`)
  - Extracted 2,600-line HTML literal from `web_server.cpp` into PROGMEM header
  - `web_server.cpp` reduced from 4,323 to ~1,720 lines

### Removed

- **11 dead files deleted** (~1,380 lines):
  - `src/animation/animator.h/.cpp` — unused EyeParams-based animator
  - `src/animation/blink_controller.h/.cpp` — old version (rewritten in B2)
  - `src/animation/look_controller.h/.cpp` — unused look controller
  - `src/behavior/emotion_engine.h/.cpp` — unused emotion engine
  - `src/behavior/emotion_types.h` — old Emotion enum (replaced by Expression)
  - `src/input/touch_handler.h/.cpp` — unused touch handler
  - `src/eyes/expressions.h` — old EyeParams + Emotion types

### Changed

- **main.cpp**: 3,216 → 1,464 lines — now pure orchestration
  - `setup()`: Hardware init → component `begin()` → wire callbacks
  - `loop()`: Frame timing → network → input → behavior → render
- **settings_menu.cpp**: Removed private drawing methods, uses `TextRenderer::`
- **reminder_manager.cpp**: Removed private drawing methods, uses `TextRenderer::`
- **breathing_exercise.cpp**: Removed private drawing methods, uses `TextRenderer::`
- **web_server.cpp**: HTML extracted to `web_ui_html.h`, uses `String(WEB_UI_HTML)`
- **llm_client.cpp, stt_client.cpp, tts_client.cpp, mcp_client.cpp**: Use `HttpHelpers::createSecureClient()`

### Technical

- Net line count: -1,707 lines (dead code removal + deduplication > new module code)
- All modules follow `begin(refs...)`/`update(dt, ...)` lifecycle pattern
- Expression priority levels: Base(0), MicroExpr(1), Timer(2), Behavior(3), Gesture(4), IMU(5), Assistant(6), Sleep(7)
- ScreenCompositor uses `Arduino_TFT*` (not `Arduino_GFX*`) for `writeAddrWindow`/`writePixels` access
- Dirty rect tracking: `computeEyeRect()` → `unionRect()` → `blitRegion()` pipeline
- PSRAM framebuffer: 336×416 RGB565 (~273KB) with heap fallback

---

## [1.3.0] - 2026-02-10

### Added
- **Tap-to-Listen**: Single tap starts listening, second tap stops and processes
  - Audio chime (listen.mp3) on activation
  - Expression changes: Listening → Thinking → Happy/emotion
  - Mic level brightness pulse during listening
- **STT Language Setting**: Configurable Whisper language hint (ISO 639-1)
  - Web UI: Assistant settings → STT Language field
  - Persisted to NVS, hot-reloadable
- **MCP Client Tool Integration**: External MCP server tools registered with LLM
  - `mcpClient.begin()` and `discoverTools()` called on startup
  - Unified tool executor: device tools first, falls back to MCP client
- **TTS Sample Rate Conversion**: Bresenham-based resampling in AudioOutputDuplex
  - Handles any decoded MP3 rate (24kHz, 22.05kHz, etc.) to 44.1kHz I2S output
- **WiFi Connection Feedback**: Display shows connection status during WiFi setup

### Fixed
- **Voice Capture Data Loss**: `getMicLevel()` was consuming I2S DMA data during listening, stealing ~50% of audio from capture pipeline. Now skipped during listening; level computed from captured data instead
- **Audio Downsampling 2x Bug**: Loop iterated total int16_t samples (512) instead of stereo frames (256), producing twice the expected output samples — audio reached Whisper at half speed
- **Stereo Channel Mixing**: ES8311 is a mono codec; right I2S slot contained zeros or DAC loopback. Averaging L+R halved the signal or mixed in speaker output. Now uses left channel only
- **Ring Buffer Overflow Freeze**: Unbounded I2S read loop with blocking ring buffer send caused infinite loop when buffer full. Now bounded to 8 reads with non-blocking send
- **TTS Chipmunk Voice**: OpenAI TTS MP3 (24kHz) played at I2S rate (44.1kHz) = 1.84x speed. Fixed with Bresenham sample rate conversion in AudioOutputDuplex
- **TTS Never Playing**: Async `processStream()` relied on `stream->connected()` which ESP32 SSL doesn't reliably report. Changed to blocking inline read for OpenAI provider
- **TTS Content Length Overflow**: `contentLength` was `size_t` but `http.getSize()` returns -1 for chunked transfer, becoming 4GB. Changed to `int`
- **Speaking→Idle Race**: TTS Complete fired before `audioPlayer.play()` started, causing immediate idle. Added 1s grace period
- **LLM 400 "Missing tool_result"**: When Claude returned multiple tool calls, results were sent one at a time violating API contract. Now executes all tools first, queues all results, sends in one request
- **LLM Message Merging**: Consecutive assistant tool_use and user tool_result messages now merged into single messages for both Claude and OpenAI API formats
- **Whisper Hallucination**: Corrupted audio (wrong sample count, stolen data, mixed channels) caused Whisper to hallucinate URLs and unrelated text instead of transcribing speech

### Changed
- **STT Buffer Drain**: `streamAudioToSTT()` now drains all available ring buffer data per frame (was reading only 1KB)
- **I2S Capture Loop**: `captureAudio()` reads up to 8 DMA batches per frame instead of just 1

### Technical
- `AudioOutputDuplex::SetRate()` override with Bresenham accumulator for rate conversion
- `VoiceInput::downsampleTo16kHz()` iterates `numFrames = srcSamples / 2` for stereo
- `VoiceInput::captureAudio()` computes `currentLevel` via `calculateRMS()` from captured data
- `LLMClient::queueToolResult()` for batching tool results before API call
- `Assistant::executeToolCalls()` executes all tools, queues results, sends with `addToolResult()`
- `buildClaudeRequest()` / `buildOpenAIRequest()` merge consecutive same-role messages
- `STTClient::setLanguage()` adds `language` field to Whisper multipart form
- `AssistantConfig::sttLanguage[8]` wired through web UI, NVS, and hot-reload

---

## [1.2.2] - 2026-02-09

### Added
- **Nadi Shodhana Breathing**: Alternate nostril breathing as a second exercise type
  - 6-phase cycle: inhale left → hold → exhale right → inhale right → hold → exhale left
  - 4 seconds per phase, 3 full cycles (72 seconds total)
  - Asymmetric eye animation: active nostril side opens wide, other eye winks shut
  - Phase text overlay on rendered eyes ("IN L" / "HOLD" / "OUT R" / "IN R" / "HOLD" / "OUT L")
- **Exercise Type Selector**: Choose between Box Breathing and Nadi Shodhana
  - On-device: Mindfulness menu → TYPE page (tap to toggle)
  - Web UI: Dropdown in Breathing Exercise section with dynamic description
  - MCP/LLM: `start_breathing` tool accepts optional `exercise` param ("box" or "nadi")
  - Persisted to NVS across reboots

### Fixed
- **Reminder False Triggers**: Reminders no longer fire at wrong times
  - Atomic `getTime()` prevents TOCTOU race condition at minute boundaries (separate hour/minute reads could mismatch)
  - Reminders only trigger when NTP time is confirmed synced (stale 12:00 fallback no longer causes false matches)
  - Auto-snooze capped at 3 consecutive auto-snoozes (~18 min), then auto-dismiss (prevents infinite snooze loop)
- **LLM System Prompt**: Now mentions both breathing exercises so the assistant knows about Nadi Shodhana

### Technical
- `BreathingType` enum with `BoxBreathing` and `NadiShodhana` variants
- `getTargetShapes(left, right)` for independent left/right eye control
- `renderPhaseTextOverlay()` draws text on top of rendered eyes (no buffer clear)
- `SettingsMenu::getTime(hour, minute)` returns bool for NTP validity
- `ReminderManager::update()` accepts `timeValid` parameter to gate time-based triggers
- `START_BREATHING_SCHEMA` with `R"json(...)json"` delimiter for enum array in raw string

---

## [1.2.1] - 2026-02-08

### Added
- **Timer Expiry Flash**: Pulsing 00:00 display after timer finishes (3 min timeout)
- **Happy Wedge Expression**: New eye geometry for celebration

### Changed
- **Neglect Chain Tuning**: Adjusted timing for idle behavior escalation

---

## [1.2.0] - 2026-02-06

### Added
- **Voice Assistant**: Full voice assistant with LLM tool use
  - Supports Claude (Sonnet 4) and OpenAI (GPT-4o), user-selectable
  - Speech-to-text via OpenAI Whisper (streaming 16kHz)
  - Text-to-speech via OpenAI TTS
  - Wake word detection: "Hey Buddy" via ESP-SR (local, no cloud)
  - Push-to-talk option (toggleable in web UI)
- **MCP Server**: Exposes 14 device tools via HTTP+SSE on port 3001
  - Compatible with Claude Desktop via mcp-remote
  - SSE keepalive (15s), Nagle disabled for reliability
- **MCP Client**: Connect to external MCP servers for tool discovery
  - Up to 8 servers, 16 tools per server
  - Configurable via web UI Assistant tab
- **Countdown Timer**: Standalone timer with on-screen progress display
  - Start/stop via web API, MCP, or voice assistant
  - Celebration animation on completion
- **Timed Reminders**: Up to 20 reminders with NVS persistence
  - Time-based triggers (hour:minute), one-shot or recurring daily
  - Full-screen message display + alert sound (joy.mp3)
  - Snooze (5 min) or dismiss via touch, auto-snooze after 60s
  - Manageable via web UI, MCP tools, or voice assistant
- **Device Settings via MCP/LLM**: `set_volume`, `set_brightness`, `set_eye_color` tools
- **Device Info Enhanced**: `get_device_info` now returns volume, brightness, eye_color
- **Web UI Assistant Tab**: LLM provider selection, API key fields with test buttons, voice settings, MCP server management
- **Web UI Restructured Tabs**: Dashboard, Productivity, Mindfulness, Assistant, Settings, Expressions

### Changed
- **Web UI Layout**: Settings tab now uses accordion sections (Display, Audio, Time, WiFi, System)
- **Productivity Tab**: Contains Reminders, Countdown Timer, and Pomodoro Timer
- **Mindfulness Tab**: Separated from Productivity with Box Breathing settings
- **Assistant Settings Input Layout**: Fixed oversized buttons/inputs with `.input-group` CSS
- **Wake Word**: Default changed from "Hi ESP" to "Hey Buddy" (WAKE_WORD_CUSTOM)
- **Breathing via MCP**: `start_breathing` tool now starts exercise immediately (skips confirmation prompt)

### Fixed
- **MCP SSE Connection Dropping**: Added flush() calls after SSE writes, disabled Nagle's algorithm, reduced keepalive from 25s to 15s
- **Raw String Literal Bug**: Schemas with `(0-100)` in descriptions used `R"json(...)json"` delimiter to avoid premature `)"` termination
- **Countdown Timer Artifacts**: Fixed visual artifacts on timer screen
- **Delete Button Styling**: Fixed delete button display issues in web UI

### Technical
- New files: `assistant.h/cpp`, `llm_client.h/cpp`, `stt_client.h/cpp`, `tts_client.h/cpp`, `mcp_server.h/cpp`, `mcp_client.h/cpp`, `voice_input.h/cpp`, `wake_word.h/cpp`, `device_tools.h/cpp`, `countdown_timer.h/cpp`, `reminder_manager.h/cpp`
- MCP server runs on dedicated FreeRTOS task
- DeviceToolCallbacks pattern for tool execution wiring
- Audio: joy.mp3 used for reminder alerts

---

## [1.1.0] - 2026-02-05

### Added
- **Breathing Exercise**: Box breathing pattern (5-5-5-5) with visual feedback
  - Progress bar fills/empties in sync with breath phases
  - Phase text overlay (IN/HOLD/OUT) with fade animation
  - 3 cycles per session (60 seconds total)
  - Post-exercise calm: Content (3s) → Relaxed (60s)
- **Mindfulness Menu**: On-device settings for breathing exercises
  - Breathe Now: Start exercise immediately
  - Schedule: Enable/disable automated reminders
  - Interval: Hours between reminders (1-8)
  - Sound: Toggle reminder sound
- **Scheduled Reminders**: Periodic breathing prompts during active hours
- **New Expressions**: BreathingPrompt, Relaxed (32 total expressions)
- **Web UI Productivity Tab**: Combined Pomodoro and Breathing settings

### Changed
- **First-Boot WiFi Flow**: Now two-phase approach
  - Phase 1: Shows WiFi info (SSID, password, IP) clearly
  - Phase 2: After AP client connects, shows Configure/Offline choice
  - Prevents UI overlay issues on small screen
- **Web UI**: Restructured with accordion sections in Productivity tab

### Fixed
- **Eye Corner Rendering**: Fixed inner/outer corner offset swap for left eye
  - Corners now render correctly with 90° screen rotation
  - Asymmetric expressions (Suspicious, Wink) display properly

### Technical
- New files: `breathing_exercise.h/cpp`
- New audio: `breathe_reminder.mp3`
- State variables: `isShowingWiFiChoice`, `lastAPClientCount`
- Breathing blocks idle behaviors during relaxed phase

---

## [1.0.0] - 2026-02-05

### Added
- **OTA Updates**: Web-based firmware updates via System tab
  - Drag-and-drop firmware upload with progress bar
  - Automatic rollback on boot failure
  - Rollback button to revert to previous firmware
  - System info display (version, build date, partition, heap)
- **NTP Time Sync**: Automatic time synchronization when WiFi connected
  - Uses pool.ntp.org and time.google.com
  - Fallback to internal clock when offline
- **Timezone Setting**: Configurable GMT offset (-12 to +14 hours)
  - New settings page on device
  - Web UI control in Time tab
- **Audio Test Button**: Test speaker output from web UI Audio tab
- **Current Mood Display**: Shows current expression on Dashboard and Expressions pages

### New API Endpoints
- `GET /api/system/info` - Firmware version, build date, memory stats
- `POST /api/ota/upload` - Upload firmware binary
- `GET /api/ota/status` - OTA progress status
- `POST /api/ota/cancel` - Cancel OTA upload
- `POST /api/system/restart` - Restart device
- `POST /api/system/rollback` - Rollback to previous firmware
- `POST /api/audio/test` - Play test sound

### Changed
- Settings menu now has 10 pages (added Timezone, WiFi toggle)
- Web UI has 8 tabs (added System tab)
- Time display uses NTP when available, falls back to internal clock

### Technical
- New files: `ota_manager.h/cpp`, `version.h`
- Partition scheme supports dual-boot OTA (APP0/APP1)
- Stack size increased for OTA upload handler

---

## [0.9.0] - 2026-02-04

### Added
- Initial release with core features
- 30 expression presets with smooth transitions
- WiFi setup with captive portal
- Web UI for remote control
- Pomodoro timer
- IMU-based interactions (tilt, shake, pick up)
- Audio feedback with MP3 playback
- Sleep behavior with breathing animation
- Time-based mood shifts
