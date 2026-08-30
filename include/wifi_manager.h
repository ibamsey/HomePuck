/*
 * OpenRemote WiFi Manager Module
 * 
 * Handles WiFi connection, reconnection with exponential backoff,
 * BSSID-locked connections, and connection metrics tracking.
 * 
 * Configuration constants are in include/config.h.
 * See docs/wifi_client_guide.md for operational guide.
 * 
 * This file is part of OpenRemote.
 * Copyright (C) 2026 Ian Bamsey
 * 
 * OpenRemote is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * OpenRemote is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with OpenRemote. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

// ============================================================================
// WiFi Connection State
// ============================================================================

enum WiFiConnectionState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_RECONNECTING,
    WIFI_STATE_ERROR
};

// ============================================================================
// WiFi Metrics Structure
// ============================================================================

struct WiFiMetrics {
    // Counters
    int total_attempts;
    int successful_connections;
    int failed_connections;
    int disconnects;
    int reconnect_attempts;
    int reconnect_successes;
    
    // Timing (milliseconds)
    unsigned long total_connect_time;
    unsigned long min_connect_time;
    unsigned long max_connect_time;
    unsigned long last_connect_time;
    
    // Connection tracking
    unsigned long current_session_start;
    unsigned long longest_session;
    unsigned long total_connected_time;
    
    // RSSI tracking
    int8_t min_rssi;
    int8_t max_rssi;
    int32_t rssi_sum;
    int rssi_samples;
    
    // Methods
    void reset();
    float getSuccessRate();
    unsigned long getAverageConnectTime();
    int8_t getAverageRSSI();
    void recordSuccess(unsigned long connect_time, int8_t rssi);
    void recordFailure();
    void recordReconnectAttempt();
    void recordReconnectSuccess(unsigned long connect_time, int8_t rssi);
    void recordDisconnect();
    void recordRSSI(int8_t rssi);
};

// ============================================================================
// WiFi Manager Class
// ============================================================================

class WiFiManager {
public:
    WiFiManager();
    
    // Initialization
    void begin();
    void begin(const char* ssid, const char* password);
    
    // Connection management
    bool connect();
    void disconnect();
    void reconnect();
    
    // Main loop handler
    void loop();
    
    // Status
    bool isConnected();
    WiFiConnectionState getState();
    IPAddress getIP();
    int8_t getRSSI();
    uint8_t getChannel();
    String getMacAddress();
    
    // Connection health
    bool checkGatewayReachable();  // Test if gateway is actually reachable
    void forceReconnect();         // Force WiFi reconnect (for zombie state recovery)
    
    // Metrics
    WiFiMetrics& getMetrics();
    void printMetrics();
    void resetMetrics();
    
    // Stress test
    void runStressTest(int cycles = 10);
    
    // Callbacks
    typedef void (*ConnectionCallback)(WiFiConnectionState state, const char* message);
    void setConnectionCallback(ConnectionCallback callback);

    // Optional progress callback for low-overhead boot UI.
    // stage examples: "SCANNING", "TARGET_FOUND", "CONNECTING", "CONNECTED", "FAILED", "RECONNECTING"
    typedef void (*ProgressCallback)(const char* stage, const char* details, int progress_percent, int8_t rssi);
    void setProgressCallback(ProgressCallback callback);

private:
    // Configuration
    const char* _ssid;
    const char* _password;
    const char* _hostname;
    
    // State
    WiFiConnectionState _state;
    unsigned long _boot_time;
    unsigned long _connect_start_time;
    int _attempt_count;
    
    // BSSID locking
    uint8_t _bssid[6];
    uint8_t _channel;
    bool _bssid_captured;
    int8_t _target_rssi;
    
    // Reconnection state machine
    enum ReconnectState {
        RECONNECT_IDLE,
        RECONNECT_WAITING,
        RECONNECT_ATTEMPTING
    };
    ReconnectState _reconnect_state;
    int _reconnect_attempt_count;
    unsigned long _reconnect_next_time;
    unsigned long _reconnect_delay;
    
    // Metrics
    WiFiMetrics _metrics;
    
    // Callback
    ConnectionCallback _connection_callback;
    ProgressCallback _progress_callback;
    
    // Internal methods
    void configure();
    bool scanNetworks();
    bool attemptConnection(bool is_reconnect);
    void handleReconnection();
    void resetReconnectState();
    void logEvent(const char* event, const char* details = "");
    void reportProgress(const char* stage, const char* details, int progress_percent, int8_t rssi);
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
};

// Global instance
extern WiFiManager wifiManager;

#endif // WIFI_MANAGER_H
