#ifndef HTTP_MANAGER_HPP
#define HTTP_MANAGER_HPP

#include "esp_http_server.h"
#include "cJSON.h"

#include "Job.hpp"

class HttpManager {
public:
  HttpManager( const char* hostname );
  void start();
private:
  httpd_handle_t server;
  const char* hostname;

  static esp_err_t handle_root_request( httpd_req_t* req );
  static esp_err_t handle_post_request( httpd_req_t* req );
};

#endif
