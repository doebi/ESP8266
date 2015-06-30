#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "ip_addr.h"
#include "mem.h"
#include "base64.h"

#include "user_json.h"
#include "user_timer.h"
#include "user_events.h"
#include "user_config.h"
#include "user_webserver.h"
#include "user_webclient.h"
#include "user_long_poll.h"

void ICACHE_FLASH_ATTR user_event_reboot() {
#if EVENTS_DEBUG
	debug("EVENTS: Reboot\n");
#endif
	char status[WEBSERVER_MAX_VALUE];
	user_event_raise(NULL, json_status(status, ESP8266, REBOOTING, NULL));
	websocket_close_all(REBOOTING, NULL);
	long_poll_close_all();
}

void ICACHE_FLASH_ATTR user_event_connect() {
#if EVENTS_DEBUG
	debug("EVENTS: Station connected\n");
	debug("Free heap: %d\n\n", system_get_free_heap_size());
#endif
	char status[WEBSERVER_MAX_VALUE];
	user_event_raise(NULL, json_status(status, ESP8266, CONNECTED, NULL));
}

void ICACHE_FLASH_ATTR user_event_reconnect() {
#if EVENTS_DEBUG
	debug("EVENTS: Reconnect station\n");
#endif
	char status[WEBSERVER_MAX_VALUE];
	user_event_raise(NULL, json_status(status, ESP8266, RECONNECT, NULL));
	websocket_close_all(RECONNECT, NULL);
	long_poll_close_all();
}

void ICACHE_FLASH_ATTR user_event_build(char *event, char *url, char *data) {
	char url_quote[2] = "\"";
	
	if (url == NULL) {
		url_quote[0] = '\0';
	}
	
	os_sprintf(
		event,
		"{\"EventURL\" : %s%s%s, \"EventData\" : %s}",
		url_quote,
		url == NULL ? "null" : url,
		url_quote,
		data
	);
}

void ICACHE_FLASH_ATTR user_event_progress(uint8 progress) {
	LOCAL uint8 prev = 0;
	
	uint32 heap = system_get_free_heap_size();
	if (abs(progress - prev) < 3 || heap < 5000) {
		return;
	}
	
	prev = progress;
	char status[WEBSERVER_MAX_VALUE];
	char progress_str[WEBSERVER_MAX_VALUE];
	json_status(status, ESP8266, json_sprintf(progress_str, "Progress: %d\%", progress), NULL);
	
#if EVENTS_DEBUG
	debug("EVENTS: Progress [%d]\n", progress);
#endif
	char event[WEBSERVER_MAX_VALUE + os_strlen(webserver_chunk_url()) + os_strlen(status)];
	
	user_event_build(event, webserver_chunk_url(), status);
	websocket_send_message(EVENTS_URL, event, NULL);
}

void ICACHE_FLASH_ATTR user_websocket_event(char *url, char *data, struct espconn *pConnection) {
	char event[WEBSERVER_MAX_VALUE + os_strlen(url) + os_strlen(data)];
	
	user_event_build(event, url, data);
	
	websocket_send_message(url, data, pConnection);
	websocket_send_message(EVENTS_URL, event, pConnection);
}

void ICACHE_FLASH_ATTR user_event_raise(char *url, char *data) {
	uint16 event_size = 
		WEBSERVER_MAX_VALUE + 
		(url == NULL ? 0 : os_strlen(url)) + 
		(data == NULL ? 0 : os_strlen(data))
	;
	
	char event[event_size];
	user_event_build(event, url, data);
	
	if (url == NULL) {
		websocket_send_message(EVENTS_URL, data, NULL);
		long_poll_response(EVENTS_URL, data);
	} else {
		// WebSockets
		websocket_send_message(url, data, NULL);
		websocket_send_message(EVENTS_URL, event, NULL);
		
		// Long Polls
		long_poll_response(url, data);
		long_poll_response(EVENTS_URL, event);
	}
	
	// POST to IoT server
	if (user_config_events_websocket()) {
		webclient_socket(
			user_config_events_ssl(),
			user_config_events_user(),
			user_config_events_password(),
			user_config_events_server(),
			user_config_events_ssl() ?
				WEBSERVER_SSL_PORT
				:
				WEBSERVER_PORT
			,
			user_config_events_path(),
			event
		);
	} else {
		webclient_post(
			user_config_events_ssl(),
			user_config_events_user(),
			user_config_events_password(),
			user_config_events_server(),
			user_config_events_ssl() ?
				WEBSERVER_SSL_PORT
				:
				WEBSERVER_PORT
			,
			user_config_events_path(),
			event
		);
	}
}

void ICACHE_FLASH_ATTR events_handler(
	struct espconn *pConnection, 
	request_method method, 
	char *url, 
	char *data, 
	uint16 data_len, 
	uint32 content_len, 
	char *response,
	uint16 response_len
) {
	webserver_set_status(0);
}

void ICACHE_FLASH_ATTR user_events_init() {
	wifi_set_station_connected_callback(user_event_connect);
	webserver_register_handler_callback(EVENTS_URL, events_handler);
}