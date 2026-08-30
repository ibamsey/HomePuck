#include "signalk_client.h"
#include "wifi_manager.h"
#include <esp_system.h>
#include <string.h>

SignalKClient signalkClient;
SignalKClient* SignalKClient::_instance = nullptr;

static const float MS_TO_KNOTS = 1.94384f;
static const float RAD_TO_DEG_F = 57.2957795f;
static const unsigned long MIN_RECONNECT_DELAY_MS = 2000;

static float normalizeAngle(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

SignalKClient::SignalKClient()
    : _ws_client(nullptr)
    , _state(SK_DISCONNECTED)
    , _last_connection_attempt(0)
    , _last_data_received(0)
    , _last_heap_log(0)
    , _reconnect_delay(SIGNALK_RECONNECT_DELAY_MS)
    , _connection_attempts(0)
    , _subscription_pending(false)
    , _total_reconnections(0)
    , _total_disconnect_events(0)
    , _total_error_events(0)
    , _total_health_failures(0)
    , _data_callback(nullptr)
    , _state_callback(nullptr) {
    _ws_url[0] = '\0';
    _instance = this;
    _data_mutex = xSemaphoreCreateMutex();
}

SignalKClient::~SignalKClient() {
    disconnect();
    if (_data_mutex != nullptr) {
        vSemaphoreDelete(_data_mutex);
    }
    _instance = nullptr;
}

bool SignalKClient::begin() {
    Serial.println("[SIGNALK] Initializing HomePuck client...");
    snprintf(_ws_url, sizeof(_ws_url), "ws://%s:%d%s",
             SIGNALK_HOST, SIGNALK_PORT, SIGNALK_PATH);
    Serial.printf("[SIGNALK] Server: %s\n", _ws_url);

    _subscribed_paths.clear();
    _subscribed_paths.push_back("environment.wind.speedApparent");
    _subscribed_paths.push_back("environment.wind.angleApparent");
    _subscribed_paths.push_back("environment.wind.speedTrue");
    _subscribed_paths.push_back("environment.wind.speedOverGround");
    _subscribed_paths.push_back("environment.wind.directionTrue");
    _subscribed_paths.push_back("navigation.headingTrue");
    _subscribed_paths.push_back("navigation.speedOverGround");
    _subscribed_paths.push_back(SK_PATH_VICTRON_A_SOLAR);
    _subscribed_paths.push_back(SK_PATH_VICTRON_A_LOAD);
    _subscribed_paths.push_back(SK_PATH_VICTRON_B_SOLAR);
    _subscribed_paths.push_back(SK_PATH_VICTRON_B_LOAD);
    _subscribed_paths.push_back(SK_PATH_BATTERY_POWER);
    _subscribed_paths.push_back(SK_PATH_BATTERY_SOC);

    Serial.printf("[SIGNALK] %d paths subscribed\n", (int)_subscribed_paths.size());
    return true;
}

void SignalKClient::_releaseWebSocketClient() {
    if (_ws_client == nullptr) return;

    esp_websocket_client_handle_t client = _ws_client;
    _ws_client = nullptr;

#if DEBUG_SIGNALK
    Serial.printf("[SIGNALK] Releasing client heap_before=%lu connected=%d\n",
                  ESP.getFreeHeap(), esp_websocket_client_is_connected(client));
#endif

    // destroy-only: stop() on an already-closed client can double-free (ESP-IDF WS task)
    esp_websocket_client_destroy(client);

#if DEBUG_SIGNALK
    Serial.printf("[SIGNALK] Client released heap_after=%lu\n", ESP.getFreeHeap());
#endif
}

bool SignalKClient::_checkHeapFloor(const char* reason) {
    uint32_t heap = ESP.getFreeHeap();
    if (heap < SIGNALK_MIN_HEAP_BYTES) {
        Serial.printf("[SIGNALK] Heap too low (%lu < %d) at %s - restarting\n",
                      (unsigned long)heap, SIGNALK_MIN_HEAP_BYTES, reason);
        delay(100);
        esp_restart();
        return false;
    }
    return true;
}

bool SignalKClient::connect() {
    if (_state == SK_CONNECTED) return true;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SIGNALK] Cannot connect - WiFi not available");
        return false;
    }

    if (!_checkHeapFloor("connect")) return false;

    if (_ws_client != nullptr) {
        _releaseWebSocketClient();
    }

    _setState(SK_CONNECTING, "Connecting...");
    Serial.printf("[SIGNALK] Connecting to: %s (heap=%lu)\n", _ws_url, ESP.getFreeHeap());

    esp_websocket_client_config_t config = {};
    config.uri = _ws_url;
    config.disable_auto_reconnect = true;
    config.pingpong_timeout_sec = 10;

    _ws_client = esp_websocket_client_init(&config);
    if (_ws_client == nullptr) {
        _setState(SK_ERROR, "Client creation failed");
        return false;
    }

    esp_websocket_register_events(_ws_client, WEBSOCKET_EVENT_ANY, _staticEventHandler, this);

    esp_err_t err = esp_websocket_client_start(_ws_client);
    if (err != ESP_OK) {
        Serial.printf("[SIGNALK] ERROR: Connection failed: %s (heap=%lu)\n",
                      esp_err_to_name(err), ESP.getFreeHeap());
        if (_ws_client != nullptr) {
            esp_websocket_client_destroy(_ws_client);
            _ws_client = nullptr;
        }
        _setState(SK_ERROR, "Connection start failed");
        return false;
    }

    _last_connection_attempt = millis();
    _connection_attempts++;
    return true;
}

void SignalKClient::disconnect() {
    _subscription_pending = false;
    _releaseWebSocketClient();

    if (_state != SK_DISCONNECTED) {
        _setState(SK_DISCONNECTED, "Disconnected");
    }

    _connection_attempts = 0;
    _reconnect_delay = SIGNALK_RECONNECT_DELAY_MS;

    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _wind.reset();
        xSemaphoreGive(_data_mutex);
    }
}

const char* SignalKClient::getStateString() const {
    switch (_state) {
        case SK_DISCONNECTED: return "DISCONNECTED";
        case SK_CONNECTING: return "CONNECTING";
        case SK_CONNECTED: return "CONNECTED";
        case SK_RECONNECTING: return "RECONNECTING";
        case SK_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void SignalKClient::loop() {
    _handleErrorRecovery();
    _processPendingSubscription();
    _handleReconnection();
    _checkConnectionHealth();
}

WindData SignalKClient::getWindData() {
    WindData copy;
    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = _wind;
        xSemaphoreGive(_data_mutex);
    }
    return copy;
}

bool SignalKClient::isWindDataStale() const {
    if (_wind.last_update == 0) return true;
    return (millis() - _wind.last_update) > SIGNALK_DATA_STALE_THRESHOLD_MS;
}

EnergyData SignalKClient::getEnergyData() {
    EnergyData copy;
    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = _energy;
        xSemaphoreGive(_data_mutex);
    }
    return copy;
}

bool SignalKClient::isEnergyDataStale() const {
    if (_energy.last_update == 0) return true;
    return (millis() - _energy.last_update) > SIGNALK_DATA_STALE_THRESHOLD_MS;
}

void SignalKClient::_staticEventHandler(void* handler_args, esp_event_base_t base,
                                        int32_t event_id, void* event_data) {
    SignalKClient* self = static_cast<SignalKClient*>(handler_args);
    if (self) {
        self->_handleWebSocketEvent(event_id, event_data);
    }
}

void SignalKClient::_handleWebSocketEvent(int32_t event_id, void* event_data) {
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            Serial.printf("[SIGNALK] WebSocket connected heap=%lu\n", ESP.getFreeHeap());
            _setState(SK_CONNECTED, "Connected to SignalK server");
            _connection_attempts = 0;
            _reconnect_delay = SIGNALK_RECONNECT_DELAY_MS;
            _last_data_received = millis();
            _subscription_pending = true;
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            _total_disconnect_events++;
            Serial.printf("[SIGNALK] DISCONNECT #%lu uptime=%lus heap=%lu\n",
                          (unsigned long)_total_disconnect_events,
                          millis() / 1000, ESP.getFreeHeap());
            _last_connection_attempt = millis();
            _setState(SK_RECONNECTING, "Connection lost");
            _total_reconnections++;
            break;

        case WEBSOCKET_EVENT_ERROR:
            _total_error_events++;
            Serial.printf("[SIGNALK] ERROR #%lu uptime=%lus heap=%lu\n",
                          (unsigned long)_total_error_events,
                          millis() / 1000, ESP.getFreeHeap());
            _last_connection_attempt = millis();
            _setState(SK_RECONNECTING, "WebSocket error");
            _total_reconnections++;
            break;

        case WEBSOCKET_EVENT_CLOSED:
            _total_disconnect_events++;
            Serial.printf("[SIGNALK] CLOSED #%lu uptime=%lus heap=%lu\n",
                          (unsigned long)_total_disconnect_events,
                          millis() / 1000, ESP.getFreeHeap());
            _last_connection_attempt = millis();
            _setState(SK_RECONNECTING, "Connection closed");
            _total_reconnections++;
            break;

        case WEBSOCKET_EVENT_DATA:
            if (event_data != nullptr) {
                esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
                if (data->data_ptr != nullptr && data->data_len > 0) {
                    _handleMessage((const char*)data->data_ptr, data->data_len);
                }
            }
            break;

        default:
            break;
    }
}

void SignalKClient::_handleMessage(const char* data, size_t len) {
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, data, len)) {
        return;
    }

    if (doc["updates"].is<JsonArray>()) {
        _processDeltaMessage(doc["updates"].as<JsonArray>());
    }
}

void SignalKClient::_processDeltaMessage(JsonArray updates) {
    _last_data_received = millis();

    for (JsonObject update : updates) {
        JsonArray values = update["values"].as<JsonArray>();
        for (JsonObject value_obj : values) {
            if (!value_obj["path"].is<const char*>()) continue;

            const char* path = value_obj["path"];
            JsonVariant value = value_obj["value"];

            if (!(value.is<float>() || value.is<double>() || value.is<int>())) {
                continue;
            }

            _storePathValue(path, value.as<float>());
        }
    }
}

void SignalKClient::_storePathValue(const char* path, float value) {
    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    unsigned long now = millis();

    if (strcmp(path, "environment.wind.speedApparent") == 0) {
        _wind.aws_knots = value * MS_TO_KNOTS;
        _wind.aws_valid = true;
        _wind.last_update = now;
    } else if (strcmp(path, "environment.wind.speedOverGround") == 0) {
        _wind.tws_knots = value * MS_TO_KNOTS;
        _wind.tws_valid = true;
        _wind.last_update = now;
    } else if (strcmp(path, "environment.wind.speedTrue") == 0) {
        if (!_wind.tws_valid) {
            _wind.tws_knots = value * MS_TO_KNOTS;
            _wind.tws_valid = true;
            _wind.last_update = now;
        }
    } else if (strcmp(path, "environment.wind.angleApparent") == 0) {
        _wind.awa_degrees = normalizeAngle(value * RAD_TO_DEG_F);
        _wind.awa_valid = true;
        _wind.last_update = now;
    } else if (strcmp(path, "environment.wind.directionTrue") == 0) {
        _wind.twd_degrees = normalizeAngle(value * RAD_TO_DEG_F);
        _wind.twd_valid = true;
        _wind.last_update = now;
#if DEBUG_SIGNALK
        static unsigned long last_twd_log = 0;
        if (now - last_twd_log > 5000) {
            Serial.printf("[SIGNALK] TWD=%.0f deg (dial=%.0f)\n", _wind.twd_degrees, _wind.twdDialDegrees());
            last_twd_log = now;
        }
#endif
    } else if (strcmp(path, "navigation.headingTrue") == 0) {
        _wind.heading_degrees = normalizeAngle(value * RAD_TO_DEG_F);
        _wind.heading_valid = true;
        _wind.last_update = now;
    } else if (strcmp(path, "navigation.speedOverGround") == 0) {
        _wind.sog_knots = value * MS_TO_KNOTS;
        _wind.sog_valid = true;
        _wind.last_update = now;
    } else if (strcmp(path, SK_PATH_VICTRON_A_SOLAR) == 0) {
        _energy.fixed_solar_w = value;
        _energy.fixed_solar_valid = true;
        _energy.last_update = now;
    } else if (strcmp(path, SK_PATH_VICTRON_A_LOAD) == 0) {
        _energy.boat_load_w = value;
        _energy.boat_load_valid = true;
        _energy.last_update = now;
    } else if (strcmp(path, SK_PATH_VICTRON_B_SOLAR) == 0) {
        _energy.portable_solar_w = value;
        _energy.portable_solar_valid = true;
        _energy.last_update = now;
    } else if (strcmp(path, SK_PATH_VICTRON_B_LOAD) == 0) {
        _energy.fridge_load_w = value;
        _energy.fridge_load_valid = true;
        _energy.last_update = now;
    } else if (strcmp(path, SK_PATH_BATTERY_POWER) == 0) {
        _energy.battery_power_w = value;
        _energy.battery_power_valid = true;
        _energy.last_update = now;
    } else if (strcmp(path, SK_PATH_BATTERY_SOC) == 0) {
        _energy.battery_soc_pct = (value <= 1.0f) ? value * 100.0f : value;
        _energy.battery_soc_valid = true;
        _energy.last_update = now;
    }

    xSemaphoreGive(_data_mutex);
}

void SignalKClient::_sendSubscription() {
    if (_state != SK_CONNECTED || _subscribed_paths.empty() || _ws_client == nullptr) {
        return;
    }

    StaticJsonDocument<1536> doc;
    doc["context"] = "vessels.self";

    JsonArray subscribe = doc["subscribe"].to<JsonArray>();
    for (const String& path : _subscribed_paths) {
        JsonObject sub = subscribe.add<JsonObject>();
        sub["path"] = path;
        sub["period"] = SIGNALK_SUBSCRIPTION_PERIOD_MS;
        sub["minPeriod"] = SIGNALK_MIN_SUBSCRIPTION_PERIOD_MS;
    }

    char json[1024];
    size_t len = serializeJson(doc, json, sizeof(json));
    if (len == 0) {
        Serial.println("[SIGNALK] ERROR: Subscription JSON serialize failed");
        return;
    }
    if (len >= sizeof(json) - 1) {
        Serial.printf("[SIGNALK] ERROR: Subscription JSON too large (%u bytes)\n", (unsigned)len);
        return;
    }

    esp_websocket_client_send_text(_ws_client, json, len, portMAX_DELAY);
    Serial.printf("[SIGNALK] Subscription sent (%d paths, %u bytes)\n",
                  (int)_subscribed_paths.size(), (unsigned)len);
}

void SignalKClient::_processPendingSubscription() {
    if (!_subscription_pending) return;
    if (_state != SK_CONNECTED || _ws_client == nullptr) return;

    _subscription_pending = false;
    _sendSubscription();
}

void SignalKClient::_handleReconnection() {
    if (_state != SK_RECONNECTING) return;

    unsigned long now = millis();
    unsigned long actual_delay = max(_reconnect_delay, MIN_RECONNECT_DELAY_MS);

    if (now - _last_connection_attempt < actual_delay) {
        return;
    }

    if (_connection_attempts >= SIGNALK_MAX_RECONNECT_ATTEMPTS) {
        Serial.println("[SIGNALK] Max attempts reached - resetting backoff");
        _connection_attempts = 0;
        _reconnect_delay = SIGNALK_RECONNECT_MAX_DELAY_MS;
        _last_connection_attempt = now;
        if (!_checkHeapFloor("max reconnect attempts")) return;
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (_connection_attempts >= 3 && !wifiManager.checkGatewayReachable()) {
        Serial.println("[SIGNALK] Zombie WiFi detected - forcing reconnect");
        wifiManager.forceReconnect();
        _connection_attempts = 0;
        _reconnect_delay = SIGNALK_RECONNECT_DELAY_MS;
        _last_connection_attempt = now;
        return;
    }

    _last_connection_attempt = now;

    Serial.printf("[SIGNALK] Reconnect attempt %d/%d heap=%lu\n",
                  _connection_attempts + 1, SIGNALK_MAX_RECONNECT_ATTEMPTS,
                  ESP.getFreeHeap());

    _releaseWebSocketClient();

    if (!connect()) {
        _reconnect_delay = min(_reconnect_delay * 2, (unsigned long)SIGNALK_RECONNECT_MAX_DELAY_MS);
    }
}

void SignalKClient::_handleErrorRecovery() {
    if (_state != SK_ERROR) return;

    unsigned long now = millis();
    unsigned long actual_delay = max(_reconnect_delay, MIN_RECONNECT_DELAY_MS);
    if (now - _last_connection_attempt < actual_delay) {
        return;
    }

    if (!_checkHeapFloor("SK_ERROR recovery")) return;

    Serial.printf("[SIGNALK] Recovering from SK_ERROR heap=%lu\n", ESP.getFreeHeap());
    _connection_attempts = 0;
    _reconnect_delay = SIGNALK_RECONNECT_DELAY_MS;
    _last_connection_attempt = now;
    _setState(SK_RECONNECTING, "Recovering from error");
}

void SignalKClient::_checkConnectionHealth() {
    if (_state != SK_CONNECTED) return;

    unsigned long now = millis();

    if (now - _last_heap_log >= SIGNALK_HEAP_LOG_INTERVAL_MS) {
        _last_heap_log = now;
        unsigned long data_age = (_last_data_received > 0) ? (now - _last_data_received) : 0;
        Serial.printf("[SIGNALK] Health: data_age=%lums heap=%lu fails=%lu disc=%lu err=%lu\n",
                      data_age, ESP.getFreeHeap(),
                      (unsigned long)_total_health_failures,
                      (unsigned long)_total_disconnect_events,
                      (unsigned long)_total_error_events);
    }

    if (_last_data_received == 0) return;

    unsigned long data_age = now - _last_data_received;
    if (data_age > 0x7FFFFFFF) return;

    if (data_age > SIGNALK_CONNECTION_TIMEOUT_MS) {
        _total_health_failures++;
        Serial.printf("[SIGNALK] Health check failed #%lu: no data for %lu ms heap=%lu\n",
                      (unsigned long)_total_health_failures, data_age, ESP.getFreeHeap());

        _releaseWebSocketClient();
        _setState(SK_RECONNECTING, "Connection timeout - no data received");
        _last_data_received = 0;
        _last_connection_attempt = now;
        _total_reconnections++;
    }
}

void SignalKClient::_setState(SignalKState new_state, const String& message) {
    if (_state != new_state) {
        _state = new_state;
        Serial.printf("[SIGNALK] State: %s - %s\n", getStateString(), message.c_str());
        if (_state_callback) {
            _state_callback(_state, message);
        }
    }
}
