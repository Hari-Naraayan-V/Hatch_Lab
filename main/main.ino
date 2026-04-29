

/*
 * ============================================================
 *  SMARTFLOCK — Simplified Environmental Monitor
 *  ESP32 + Expansion Board (3.3V / 5V / 12V)
 *  Dashboard: Blynk IoT (blynk.cloud)
 * ============================================================
 *
 *  SENSORS
 *    DHT22  — GPIO4          — Temperature + Humidity
 *    MQ135  — GPIO34 (ADC1)  — Ammonia NH3 ppm
 *    INA219 x3 (I2C)
 *      0x40 — Exhaust Fan current
 *      0x41 — Inlet Fan current
 *      0x44 — DC Motor current
 *
 *  ACTUATORS  (Active LOW relay — HIGH=OFF, LOW=ON)
 *    GPIO26 — Relay 1 — Exhaust Fan  (12V DC)
 *    GPIO27 — Relay 2 — Inlet Fan    (12V DC)
 *    GPIO25 — Relay 3 — DC Motor     (12V DC)
 *    GPIO33 — Relay 4 — Heater Bulb  (230V AC)
 *    GPIO13 — LED    — Heater indicator
 *    GPIO32 — Buzzer
 *
 *  LOGIC
 *    NH3  0–5  ppm  : All OFF
 *    NH3  5–10 ppm  : Exhaust ON (pulse 45s ON / 15s OFF)
 *    NH3  10+  ppm  : Exhaust + Inlet ON (pulse)
 *    NH3  15+  ppm  : DC Motor ON (continuous)
 *    Temp < 25°C    : Heater ON
 *    Temp 25–30°C   : Nothing ON
 *    Temp > 30°C    : Exhaust ON (pulse)
 *    Humidity       : Displayed only
 *
 *  BLYNK VIRTUAL PINS
 *    V0  — NH3 (ppm)          — Gauge
 *    V1  — Temperature (°C)   — Gauge
 *    V2  — Humidity (%)       — Gauge
 *    V3  — Exhaust Fan        — LED
 *    V4  — Inlet Fan          — LED
 *    V5  — DC Motor           — LED
 *    V6  — Heater             — LED
 *    V7  — Exhaust Power (W)  — Label
 *    V8  — Inlet Power (W)    — Label
 *    V9  — DC Motor Power (W) — Label
 *    V10 — Total Current (A)  — Label
 *    V11 — Energy Saved (Rs)  — Label
 *    V12 — Pulse Status       — Label
 *    V13 — System Status      — Label
 *    V14 — Total Energy (kWh) — Label
 * ============================================================
 */

// ── Blynk — MUST be before all includes ───────────────────
#define BLYNK_TEMPLATE_ID   "TMPL3uFgwhcGI"
#define BLYNK_TEMPLATE_NAME "hatch"
#define BLYNK_AUTH_TOKEN    "E9PkVqi_39YzBWAaYCtM0fol_vxZMiNO"
#define BLYNK_PRINT         Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <esp_task_wdt.h>

// ── WiFi ──────────────────────────────────────────────────
const char* WIFI_SSID = "Kp07";
const char* WIFI_PASS = "kp432100";

// ── Pin Definitions ───────────────────────────────────────
#define PIN_DHT            4
#define PIN_MQ135         34
#define PIN_RELAY_EXHAUST 26
#define PIN_RELAY_INLET   27
#define PIN_RELAY_MOTOR   25
#define PIN_RELAY_HEATER  33
#define PIN_LED_HEATER    13
#define PIN_BUZZER        32

// ── Sensors ───────────────────────────────────────────────
DHT dht(PIN_DHT, DHT22);

Adafruit_INA219 ina_exhaust(0x40);
Adafruit_INA219 ina_inlet(0x41);
Adafruit_INA219 ina_motor(0x44);
bool ina_present[3] = {false, false, false};

// ── Constants ─────────────────────────────────────────────
const float        MQ135_R0       = 16.7f;    // Calibrated R0 kΩ
const float        MQ135_RL       = 10.0f;    // Load resistor kΩ
const float        MQ135_A        = 102.2f;
const float        MQ135_B        = -2.473f;
const float        TARIFF         = 8.0f;     // Rs per kWh
const float        BULB_WATTAGE   = 0.5f;     // Zero-watt bulb actual draw W
const float        FAN_RATED_W    = 15.0f;    // Baseline fan watts
const float        MOTOR_RATED_W  = 10.0f;    // Baseline motor watts
const unsigned long PULSE_ON_MS  = 45000UL;   // 45 seconds ON
const unsigned long PULSE_OFF_MS = 15000UL;   // 15 seconds OFF
const unsigned long SENSOR_MS    = 3000UL;    // Sensor read + dashboard interval

// ── NH3 Thresholds (ppm) ──────────────────────────────────
const float NH3_EXHAUST  =  5.0f;
const float NH3_INLET    = 10.0f;
const float NH3_MOTOR    = 15.0f;

// ── Temperature Thresholds (°C) ───────────────────────────
const float TEMP_HEAT    = 25.0f;   // Below → Heater ON
const float TEMP_COOL    = 30.0f;   // Above → Exhaust ON

// ── Sensor Values ─────────────────────────────────────────
float g_nh3  = 0.0f;
float g_temp = 25.0f;   // Safe default until first read
float g_hum  = 50.0f;

// ── Actuator States ───────────────────────────────────────
bool exhaust_on = false;
bool inlet_on   = false;
bool motor_on   = false;
bool heater_on  = false;

// ── Decision Engine Outputs ───────────────────────────────
bool need_exhaust = false;
bool need_inlet   = false;
bool need_motor   = false;
bool need_heater  = false;

// ── Pulse State ───────────────────────────────────────────
bool          pulse_active      = false;
bool          pulse_in_on       = false;
unsigned long pulse_phase_start = 0;

// ── INA219 Measurements ───────────────────────────────────
float cur_exhaust = 0.0f;  // Amps
float cur_inlet   = 0.0f;
float cur_motor   = 0.0f;
float pwr_exhaust = 0.0f;  // Watts
float pwr_inlet   = 0.0f;
float pwr_motor   = 0.0f;

// ── Energy Accumulators ───────────────────────────────────
float kwh_exhaust = 0.0f;
float kwh_inlet   = 0.0f;
float kwh_motor   = 0.0f;
float kwh_heater  = 0.0f;

unsigned long heater_on_start = 0;

// ── Blynk Timer ───────────────────────────────────────────
BlynkTimer timer;

// ── Warmup Complete Flag ──────────────────────────────────
bool mq135_ready = false;

// ═══════════════════════════════════════════════════════════
//  RELAY CONTROL — Active LOW
// ═══════════════════════════════════════════════════════════

void setExhaust(bool on) {
  if (exhaust_on == on) return;
  exhaust_on = on;
  digitalWrite(PIN_RELAY_EXHAUST, on ? LOW : HIGH);
}

void setInlet(bool on) {
  if (inlet_on == on) return;
  inlet_on = on;
  digitalWrite(PIN_RELAY_INLET, on ? LOW : HIGH);
}

void setMotor(bool on) {
  if (motor_on == on) return;
  motor_on = on;
  digitalWrite(PIN_RELAY_MOTOR, on ? LOW : HIGH);
}

void setHeater(bool on) {
  if (heater_on == on) return;
  heater_on = on;
  if (on) {
    digitalWrite(PIN_RELAY_HEATER, LOW);
    digitalWrite(PIN_LED_HEATER, HIGH);
    heater_on_start = millis();
  } else {
    digitalWrite(PIN_RELAY_HEATER, HIGH);
    digitalWrite(PIN_LED_HEATER, LOW);
    if (heater_on_start > 0) {
      float secs  = (millis() - heater_on_start) / 1000.0f;
      kwh_heater += (BULB_WATTAGE * secs) / 3600000.0f;
      heater_on_start = 0;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  MQ135 — NH3 PPM
// ═══════════════════════════════════════════════════════════

float readNH3() {
  int raw = analogRead(PIN_MQ135);
  if (raw < 100 || raw > 4000) return g_nh3;  // Keep last valid
  float voltage = (raw / 4095.0f) * 3.3f;
  if (voltage < 0.01f) return 0.0f;
  float rs    = ((3.3f - voltage) / voltage) * MQ135_RL;
  float ratio = rs / MQ135_R0;
  float ppm   = MQ135_A * powf(ratio, MQ135_B);
  ppm = constrain(ppm, 0.0f, 500.0f);
  return ppm;
}

// ═══════════════════════════════════════════════════════════
//  DECISION ENGINE — sets need_* flags
// ═══════════════════════════════════════════════════════════

void runDecisionEngine() {
  need_exhaust = false;
  need_inlet   = false;
  need_motor   = false;
  need_heater  = false;

  // NH3 rules
  if (g_nh3 > NH3_INLET)   { need_exhaust = true; need_inlet = true; }
  else if (g_nh3 > NH3_EXHAUST) { need_exhaust = true; }

  if (g_nh3 > NH3_MOTOR)   { need_motor = true; }

  // Temperature rules
  if (g_temp > TEMP_COOL) {
    need_exhaust = true;
    need_heater  = false;  // Never heat when cooling
  } else if (g_temp < TEMP_HEAT) {
    // Heater only if no fan is needed
    if (!need_exhaust && !need_inlet) {
      need_heater = true;
    }
  }

  // Hard interlock — heater OFF if any fan is running
  if (need_heater && (need_exhaust || need_inlet)) {
    need_heater = false;
  }
}

// ═══════════════════════════════════════════════════════════
//  FAN PULSE MANAGER  (45s ON / 15s OFF)
//  Called every loop() — NOT inside the 3s timer
// ═══════════════════════════════════════════════════════════

void runPulse() {
  // No fans needed — reset pulse and turn off
  if (!need_exhaust && !need_inlet) {
    setExhaust(false);
    setInlet(false);
    pulse_active      = false;
    pulse_in_on       = false;
    pulse_phase_start = 0;
    return;
  }

  unsigned long now     = millis();
  unsigned long elapsed = now - pulse_phase_start;

  // Start fresh pulse cycle
  if (!pulse_active) {
    pulse_active      = true;
    pulse_in_on       = true;
    pulse_phase_start = now;
    elapsed           = 0;
    if (need_exhaust) setExhaust(true);
    if (need_inlet)   setInlet(true);
    Serial.println("[PULSE] Start — ON 45s");
  }

  if (pulse_in_on) {
    // ON phase — update fan states in case need flags changed
    if (need_exhaust) setExhaust(true); else setExhaust(false);
    if (need_inlet)   setInlet(true);   else setInlet(false);

    if (elapsed >= PULSE_ON_MS) {
      // Switch to OFF phase
      setExhaust(false);
      setInlet(false);
      pulse_in_on       = false;
      pulse_phase_start = now;
      Serial.println("[PULSE] OFF 15s");
    }

  } else {
    // OFF phase — force fans OFF during rest period
    setExhaust(false);
    setInlet(false);

    if (elapsed >= PULSE_OFF_MS) {
      // Back to ON phase
      pulse_in_on       = true;
      pulse_phase_start = now;
      if (need_exhaust) setExhaust(true);
      if (need_inlet)   setInlet(true);
      Serial.println("[PULSE] ON 45s");
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  INA219 READINGS
// ═══════════════════════════════════════════════════════════

void readINA219() {
  if (ina_present[0]) {
    cur_exhaust = max(0.0f, ina_exhaust.getCurrent_mA() / 1000.0f);
    float v     = ina_exhaust.getBusVoltage_V();
    pwr_exhaust = (v > 0.0f) ? v * cur_exhaust : 0.0f;
    kwh_exhaust += pwr_exhaust * (SENSOR_MS / 3600000000.0f);
  }
  if (ina_present[1]) {
    cur_inlet = max(0.0f, ina_inlet.getCurrent_mA() / 1000.0f);
    float v   = ina_inlet.getBusVoltage_V();
    pwr_inlet = (v > 0.0f) ? v * cur_inlet : 0.0f;
    kwh_inlet += pwr_inlet * (SENSOR_MS / 3600000000.0f);
  }
  if (ina_present[2]) {
    cur_motor = max(0.0f, ina_motor.getCurrent_mA() / 1000.0f);
    float v   = ina_motor.getBusVoltage_V();
    pwr_motor = (v > 0.0f) ? v * cur_motor : 0.0f;
    kwh_motor += pwr_motor * (SENSOR_MS / 3600000000.0f);
  }
}

// ── Energy helpers ────────────────────────────────────────

float heaterKwhNow() {
  float kwh = kwh_heater;
  if (heater_on && heater_on_start > 0) {
    float secs = (millis() - heater_on_start) / 1000.0f;
    kwh += (BULB_WATTAGE * secs) / 3600000.0f;
  }
  return kwh;
}

float totalKwh() {
  return kwh_exhaust + kwh_inlet + kwh_motor + heaterKwhNow();
}

float baselineKwh() {
  float elapsedSec = millis() / 1000.0f;
  float baseW      = FAN_RATED_W + FAN_RATED_W + MOTOR_RATED_W + BULB_WATTAGE;
  return (baseW * elapsedSec) / 3600000.0f;
}

float savedRs() {
  float saved = baselineKwh() - totalKwh();
  return max(0.0f, saved) * TARIFF;
}

float totalCurrentA() {
  return cur_exhaust + cur_inlet + cur_motor;
}

// ═══════════════════════════════════════════════════════════
//  BLYNK DASHBOARD UPDATE
// ═══════════════════════════════════════════════════════════

void sendToBlynk() {
  if (!Blynk.connected()) return;  // Skip if not connected

  // ── Sensors ───────────────────────────────────────────
  Blynk.virtualWrite(V0, g_nh3);
  Blynk.virtualWrite(V1, g_temp);
  Blynk.virtualWrite(V2, g_hum);

  // ── Actuator LEDs — 255 = ON (green), 0 = OFF (grey) ─
  Blynk.virtualWrite(V3, exhaust_on ? 255 : 0);
  Blynk.virtualWrite(V4, inlet_on   ? 255 : 0);
  Blynk.virtualWrite(V5, motor_on   ? 255 : 0);
  Blynk.virtualWrite(V6, heater_on  ? 255 : 0);

  // ── Power labels ──────────────────────────────────────
  char buf[40];

  snprintf(buf, sizeof(buf), "%.2f W", pwr_exhaust);
  Blynk.virtualWrite(V7, buf);

  snprintf(buf, sizeof(buf), "%.2f W", pwr_inlet);
  Blynk.virtualWrite(V8, buf);

  snprintf(buf, sizeof(buf), "%.2f W", pwr_motor);
  Blynk.virtualWrite(V9, buf);

  // ── Total current ─────────────────────────────────────
  snprintf(buf, sizeof(buf), "%.3f A", totalCurrentA());
  Blynk.virtualWrite(V10, buf);

  // ── Energy saved ──────────────────────────────────────
  snprintf(buf, sizeof(buf), "Rs. %.2f", savedRs());
  Blynk.virtualWrite(V11, buf);

  // ── Pulse status — safe countdown (no unsigned underflow)
  if (!pulse_active) {
    Blynk.virtualWrite(V12, "FANS OFF");
  } else {
    unsigned long elapsed = millis() - pulse_phase_start;
    if (pulse_in_on) {
      unsigned long rem = (elapsed < PULSE_ON_MS)
                          ? (PULSE_ON_MS - elapsed) / 1000UL : 0;
      snprintf(buf, sizeof(buf), "ON — %lus left", rem);
    } else {
      unsigned long rem = (elapsed < PULSE_OFF_MS)
                          ? (PULSE_OFF_MS - elapsed) / 1000UL : 0;
      snprintf(buf, sizeof(buf), "OFF — ON in %lus", rem);
    }
    Blynk.virtualWrite(V12, buf);
  }

  // ── System status ─────────────────────────────────────
  String status;
  if (!need_exhaust && !need_inlet && !need_motor && !need_heater) {
    status = "ALL OK — Standby";
  } else {
    if (need_exhaust) status += "EXHAUST ";
    if (need_inlet)   status += "INLET ";
    if (need_motor)   status += "MOTOR ";
    if (need_heater)  status += "HEATER ";
    status.trim();
  }
  Blynk.virtualWrite(V13, status);

  // ── Total energy kWh ──────────────────────────────────
  snprintf(buf, sizeof(buf), "%.5f kWh", totalKwh());
  Blynk.virtualWrite(V14, buf);

  // ── Serial monitor ────────────────────────────────────
  Serial.printf(
    "NH3:%.1f ppm | T:%.1fC | H:%.1f%% | "
    "EX:%s IN:%s MT:%s HT:%s | "
    "%.2fA | Rs.%.2f saved\n",
    g_nh3, g_temp, g_hum,
    exhaust_on ? "ON" : "off",
    inlet_on   ? "ON" : "off",
    motor_on   ? "ON" : "off",
    heater_on  ? "ON" : "off",
    totalCurrentA(), savedRs());
}

// ═══════════════════════════════════════════════════════════
//  SENSOR READ + CONTROL LOOP  (every 3 seconds via timer)
// ═══════════════════════════════════════════════════════════

void sensorLoop() {
  esp_task_wdt_reset();

  // Read DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && t > -10.0f && t < 70.0f)  g_temp = t;
  if (!isnan(h) && h >= 0.0f  && h <= 100.0f) g_hum  = h;

  // Read MQ135
  if (mq135_ready) g_nh3 = readNH3();

  // Read INA219
  readINA219();

  // Decision engine
  runDecisionEngine();

  // Motor and heater are direct (no pulse)
  setMotor(need_motor);
  setHeater(need_heater);

  // Send to Blynk dashboard
  sendToBlynk();
}

// ═══════════════════════════════════════════════════════════
//  BLYNK CONNECTED CALLBACK
//  Fires every time connection is established / restored
// ═══════════════════════════════════════════════════════════

BLYNK_CONNECTED() {
  Serial.println("[BLYNK] Connected — pushing initial values");
  sendToBlynk();  // Immediately populate dashboard on connect
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SmartFlock Simple Boot ===");

  // ── GPIO ──────────────────────────────────────────────
  pinMode(PIN_RELAY_EXHAUST, OUTPUT);
  pinMode(PIN_RELAY_INLET,   OUTPUT);
  pinMode(PIN_RELAY_MOTOR,   OUTPUT);
  pinMode(PIN_RELAY_HEATER,  OUTPUT);
  pinMode(PIN_LED_HEATER,    OUTPUT);
  pinMode(PIN_BUZZER,        OUTPUT);

  // All relays OFF immediately (active LOW — HIGH = off)
  digitalWrite(PIN_RELAY_EXHAUST, HIGH);
  digitalWrite(PIN_RELAY_INLET,   HIGH);
  digitalWrite(PIN_RELAY_MOTOR,   HIGH);
  digitalWrite(PIN_RELAY_HEATER,  HIGH);
  digitalWrite(PIN_LED_HEATER,    LOW);
  digitalWrite(PIN_BUZZER,        LOW);
  Serial.println("[GPIO] All relays OFF");

  // ── ADC for MQ135 ─────────────────────────────────────
  analogSetPinAttenuation(PIN_MQ135, ADC_11db);
  analogReadResolution(12);
  Serial.println("[ADC] MQ135 — 12-bit, 11dB attenuation");

  // ── DHT22 ─────────────────────────────────────────────
  dht.begin();
  Serial.println("[DHT22] Initialized");

  // ── INA219 ────────────────────────────────────────────
  Wire.begin(21, 22);
  ina_present[0] = ina_exhaust.begin();
  ina_present[1] = ina_inlet.begin();
  ina_present[2] = ina_motor.begin();
  Serial.printf("[INA219] 0x40(Exhaust)=%s  0x41(Inlet)=%s  0x44(Motor)=%s\n",
    ina_present[0] ? "OK" : "MISSING",
    ina_present[1] ? "OK" : "MISSING",
    ina_present[2] ? "OK" : "MISSING");

  // ── Watchdog — compatible with core 2.x and 3.x ───────
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms     = 8000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic  = true
  };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(8, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.println("[WDT] Watchdog 8s enabled");

  // ── WiFi ──────────────────────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WIFI] Connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    esp_task_wdt_reset();
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected — IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WIFI] Failed — offline mode");
  }

  // ── Blynk ─────────────────────────────────────────────
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(3000);
  Serial.println("[BLYNK] Config done");

  // ── MQ135 warm-up (60 seconds) ────────────────────────
  Serial.println("[MQ135] Warming up 60 seconds...");
  digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);

  unsigned long warmStart = millis();
  while (millis() - warmStart < 60000UL) {
    esp_task_wdt_reset();
    Blynk.run();

    // Send live temp/humidity to dashboard during warm-up
    // so farmer sees something immediately
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) g_temp = t;
    if (!isnan(h)) g_hum  = h;

    if (Blynk.connected()) {
      Blynk.virtualWrite(V1, g_temp);
      Blynk.virtualWrite(V2, g_hum);
      unsigned long remSec = (60000UL - (millis() - warmStart)) / 1000UL;
      char buf[32];
      snprintf(buf, sizeof(buf), "Warming up — %lus left", remSec);
      Blynk.virtualWrite(V13, buf);
      Blynk.virtualWrite(V0, 0.0f);  // NH3 shows 0 during warmup
    }
    delay(1000);
  }

  mq135_ready = true;
  Serial.println("[MQ135] Ready — R0 = 16.7 kΩ (pre-calibrated)");
  digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);

  // ── Blynk timer — sensor + dashboard every 3 seconds ──
  timer.setInterval(SENSOR_MS, sensorLoop);

  Serial.println("=== SmartFlock Ready ===\n");
}

// ═══════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  esp_task_wdt_reset();
  Blynk.run();
  timer.run();

  // runPulse() here — called every loop iteration
  // NOT inside the 3s timer so 45s/15s timing is accurate
  runPulse();

  // WiFi reconnect check every 30 seconds
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 30000UL) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Reconnecting...");
      WiFi.reconnect();
    }
  }
}

/*
 * ============================================================
 *  BLYNK CONSOLE SETUP — DO THIS BEFORE UPLOADING
 * ============================================================
 *
 *  1. Go to blynk.cloud
 *  2. Your template is already set: TMPL3uFgwhcGI "hatch"
 *
 *  DATASTREAMS — Create each one exactly like this:
 *
 *  V0   Type: Double   Name: NH3 ppm         Min:0   Max:300
 *  V1   Type: Double   Name: Temperature C   Min:0   Max:50
 *  V2   Type: Double   Name: Humidity Pct    Min:0   Max:100
 *  V3   Type: Integer  Name: Exhaust Fan      Min:0   Max:255
 *  V4   Type: Integer  Name: Inlet Fan        Min:0   Max:255
 *  V5   Type: Integer  Name: DC Motor         Min:0   Max:255
 *  V6   Type: Integer  Name: Heater           Min:0   Max:255
 *  V7   Type: String   Name: Exhaust Power W
 *  V8   Type: String   Name: Inlet Power W
 *  V9   Type: String   Name: Motor Power W
 *  V10  Type: String   Name: Total Current A
 *  V11  Type: String   Name: Energy Saved Rs
 *  V12  Type: String   Name: Pulse Status
 *  V13  Type: String   Name: System Status
 *  V14  Type: String   Name: Total Energy kWh
 *
 *  DASHBOARD WIDGETS:
 *  Gauge  → V0  Title: Ammonia ppm    Color: Green<5, Yellow<10, Red>10
 *  Gauge  → V1  Title: Temperature C  Color: Blue<25, Green 25-30, Red>30
 *  Gauge  → V2  Title: Humidity Pct
 *  LED    → V3  Title: Exhaust Fan
 *  LED    → V4  Title: Inlet Fan
 *  LED    → V5  Title: DC Motor
 *  LED    → V6  Title: Heater
 *  Label  → V7  Title: Exhaust W
 *  Label  → V8  Title: Inlet W
 *  Label  → V9  Title: Motor W
 *  Label  → V10 Title: Total Current
 *  Label  → V11 Title: Saved Rs
 *  Label  → V12 Title: Fan Pulse
 *  Label  → V13 Title: Status
 *  Label  → V14 Title: Total kWh
 *
 * ============================================================
 *  TRIGGER LOGIC SUMMARY
 * ============================================================
 *
 *  NH3  0–5  ppm  → All OFF
 *  NH3  5–10 ppm  → Exhaust ON  (45s ON / 15s OFF pulse)
 *  NH3  10+  ppm  → Exhaust + Inlet ON  (same pulse)
 *  NH3  15+  ppm  → DC Motor ON (continuous — no pulse)
 *  Temp < 25°C    → Heater ON
 *  Temp 25–30°C   → Nothing ON
 *  Temp > 30°C    → Exhaust ON (pulse)
 *  Heater NEVER ON while any fan is needed
 *
 * ============================================================
 *  LIBRARIES — Install via Arduino IDE Library Manager
 * ============================================================
 *  Blynk             by Volodymyr Shymanskyy  >= 1.3.2
 *  DHT sensor library by Adafruit             >= 1.4.4
 *  Adafruit INA219   by Adafruit              >= 1.2.1
 *  Adafruit BusIO    by Adafruit (dependency) >= 1.14.0
 * ============================================================
 */