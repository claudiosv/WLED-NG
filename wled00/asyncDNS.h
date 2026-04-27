#pragma once
/*
  asyncDNS.h - wrapper class for asynchronous DNS lookups using lwIP
  by @dedehai, C++ improvements & hardening by @willmmiles
*/

#include <Arduino.h>
#include <lwip/dns.h>
#include <lwip/err.h>

#include <atomic>
#include <memory>

class AsyncDNS {
  // C++14 shim
#if __cplusplus < 201402L
  // Really simple C++11 shim for non-array case; implementation from cppreference.com
  template <class T, class... Args>
  static std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }
#endif

 public:
  // note: passing the IP as a pointer to query() is not implemented because it is not thread-safe without mutexes
  //       with the IDF V4 bug external error handling is required anyway or dns can just stay stuck
  enum class result {
    kIdle,
    kBusy,
    kSuccess,
    kError
  };

  // non-blocking query function to start DNS lookup
  static std::shared_ptr<AsyncDNS> query(const char* hostname, std::shared_ptr<AsyncDNS> current = {}) {
    if (!current || (current->_status == result::kBusy)) {
      current.reset(new AsyncDNS());
    }

#if __cplusplus >= 201402L
    using std::make_unique;
#endif

    std::unique_ptr<std::shared_ptr<AsyncDNS>> callback_state = make_unique<std::shared_ptr<AsyncDNS>>(current);
    if (!callback_state) {
      return {};
    }

    current->_status = result::kBusy;
    err_t err        = dns_gethostbyname(hostname, &current->_raw_addr, dns_callback, callback_state.get());
    if (err == ERR_OK) {
      current->_status = result::kSuccess;  // result already in cache
    } else if (err == ERR_INPROGRESS) {
      callback_state.release();  // belongs to the callback now
    } else {
      current->_status = result::kError;
      current->_errorcount++;
    }
    return current;
  }

  // get the IP once Success is returned
  IPAddress getIP() const {
    if (_status != result::Success) {
      return IPAddress(0, 0, 0, 0);
    }
#ifdef ARDUINO_ARCH_ESP32
    return IPAddress(raw_addr_.u_addr.ip4.addr);
#else
    return IPAddress(_raw_addr.addr);
#endif
  }

  void reset() {
    errorcount_ = 0;
  }  // reset status and error count
  result status() {
    return _status;
  }
  uint16_t getErrorCount() const {
    return errorcount_;
  }

 private:
  ip_addr_t           raw_addr_;
  std::atomic<result> status_{result::Idle};
  uint16_t            errorcount_ = 0;

  AsyncDNS() {};  // may not be explicitly instantiated - use query()

  // callback for dns_gethostbyname(), called when lookup is complete or timed out
  static void dns_callback(const char*  /*name*/, const ip_addr_t* ipaddr, void* arg) {
    std::shared_ptr<AsyncDNS>* instance_ptr = reinterpret_cast<std::shared_ptr<AsyncDNS>*>(arg);
    AsyncDNS&                  instance     = **instance_ptr;
    if (ipaddr) {
      instance.raw_addr_ = *ipaddr;
      instance._status   = result::kSuccess;
    } else {
      instance._status = result::kError;  // note: if query timed out (~5s), DNS lookup is broken until WiFi connection
                                         // is reset (IDF V4 bug)
      instance.errorcount_++;
    }
    delete instance_ptr;
  }
};
