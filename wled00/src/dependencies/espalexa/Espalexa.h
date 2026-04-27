#ifndef Espalexa_h
#define Espalexa_h

/*
 * Alexa Voice On/Off/Brightness/Color Control. Emulates a Philips Hue bridge to Alexa.
 *
 * This was put together from these two excellent projects:
 * https://github.com/kakopappa/arduino-esp8266-alexa-wemo-switch
 * https://github.com/probonopd/ESP8266HueEmulator
 */
/*
 * @title Espalexa library
 * @version 2.7.1
 * @author Christian Schwinne
 * @license MIT
 * @contributors d-999
 */

#include "Arduino.h"

// you can use these defines for library config in your sketch. Just use them before #include <Espalexa.h>
// #define ESPALEXA_ASYNC

// in case this is unwanted in your application (will disable the /espalexa value page)
// #define ESPALEXA_NO_SUBPAGE

#ifndef ESPALEXA_MAXDEVICES
#define ESPALEXA_MAXDEVICES 10  // this limit only has memory reasons, set it higher should you need to, max 128
#endif

// #define ESPALEXA_DEBUG

#ifdef ESPALEXA_ASYNC
#ifdef ARDUINO_ARCH_ESP32
#include <AsyncTCP.h>
#else
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#else
#ifdef ARDUINO_ARCH_ESP32
#include <WebServer.h>  //if you get an error here please update to ESP32 arduino core 1.0.0
#include <WiFi.h>
#endif
#endif
#include <WiFiUdp.h>

#include <utility>

#include "../network/Network.h"

#ifdef ESPALEXA_DEBUG
#pragma message "Espalexa 2.7.1 debug mode"
#define EA_DEBUG(x)   Serial.print(x)
#define EA_DEBUGLN(x) Serial.println(x)
#else
#define EA_DEBUG(x)
#define EA_DEBUGLN(x)
#endif

#include "EspalexaDevice.h"

#define DEVICE_UNIQUE_ID_LENGTH 12

class Espalexa {
 private:
// private member vars
#ifdef ESPALEXA_ASYNC
  AsyncWebServer*        serverAsync_;
  AsyncWebServerRequest* server_;  // this saves many #defines
  String                 body_ = "";
#elif defined(ARDUINO_ARCH_ESP32)
  WebServer* server;
#endif
  uint8_t currentDeviceCount_ = 0;
  bool    discoverable_       = true;
  bool    udpConnected_       = false;

  EspalexaDevice* devices_[ESPALEXA_MAXDEVICES] = {};
  // Keep in mind that Device IDs go from 1 to DEVICES, cpp arrays from 0 to DEVICES-1!!

  WiFiUDP   espalexaUdp_;
  IPAddress ipMulti_;
  uint32_t  mac24_;            // bottom 24 bits of mac
  String    escapedMac_ = "";  // lowercase mac address
  String    bridgeId_   = "";  // uppercase EUI-64 bridge ID (16 hex chars)

  // private member functions
  static const char* modeString(EspalexaColorMode m) {
    if (m == EspalexaColorMode::kXy) {
      return "xy";
    }
    if (m == EspalexaColorMode::kHs) {
      return "hs";
    }
    return "ct";
  }

  static const char* typeString(EspalexaDeviceType t) {
    switch (t) {
      case EspalexaDeviceType::kDimmable:
        return "Dimmable light";
      case EspalexaDeviceType::kWhitespectrum:
        return "Color temperature light";
      case EspalexaDeviceType::kColor:
        return "Color light";
      case EspalexaDeviceType::kExtendedcolor:
        return "Extended color light";
      default:
        return "";
    }
  }

  static const char* modelidString(EspalexaDeviceType t) {
    switch (t) {
      case EspalexaDeviceType::kDimmable:
        return "LWB010";
      case EspalexaDeviceType::kWhitespectrum:
        return "LWT010";
      case EspalexaDeviceType::kColor:
        return "LST001";
      case EspalexaDeviceType::kExtendedcolor:
        return "LCT015";
      default:
        return "";
    }
  }

  static void encodeLightId(uint8_t idx, const char* out) {
    String mymac = WiFi.macAddress();
    sprintf(out, "%02X:%s:AB-%02X", idx, mymac.c_str(), idx);
  }

  // construct 'globally unique' Json dict key fitting into signed int
  int encodeLightKey(uint8_t idx) const {
    // return idx +1;
    static_assert(ESPALEXA_MAXDEVICES <= 128, "");
    return (mac24_ << 7) | idx;
  }

  // get device index from Json key
  uint8_t decodeLightKey(int key) const {
    // return key -1;
    return ((static_cast<uint32_t>(key) >> 7) == mac24_) ? (key & 127U) : 255U;
  }

  // device JSON string: color+temperature device emulates LCT015, dimmable device LWB010, (TODO: on/off Plug 01, color
  // temperature device LWT010, color device LST001)
  void deviceJsonString(EspalexaDevice* dev, const char* buf,
                        size_t maxBuf)  // softhack007 "size" parameter added, to avoid buffer overrun
  {
    char buf_lightid[27];
    encodeLightId(dev->getId() + 1, buf_lightid);

    char buf_col[80] = "";
    // color support
    if (static_cast<uint8_t>(dev->getType()) > 2) {
      // TODO: claudio - %f is not working for some reason on ESP8266 in v0.11.0 (was fine in 0.10.2). Need to investigate
      // sprintf(buf_col,",\"hue\":%u,\"sat\":%u,\"effect\":\"none\",\"xy\":[%f,%f]"
      //   ,dev->getHue(), dev->getSat(), dev->getX(), dev->getY());
      snprintf(buf_col, sizeof(buf_col), R"(,"hue":%u,"sat":%u,"effect":"none","xy":[%s,%s])", dev->getHue(),
               dev->getSat(), (String(dev->getX())).c_str(), (String(dev->getY())).c_str());
    }

    char buf_ct[16] = "";
    // white spectrum support
    if (static_cast<uint8_t>(dev->getType()) > 1 && dev->getType() != EspalexaDeviceType::kColor) {
      snprintf(buf_ct, sizeof(buf_ct), ",\"ct\":%u", dev->getCt());
    }

    char buf_cm[20] = "";
    if (static_cast<uint8_t>(dev->getType()) > 1) {
      snprintf(buf_cm, sizeof(buf_cm), R"(","colormode":"%s)", modeString(dev->getColorMode()));
    }

    snprintf(
        buf, maxBuf,
        PSTR(
            "{\"state\":{\"on\":%s,\"bri\":%u%s%s,\"alert\":\"none%s\",\"mode\":\"homeautomation\",\"reachable\":true},"
            "\"type\":\"%s\",\"name\":\"%s\",\"modelid\":\"%s\",\"manufacturername\":\"Philips\",\"productname\":\"E%u"
            "\",\"uniqueid\":\"%s\",\"swversion\":\"espalexa-2.7.0\"}")

            ,
        (dev->getValue()) ? "true" : "false", dev->getLastValue() - 1, buf_col, buf_ct, buf_cm,
        typeString(dev->getType()), dev->getName().c_str(), modelidString(dev->getType()),
        static_cast<uint8_t>(dev->getType()), buf_lightid);
  }

// Espalexa status page /espalexa
#ifndef ESPALEXA_NO_SUBPAGE
  void servePage() {
    EA_DEBUGLN("HTTP Req espalexa ...\n");
    String res = "Hello from Espalexa!\r\n\r\n";
    for (int i = 0; i < currentDeviceCount; i++) {
      EspalexaDevice* dev = devices[i];
      res += "Value of device " + String(i + 1) + " (" + dev->getName() + "): " + String(dev->getValue()) + " (" +
             typeString(dev->getType());
      if (static_cast<uint8_t>(dev->getType()) > 1)  // color support
      {
        res += ", colormode=" + String(modeString(dev->getColorMode())) + ", r=" + String(dev->getR()) +
               ", g=" + String(dev->getG()) + ", b=" + String(dev->getB());
        res += ", ct=" + String(dev->getCt()) + ", hue=" + String(dev->getHue()) + ", sat=" + String(dev->getSat()) +
               ", x=" + String(dev->getX()) + ", y=" + String(dev->getY());
      }
      res += ")\r\n";
    }
    res += "\r\nFree Heap: " + (String)ESP.getFreeHeap();
    res += "\r\nUptime: " + (String)millis();
    res += "\r\n\r\nEspalexa library v2.7.0 by Christian Schwinne 2021";
    server->send(200, "text/plain", res);
  }
#endif

  // not found URI (only if internal webserver is used)
  void serveNotFound() {
    EA_DEBUGLN("Not-Found HTTP call:");
#ifndef ESPALEXA_ASYNC
    EA_DEBUGLN("URI: " + server->uri());
    EA_DEBUGLN("Body: " + server->arg(0));
    if (!handleAlexaApiCall(server->uri(), server->arg(0)))
#else
    EA_DEBUGLN("URI: " + server->url());
    EA_DEBUGLN("Body: " + body);
    if (!handleAlexaApiCall(server_))
#endif
      server_->send(404, "text/plain", "Not Found (espalexa)");
  }

  // send description.xml device property page
  void serveDescription() {
    EA_DEBUGLN("# Responding to description.xml ... #\n");
    IPAddress localIP = Network.localIP();
    char      s[16];
    snprintf(s, sizeof(s), "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
    char buf[1024];

    snprintf(buf, sizeof(buf),
             PSTR("<?xml version=\"1.0\" ?>"
                  "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                  "<specVersion><major>1</major><minor>0</minor></specVersion>"
                  "<URLBase>http://%s:80/</URLBase>"
                  "<device>"
                  "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
                  "<friendlyName>Espalexa (%s:80)</friendlyName>"
                  "<manufacturer>Royal Philips Electronics</manufacturer>"
                  "<manufacturerURL>http://www.philips.com</manufacturerURL>"
                  "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
                  "<modelName>Philips hue bridge 2012</modelName>"
                  "<modelNumber>929000226503</modelNumber>"
                  "<modelURL>http://www.meethue.com</modelURL>"
                  "<serialNumber>%s</serialNumber>"
                  "<UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN>"
                  "<presentationURL>index.html</presentationURL>"
                  "</device>"
                  "</root>"),
             s, s, escapedMac_.c_str(), escapedMac_.c_str());

    server_->send(200, "text/xml", buf);

    EA_DEBUGLN("Send setup.xml");
    EA_DEBUGLN(buf);
  }

  // init the server
  void startHttpServer() {
#ifdef ESPALEXA_ASYNC
    if (serverAsync_ == nullptr) {
      serverAsync_ = new AsyncWebServer(80);
      serverAsync_->onNotFound([=](AsyncWebServerRequest* request) {
        server_ = request;
        serveNotFound();
      });
    }

    serverAsync_->onRequestBody(
        [=](AsyncWebServerRequest*  /*request*/, const uint8_t* data, size_t len, size_t  /*index*/, size_t  /*total*/) {
          char b[len + 1];
          b[len] = 0;
          memcpy(b, data, len);
          body_ = b;  // save the body so we can use it for the API call
          EA_DEBUG("Received body: ");
          EA_DEBUGLN(body);
        });
#ifndef ESPALEXA_NO_SUBPAGE
    serverAsync->on("/espalexa", HTTP_GET, [=](AsyncWebServerRequest* request) {
      server = request;
      servePage();
    });
#endif
    serverAsync_->on("/description.xml", HTTP_GET, [=](AsyncWebServerRequest* request) {
      server_ = request;
      serveDescription();
    });
    serverAsync_->begin();

#else
    if (server == nullptr) {
      server = new WebServer(80);
      server->onNotFound([=]() {
        serveNotFound();
      });
    }

#ifndef ESPALEXA_NO_SUBPAGE
    server->on("/espalexa", HTTP_GET, [=]() {
      servePage();
    });
#endif
    server->on("/description.xml", HTTP_GET, [=]() {
      serveDescription();
    });
    server->begin();
#endif
  }

  // respond to UDP SSDP M-SEARCH
  void respondToSearch() {
    IPAddress localIP = Network.localIP();
    char      s[16];
    sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

    char buf[1024];

    snprintf(buf, sizeof(buf),
             PSTR("HTTP/1.1 200 OK\r\n"
                  "EXT:\r\n"
                  "CACHE-CONTROL: max-age=86400\r\n"  // SSDP_INTERVAL
                  "LOCATION: http://%s:80/description.xml\r\n"
                  "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\n"  // _modelName, _modelNumber
                  "hue-bridgeid: %s\r\n"
                  "ST: urn:schemas-upnp-org:device:Basic:1\r\n"                                    // _deviceType
                  "USN: uuid:2f402f80-da50-11e1-9b23-%s::urn:schemas-upnp-org:device:Basic:1\r\n"  // _uuid::_deviceType
                  "\r\n"),
             s, bridgeId_.c_str(), escapedMac_.c_str());

    espalexaUdp_.beginPacket(espalexaUdp_.remoteIP(), espalexaUdp_.remotePort());
#ifdef ARDUINO_ARCH_ESP32
    espalexaUdp_.write(reinterpret_cast<uint8_t*>(buf), strlen(buf));
#else
    espalexaUdp.write(buf);
#endif
    espalexaUdp_.endPacket();
  }

 public:
  Espalexa() {
  }

// initialize interfaces
#ifdef ESPALEXA_ASYNC
  bool begin(AsyncWebServer* externalServer = nullptr)
#elif defined ARDUINO_ARCH_ESP32
  bool begin(WebServer* externalServer = nullptr)
#endif
  {
    EA_DEBUGLN("Espalexa Begin...");
    EA_DEBUG("MAXDEVICES ");
    EA_DEBUGLN(ESPALEXA_MAXDEVICES);
    escapedMac_ = WiFi.macAddress();
    escapedMac_.replace(":", "");
    escapedMac_.toLowerCase();

    // Compute EUI-64 bridge ID from MAC-48: insert standard "FFFE" padding between
    // the first 6 hex chars (OUI/manufacturer) and last 6 hex chars (device), then uppercase
    bridgeId_ = escapedMac_.substring(0, 6) + "fffe" + escapedMac_.substring(6);
    bridgeId_.toUpperCase();

    String macSubStr = escapedMac_.substring(6, 12);
    mac24_            = strtol(macSubStr.c_str(), 0, 16);

#ifdef ESPALEXA_ASYNC
    serverAsync_ = externalServer;
#else
    server = externalServer;
#endif
#ifdef ARDUINO_ARCH_ESP32
    udpConnected_ = (espalexaUdp_.beginMulticast(IPAddress(239, 255, 255, 250), 1900) != 0u);
#else
    udpConnected = espalexaUdp.beginMulticast(Network.localIP(), IPAddress(239, 255, 255, 250), 1900);
#endif

    if (udpConnected_) {
      startHttpServer();
      EA_DEBUGLN("Done");
      return true;
    }
    EA_DEBUGLN("Failed");
    return false;
  }

  // get device count, function only in WLED version of Espalexa
  uint8_t getDeviceCount() const {
    return currentDeviceCount_;
  }

  // service loop
  void loop() {
#ifndef ESPALEXA_ASYNC
    if (server == nullptr) {
      return;  // only if begin() was not called
    }
    server->handleClient();
#endif

    if (!udpConnected_) {
      return;
    }
    int packetSize = espalexaUdp_.parsePacket();
    if (packetSize < 1) {
      return;  // no new udp packet
    }

    EA_DEBUGLN("Got UDP!");

    unsigned char packetBuffer[packetSize + 1];  // buffer to hold incoming udp packet
    espalexaUdp_.read(packetBuffer, packetSize);
    packetBuffer[packetSize] = 0;

    espalexaUdp_.flush();
    if (!discoverable_) {
      return;  // do not reply to M-SEARCH if not discoverable
    }

    const char* request = reinterpret_cast<const char*>(packetBuffer);
    if (strstr(request, "M-SEARCH") == nullptr) {
      return;
    }

    EA_DEBUGLN(request);
    if (strstr(request, "ssdp:disc") != nullptr &&    // short for "ssdp:discover"
        (strstr(request, "upnp:rootd") != nullptr ||  // short for "upnp:rootdevice"
         strstr(request, "ssdp:all") != nullptr || strstr(request, "asic:1") != nullptr))  // short for "device:basic:1"
    {
      EA_DEBUGLN("Responding search req...");
      respondToSearch();
    }
  }

  // Function only in WLED version of Espalexa, does not actually release memory for names
  void removeAllDevices() {
    currentDeviceCount_ = 0;
     }

  // returns device index or 0 on failure
  uint8_t addDevice(EspalexaDevice* d) {
    EA_DEBUG("Adding device ");
    EA_DEBUGLN((currentDeviceCount + 1));
    if (currentDeviceCount_ >= ESPALEXA_MAXDEVICES) {
      return 0;
    }
    if (d == nullptr) {
      return 0;
    }
    d->setId(currentDeviceCount_);
    devices_[currentDeviceCount_] = d;
    return ++currentDeviceCount_;
  }

  // brightness-only callback
  uint8_t addDevice(String deviceName, BrightnessCallbackFunction callback, uint8_t initialValue = 0) {
    EA_DEBUG("Constructing device ");
    EA_DEBUGLN((currentDeviceCount + 1));
    if (currentDeviceCount_ >= ESPALEXA_MAXDEVICES) {
      return 0;
    }
    auto* d = new EspalexaDevice(std::move(deviceName), callback, initialValue);
    return addDevice(d);
  }

  // brightness-only callback
  uint8_t addDevice(String deviceName, ColorCallbackFunction callback, uint8_t initialValue = 0) {
    EA_DEBUG("Constructing device ");
    EA_DEBUGLN((currentDeviceCount + 1));
    if (currentDeviceCount_ >= ESPALEXA_MAXDEVICES) {
      return 0;
    }
    auto* d = new EspalexaDevice(std::move(deviceName), callback, initialValue);
    return addDevice(d);
  }

  uint8_t addDevice(String deviceName, DeviceCallbackFunction callback,
                    EspalexaDeviceType t = EspalexaDeviceType::kDimmable, uint8_t initialValue = 0) {
    EA_DEBUG("Constructing device ");
    EA_DEBUGLN((currentDeviceCount + 1));
    if (currentDeviceCount_ >= ESPALEXA_MAXDEVICES) {
      return 0;
    }
    auto* d = new EspalexaDevice(std::move(deviceName), callback, t, initialValue);
    return addDevice(d);
  }

  void renameDevice(uint8_t id, const String& deviceName) {
    unsigned int index = id - 1;
    if (index < currentDeviceCount_) {
      devices_[index]->setName(deviceName);
    }
  }

// basic implementation of Philips hue api functions needed for basic Alexa control
#ifdef ESPALEXA_ASYNC
  bool handleAlexaApiCall(AsyncWebServerRequest* request) {
    server_     = request;         // copy request reference
    String req = request->url();  // body from global variable
    EA_DEBUGLN(request->contentType());
    if (request->hasParam("body", true))  // This is necessary, otherwise ESP crashes if there is no body
    {
      EA_DEBUG("BodyMethod2");
      body_ = request->getParam("body", true)->value();
    }
    EA_DEBUG("FinalBody: ");
    EA_DEBUGLN(body);
#else
  bool handleAlexaApiCall(String req, String body) {
#endif
    EA_DEBUG("URL: ");
    EA_DEBUGLN(req);
    EA_DEBUGLN("AlexaApiCall");
    if (req.indexOf("api") < 0) {
      return false;  // return if not an API call
    }
    EA_DEBUGLN("ok");

    if (body_.indexOf("devicetype") > 0)  // client wants a hue api username, we don't care and give static
    {
      EA_DEBUGLN("devType");
      body_ = "";
      server_->send(200, "application/json",
                   R"([{"success":{"username":"2BLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBGr"}}])");
      return true;
    }

    if ((req.indexOf("state") > 0) && (body_.length() > 0))  // client wants to control light
    {
      uint32_t devId = req.substring(req.indexOf("lights") + 7).toInt();
      EA_DEBUG("ls");
      EA_DEBUGLN(devId);
      unsigned idx = decodeLightKey(devId);
      EA_DEBUGLN(idx);
      char buf[50];
      snprintf(buf, sizeof(buf), R"([{"success":{"/lights/%u/state/": true}}])", devId);
      server_->send(200, "application/json", buf);
      if (idx >= currentDeviceCount_) {
        return true;  // return if invalid ID
      }
      EspalexaDevice* dev = devices_[idx];

      dev->setPropertyChanged(EspalexaDeviceProperty::kNone);

      if (body_.indexOf("false") > 0)  // OFF command
      {
        dev->setValue(0);
        dev->setPropertyChanged(EspalexaDeviceProperty::kOff);
        dev->doCallback();
        return true;
      }

      if (body_.indexOf("true") > 0)  // ON command
      {
        dev->setValue(dev->getLastValue());
        dev->setPropertyChanged(EspalexaDeviceProperty::kOn);
      }

      if (body_.indexOf("bri") > 0)  // BRIGHTNESS command
      {
        uint8_t briL = body_.substring(body_.indexOf("bri") + 5).toInt();
        if (briL == 255) {
          dev->setValue(255);
        } else {
          dev->setValue(briL + 1);
        }
        dev->setPropertyChanged(EspalexaDeviceProperty::kBri);
      }

      if (body_.indexOf("xy") > 0)  // COLOR command (XY mode)
      {
        dev->setColorXY(body_.substring(body_.indexOf("[") + 1).toFloat(),
                        body_.substring(body_.indexOf(",0") + 1).toFloat());
        dev->setPropertyChanged(EspalexaDeviceProperty::kXy);
      }

      if (body_.indexOf("hue") > 0)  // COLOR command (HS mode)
      {
        dev->setColor(body_.substring(body_.indexOf("hue") + 5).toInt(), body_.substring(body_.indexOf("sat") + 5).toInt());
        dev->setPropertyChanged(EspalexaDeviceProperty::kHs);
      }

      if (body_.indexOf("ct") > 0)  // COLOR TEMP command (white spectrum)
      {
        dev->setColor(body_.substring(body_.indexOf("ct") + 4).toInt());
        dev->setPropertyChanged(EspalexaDeviceProperty::kCt);
      }

      dev->doCallback();

#ifdef ESPALEXA_DEBUG
      if (dev->getLastChangedProperty() == EspalexaDeviceProperty::none) {
        EA_DEBUGLN("STATE REQ WITHOUT BODY (likely Content-Type issue #6)");
      }
#endif
      return true;
    }

    int pos = req.indexOf("lights");
    if (pos > 0)  // client wants light info
    {
      int devId = req.substring(pos + 7).toInt();
      EA_DEBUG("l");
      EA_DEBUGLN(devId);

      if (devId == 0)  // client wants all lights
      {
        EA_DEBUGLN("lAll");
        String jsonTemp = "{";
        for (int i = 0; i < currentDeviceCount_; i++) {
          jsonTemp += '"';
          jsonTemp += encodeLightKey(i);
          jsonTemp += '"';
          jsonTemp += ':';

          char buf[512];
          deviceJsonString(devices_[i], buf, sizeof(buf) - 1);
          jsonTemp += buf;
          if (i < currentDeviceCount_ - 1) {
            jsonTemp += ',';
          }
        }
        jsonTemp += '}';
        server_->send(200, "application/json", jsonTemp);
      } else  // client wants one light (devId)
      {
        EA_DEBUGLN(devId);
        unsigned int idx = decodeLightKey(devId);

        if (idx >= currentDeviceCount_) {
          idx = 0;  // send first device if invalid
        }
        if (currentDeviceCount_ == 0) {
          server_->send(200, "application/json", "{}");
          return true;
        }
        char buf[512];
        deviceJsonString(devices_[idx], buf, sizeof(buf) - 1);
        server_->send(200, "application/json", buf);
      }

      return true;
    }

    // we don't care about other api commands at this time and send empty JSON
    server_->send(200, "application/json", "{}");
    return true;
  }

  // set whether Alexa can discover any devices
  void setDiscoverable(bool d) {
    discoverable_ = d;
  }

  // get EspalexaDevice at specific index
  EspalexaDevice* getDevice(uint8_t index) {
    if (index >= currentDeviceCount_) {
      return nullptr;
    }
    return devices_[index];
  }

  // is an unique device ID
  String getEscapedMac() {
    return escapedMac_;
  }

  // convert brightness (0-255) to percentage
  static uint8_t toPercent(uint8_t bri) {
    uint16_t perc = bri * 100;
    return perc / 255;
  }

  ~Espalexa() = default;  // note: Espalexa is NOT meant to be destructed
};

#endif
