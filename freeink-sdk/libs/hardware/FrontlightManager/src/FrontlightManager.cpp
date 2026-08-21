#include "FrontlightManager.h"

#if FREEINK_CAP_FRONTLIGHT
#include <M5Pm1.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO && defined(ARDUINO) && \
    ESP_ARDUINO_VERSION_MAJOR >= 3
#include <driver/ledc.h>
#define FREEINK_X4PRO_LEDC_SLEEP_KEEP_ALIVE 1
#else
#define FREEINK_X4PRO_LEDC_SLEEP_KEEP_ALIVE 0
#endif

namespace {
constexpr uint32_t maxDuty(uint8_t bits) { return (1u << bits) - 1u; }

// Paper Mono: the PWM lives in the M5PM1 PMIC, not the ESP. PM1 GPIO3 routed to
// alt-function PWM0 drives the AW9967 frontlight driver. Duty register is
// 12-bit; the high byte's bit 4 is the channel-enable bit. Perception-weighted
// like M5Unified's bring-up: duty = brightness^2 scaled into 12 bits.
constexpr uint8_t PM1_PWM_ENABLE = 0x10;

void pm1FrontlightAttach(uint32_t freqHz) {
  freeink::m5pm1::beginBus();
  // GPIO3 to push-pull, alt-function PWM0.
  freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_DRV, 1u << 3, 0);
  freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_FUNC0, 0xC0, 0xC0);
  freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM_FREQ_L, static_cast<uint16_t>(freqHz));
}

void pm1FrontlightWrite(uint32_t pct) {
  const uint32_t duty = (pct * pct * 4095u) / 10000u;  // 0-100% -> 12-bit, gamma ~2
  const uint8_t data[2] = {static_cast<uint8_t>(duty & 0xFF),
                           static_cast<uint8_t>(((duty >> 8) & 0x0F) | (duty ? PM1_PWM_ENABLE : 0))};
  freeink::m5pm1::writeBytes(freeink::m5pm1::REG_PWM0_DUTY_L, data, sizeof(data));
}

// The X4 Pro's OEM firmware uses channels 4 and 5. Its profile has no other
// LEDC peripheral, so the native driver can own those channels and preserve PWM
// output through automatic light sleep. Other boards retain the Arduino paths.
#if FREEINK_X4PRO_LEDC_SLEEP_KEEP_ALIVE
constexpr uint8_t LEDC_CH_COOL = LEDC_CHANNEL_4;
constexpr uint8_t LEDC_CH_WARM = LEDC_CHANNEL_5;
#else
constexpr uint8_t LEDC_CH_COOL = 0;
constexpr uint8_t LEDC_CH_WARM = 1;
#endif

// Apply the board's output polarity to a logical 0..full LED duty.
uint32_t physicalDuty(uint32_t logicalDuty, uint32_t full, bool activeHigh) {
  return activeHigh ? logicalDuty : full - logicalDuty;
}

#if FREEINK_X4PRO_LEDC_SLEEP_KEEP_ALIVE
bool prepareChannels(uint32_t freq, uint8_t bits) {
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = static_cast<ledc_timer_bit_t>(bits);
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = freq;
  // XTAL is independent of APB/CPU frequency and is explicitly supported by
  // ESP-IDF's LEDC keep-alive path on ESP32-S3.
  timer.clk_cfg = LEDC_USE_XTAL_CLK;
  const esp_err_t err = ledc_timer_config(&timer);
  if (err != ESP_OK) {
    log_e("X4 Pro frontlight timer setup failed: %d", static_cast<int>(err));
    return false;
  }
  return true;
}

bool attachChannel(int8_t gpio, uint8_t ch, uint32_t /*freq*/, uint8_t /*bits*/) {
  ledc_channel_config_t channel{};
  channel.gpio_num = gpio;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = static_cast<ledc_channel_t>(ch);
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = 0;
  channel.hpoint = 0;
  // Arduino's default is NO_ALIVE_NO_PD, which switches the output off during
  // every tickless light-sleep interval. At low duty that appears as flashing.
  channel.sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE;
  const esp_err_t err = ledc_channel_config(&channel);
  if (err != ESP_OK) {
    log_e("X4 Pro frontlight channel %u setup failed: %d", ch, static_cast<int>(err));
    return false;
  }
  return true;
}

void writeChannel(int8_t /*gpio*/, uint8_t ch, uint32_t duty) {
  const auto channel = static_cast<ledc_channel_t>(ch);
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
  if (err == ESP_OK) {
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
  }
  if (err != ESP_OK) {
    log_e("X4 Pro frontlight channel %u write failed: %d", ch, static_cast<int>(err));
  }
}
#elif defined(ARDUINO) && ESP_ARDUINO_VERSION_MAJOR >= 3
bool prepareChannels(uint32_t /*freq*/, uint8_t /*bits*/) { return true; }
bool attachChannel(int8_t gpio, uint8_t /*ch*/, uint32_t freq, uint8_t bits) {
  return ledcAttach(gpio, freq, bits);
}
void writeChannel(int8_t gpio, uint8_t /*ch*/, uint32_t duty) { ledcWrite(gpio, duty); }
#else
bool prepareChannels(uint32_t /*freq*/, uint8_t /*bits*/) { return true; }
bool attachChannel(int8_t gpio, uint8_t ch, uint32_t freq, uint8_t bits) {
  ledcSetup(ch, freq, bits);
  ledcAttachPin(gpio, ch);
  return true;
}
void writeChannel(int8_t /*gpio*/, uint8_t ch, uint32_t duty) { ledcWrite(ch, duty); }
#endif
}  // namespace
#endif

void FrontlightManager::begin() {
#if FREEINK_CAP_FRONTLIGHT
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  if (fl.viaPm1Pwm) {
    pm1FrontlightAttach(fl.pwmFrequency);
    _begun = true;
    setBrightness(0);
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

  if (!prepareChannels(fl.pwmFrequency, fl.pwmResolutionBits) ||
      !attachChannel(fl.gpio, LEDC_CH_COOL, fl.pwmFrequency, fl.pwmResolutionBits)) {
    return;
  }
  if (fl.gpioWarm != BoardConfig::PIN_UNASSIGNED) {
    if (!attachChannel(fl.gpioWarm, LEDC_CH_WARM, fl.pwmFrequency, fl.pwmResolutionBits)) {
      return;
    }
  }
  _begun = true;
  setBrightness(0);
#endif
}

#if FREEINK_CAP_FRONTLIGHT
void FrontlightManager::apply() {
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  if (!_begun) return;
  if (fl.viaPm1Pwm) {
    pm1FrontlightWrite(_brightness);
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

  const uint32_t full = maxDuty(fl.pwmResolutionBits);
  const bool dual = fl.gpioWarm != BoardConfig::PIN_UNASSIGNED;

  // Convert brightness to PWM precision BEFORE splitting it between channels.
  // Splitting integer percentages first loses both fractional parts at low
  // levels: brightness=1, warmth=50 previously became cool=0% + warm=0%.
  // Splitting the total duty also keeps cool+warm equal to the requested total.
  uint32_t totalDuty = 0;
  if (_useLevel && _brightnessLevel > 0) {
    const uint32_t n = static_cast<uint32_t>(_brightnessLevel - 1u);
    totalDuty = 1u + (n * n * (full - 1u)) / (254u * 254u);
  } else if (!_useLevel) {
    totalDuty = (static_cast<uint32_t>(_brightness) * full + 50u) / 100u;
  }
  uint32_t warmDuty = 0;
  uint32_t coolDuty = totalDuty;
  if (dual) {
    warmDuty = (totalDuty * _warmPercent + 50u) / 100u;
    coolDuty = totalDuty - warmDuty;
  }
  writeChannel(fl.gpio, LEDC_CH_COOL, physicalDuty(coolDuty, full, fl.activeHigh));

  if (dual) {
    writeChannel(fl.gpioWarm, LEDC_CH_WARM, physicalDuty(warmDuty, full, fl.activeHigh));
  }
}
#endif

void FrontlightManager::setBrightness(uint8_t percent) {
#if FREEINK_CAP_FRONTLIGHT
  if (percent > 100) percent = 100;
  _brightness = percent;
  _brightnessLevel = (static_cast<uint16_t>(percent) * 255u) / 100u;
  _useLevel = false;
  if (percent > 0) _lastBrightness = percent;
  apply();
#else
  (void)percent;
#endif
}

void FrontlightManager::setBrightnessLevel(uint8_t level) {
#if FREEINK_CAP_FRONTLIGHT
  _brightnessLevel = level;
  _brightness = (static_cast<uint16_t>(level) * 100u) / 255u;
  _useLevel = true;
  apply();
#else
  (void)level;
#endif
}

void FrontlightManager::off() { setBrightness(0); }
void FrontlightManager::on() { setBrightness(_lastBrightness); }

void FrontlightManager::setColorTemperature(uint8_t warmPercent) {
#if FREEINK_CAP_FRONTLIGHT
  _warmPercent = warmPercent > 100 ? 100 : warmPercent;
  // Only re-drives hardware when a warm channel exists; on single-channel boards this just
  // records the request (apply() ignores _warmPercent without a second channel).
  apply();
#else
  (void)warmPercent;
#endif
}
