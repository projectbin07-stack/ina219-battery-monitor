// ina219-battery-monitor firmware (Arduino framework: ESP32, Arduino Nano).
//
// Samples an INA219 at a configurable rate, integrates charge and energy,
// estimates state of charge, drives an optional SSD1306 OLED and prints one
// CRC-protected telemetry frame per sample on the serial port.
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "two_wire_bus.h"

#include <ina219/driver.h>
#include <powercore/command.h>
#include <powercore/coulomb_counter.h>
#include <powercore/cutoff.h>
#include <powercore/filters.h>
#include <powercore/ina219_math.h>
#include <powercore/soc.h>
#include <powercore/telemetry.h>

#if BM_ENABLE_OLED
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

namespace {

using powercore::CommandType;

const powercore::CutoffConfig kCutoffConfig = {BM_CUTOFF_MV, BM_CUTOFF_RECOVER_MV,
                                               BM_CUTOFF_SAMPLES, BM_CUTOFF_SAMPLES};

TwoWireBus g_bus(Wire);
ina219drv::Ina219 g_ina(g_bus, BM_INA219_ADDRESS);
powercore::CoulombCounter g_counter;
powercore::SocEstimator g_soc(powercore::liIon1SConfig(BM_BATTERY_CAPACITY_MAH));
powercore::LowVoltageCutoff g_cutoff(kCutoffConfig);

uint32_t g_sample_period_ms = BM_SAMPLE_PERIOD_MS;
uint32_t g_shunt_uohm = BM_SHUNT_UOHM;
uint32_t g_max_mA = BM_MAX_CURRENT_MA;
uint32_t g_seq = 0;
uint32_t g_last_sample_ms = 0;
uint8_t g_samples_since_check = 0;
bool g_sensor_ok = false;

// Most recent sample, kept for the display.
int32_t g_bus_mV = 0;
int32_t g_current_uA = 0;
int32_t g_power_uW = 0;

char g_line[BM_COMMAND_BUFFER];
uint8_t g_line_length = 0;
bool g_line_overflow = false;

#if BM_ENABLE_OLED
Adafruit_SSD1306 g_display(BM_OLED_WIDTH, BM_OLED_HEIGHT, &Wire, -1);
powercore::Ema g_display_mV(BM_DISPLAY_EMA_ALPHA);
powercore::Ema g_display_mA(BM_DISPLAY_EMA_ALPHA);
uint32_t g_last_draw_ms = 0;
bool g_oled_ok = false;
#endif

// The counter must tolerate the configured sample period; allow a few missed
// samples before an interval is treated as a gap.
void applyMaxGap() {
  const uint32_t gap = g_sample_period_ms * 4UL;
  g_counter.setMaxGap(gap < 2000UL ? 2000UL : gap);
}

bool initSensor() {
  ina219drv::Settings settings;
  settings.shunt_uohm = g_shunt_uohm;
  settings.max_expected_mA = g_max_mA;
  settings.bus_range =
      BM_BUS_RANGE_32V ? powercore::ina219::BusRange::V32 : powercore::ina219::BusRange::V16;
  settings.bus_adc = BM_ADC_MODE;
  settings.shunt_adc = BM_ADC_MODE;
  g_sensor_ok = (g_ina.begin(settings) == ina219drv::Status::Ok);
  g_samples_since_check = 0;
  return g_sensor_ok;
}

void resetAccounting() {
  g_counter.reset();
  g_soc.reset();
  g_cutoff.reset();
}

void handleCommand(const char* line) {
  const powercore::Command command = powercore::parseCommand(line);
  switch (command.type) {
    case CommandType::None:
      break;
    case CommandType::Reset:
      resetAccounting();
      Serial.println(F("#OK RESET"));
      break;
    case CommandType::Rate:
      g_sample_period_ms = command.rate_ms;
      applyMaxGap();
      Serial.print(F("#OK RATE "));
      Serial.println(g_sample_period_ms);
      break;
    case CommandType::Cal: {
      const powercore::ina219::Calibration cal =
          powercore::ina219::computeCalibration(command.shunt_uohm, command.max_mA);
      if (cal.status != powercore::ina219::CalStatus::Ok) {
        Serial.print(F("#ERR CAL status="));
        Serial.println(static_cast<uint8_t>(cal.status));
        break;
      }
      g_shunt_uohm = command.shunt_uohm;
      g_max_mA = command.max_mA;
      if (!initSensor()) {
        Serial.println(F("#ERR CAL sensor not responding"));
        break;
      }
      Serial.print(F("#OK CAL reg="));
      Serial.print(cal.cal_register);
      Serial.print(F(" lsb_uA="));
      Serial.print(cal.current_lsb_uA, 3);
      Serial.print(F(" max_mA="));
      Serial.println(cal.max_current_mA, 1);
      break;
    }
    case CommandType::Invalid:
    default:
      Serial.println(F("#ERR expected RESET | RATE <ms> | CAL <shunt_mohm> <max_A>"));
      break;
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (g_line_overflow) {
        Serial.println(F("#ERR line too long"));
      } else if (g_line_length > 0) {
        g_line[g_line_length] = '\0';
        handleCommand(g_line);
      }
      g_line_length = 0;
      g_line_overflow = false;
    } else if (g_line_length < sizeof(g_line) - 1) {
      g_line[g_line_length++] = c;
    } else {
      g_line_overflow = true;
    }
  }
}

void sampleOnce(uint32_t now_ms) {
  uint8_t flags = 0;
  ina219drv::Reading reading;
  bool valid = false;

  // Retry a sensor that failed earlier (unplugged, brown-out, ...).
  if (g_sensor_ok || initSensor()) {
    if (g_ina.read(reading) == ina219drv::Status::Ok) {
      valid = true;
    } else {
      g_sensor_ok = false;
    }
  }

  if (!valid) {
    flags |= powercore::kFlagSensorError;
    g_bus_mV = 0;
    g_current_uA = 0;
    g_power_uW = 0;
  } else {
    const int32_t current = BM_INVERT_CURRENT ? -reading.current_uA : reading.current_uA;
    // The INA219 power register is unsigned; give it the sign of the current.
    const int32_t power = current < 0 ? -reading.power_uW : reading.power_uW;
    g_bus_mV = reading.bus_mV;
    g_current_uA = current;
    g_power_uW = power;

    if (reading.overflow) flags |= powercore::kFlagOverflow;
    if (!reading.ready) flags |= powercore::kFlagNotReady;

    // Current and power are meaningless while OVF is set. The span around an
    // OVF sample is treated as a gap (not integrated, counted, integrator
    // re-seeded by the next good sample) rather than bridged with the last
    // good current, and it also restarts the SoC rest timer. The voltage is
    // still good.
    if (reading.overflow) {
      g_counter.markGap();
      g_soc.breakRest();
    } else {
      g_counter.update(now_ms, current, power);
      g_soc.update(static_cast<float>(reading.bus_mV), static_cast<float>(current) / 1000.0f,
                   g_counter.lastDelta_mAh(), g_counter.lastInterval_ms());
    }
    g_cutoff.update(reading.bus_mV);

    // The calibration register is volatile; restore it if the device lost it.
    if (++g_samples_since_check >= 16) {
      g_samples_since_check = 0;
      bool intact = true;
      if (g_ina.calibrationIntact(intact) != ina219drv::Status::Ok) {
        g_sensor_ok = false;
      } else if (!intact) {
        Serial.println(F("#WARN calibration lost, reprogramming"));
        initSensor();
      }
    }
  }

  if (!g_cutoff.loadEnabled()) flags |= powercore::kFlagCutoff;
#if BM_LOAD_ENABLE_PIN >= 0
  digitalWrite(BM_LOAD_ENABLE_PIN, g_cutoff.loadEnabled() ? HIGH : LOW);
#endif

#if BM_ENABLE_OLED
  if (valid) {
    g_display_mV.add(static_cast<float>(g_bus_mV));
    g_display_mA.add(static_cast<float>(g_current_uA) / 1000.0f);
  }
#endif

  powercore::TelemetryFrame frame;
  frame.seq = g_seq++;
  frame.t_ms = now_ms;
  frame.bus_mV = g_bus_mV;
  frame.current_uA = g_current_uA;
  frame.power_uW = g_power_uW;
  frame.charge_uAh = g_counter.charge_uAh();
  frame.energy_uWh = g_counter.energy_uWh();
  frame.soc_permille = static_cast<uint16_t>(g_soc.percent() * 10.0f + 0.5f);
  frame.flags = flags;

  char buffer[powercore::kFrameBufferSize];
  if (powercore::encodeFrame(frame, buffer, sizeof(buffer)) > 0) {
    Serial.println(buffer);
  }
}

#if BM_ENABLE_OLED
void drawDisplay() {
  if (!g_oled_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);

  if (!g_sensor_ok) {
    g_display.setTextSize(1);
    g_display.setCursor(0, 0);
    g_display.print(F("INA219 not found"));
    g_display.display();
    return;
  }

  const float soc = g_soc.percent();

  g_display.setTextSize(2);
  g_display.setCursor(0, 0);
  g_display.print(g_display_mV.value() / 1000.0f, 2);
  g_display.print('V');

  g_display.setTextSize(1);
  g_display.setCursor(98, 0);
  g_display.print(soc, 0);
  g_display.print('%');
  if (!g_cutoff.loadEnabled()) {
    g_display.setCursor(86, 9);
    g_display.print(F("LOW BAT"));
  }

  g_display.setCursor(0, 20);
  g_display.print(F("I "));
  g_display.print(g_display_mA.value(), 1);
  g_display.print(F(" mA"));

  g_display.setCursor(0, 31);
  g_display.print(F("P "));
  g_display.print(static_cast<float>(g_power_uW) / 1000.0f, 1);
  g_display.print(F(" mW"));

  g_display.setCursor(0, 42);
  g_display.print(F("Q "));
  g_display.print(g_counter.charge_mAh(), 2);
  g_display.print(F(" mAh"));

  // State-of-charge bar along the bottom edge.
  g_display.drawRect(0, 54, 128, 10, SSD1306_WHITE);
  const int16_t fill = static_cast<int16_t>(124.0f * soc / 100.0f);
  if (fill > 0) g_display.fillRect(2, 56, fill, 6, SSD1306_WHITE);

  g_display.display();
}
#endif

}  // namespace

void setup() {
  Serial.begin(BM_SERIAL_BAUD);

#if BM_LOAD_ENABLE_PIN >= 0
  pinMode(BM_LOAD_ENABLE_PIN, OUTPUT);
  digitalWrite(BM_LOAD_ENABLE_PIN, HIGH);
#endif

#if defined(ARDUINO_ARCH_ESP32)
  Wire.begin(BM_I2C_SDA_PIN, BM_I2C_SCL_PIN);
#else
  Wire.begin();
#endif

  Serial.println(F("#ina219-battery-monitor " BM_FIRMWARE_VERSION));

#if BM_ENABLE_OLED
  // begin() allocates the frame buffer (width * height / 8 bytes) and fails
  // if there is not enough free RAM; the firmware then runs serial-only.
  g_oled_ok = g_display.begin(SSD1306_SWITCHCAPVCC, BM_OLED_ADDRESS);
  if (!g_oled_ok) Serial.println(F("#WARN OLED init failed, continuing without display"));
#endif

  applyMaxGap();
  if (!initSensor()) Serial.println(F("#WARN INA219 not responding"));

  // Start one period in the past so the first sample is taken immediately.
  g_last_sample_ms = millis() - g_sample_period_ms;
}

void loop() {
  pollSerial();

  const uint32_t now = millis();
  if (now - g_last_sample_ms >= g_sample_period_ms) {
    g_last_sample_ms = now;
    sampleOnce(now);
  }

#if BM_ENABLE_OLED
  if (now - g_last_draw_ms >= BM_DISPLAY_PERIOD_MS) {
    g_last_draw_ms = now;
    drawDisplay();
  }
#endif
}
