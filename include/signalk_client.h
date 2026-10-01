#ifndef SIGNALK_CLIENT_H
#define SIGNALK_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_websocket_client.h>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "energy_data.h"
#include "environment_data.h"
#include "attitude_data.h"
#include "position_data.h"

enum SignalKState {
    SK_DISCONNECTED = 0,
    SK_CONNECTING,
    SK_CONNECTED,
    SK_RECONNECTING,
    SK_ERROR
};

struct SignalKValue {
    String path;
    float value;
    String source;
    unsigned long timestamp;
    bool valid;

    SignalKValue() : value(0.0f), timestamp(0), valid(false) {}
};

struct WindData {
    float aws_knots;
    float tws_knots;
    float sog_knots;
    float awa_degrees;
    float twd_degrees;
    float heading_degrees;
    bool aws_valid;
    bool tws_valid;
    bool sog_valid;
    bool awa_valid;
    bool twd_valid;
    bool heading_valid;
    unsigned long last_update;

    WindData() { reset(); }
    void reset() {
        aws_knots = 0.0f;
        tws_knots = 0.0f;
        sog_knots = 0.0f;
        awa_degrees = 0.0f;
        twd_degrees = 0.0f;
        heading_degrees = 0.0f;
        aws_valid = false;
        tws_valid = false;
        sog_valid = false;
        awa_valid = false;
        twd_valid = false;
        heading_valid = false;
        last_update = 0;
    }

    bool hasSpeed() const { return aws_valid || tws_valid; }
    bool hasDirection() const { return awa_valid || twd_valid; }

    float twdDialDegrees() const {
        if (!twd_valid) return 0.0f;
        if (!heading_valid) return twd_degrees;
        float rel = twd_degrees - heading_degrees;
        while (rel < 0.0f) rel += 360.0f;
        while (rel >= 360.0f) rel -= 360.0f;
        return rel;
    }
};

typedef std::function<void(const String& path, const SignalKValue& value)> SignalKDataCallback;
typedef std::function<void(SignalKState state, const String& message)> SignalKStateCallback;

class SignalKClient {
public:
    SignalKClient();
    ~SignalKClient();

    bool begin();
    void loop();

    bool connect();
    void disconnect();
    bool isConnected() const { return _state == SK_CONNECTED; }
    SignalKState getState() const { return _state; }
    const char* getStateString() const;

    WindData getWindData();
    bool isWindDataStale() const;
    EnergyData getEnergyData();
    bool isEnergyDataStale() const;
    EnvironmentData getEnvironmentData();
    bool isEnvironmentDataStale() const;
    AttitudeData getAttitudeData();
    bool isAttitudeDataStale() const;
    PositionData getPositionData();
    bool isPositionDataStale() const;

    void onDataReceived(SignalKDataCallback cb) { _data_callback = cb; }
    void onStateChange(SignalKStateCallback cb) { _state_callback = cb; }

private:
    esp_websocket_client_handle_t _ws_client;
    char _ws_url[96];
    SignalKState _state;
    unsigned long _last_connection_attempt;
    unsigned long _last_data_received;
    unsigned long _last_heap_log;
    unsigned long _reconnect_delay;
    int _connection_attempts;
    bool _subscription_pending;

    uint32_t _total_reconnections;
    uint32_t _total_disconnect_events;
    uint32_t _total_error_events;
    uint32_t _total_health_failures;

    std::vector<String> _subscribed_paths;
    SemaphoreHandle_t _data_mutex;
    WindData _wind;
    EnergyData _energy;
    EnvironmentData _environment;
    AttitudeData _attitude;
    PositionData _position;

    SignalKDataCallback _data_callback;
    SignalKStateCallback _state_callback;

    void _setState(SignalKState new_state, const String& message);
    void _releaseWebSocketClient();
    bool _checkHeapFloor(const char* reason);
    void _handleWebSocketEvent(int32_t event_id, void* event_data);
    void _handleMessage(const char* data, size_t len);
    void _processDeltaMessage(JsonArray updates);
    void _sendSubscription();
    void _processPendingSubscription();
    void _handleReconnection();
    void _handleErrorRecovery();
    void _checkConnectionHealth();
    void _storePathValue(const char* path, float value);
    void _storeAttitudeObject(JsonObjectConst obj, unsigned long now);
    void _storePositionObject(JsonObjectConst obj, unsigned long now);

    static void _staticEventHandler(void* handler_args, esp_event_base_t base,
                                    int32_t event_id, void* event_data);
    static SignalKClient* _instance;
};

extern SignalKClient signalkClient;

#endif // SIGNALK_CLIENT_H
