# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a multiplayer "Squid Game" controller system with two main components:

### 1. Web Console Game Interface (`console/`)
- **HTML5 game console** with animated card-based game selection
- **Real-time WebSocket communication** for multiplayer coordination
- **Visual game states** with red/green light indicators and player progress tracking
- **Multiple game modes**: Red Light Green Light, Dalgona, Rock Paper Scissors Minus One

### 2. Arduino-based Physical Controllers (`controls/firmware_controls/`)
- **ESP32-based game controllers** with accelerometer shake detection
- **TFT display** showing game state (red/green light, player progress)
- **NeoPixel LED progress bar** and haptic feedback
- **JSON command processing** via serial communication

## Architecture

### Web Console (`console/index.html`)
**Frontend Game Interface**
- **Game Selection**: Interactive card flip animations for game modes
- **WebSocket Client**: Connects to `ws://localhost:8765` for real-time communication
- **Player Progress Tracking**: Visual sliders with 4-player support
- **Game State Management**: Countdown timers, red/green light cycles, win/lose conditions
- **Animation System**: CSS animations for game transitions and player movement

### WebSocket Bridge (`console/python websocket_bridge.py`)
**Communication Hub**
- **Serial Port Management**: Auto-discovers and connects to ESP32 controllers
- **WebSocket Server**: Handles web console connections on port 8765
- **Message Routing**: Bidirectional JSON message translation between web and hardware
- **Player Assignment**: Automatically assigns player numbers (1-4) to connected controllers

### Hardware Controllers (`controls/firmware_controls/firmware_controls.ino`)
**Physical Game Controller**
- **Hardware Components**:
  - ADXL345 accelerometer for shake detection
  - 240x240 TFT display (TFT_eSPI library)
  - 24-LED NeoPixel strip for progress indication
  - Vibration motor for haptic feedback
- **RTOS Architecture**: 3 concurrent tasks
  - `serialTask`: JSON command parsing and game state updates
  - `animationTask`: LED animations and progress bar management  
  - `tftTask`: TFT screen updates (red/green background, player progress circle)
- **Game State Integration**: Responds to JSON commands for light state, player progress, win/lose animations

## Current System Workflow

1. **Web Console** displays game selection cards
2. **Player selects game** → triggers WebSocket connection to bridge
3. **Python Bridge** discovers connected ESP32 controllers via serial ports
4. **Game starts** → web sends JSON commands to all controllers
5. **Controllers respond** with shake scores via serial → bridge forwards to web
6. **Web updates** player progress and game state in real-time
7. **Win/lose conditions** trigger appropriate animations on both web and hardware

## Development Commands

### Web Console
```bash
# Serve the HTML file locally
python -m http.server 8000
# Open browser to http://localhost:8000/console/
```

### WebSocket Bridge
```bash
cd console/
python "python websocket_bridge.py"
```

### Arduino Firmware
- Use Arduino IDE with ESP32 board package
- Install required libraries: `TFT_eSPI`, `ADXL345`, `Adafruit_NeoPixel`, `ArduinoJson`
- Serial communication at 115200 baud

## Hardware Configuration

### ESP32 Controller Pins
```cpp
#define LED_PIN 43        // NeoPixel data pin
#define LED_COUNT 24      // Number of LEDs
#define VIBRATION_PIN 1   // Haptic motor control
// TFT: Uses TFT_eSPI default pins
// I2C: SDA/SCL for ADXL345 accelerometer
```

### Communication Protocol

**JSON Message Format (Web → Hardware):**
```json
{
  "light": "green|red",
  "player_id": 1,
  "progress": 0-100,
  "status": "playing|winner|eliminated|game_over|game_started|game_stopped",
  "player_color": [r, g, b]
}
```

**Serial Message Format (Hardware → Web):**
```json
{"player": 1, "data": "shake_score"}
```

## Current Functionality

### Web Console Features
- **Multi-game support** with animated card selection
- **Real-time multiplayer** progress tracking (up to 4 players)
- **Red Light/Green Light** game mechanics with grace periods
- **Automatic win/lose detection** and game end handling
- **WebSocket reconnection** and error handling

### Hardware Controller Features  
- **Accelerometer-based shake detection** with discrete scoring levels (0-6)
- **Dynamic TFT display** showing game state and player progress
- **LED progress bar** with game state colors
- **Haptic feedback** for win/lose events
- **JSON command processing** for real-time game updates

The system creates an engaging physical gaming experience where players use motion controllers while viewing their progress on both local displays and a shared web console.