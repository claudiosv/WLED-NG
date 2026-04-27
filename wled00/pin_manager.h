#ifndef WLED_PIN_MANAGER_H
#define WLED_PIN_MANAGER_H
/*
 * Registers pins so there is no attempt for two interfaces to use the same pin
 */

#define WLED_NUM_PINS (GPIO_PIN_COUNT)

// Pin capability flags - only "special" capabilities useful for debugging (note: touch capability is provided by
// appendGPIOinfo() via d.touch)
enum {
PIN_CAP_ADC =        0x02,  // has ADC capability (analog input)
PIN_CAP_PWM =        0x04,  // can be used for PWM (analog LED output) -> unused, all pins can use ledc PWM
PIN_CAP_BOOT =       0x08,  // bootloader pin
PIN_CAP_BOOTSTRAP =  0x10,  // bootstrap pin (strapping pin affecting boot mode)
PIN_CAP_INPUT_ONLY = 0x20  // input only pin (cannot be used as output)
};

using managed_pin_type = struct PinManagerPinType {
  int8_t pin;
  bool   isOutput;
};

/*
 * Allows PinManager to "lock" an allocation to a specific
 * owner, so someone else doesn't accidentally de-allocate
 * a pin it hasn't allocated.  Also enhances debugging.
 *
 * RAM Cost:
 *     40 bytes on ESP32
 */
enum struct PinOwner : uint8_t {
  kNone = 0,  // default == legacy == unspecified owner
  // High bit is set for all built-in pin owners
  kEthernet   = 0x81,
  kBusDigital = 0x82,
  kBusOnOff   = 0x83,
  kBusPwm     = 0x84,  // 'BusP'      == PWM output using BusPwm
  kButton     = 0x85,  // 'Butn'      == button from configuration
  kIr         = 0x86,  // 'IR'        == IR receiver pin from configuration
  kRelay      = 0x87,  // 'Rly'       == Relay pin from configuration
  kSpiRam    = 0x88,  // 'SpiR'      == SPI RAM
  kDebugOut   = 0x89,  // 'Dbg'       == debug output always IO1
  kDmx        = 0x8A,  // 'DMX'       == DMX output, hard-coded to IO2
  kHwI2C     = 0x8B,  // 'I2C'       == hardware I2C pins (4&5 on ESP8266, 21&22 on ESP32)
  kHwSpi     = 0x8C,  // 'SPI'       == hardware (V)SPI pins (13,14&15 on ESP8266, 5,18&23 on ESP32)
  kDmxInput  = 0x8D,  // 'DMX_INPUT' == DMX input via serial
  kHuB75      = 0x8E,  // 'Hub75' == Hub75 driver
  // Use UserMod IDs from const.h here
  kUmUnspecified = 143 = USERMOD_ID_UNSPECIFIED,  // 0x01
  kUmExample = 144     = USERMOD_ID_EXAMPLE,      // 0x02 // Usermod "usermod_v2_example.h"
  kUmTemperature = 145 = USERMOD_ID_TEMPERATURE,  // 0x03 // Usermod "usermod_temperature.h"
  // #define USERMOD_ID_FIXNETSERVICES                  // 0x04 // Usermod "usermod_Fix_unreachable_netservices.h" --
  // Does not allocate pins
  kUmPir = 146 = USERMOD_ID_PIRSWITCH,  // 0x05 // Usermod "usermod_PIR_sensor_switch.h"
  kUmImu = 147 = USERMOD_ID_IMU,        // 0x06 // Usermod "usermod_mpu6050_imu.h" -- Interrupt pin
  kUmFourLineDisplay = 148 =
      USERMOD_ID_FOUR_LINE_DISP,  // 0x07 // Usermod "usermod_v2_four_line_display.h -- May use "standard" HW_I2C pins
  kUmRotaryEncoderUi = 149 = USERMOD_ID_ROTARY_ENC_UI,  // 0x08 // Usermod "usermod_v2_rotary_encoder_ui.h"
  // #define USERMOD_ID_AUTO_SAVE                       // 0x09 // Usermod "usermod_v2_auto_save.h" -- Does not allocate
  // pins #define USERMOD_ID_DHT                             // 0x0A // Usermod "usermod_dht.h" -- Statically allocates
  // pins, not compatible with pinManager? #define USERMOD_ID_VL53L0X                         // 0x0C // Usermod
  // "usermod_vl53l0x_gestures.h" -- Uses "standard" HW_I2C pins
  kUmMultiRelay = 150        = USERMOD_ID_MULTI_RELAY,         // 0x0D // Usermod "usermod_multi_relay.h"
  kUmAnimatedStaircase = 151 = USERMOD_ID_ANIMATED_STAIRCASE,  // 0x0E // Usermod "Animated_Staircase.h"
  kUmBattery = 152           = USERMOD_ID_BATTERY,             //
  // #define USERMOD_ID_RTC                             // 0x0F // Usermod "usermod_rtc.h" -- Uses "standard" HW_I2C
  // pins #define USERMOD_ID_ELEKSTUBE_IPS                   // 0x10 // Usermod "usermod_elekstube_ips.h" -- Uses quite
  // a few pins ... see Hardware.h and User_Setup.h #define USERMOD_ID_SN_PHOTORESISTOR                // 0x11 //
  // Usermod "usermod_sn_photoresistor.h" -- Uses hard-coded pin (PHOTORESISTOR_PIN == A0), but could be easily updated
  // to use pinManager
  kUmBH1750 = 153           = USERMOD_ID_BH1750,            // 0x14 // Usermod "bh1750.h -- Uses "standard" HW_I2C pins
  kUmRgbRotaryEncoder = 154 = USERMOD_RGB_ROTARY_ENCODER,   // 0x16 // Usermod "rgb-rotary-encoder.h"
  kUmQuinLedAnPenta = 155   = USERMOD_ID_QUINLED_AN_PENTA,  // 0x17 // Usermod "quinled-an-penta.h"
  kUmBmE280 = 156           = USERMOD_ID_BME280,           // 0x1E // Usermod "usermod_bme280.h -- Uses "standard" HW_I2C pins
  kUmAudioreactive = 157    = USERMOD_ID_AUDIOREACTIVE,    // 0x20 // Usermod "audio_reactive.h"
  kUmSdCard = 158           = USERMOD_ID_SD_CARD,          // 0x25 // Usermod "usermod_sd_card.h"
  kUmPwmOutputs = 159      = USERMOD_ID_PWM_OUTPUTS,      // 0x26 // Usermod "usermod_pwm_outputs.h"
  kUmLdrDuskDawn = 160    = USERMOD_ID_LDR_DUSK_DAWN,    // 0x2B // Usermod "usermod_LDR_Dusk_Dawn_v2.h"
  kUmMaX17048 = 161         = USERMOD_ID_MAX17048,         // 0x2F // Usermod "usermod_max17048.h"
  kUmBmE68X = 162           = USERMOD_ID_BME68X,           // 0x31 // Usermod "usermod_bme68x.h -- Uses "standard" HW_I2C pins
  kUmPixelsDiceTray = 163 = USERMOD_ID_PIXELS_DICE_TRAY  // 0x35 // Usermod "pixels_dice_tray.h" -- Needs compile time
                                                     // specified 6 pins for display including SPI.
};
static_assert(0U == static_cast<uint8_t>(PinOwner::kNone),
              "PinOwner::None must be zero, so default array initialization works as expected");

namespace pin_manager {
  // De-allocates a single pin
  bool deallocatePin(byte gpio, PinOwner tag);
  // De-allocates multiple pins but only if all can be deallocated (PinOwner has to be specified)
  bool deallocateMultiplePins(const uint8_t *pinArray, byte arrayElementCount, PinOwner tag);
  bool deallocateMultiplePins(const managed_pin_type *pinArray, byte arrayElementCount, PinOwner tag);
  // Allocates a single pin, with an owner tag.
  // De-allocation requires the same owner tag (or override)
  bool allocatePin(byte gpio, bool output, PinOwner tag);
  // Allocates all the pins, or allocates none of the pins, with owner tag.
  // Provided to simplify error condition handling in clients
  // using more than one pin, such as I2C, SPI, rotary encoders,
  // ethernet, etc..
  bool allocateMultiplePins(const managed_pin_type *mptArray, byte arrayElementCount, PinOwner tag);
  bool allocateMultiplePins(const int8_t *mptArray, byte arrayElementCount, PinOwner tag, boolean output);

  [[deprecated("Replaced by three-parameter allocatePin(gpio, output, ownerTag), for improved debugging")]]
  inline bool allocatePin(byte gpio, bool output = true) {
    return allocatePin(gpio, output, PinOwner::kNone);
  }
  [[deprecated("Replaced by two-parameter deallocatePin(gpio, ownerTag), for improved debugging")]]
  inline void deallocatePin(byte gpio) {
    deallocatePin(gpio, PinOwner::kNone);
  }

  // will return true for reserved pins
  bool isPinAllocated(byte gpio, PinOwner tag = PinOwner::kNone);
  // will return false for reserved pins
  bool isPinOk(byte gpio, bool output = true);

  bool isReadOnlyPin(byte gpio);
  int  getButtonIndex(byte gpio);  // returns button index if pin is used for button, otherwise -1
  bool isAnalogPin(byte gpio);     // returns true if pin has ADC capability, otherwise false

  PinOwner    getPinOwner(byte gpio);
  const char *getPinOwnerName(uint8_t gpio);

#ifdef ARDUINO_ARCH_ESP32
  byte allocateLedc(byte channels);
  void deallocateLedc(byte pos, byte channels);
#endif
};  // namespace PinManager

// extern PinManager pinManager;
#endif
