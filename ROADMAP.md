# Roadmap

- [ ] Remove ESP8266 support
- [ ] Remove F string macro usage (RAM savings negligible on ESP32, adds overhead)
- [ ] Use inline constexpr functions instead of macros where possible
- [ ] Use `static_assert` instead of `#if ... #error` for compile
- [ ] Break up platformio.ini into multiple files for better maintainability
- [ ] Use static_cast instead of C-style casts
- [ ] Switch to pioarduino 55.*
- [ ] Remove SPIFFS
- [ ] Upstream ESPAsyncWebServer
- [ ] Update NeoPixelBus
- [ ] Bump platform packages
- [ ] Tweak build flags for speed