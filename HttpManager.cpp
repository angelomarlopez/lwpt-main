#include "HttpManager.hpp"
#include "esp_log.h"
#include "string.h"

#include "Job.hpp"

static const char* TAG = "HTTP MANAGER";

HttpManager::HttpManager( const char* hostname ) : server( NULL ), hostname( hostname ) {}

esp_err_t HttpManager::handle_ping_request( httpd_req_t* req ) {
  // ESP_LOGI( TAG, "Ping request received, responding with Pong" );
  httpd_resp_send( req, "{\"status\":200}", HTTPD_RESP_USE_STRLEN );
  return ESP_OK;
}

esp_err_t HttpManager::handle_start_request( httpd_req_t* req ) {
  ESP_LOGI( TAG, "Start Job request received, responding with Job Information" );
  Job& job = Job::get_instance();
  job.start_new();

  httpd_resp_send( req, "STARTING", HTTPD_RESP_USE_STRLEN );
  return ESP_OK;
}

void HttpManager::start() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if ( httpd_start( &server, &config ) == ESP_OK ) {
    httpd_uri_t ping_URI = {
      .uri = "/ping",
      .method = HTTP_GET,
      .handler = handle_ping_request,
      .user_ctx = NULL
    };
    httpd_register_uri_handler( server, &ping_URI );

    httpd_uri_t job_start_URI = {
      .uri = "/start",
      .method = HTTP_GET,
      .handler = handle_start_request,
      .user_ctx = this
    };
    httpd_register_uri_handler( server, &job_start_URI );

    ESP_LOGI( TAG, "HTTP Server Started." );
  } else {
    ESP_LOGE( TAG, "Failed to start HTTP Server." );
  }
}
