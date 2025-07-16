#include "HttpManager.hpp"
#include "esp_log.h"
#include "string.h"

#include "Job.hpp"

static const char* TAG = "HTTP MANAGER";

HttpManager::HttpManager( const char* hostname ) : server( NULL ), hostname( hostname ) {}

esp_err_t HttpManager::handle_root_request( httpd_req_t* req ) {
  ESP_LOGI( TAG, "Ping request received, responding with Pong" );
  httpd_resp_send( req, "PONG", HTTPD_RESP_USE_STRLEN );
  return ESP_OK;
}

esp_err_t HttpManager::handle_post_request( httpd_req_t* req ) {
  auto* instance = static_cast<HttpManager*>( req->user_ctx );
  if ( !instance ) return ESP_FAIL;

  char buffer[ 100 ];
  int received = httpd_req_recv( req, buffer, sizeof( buffer ) - 1 );
  if ( received <= 0 ) {
    httpd_resp_send_500( req );
    return ESP_FAIL;
  }

  buffer[ received ] = '\0';
  ESP_LOGI( TAG, "Received JSON: %s", buffer );

  cJSON* result = cJSON_Parse( buffer );
  if ( result == NULL ) {
    httpd_resp_send_500( req );
    return ESP_FAIL;
  }

  cJSON* jobID = cJSON_GetObjectItem( result, "id" );
  cJSON* jobGoal = cJSON_GetObjectItem( result, "goal" );

  int newID = jobID->valueint;
  int newGoal = jobGoal->valueint;

  ESP_LOGI( TAG, "Setting New Goal: %d", newGoal );

  Job::get_instance().set( 0, newGoal, 0 ); 
  /* Job& job = Job::get_instance(); 
  job.punchGoal = newGoal;
  job.punches = 0;
  job.totalCount = 0;
  */
  

  ESP_LOGI ( TAG, "Received id: %d, Goal: %d", newID, newGoal );

  httpd_resp_set_type( req, "application/json" );
  httpd_resp_send( req, "{\"status\": \"received\"}", HTTPD_RESP_USE_STRLEN );
  cJSON_Delete( result );
  return ESP_OK;
}

void HttpManager::start() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if ( httpd_start( &server, &config ) == ESP_OK ) {
    httpd_uri_t rootURI = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = handle_root_request,
      .user_ctx = NULL
    };
    httpd_register_uri_handler( server, &rootURI );

    httpd_uri_t postURI = {
      .uri = "/update",
      .method = HTTP_POST,
      .handler = handle_post_request,
      .user_ctx = this
    };
    httpd_register_uri_handler( server, &postURI );

    ESP_LOGI( TAG, "HTTP Server Started." );
  } else {
    ESP_LOGE( TAG, "Failed to start HTTP Server." );
  }
}
