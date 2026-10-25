# ESP32 Touchscreen MP3 Player

## Project Overview
This project is an ESP32-based touchscreen music player developed using C++ and the Arduino framework in PlatformIO.

The goal is to build a music player that can display song information, respond to touchscreen input, and eventually play MP3 files stored on a microSD card.

## Hardware Components
- ESP32 development board
- ILI9341 TFT display
- XPT2046 touch controller
- microSD card module (planned integration)
- I2S audio amplifier and speaker (planned integration)

## Current Features
- Working touchscreen interface
- Play/Pause button with simulated playback state
- Next and Previous song navigation
- Song selection using an array
- Serial Monitor debugging
- Touchscreen coordinate calibration

## Current Project Status
The touchscreen interface and navigation controls are working.

Audio playback and microSD card integration have not yet been implemented.

## Development Environment
- Visual Studio Code
- PlatformIO
- Arduino framework
- C++

## Future Improvements
- Read MP3 files from a microSD card
- Implement audio playback using I2S
- Add volume controls
- Display song progress
- Improve the user interface

## Learning Objectives
This project is helping me develop practical skills in embedded C++, microcontroller programming, SPI communication, touchscreen interfaces, debugging, and firmware development.

## Hardware Documentation
See the [ESP32 Hardware Guide](docs/hardware-guide.md)
for the wiring connections, touchscreen calibration,
hardware photograph, and firmware setup instructions.