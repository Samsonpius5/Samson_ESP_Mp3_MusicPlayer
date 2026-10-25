# ESP32 MP3 Player — Hardware Guide

## 1. Project Description

This project uses an ESP32 microcontroller, an ILI9341 TFT display, and an XPT2046 touch controller to create an interactive music-player interface.

The current version supports touchscreen navigation and simulated playback. Real MP3 audio playback is still under development.

## 2. Hardware Components

- ESP32 Dev Module
- ILI9341 TFT display
- XPT2046 touchscreen controller
- USB cable for programming and power

**Planned integrations:**
- microSD card module for music storage
- I2S audio amplifier and speaker

## 3. ESP32 Pin Connections

| ESP32 Pin | Hardware Connection |
|---|---|
| GPIO 18 | TFT SCK and Touch T_CLK |
| GPIO 23 | TFT MOSI (SDI) and Touch T_DIN |
| GPIO 19 | TFT MISO (SDO) and Touch T_DO |
| GPIO 27 | TFT CS |
| GPIO 26 | TFT DC |
| GPIO 25 | TFT RESET |
| GPIO 33 | Touch CS |

The display and touch controller share the SPI clock and data lines but use separate chip-select pins.

**Power connections:** Verify and document the actual VCC, LED/backlight, and GND wiring. The correct supply voltage depends on the specific display module and its onboard circuitry.

## 4. Display Configuration

- Display orientation: Landscape
- Display resolution in landscape: 320 × 240 pixels
- Display rotation: 1
- Touch rotation: 1

## 5. Touchscreen Calibration

The current calibration constants are:

```cpp
#define RAW_LEFT 421
#define RAW_RIGHT 3425
#define RAW_TOP 630
#define RAW_BOTTOM 3526
```

Raw touch coordinates are converted into display coordinates before button detection.

## 6. Features Tested

- TFT display initialization
- Touchscreen detection
- Touch coordinate conversion
- Previous and Next track selection
- Play/Pause state switching
- Song information displayed on screen
- Serial Monitor debugging

## 7. Firmware Setup

The project is developed using Visual Studio Code with PlatformIO.

Configuration:

- Board: ESP32 Dev Module (`esp32dev`)
- Framework: Arduino
- Serial Monitor baud rate: 115200

To run the project:

1. Open the repository folder in VS Code.
2. Install PlatformIO if necessary.
3. Connect the ESP32 using USB.
4. Build the firmware using PlatformIO.
5. Upload the firmware to the ESP32.
6. Open Serial Monitor at 115200 baud to inspect debug output.

## 8. Current Limitations

The song names are currently simulated in software.

Actual MP3 files are not yet being read from a microSD card, and the music-player interface does not currently produce audio.

## 9. Next Development Steps

- Integrate microSD file reading
- Connect and test the I2S amplifier
- Implement MP3 decoding and audio output
- Add volume and playback progress controls
- Add photographs of the completed hardware
