# Natatometer Wiring Guide

## ESP32-WROVER CAM + BNO055 Connections

### BNO055 to ESP32

| BNO055 Pin | ESP32 Pin | Notes |
|------------|-----------|-------|
| VIN        | 3.3V      | **Use 3.3V, not 5V** |
| GND        | GND       | Common ground |
| SDA        | GPIO 21   | I2C Data |
| SCL        | GPIO 22   | I2C Clock |

### Buzzer to ESP32

| Component | ESP32 Pin | Notes |
|-----------|-----------|-------|
| Buzzer +  | GPIO 25   | PWM capable pin |
| Buzzer -  | GND       | Common ground |

### I2C Troubleshooting (BNO055)

The BNO055 has known I2C quirks with ESP32. If you have issues:

1. **Add pull-up resistors**: 10kΩ on both SDA and SCL to 3.3V
2. **Slow I2C clock**: Add to setup() before bno.begin():
   ```cpp
   Wire.begin(21, 22);
   Wire.setClock(100000);  // 100kHz instead of 400kHz
   ```
3. **Check solder jumpers**: Some clone boards need jumpers soldered for I2C mode

### Pin Diagram

```
ESP32-WROVER CAM (Freenove breakout)
                    ┌─────────────────┐
                    │     USB         │
                    │    ┌─────┐      │
              3.3V ─┤    │     │      ├─ GND
              GPIO2─┤    │ESP32│      ├─ GPIO4
                    │    │     │      │
             GPIO21─┤    │     │      ├─ GPIO22
              (SDA) │    └─────┘      │  (SCL)
                    │                 │
             GPIO25─┤                 ├─
            (BUZZ)  │                 │
                    └─────────────────┘
```

### BNO055 Module Pinout

```
┌─────────────────────┐
│  BNO055 Breakout    │
│                     │
│  VIN  GND  SDA  SCL │
│   │    │    │    │  │
└───┴────┴────┴────┴──┘
    │    │    │    │
    │    │    │    └──── ESP32 GPIO22
    │    │    └───────── ESP32 GPIO21
    │    └────────────── ESP32 GND
    └─────────────────── ESP32 3.3V
```
