# Natatometer

Real-time swimming velocity feedback system providing haptic/audio signals based on instantaneous forward velocity to help swimmers detect intra-stroke velocity distribution deficiencies.

## Project Status

**Current Phase:** Pedometer (dryland) prototype with BNO055 IMU

## Hardware

- ESP32-WROVER CAM (Freenove Ultimate Starter Kit)
- BNO055 9-DOF IMU (on-chip sensor fusion)
- Passive buzzer (audio feedback from kit)

## Branch Structure

- `main` - Stable releases
- `pedometer-dev` - Dryland walking prototype (current)

## Algorithm Overview

1. BNO055 outputs gravity-removed linear acceleration (`VECTOR_LINEARACCEL`)
2. Integrate acceleration → velocity (dead reckoning)
3. High-pass filter (0.2-0.3 Hz) removes drift while preserving stride/stroke variations
4. Compare velocity to declared target → FAST/SLOW audio feedback

## Key Insights from SensorLogger Development

- Phone IMU drift: ~0.01 m/s² bias causes 0.6 m/s error after 60 seconds
- Motion-induced drift dominates static drift by 45:1 ratio
- HP filter achieves ~80-85% correlation with true velocity
- Detection delay: ~300-400ms for pace changes, ~30-50ms for phase variations

## Setup

1. Install Arduino IDE 2.x with ESP32 board support
2. Install Adafruit BNO055 library (+ dependencies)
3. Select board: ESP32 Wrover Module
4. Connect BNO055 via I2C (SDA, SCL, 3.3V, GND)

## License

MIT
