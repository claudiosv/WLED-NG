#ifndef EspalexaDevice_h
#define EspalexaDevice_h

#include <functional>

#include "Arduino.h"

class EspalexaDevice;

using BrightnessCallbackFunction = ;
using DeviceCallbackFunction = ;
using ColorCallbackFunction = ;

enum class EspalexaColorMode : uint8_t {
  kNone = 0,
  kCt   = 1,
  kHs   = 2,
  kXy   = 3
};
enum class EspalexaDeviceType : uint8_t {
  kOnoff         = 0,
  kDimmable      = 1,
  kWhitespectrum = 2,
  kColor         = 3,
  kExtendedcolor = 4
};
enum class EspalexaDeviceProperty : uint8_t {
  kNone = 0,
  kOn   = 1,
  kOff  = 2,
  kBri  = 3,
  kHs   = 4,
  kCt   = 5,
  kXy   = 6
};

class EspalexaDevice {
 private:
  String                     deviceName_;
  BrightnessCallbackFunction _callback    = nullptr;
  DeviceCallbackFunction     _callbackDev = nullptr;
  ColorCallbackFunction      _callbackCol = nullptr;
  uint8_t                    val_, val_last_, sat_ = 0;
  uint16_t                   hue_ = 0, ct_ = 0;
  float                      x_ = 0.5F, y_ = 0.5F;
  uint32_t                   rgb_ = 0;
  uint8_t                    id_  = 0;
  EspalexaDeviceType         type_;
  EspalexaDeviceProperty     changed_ = EspalexaDeviceProperty::kNone;
  EspalexaColorMode          mode_    = EspalexaColorMode::kXy;

 public:
  EspalexaDevice();
  ~EspalexaDevice();
  EspalexaDevice(String deviceName, BrightnessCallbackFunction bcb, uint8_t initialValue = 0);
  EspalexaDevice(String deviceName, DeviceCallbackFunction dcb, EspalexaDeviceType t = EspalexaDeviceType::kDimmable,
                 uint8_t initialValue = 0);
  EspalexaDevice(String deviceName, ColorCallbackFunction ccb, uint8_t initialValue = 0);

  String                 getName();
  uint8_t                getId();
  EspalexaDeviceProperty getLastChangedProperty();
  uint8_t                getValue();
  uint8_t                getLastValue();  // last value that was not off (1-255)
  bool                   getState();
  uint8_t                getPercent();
  uint8_t                getDegrees();
  uint16_t               getHue();
  uint8_t                getSat();
  uint16_t               getCt();
  uint32_t               getKelvin();
  float                  getX();
  float                  getY();
  uint32_t               getRGB();
  uint8_t                getR();
  uint8_t                getG();
  uint8_t                getB();
  uint8_t                getW();
  EspalexaColorMode      getColorMode();
  EspalexaDeviceType     getType();

  void setId(uint8_t id);
  void setPropertyChanged(EspalexaDeviceProperty p);
  void setValue(uint8_t bri);
  void setState(bool onoff);
  void setPercent(uint8_t perc);
  void setName(String name);
  void setColor(uint16_t ct);
  void setColor(uint16_t hue, uint8_t sat);
  void setColorXY(float x, float y);
  void setColor(uint8_t r, uint8_t g, uint8_t b);

  void doCallback();
};

#endif