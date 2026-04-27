// AsyncJson-v6.h
/*
  Original file at: https://github.com/baggior/ESPAsyncWebServer/blob/master/src/AsyncJson.h
  Only changes are ArduinoJson lib path and removed content-type check

  Async Response to use with ArduinoJson and AsyncWebServer
  Written by Andrew Melvin (SticilFace) with help from me-no-dev and BBlanchon.

  --------------------
  Async Request to use with ArduinoJson and AsyncWebServer
  Written by Arsène von Wyss (avonwyss)
*/
#ifndef ASYNC_JSON_H_
#define ASYNC_JSON_H_
#include <Print.h>

#include <utility>

#include "ArduinoJson-v6.h"

#define DYNAMIC_JSON_DOCUMENT_SIZE 16384

/*
 * Json Response
 * */

class ChunkPrint : public Print {
 private:
  uint8_t *_destination;
  size_t   to_skip_;
  size_t   _to_write;
  size_t   _pos{0};

 public:
  ChunkPrint(uint8_t *destination, size_t from, size_t len)
      : _destination(destination), to_skip_(from), _to_write(len) {
  }
  ~ChunkPrint() override = default;
  size_t write(uint8_t c) override {
    if (to_skip_ > 0) {
      to_skip_--;
      return 1;
    } if (_to_write > 0) {
      _to_write--;
      _destination[_pos++] = c;
      return 1;
    }
    return 0;
  }
  size_t write(const uint8_t *buffer, size_t size) override {
    return this->Print::write(buffer, size);
  }
};

class AsyncJsonResponse : public AsyncAbstractResponse {
 private:
  DynamicJsonDocument jsonBuffer_;

  JsonVariant root_;
  bool        isValid_;

 public:
  explicit AsyncJsonResponse(JsonDocument *ref, bool isArray = false) : _jsonBuffer(1), isValid_{false} {
    _code        = 200;
    _contentType = CONTENT_TYPE_JSON;
    if (isArray) {
      _root = ref->to<JsonArray>();
    } else {
      _root = ref->to<JsonObject>();
    }
  }

  explicit AsyncJsonResponse(size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE, bool isArray = false)
      : _jsonBuffer(maxJsonBufferSize), isValid_{false} {
    _code        = 200;
    _contentType = CONTENT_TYPE_JSON;
    if (isArray) {
      _root = _jsonBuffer.createNestedArray();
    } else {
      _root = _jsonBuffer.createNestedObject();
    }
  }

  ~AsyncJsonResponse() override = default;
  JsonVariant &getRoot() {
    return _root;
  }
  bool _sourceValid() const override {
    return isValid_;
  }
  size_t setLength() {
    _contentLength = measureJson(_root);

    if (_contentLength) {
      isValid_ = true;
    }
    return _contentLength;
  }

  size_t getSize() {
    return _root.size();
  }

  size_t _fillBuffer(uint8_t *data, size_t len) override {
    ChunkPrint dest(data, _sentLength, len);

    serializeJson(_root, dest);
    return len;
  }
};

using ArJsonRequestHandlerFunction = ;

class AsyncCallbackJsonWebHandler : public AsyncWebHandler {
 private:
 protected:
  const String                 uri_;
  WebRequestMethodComposite    _method{HTTP_POST | HTTP_PUT | HTTP_PATCH};
  ArJsonRequestHandlerFunction _onRequest;
  int                          contentLength_;
  const size_t                 maxJsonBufferSize_;
  int                          _maxContentLength{16384};

 public:
  AsyncCallbackJsonWebHandler(String uri, ArJsonRequestHandlerFunction onRequest,
                              size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE)
      : uri_(std::move(uri)),
        
        _onRequest(onRequest),
        maxJsonBufferSize_(maxJsonBufferSize)
        {
  }

  void setMethod(WebRequestMethodComposite method) {
    _method = method;
  }
  void setMaxContentLength(int maxContentLength) {
    _maxContentLength = maxContentLength;
  }
  void onRequest(ArJsonRequestHandlerFunction fn) {
    _onRequest = fn;
  }

  bool canHandle(AsyncWebServerRequest *request) final {
    if (!_onRequest) {
      return false;
    }

    if (!(_method & request->method())) {
      return false;
    }

    if ((uri_.length() != 0u) && (uri_ != request->url() && !request->url().startsWith(uri_ + "/"))) {
      return false;
    }

    request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) final {
    if (_onRequest) {
      if (request->_tempObject != nullptr) {
        _onRequest(request);
        return;
      }
      request->send(contentLength_ > _maxContentLength ? 413 : 400);
    } else {
      request->send(500);
    }
  }
  void handleUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
                            size_t len, bool final) final {
  }
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                          size_t total) final {
    if (_onRequest) {
      contentLength_ = total;
      if (total > 0 && request->_tempObject == nullptr && static_cast<int>(total) < _maxContentLength) {
        request->_tempObject = malloc(total);
      }
      if (request->_tempObject != nullptr) {
        memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
      }
    }
  }
  bool isRequestHandlerTrivial() final {
    return _onRequest == 0;
  }
};
#endif