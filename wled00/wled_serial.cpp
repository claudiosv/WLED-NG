#include "wled.h"

// forward declarations
static void sendBytes();

/*
 * Adalight and TPM2 handler
 */

enum class AdaState {
  kHeaderA,
  kHeaderD,
  kHeaderA,
  kHeaderCountHi,
  kHeaderCountLo,
  kHeaderCountCheck,
  kDataRed,
  kDataGreen,
  kDataBlue,
  kTpM2HeaderType,
  kTpM2HeaderCountHi,
  kTpM2HeaderCountLo,
};

static uint16_t currentBaud       = 1152;  // default baudrate 115200 (divided by 100)
static bool     continuousSendLED = false;
static uint32_t lastUpdate        = 0;

void updateBaudRate(uint32_t rate) {
  unsigned rate100 = rate / 100;
  if (rate100 == currentBaud || rate100 < 96) {
    return;
  }
  currentBaud = rate100;

  if (serialCanTX) {
    Serial.print("Baud is now ");
    Serial.println(rate);
  }

  Serial.flush();
  Serial.begin(rate);
}

// RGB LED data return as JSON array. Slow, but easy to use on the other end.
static inline void sendJSON() {
  if (serialCanTX) {
    unsigned used = strip.getLengthTotal();
    Serial.write('[');
    for (unsigned i = 0; i < used; i++) {
      Serial.print(strip.getPixelColor(i));
      if (i != used - 1) {
        Serial.write(',');
      }
    }
    Serial.println("]");
  }
}

// RGB LED data returned as bytes in TPM2 format. Faster, and slightly less easy to use on the other end.
static void sendBytes() {
  if (serialCanTX) {
    Serial.write(0xC9);
    Serial.write(0xDA);
    unsigned used = strip.getLengthTotal();
    unsigned len  = used * 3;
    Serial.write(highByte(len));
    Serial.write(lowByte(len));
    for (unsigned i = 0; i < used; i++) {
      uint32_t c = strip.getPixelColor(i);
      Serial.write(qadd8(W(c), R(c)));  // R, add white channel to RGB channels as a simple RGBW -> RGB map
      Serial.write(qadd8(W(c), G(c)));  // G
      Serial.write(qadd8(W(c), B(c)));  // B
    }
    Serial.write(0x36);
    Serial.write('\n');
  }
}

void handleSerial() {
  if (!(serialCanRX && Serial)) {
    return;  // arduino docs: `if (Serial)` indicates whether or not the USB CDC serial connection is open. For all
             // non-USB CDC ports, this will always return true
  }

  static auto     state = AdaState::kHeaderA;
  static uint16_t count = 0;
  static uint16_t pixel = 0;
  static byte     check = 0x00;
  static byte     red   = 0x00;
  static byte     green = 0x00;

  while (Serial.available() > 0) {
    yield();
    byte next = Serial.peek();
    switch (state) {
      case AdaState::kHeaderA:
        if (next == 'A') {
          state = AdaState::kHeaderD;
        } else if (next == 0xC9) {
          state = AdaState::kTpM2HeaderType;
        }  // TPM2 start byte
        else if (next == 'I') {
          handleImprovPacket();
          return;
        } else if (next == 'v') {
          Serial.print("WLED");
          Serial.write(' ');
          Serial.println(VERSION);
        } else if (next == 0xB0) {
          updateBaudRate(115200);
        } else if (next == 0xB1) {
          updateBaudRate(230400);
        } else if (next == 0xB2) {
          updateBaudRate(460800);
        } else if (next == 0xB3) {
          updateBaudRate(500000);
        } else if (next == 0xB4) {
          updateBaudRate(576000);
        } else if (next == 0xB5) {
          updateBaudRate(921600);
        } else if (next == 0xB6) {
          updateBaudRate(1000000);
        } else if (next == 0xB7) {
          updateBaudRate(1500000);
        } else if (next == 'l') {
          sendJSON();
        }  // Send LED data as JSON Array
        else if (next == 'L') {
          sendBytes();
        }  // Send LED data as TPM2 Data Packet
        else if (next == 'o') {
          continuousSendLED = false;
        }  // Disable Continuous Serial Streaming
        else if (next == 'O') {
          continuousSendLED = true;
        }  // Enable Continuous Serial Streaming
        else if (next == '{') {  // JSON API
          bool verboseResponse = false;
          if (!requestJSONBufferLock(JSON_LOCK_SERIAL)) {
            Serial.printf("{\"error\":%d}\n", ERR_NOBUF);
            return;
          }
          Serial.setTimeout(100);
          DeserializationError error = deserializeJson(*pDoc, Serial);
          if (!error) {
            verboseResponse = deserializeState(pDoc->as<JsonObject>());
            // only send response if TX pin is unused for other purposes
            if (verboseResponse && serialCanTX) {
              pDoc->clear();
              JsonObject stateDoc = pDoc->createNestedObject("state");
              serializeState(stateDoc);
              JsonObject info = pDoc->createNestedObject("info");
              serializeInfo(info);

              serializeJson(*pDoc, Serial);
              Serial.println();
            }
          }
          releaseJSONBufferLock();
        }
        break;
      case AdaState::kHeaderD:
        if (next == 'd') {
          state = AdaState::kHeaderA;
        } else {
          state = AdaState::kHeaderA;
        }
        break;
      case AdaState::kHeaderA:
        if (next == 'a') {
          state = AdaState::kHeaderCountHi;
        } else {
          state = AdaState::kHeaderA;
        }
        break;
      case AdaState::kHeaderCountHi:
        pixel = 0;
        count = next * 0x100;
        check = next;
        state = AdaState::kHeaderCountLo;
        break;
      case AdaState::kHeaderCountLo:
        count += next + 1;
        check = check ^ next ^ 0x55;
        state = AdaState::kHeaderCountCheck;
        break;
      case AdaState::kHeaderCountCheck:
        if (check == next) {
          state = AdaState::kDataRed;
        } else {
          state = AdaState::kHeaderA;
        }
        break;
      case AdaState::kTpM2HeaderType:
        state = AdaState::kHeaderA;  //(unsupported) TPM2 command or invalid type
        if (next == 0xDA) {
          state = AdaState::kTpM2HeaderCountHi;  // TPM2 data
        } else if (next == 0xAA) {
          Serial.write(0xAC);  // TPM2 ping
        }
        break;
      case AdaState::kTpM2HeaderCountHi:
        pixel = 0;
        count = (next * 0x100) / 3;
        state = AdaState::kTpM2HeaderCountLo;
        break;
      case AdaState::kTpM2HeaderCountLo:
        count += next / 3;
        state = AdaState::kDataRed;
        break;
      case AdaState::kDataRed:
        red   = next;
        state = AdaState::kDataGreen;
        break;
      case AdaState::kDataGreen:
        green = next;
        state = AdaState::kDataBlue;
        break;
      case AdaState::kDataBlue:
        byte blue = next;
        if (!realtimeOverride) {
          setRealtimePixel(pixel++, red, green, blue, 0);
        }
        if (--count > 0) {
          state = AdaState::kDataRed;
        } else {
          realtimeLock(realtimeTimeoutMs, REALTIME_MODE_ADALIGHT);

          if (!realtimeOverride) {
            strip.show();
          }
          state = AdaState::kHeaderA;
        }
        break;
    }

    // All other received bytes will disable Continuous Serial Streaming
    if (continuousSendLED && next != 'O') {
      continuousSendLED = false;
    }

    Serial.read();  // discard the byte
  }

  // If Continuous Serial Streaming is enabled, send new LED data as bytes
  if (continuousSendLED && (lastUpdate != strip.getLastShow())) {
    sendBytes();
    lastUpdate = strip.getLastShow();
  }
}
