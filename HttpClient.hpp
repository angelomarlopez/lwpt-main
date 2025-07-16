#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include "esp_http_client.h"
#include <string>

class HttpClient {
public:
  static HttpClient& get_instance();

  void init(); // Initialize persistent connection
  std::string send_post( const std::string& url, const std::string& payload, const std::string& content_type );
  void cleanup();

  // HttpClient( const std::string& serverURL );
  // esp_err_t sendPostRequest( const std::string& jsonData );
private:
  HttpClient();
  ~HttpClient();
  HttpClient( const HttpClient& ) = delete;
  HttpClient& operator=( const HttpClient& ) = delete;

  esp_http_client_handle_t client;
  std::string default_url;
  //std::string serverURL;
};

#endif
