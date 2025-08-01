#include "HttpClient.hpp"
#include "esp_log.h"
#include "string.h"

static const char* TAG = "HTTP CLIENT";

HttpClient::HttpClient() : client( nullptr ), default_url( "" ) {}

HttpClient::~HttpClient() {
  cleanup();
}

HttpClient& HttpClient::get_instance() {
  static HttpClient instance;
  return instance;
}

void HttpClient::init() {
  if ( client != nullptr ) return;

  esp_http_client_config_t config = {};
  config.url = "http://localhost.local/";
  config.method = HTTP_METHOD_POST;
  config.keep_alive_enable = false;
  config.timeout_ms = 10000;
  config.disable_auto_redirect = true;

  client = esp_http_client_init( &config );
}

void HttpClient::cleanup() {
  if ( client ) {
    esp_http_client_cleanup( client );
    client = nullptr;
  }
}

std::string HttpClient::send_post( const std::string& url, const std::string& payload, const std::string& content_type ) {
  if ( !client ) init();

  const int max_retries = 5;
  const int delay_ms = 1000;

  for ( int attempt = 1; attempt <= max_retries; ++attempt ) {
    esp_http_client_set_url( client, url.c_str() );
    esp_http_client_set_method( client, HTTP_METHOD_POST );
    esp_http_client_set_header( client, "Content-Type", content_type.c_str() );
    esp_http_client_set_header( client, "Connection", "close" );
    esp_http_client_set_post_field( client, payload.c_str(), payload.length() );

    esp_err_t err = esp_http_client_perform( client );
    if ( err == ESP_OK ) {
      char buffer[ 512 ];
      int read_len = esp_http_client_read_response( client, buffer, sizeof( buffer ) - 1 );
      if ( read_len >= 0 ) {
        buffer[ read_len ] = 0;
        return std::string( buffer );
      } else {
        return "Failed to read response.";
      }
    } else {
      ESP_LOGE( TAG, "Attempt %d: HTTP POST failed: %s\n", attempt, esp_err_to_name( err ) );
      if ( attempt < max_retries ) {
        vTaskDelay( pdMS_TO_TICKS( delay_ms ) );
      } else {
        return "HTTP POST failed after retries: " + std::string( esp_err_to_name( err ) );
      }
    }
  }

  return "Unexpected error in send_post";
}

/*
HttpClient::HttpClient( const std::string& serverURL ) : serverURL( serverURL ) {}

esp_err_t HttpClient::sendPostRequest( const std::string& jsonData ) {
  ESP_LOGI( TAG, "Preparing to send data: %s", jsonData.c_str() );

  esp_http_client_config_t config = {
    .url = serverURL.c_str(),
    .method = HTTP_METHOD_POST
  };

  esp_http_client_handle_t client = esp_http_client_init( &config );
  if ( !client ) {
    ESP_LOGE( TAG, "Failed to Initialize HTTP Client." );
    return ESP_FAIL;
  }

  esp_http_client_set_header( client, "Content-Type", "application/json" );
  esp_http_client_set_post_field( client, jsonData.c_str(), jsonData.length() );

  esp_err_t err = esp_http_client_perform( client );
  if ( err == ESP_OK ) {
    int statusCode = esp_http_client_get_status_code( client );
    ESP_LOGI( TAG, "%s -> %s", serverURL.c_str(), jsonData.c_str() );
    ESP_LOGI( TAG, "HTTP POST status = %d", statusCode );

    char responseBuffer[ 100 ];
    int contentLength = esp_http_client_read( client, responseBuffer, sizeof( responseBuffer ) - 1 );
    if ( contentLength > 0 ) {
      responseBuffer[ contentLength ] = '\0';
      ESP_LOGI( TAG, "Response: %s", responseBuffer );
    }
  } else {
    ESP_LOGE( TAG, "HTTP POST Request Failed: %s", esp_err_to_name( err ) );
  }

  esp_http_client_cleanup( client );
  return err;
}
*/
