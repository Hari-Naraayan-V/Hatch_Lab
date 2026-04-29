# SmartFlock

### IoT-Based Poultry Environmental Monitoring & Control System using ESP32 and Blynk

SmartFlock is an intelligent poultry shed environmental monitoring and automation system built around the **ESP32** microcontroller and **Blynk IoT Cloud**.

It continuously monitors environmental conditions such as:

* Ammonia concentration (NH₃)
* Temperature
* Humidity
* Device current consumption
* Power usage

and automatically controls ventilation, air circulation, and heating to maintain optimal poultry conditions.

The system is designed to reduce:

* Poultry mortality due to high ammonia concentration
* Heat stress
* Poor ventilation
* Cold stress
* Unnecessary energy consumption

---

# Project Overview

Poultry sheds produce ammonia from decomposing litter and waste. High ammonia concentration causes:

* Respiratory issues
* Eye irritation
* Reduced growth rate
* Increased mortality

SmartFlock uses sensors to detect environmental conditions and automatically reacts by controlling fans, motors, and heating systems.

IoT integration through **Blynk** enables:

* Remote monitoring
* Live sensor data visualization
* Actuator status monitoring
* Power analytics
* Energy savings tracking

---

# System Architecture

```text
MQ135 (NH3)
        \
         \
DHT22 ----> ESP32 ----> Relay Module ----> Fans / Heater / Motor
         /
INA219 ---

ESP32 ----> WiFi ----> Blynk Cloud ----> Mobile Dashboard
```

---

# Components Used

## Microcontroller

* ESP32 Dev Board

## Sensors

* DHT22 Temperature & Humidity Sensor
* MQ135 Gas Sensor (Ammonia Detection)

## Power Monitoring

* 3x INA219 Current Sensor Modules

Used for:

* Exhaust fan current
* Inlet fan current
* Motor current

## Actuators

* Exhaust fan (12V DC)
* Inlet fan (12V DC)
* DC motor (12V DC)
* Heater bulb (230V AC)

## Switching

* 4-channel Relay Module (Active LOW)

## Indicators

* LED indicator
* Buzzer

## IoT Platform

* Blynk Cloud Platform

## Communication

* WiFi (ESP32 built-in)

---

# Pin Configuration

| Component           | ESP32 GPIO |
| ------------------- | ---------: |
| DHT22 Data          |      GPIO4 |
| MQ135 Analog Output |     GPIO34 |
| Exhaust Relay       |     GPIO26 |
| Inlet Relay         |     GPIO27 |
| Motor Relay         |     GPIO25 |
| Heater Relay        |     GPIO33 |
| Heater LED          |     GPIO13 |
| Buzzer              |     GPIO32 |
| I2C SDA             |     GPIO21 |
| I2C SCL             |     GPIO22 |

---

# INA219 Address Configuration

| Device             | Address |
| ------------------ | ------- |
| Exhaust Fan INA219 | 0x40    |
| Inlet Fan INA219   | 0x41    |
| Motor INA219       | 0x44    |

---

# Working Logic

## Ammonia Control Logic

| NH3 Level | Action                          |
| --------- | ------------------------------- |
| 0–5 ppm   | All systems OFF                 |
| 5–10 ppm  | Exhaust ON (Pulse Mode)         |
| 10+ ppm   | Exhaust + Inlet ON (Pulse Mode) |
| 15+ ppm   | Motor ON (Continuous)           |

---

## Temperature Control Logic

| Temperature | Action                  |
| ----------- | ----------------------- |
| Below 25°C  | Heater ON               |
| 25°C – 30°C | No temperature action   |
| Above 30°C  | Exhaust ON (Pulse Mode) |

---

## Safety Interlock Logic

Heater will never turn ON if:

* Exhaust fan is active
* Inlet fan is active

This prevents heating and cooling conflicts.

---

# Pulse Control Logic

Ventilation uses pulse-based operation to save power.

Cycle:

```text
45 seconds ON
15 seconds OFF
Repeat
```

Used for:

* Exhaust fan
* Inlet fan

Benefits:

* Reduced power usage
* Reduced motor wear
* Better airflow cycling

---

# IoT Dashboard (Blynk)

SmartFlock uses **Blynk IoT** for remote monitoring.

## Dashboard Features

### Live Sensor Monitoring

* NH3 concentration
* Temperature
* Humidity

### Actuator Status

* Exhaust fan status
* Inlet fan status
* Motor status
* Heater status

### Power Monitoring

* Exhaust power
* Inlet power
* Motor power
* Total current

### Energy Monitoring

* Total energy consumption
* Energy saved
* Cost savings

### System Status

* Pulse cycle countdown
* Current active systems

---

# Blynk Virtual Pins

| Virtual Pin | Function           |
| ----------- | ------------------ |
| V0          | NH3 ppm            |
| V1          | Temperature        |
| V2          | Humidity           |
| V3          | Exhaust Fan Status |
| V4          | Inlet Fan Status   |
| V5          | Motor Status       |
| V6          | Heater Status      |
| V7          | Exhaust Power      |
| V8          | Inlet Power        |
| V9          | Motor Power        |
| V10         | Total Current      |
| V11         | Energy Saved       |
| V12         | Pulse Status       |
| V13         | System Status      |
| V14         | Total Energy       |

---

# Energy Calculation

Energy is calculated in kWh using:

```text
Energy = Power × Time
```

Savings are estimated using:

```text
Savings = (Baseline Energy - Actual Energy) × Tariff
```

Tariff used:

```text
₹8 per kWh
```

---

# Watchdog Protection

The system uses ESP32 watchdog timer protection to prevent system hangs.

Features:

* Auto reset on freeze
* Continuous watchdog feeding
* Reliable long-term operation

---

# Startup Sequence

1. Initialize GPIO
2. Initialize sensors
3. Connect WiFi
4. Connect Blynk Cloud
5. Warm up MQ135 (60 seconds)
6. Start monitoring
7. Start control logic
8. Start cloud sync

---

# Libraries Used

## ESP32 Core

Built-in

## Required Libraries

* Blynk Library
* DHT sensor library
* Adafruit INA219
* Adafruit BusIO

---

# Future Improvements

* Automatic litter moisture monitoring
* AI-based mortality prediction
* Fungal spore detection
* CO₂ monitoring
* Water quality monitoring
* SMS alerts
* Historical data logging
* OTA firmware updates

---

# Applications

* Poultry farms
* Broiler farms
* Layer farms
* Hatcheries
* Smart livestock systems

---

# Project Goals

* Improve poultry health
* Reduce mortality
* Improve air quality
* Reduce energy wastage
* Enable remote farm monitoring
* Automate environmental management

---

# Author

Developed as an embedded IoT automation project using:

* ESP32
* Blynk
* Sensor-based automation
* Power analytics
* Smart poultry environment control

---

## License

Open-source project for educational and research purposes.
