#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include "esp_http_client.h"
#include <string>

struct HttpResult {
  int status = -1;
  std::string body;
  esp_err_t transport = ESP_FAIL;
};

class HttpClient {
public:
  static HttpClient& get_instance();

  void init();
  HttpResult send_post(const std::string& url, 
                       const std::string& payload, 
                       const std::string& content_type );
  void cleanup();
private:
  HttpClient();
  ~HttpClient();
  HttpClient( const HttpClient& ) = delete;
  HttpClient& operator=( const HttpClient& ) = delete;

  esp_http_client_handle_t client;
  std::string default_url;
};

#endif
