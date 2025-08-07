# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an Arduino-based game controller project that simulates functionality for "Squid Game" scenarios. It's designed to run on a Seeed XIAO round screen microcontroller with various hardware components:

- **Main controller**: Seeed XIAO with round display (240x240 TFT)
- **Accelerometer**: ADXL345 for motion/shake detection
- **LEDs**: Addressable NeoPixel strip (60 LEDs)
- **Hardware**: Vibration motor, two buttons (L/R), SD card slot
- **Display**: LVGL-based GUI with touch support

## Architecture

### Core Components

**Main Application** (`Defcon_squid_Game_Controller.ino:26-95`)
- Hardware initialization and pin setup
- LVGL display system initialization 
- Accelerometer configuration (mostly commented out advanced features)
- Main hardware test UI launch

**Hardware Test Interface** (`lv_hardware_test.h`)
- LVGL-based GUI with real-time hardware monitoring
- Battery level monitoring with visual indicator
- SD card detection and status display
- Real-time clock display
- Interactive slider component

**Motion Detection System** (`Defcon_squid_Game_Controller.ino:98-124`)
- Shake scoring algorithm based on accelerometer magnitude
- Real-time acceleration data processing
- Score calculation using absolute values from all three axes

### Key Hardware Pins
```
#define VIBRATION 1    // Vibration motor control
#define RGB 43         // NeoPixel LED strip data
#define BUTTON_L 41    // Left button input
#define BUTTON_R 42    // Right button input
```

## Development Commands

This is an Arduino project, so development uses the Arduino IDE or compatible tools:

### Compilation and Upload
- Use Arduino IDE with Seeed XIAO board package
- Select appropriate board: Seeed XIAO (specific variant depends on hardware)
- Compile and upload via USB connection
- Serial monitor available at 9600 baud rate

### Required Libraries
Install these through Arduino Library Manager:
- `lvgl` - Graphics library for display
- `ADXL345` - Accelerometer communication
- `Adafruit_NeoPixel` - LED strip control
- `I2C_BM8563` - Real-time clock module
- `TFT_eSPI` or `Arduino_GFX` - Display driver (TFT_eSPI is currently selected)

## Hardware-Specific Notes

### Display Configuration
- The project supports both TFT_eSPI and Arduino_GFX libraries
- Currently configured for TFT_eSPI (`#define USE_TFT_ESPI_LIBRARY`)
- Display requires `lv_xiao_round_screen.h` header (external dependency)

### Accelerometer Features
- Basic motion detection is active
- Advanced features (tap detection, freefall, activity thresholds) are commented out
- Interrupt-based detection is disabled in current configuration

### Power Management
- Battery level monitoring supports multiple XIAO variants (ESP32S3, NRF52840, RP2040)
- Voltage reference and ADC configuration varies by board type

## Current Functionality

The controller currently implements:
1. **Real-time shake detection** with scoring algorithm
2. **Hardware status monitoring** (battery, SD card, clock)
3. **Interactive GUI** with touch controls
4. **Serial output** of shake scores for monitoring/debugging

The shake detection algorithm calculates scores from 0-100 based on combined acceleration magnitude across all axes, with real-time serial output for monitoring.