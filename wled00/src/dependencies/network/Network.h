#include <ETH.h>
#include <WiFi.h>

#ifndef Network_h
#define Network_h

class NetworkClass {
 public:
  IPAddress localIP();
  IPAddress subnetMask();
  IPAddress gatewayIP();
  void      localMAC(uint8_t* MAC);
  bool      isConnected();
  bool      isEthernet();
};

extern NetworkClass Network;

#endif