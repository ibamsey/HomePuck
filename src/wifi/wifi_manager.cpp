/*
 * OpenRemote WiFi Manager Implementation
 * 
 * Handles WiFi connection, reconnection with exponential backoff,
 * BSSID-locked connections, and connection metrics tracking.
 * 
 * See docs/wifi_client_guide.md for configuration and troubleshooting.
 * See docs/adr/001-wifi-connection-strategy.md for design decisions.
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

#include "wifi_manager.h"
#include "config.h"
#include <esp_wifi_types.h>
#include <esp_wifi.h>

// Global instance
WiFiManager wifiManager;

// Static reference for event handler
static WiFiManager* _instance = nullptr;

// ============================================================================
// WiFiMetrics Implementation
// ============================================================================

void WiFiMetrics::reset() {
    total_attempts = 0;
    successful_connections = 0;
    failed_connections = 0;
    disconnects = 0;
    reconnect_attempts = 0;
    reconnect_successes = 0;
    
    total_connect_time = 0;
    min_connect_time = UINT32_MAX;
    max_connect_time = 0;
    last_connect_time = 0;
    
    current_session_start = 0;
    longest_session = 0;
    total_connected_time = 0;
    
    min_rssi = 0;
    max_rssi = -128;
    rssi_sum = 0;
    rssi_samples = 0;
}

float WiFiMetrics::getSuccessRate() {
    if (total_attempts == 0) return 0.0f;
    return (float)successful_connections / (float)total_attempts * 100.0f;
}

unsigned long WiFiMetrics::getAverageConnectTime() {
    if (successful_connections == 0) return 0;
    return total_connect_time / successful_connections;
}

int8_t WiFiMetrics::getAverageRSSI() {
    if (rssi_samples == 0) return 0;
    return (int8_t)(rssi_sum / rssi_samples);
}

void WiFiMetrics::recordSuccess(unsigned long connect_time, int8_t rssi) {
    total_attempts++;
    successful_connections++;
    last_connect_time = connect_time;
    total_connect_time += connect_time;
    
    if (connect_time < min_connect_time) min_connect_time = connect_time;
    if (connect_time > max_connect_time) max_connect_time = connect_time;
    
    current_session_start = millis();
    recordRSSI(rssi);
}

void WiFiMetrics::recordFailure() {
    total_attempts++;
    failed_connections++;
}

void WiFiMetrics::recordReconnectAttempt() {
    reconnect_attempts++;
}

void WiFiMetrics::recordReconnectSuccess(unsigned long connect_time, int8_t rssi) {
    reconnect_successes++;
    recordSuccess(connect_time, rssi);
}

void WiFiMetrics::recordDisconnect() {
    disconnects++;
    if (current_session_start > 0) {
        unsigned long session_duration = millis() - current_session_start;
        total_connected_time += session_duration;
        if (session_duration > longest_session) {
            longest_session = session_duration;
        }
        current_session_start = 0;
    }
}

void WiFiMetrics::recordRSSI(int8_t rssi) {
    rssi_sum += rssi;
    rssi_samples++;
    if (rssi > max_rssi) max_rssi = rssi;
    if (rssi < min_rssi) min_rssi = rssi;
}

// ============================================================================
// WiFiManager Implementation
// ============================================================================

WiFiManager::WiFiManager() {
    _ssid = WIFI_SSID;
    _password = WIFI_PASSWORD;
    _hostname = WIFI_HOSTNAME;
    _state = WIFI_STATE_DISCONNECTED;
    _boot_time = 0;
    _connect_start_time = 0;
    _attempt_count = 0;
    _channel = 0;
    _bssid_captured = false;
    memset(_bssid, 0, 6);
    _target_rssi = -127;
    _reconnect_state = RECONNECT_IDLE;
    _reconnect_attempt_count = 0;
    _reconnect_next_time = 0;
    _reconnect_delay = WIFI_RECONNECT_DELAY_MS;
    _connection_callback = nullptr;
    _progress_callback = nullptr;
    _instance = this;
}

void WiFiManager::setProgressCallback(ProgressCallback callback) {
    _progress_callback = callback;
}

void WiFiManager::reportProgress(const char* stage, const char* details, int progress_percent, int8_t rssi) {
    if (_progress_callback) {
        _progress_callback(stage, details, progress_percent, rssi);
    }
}

void WiFiManager::begin() {
    begin(_ssid, _password);
}

void WiFiManager::begin(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
    _boot_time = millis();
    _metrics.reset();
    resetReconnectState();
    
    // Register event handler
    WiFi.onEvent(onWiFiEvent);
    
    // Configure WiFi
    configure();

#if DEBUG_WIFI
    Serial.println();
    Serial.println("[WiFi] ========== HomePuck WiFi Manager ==========");
    Serial.printf("[WiFi] SSID=%s hostname=%s\n", _ssid, _hostname);
    Serial.printf("[WiFi] MAC=%s connect_timeout=%dms max_retries=%d\n",
                  WiFi.macAddress().c_str(), WIFI_CONNECT_TIMEOUT_MS, WIFI_MAX_RECONNECT_ATTEMPTS);
    Serial.printf("[WiFi] reconnect_delay=%dms backoff=%.1fx max_delay=%dms\n",
                  WIFI_RECONNECT_DELAY_MS, WIFI_RECONNECT_BACKOFF_MULTIPLIER, WIFI_RECONNECT_MAX_DELAY_MS);
    Serial.println("[WiFi] Debug logging enabled (DEBUG_WIFI=1)");
    Serial.println("[WiFi] ============================================");
#endif
    
    // Scan for networks
    if (scanNetworks()) {
        // Connect
        connect();
    } else {
        logEvent("ERROR", "Target network not found");
        _state = WIFI_STATE_ERROR;
        if (_connection_callback) {
            _connection_callback(_state, "Target network not found");
        }
    }
}

static const char* wifiDisconnectReasonToString(uint8_t reason) {
    switch (reason) {
        // 802.11 reason codes (common subset)
        case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
        case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
        case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
        case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
        case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
        case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";

        // ESP-specific/extended reason codes (these are very useful for field debugging)
        case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
        default: return "UNKNOWN";
    }
}

void WiFiManager::configure() {
    logEvent("INIT_START", "Configuring WiFi...");
    
    WiFi.disconnect(true, true);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    WiFi.setAutoConnect(false);
    WiFi.setHostname(_hostname);
    
    logEvent("INIT_COMPLETE", "WiFi configured");
}

bool WiFiManager::scanNetworks() {
    logEvent("SCAN_START", "Scanning for networks...");
    reportProgress("SCANNING", "", 0, 0);
    unsigned long scan_start = millis();
    
    int n = WiFi.scanNetworks();
    unsigned long scan_duration = millis() - scan_start;
    
    char msg[128];
    snprintf(msg, sizeof(msg), "SCAN_DONE networks=%d duration=%lums", n, scan_duration);
    logEvent(msg);
    reportProgress("SCAN_DONE", msg, 20, 0);
    
    if (n == 0) {
        logEvent("SCAN_EMPTY", "No networks found - check RF environment / antenna");
        return false;
    }
    
    bool target_ssid_seen = false;
    
    // Log all networks and find target
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int8_t rssi = WiFi.RSSI(i);
        uint8_t* bssid = (uint8_t*)WiFi.BSSID(i);
        int channel = WiFi.channel(i);
        
        char scan_msg[256];
        snprintf(scan_msg, sizeof(scan_msg), 
            "SCAN_RESULT ssid=%s rssi=%d channel=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
            ssid.c_str(), rssi, channel,
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        logEvent(scan_msg);
        
        // Check for target network
        if (ssid == _ssid) {
            target_ssid_seen = true;
            _channel = channel;
            memcpy(_bssid, bssid, 6);
            _bssid_captured = true;
            _target_rssi = rssi;
            
            char target_msg[256];
            snprintf(target_msg, sizeof(target_msg),
                "TARGET_FOUND ssid=%s rssi=%d channel=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
                _ssid, rssi, _channel,
                _bssid[0], _bssid[1], _bssid[2], _bssid[3], _bssid[4], _bssid[5]);
            logEvent(target_msg);
            reportProgress("TARGET_FOUND", target_msg, 40, rssi);
        }
    }

    if (!target_ssid_seen) {
        char miss_msg[128];
        snprintf(miss_msg, sizeof(miss_msg),
                 "TARGET_MISSING ssid=%s not in scan (out of range or AP down?)", _ssid);
        logEvent(miss_msg);
    } else if (!_bssid_captured) {
        logEvent("TARGET_ERROR", "SSID seen but BSSID not captured");
    }
    
    return _bssid_captured;
}

bool WiFiManager::connect() {
    return attemptConnection(false);
}

bool WiFiManager::attemptConnection(bool is_reconnect) {
    _attempt_count++;
    _connect_start_time = millis();
    _state = is_reconnect ? WIFI_STATE_RECONNECTING : WIFI_STATE_CONNECTING;
    
    if (is_reconnect) {
        _metrics.recordReconnectAttempt();
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "CONNECT_START ssid=%s attempt=%d reconnect=%s", 
        _ssid, _attempt_count, is_reconnect ? "true" : "false");
    logEvent(msg);
    reportProgress(is_reconnect ? "RECONNECTING" : "CONNECTING", msg, 60, _target_rssi);

    // Log best-known target RSSI from the last scan (helps distinguish RF issues vs auth issues)
    {
        char rssi_msg[96];
        snprintf(rssi_msg, sizeof(rssi_msg), "TARGET_RSSI last_scan=%d dBm", (int)_target_rssi);
        logEvent(rssi_msg);
    }
    
    auto waitForConnect = [&]() -> bool {
        unsigned long start = millis();
        unsigned long last_status_log = 0;
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
            delay(500);
            
            unsigned long elapsed = millis() - start;
            if (elapsed - last_status_log >= 5000) {
                last_status_log = elapsed;
                wl_status_t status = WiFi.status();
                const char* status_name = "UNKNOWN";
                switch (status) {
                    case WL_IDLE_STATUS: status_name = "IDLE"; break;
                    case WL_NO_SSID_AVAIL: status_name = "NO_SSID_AVAIL"; break;
                    case WL_SCAN_COMPLETED: status_name = "SCAN_COMPLETED"; break;
                    case WL_CONNECTED: status_name = "CONNECTED"; break;
                    case WL_CONNECT_FAILED: status_name = "CONNECT_FAILED"; break;
                    case WL_CONNECTION_LOST: status_name = "CONNECTION_LOST"; break;
                    case WL_DISCONNECTED: status_name = "DISCONNECTED"; break;
                }
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg),
                    "CONNECT_WAIT status=%d (%s) elapsed=%lums rssi=%d",
                    status, status_name, elapsed, (int)WiFi.RSSI());
                logEvent(status_msg);
            }
        }
        return WiFi.status() == WL_CONNECTED;
    };

    // Small delay to let the radio settle after scan/configure before attempting connection.
    // This can help avoid AUTH_EXPIRE on some AP/client combinations.
    delay(500);

    // Use BSSID-locked connection if available (more reliable than SSID-only)
    if (_bssid_captured) {
        char bssid_msg[256];
        snprintf(bssid_msg, sizeof(bssid_msg),
            "CONNECT_METHOD bssid_locked channel=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
            _channel,
            _bssid[0], _bssid[1], _bssid[2], _bssid[3], _bssid[4], _bssid[5]);
        logEvent(bssid_msg);
        WiFi.begin(_ssid, _password, _channel, _bssid, true);
    } else {
        logEvent("CONNECT_METHOD", "standard");
        WiFi.begin(_ssid, _password);
    }

    bool connected = waitForConnect();
    
    unsigned long connect_duration = millis() - _connect_start_time;
    
    if (connected) {
        int8_t rssi = WiFi.RSSI();
        _state = WIFI_STATE_CONNECTED;
        
        if (is_reconnect) {
            _metrics.recordReconnectSuccess(connect_duration, rssi);
        } else {
            _metrics.recordSuccess(connect_duration, rssi);
        }
        
        char conn_msg[256];
        snprintf(conn_msg, sizeof(conn_msg),
            "CONNECT_SUCCESS duration=%lums rssi=%d ip=%s gateway=%s",
            connect_duration, rssi, 
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str());
        logEvent(conn_msg);
        reportProgress("CONNECTED", conn_msg, 100, rssi);
        
        if (_connection_callback) {
            _connection_callback(_state, "Connected");
        }
        
        return true;
    } else {
        _metrics.recordFailure();
        _state = WIFI_STATE_DISCONNECTED;

        wl_status_t final_status = WiFi.status();
        const char* final_name = "UNKNOWN";
        switch (final_status) {
            case WL_IDLE_STATUS: final_name = "IDLE"; break;
            case WL_NO_SSID_AVAIL: final_name = "NO_SSID_AVAIL"; break;
            case WL_SCAN_COMPLETED: final_name = "SCAN_COMPLETED"; break;
            case WL_CONNECTED: final_name = "CONNECTED"; break;
            case WL_CONNECT_FAILED: final_name = "CONNECT_FAILED"; break;
            case WL_CONNECTION_LOST: final_name = "CONNECTION_LOST"; break;
            case WL_DISCONNECTED: final_name = "DISCONNECTED"; break;
        }
        
        char fail_msg[192];
        snprintf(fail_msg, sizeof(fail_msg),
            "CONNECT_FAILED duration=%lums timeout=%dms final_status=%d (%s)",
            connect_duration, WIFI_CONNECT_TIMEOUT_MS, final_status, final_name);
        logEvent(fail_msg);
        reportProgress("FAILED", fail_msg, 0, _target_rssi);
        
        if (_connection_callback) {
            _connection_callback(_state, "Connection timeout");
        }
        
        return false;
    }
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    _state = WIFI_STATE_DISCONNECTED;
    _metrics.recordDisconnect();
    logEvent("DISCONNECT", "Manual disconnect");
}

void WiFiManager::reconnect() {
    disconnect();
    delay(1000);
    connect();
}

void WiFiManager::loop() {
    static unsigned long last_status_check = 0;
    static unsigned long last_rssi_sample = 0;
    static unsigned long last_desync_log = 0;
    unsigned long now = millis();
    
    // Detect manager/WiFi stack state mismatch (zombie WiFi)
    if (_state == WIFI_STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
        if (now - last_desync_log >= 5000) {
            last_desync_log = now;
            char desync_msg[128];
            snprintf(desync_msg, sizeof(desync_msg),
                     "STATE_DESYNC manager=CONNECTED wifi_status=%d", WiFi.status());
            logEvent(desync_msg);
        }
    }
    
    // Handle reconnection if needed
    if (_state != WIFI_STATE_CONNECTED || WiFi.status() != WL_CONNECTED) {
        handleReconnection();
    } else {
        if (_reconnect_state != RECONNECT_IDLE) {
            resetReconnectState();
        }
    }
    
    // Sample RSSI periodically
    if (_state == WIFI_STATE_CONNECTED && (now - last_rssi_sample >= 5000)) {
        last_rssi_sample = now;
        if (WiFi.status() == WL_CONNECTED) {
            _metrics.recordRSSI(WiFi.RSSI());
        }
    }
    
    // Status check
    if (now - last_status_check >= WIFI_STATUS_CHECK_INTERVAL_MS) {
        last_status_check = now;
        
        if (_state == WIFI_STATE_CONNECTED && WiFi.status() == WL_CONNECTED) {
            unsigned long uptime = (now - _boot_time) / 1000;
            unsigned long session = 0;
            if (_metrics.current_session_start > 0) {
                session = (now - _metrics.current_session_start) / 1000;
            }
            
            char status_msg[256];
            snprintf(status_msg, sizeof(status_msg),
                "STATUS connected uptime=%lus session=%lus rssi=%d ip=%s heap=%d",
                uptime, session, WiFi.RSSI(), 
                WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
            logEvent(status_msg);
        }
    }
}

void WiFiManager::handleReconnection() {
    unsigned long now = millis();
    
    switch (_reconnect_state) {
        case RECONNECT_IDLE:
            if (_state != WIFI_STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
                logEvent("RECONNECT_INIT", "Starting reconnection sequence");
                _reconnect_state = RECONNECT_WAITING;
                _reconnect_attempt_count = 0;
                _reconnect_delay = WIFI_RECONNECT_DELAY_MS;
                _reconnect_next_time = now + _reconnect_delay;
                
                if (_connection_callback) {
                    _connection_callback(WIFI_STATE_RECONNECTING, "Reconnecting");
                }
            }
            break;
            
        case RECONNECT_WAITING:
            if (now >= _reconnect_next_time) {
                logEvent("RECONNECT_READY", "Backoff elapsed, attempting now");
                _reconnect_state = RECONNECT_ATTEMPTING;
            }
            break;
            
        case RECONNECT_ATTEMPTING: {
            _reconnect_attempt_count++;
            
            char attempt_msg[128];
            snprintf(attempt_msg, sizeof(attempt_msg), 
                "RECONNECT_ATTEMPT attempt=%d/%d delay=%lums",
                _reconnect_attempt_count, WIFI_MAX_RECONNECT_ATTEMPTS, _reconnect_delay);
            logEvent(attempt_msg);
            
            if (attemptConnection(true)) {
                logEvent("RECONNECT_SUCCESS", "Reconnection successful");
                resetReconnectState();
            } else {
                if (_reconnect_attempt_count >= WIFI_MAX_RECONNECT_ATTEMPTS) {
                    logEvent("RECONNECT_EXHAUSTED", "Max attempts reached, rescanning...");
                    
                    // Rescan and retry
                    if (scanNetworks()) {
                        _reconnect_attempt_count = 0;
                        _reconnect_delay = WIFI_RECONNECT_DELAY_MS;
                    } else {
                        _reconnect_delay = WIFI_RECONNECT_MAX_DELAY_MS;
                    }
                } else {
                    // Exponential backoff
                    _reconnect_delay = min(
                        (unsigned long)(_reconnect_delay * WIFI_RECONNECT_BACKOFF_MULTIPLIER),
                        (unsigned long)WIFI_RECONNECT_MAX_DELAY_MS
                    );
                    
                    char backoff_msg[128];
                    snprintf(backoff_msg, sizeof(backoff_msg), 
                        "RECONNECT_BACKOFF next_attempt_in=%lums", _reconnect_delay);
                    logEvent(backoff_msg);
                }
                // Use fresh millis() — not `now` from function entry — because
                // attemptConnection() blocked for up to 15 s, making `now` stale.
                _reconnect_next_time = millis() + _reconnect_delay;
                _reconnect_state = RECONNECT_WAITING;
            }
            break;
        }
    }
}

void WiFiManager::resetReconnectState() {
    _reconnect_state = RECONNECT_IDLE;
    _reconnect_attempt_count = 0;
    _reconnect_delay = WIFI_RECONNECT_DELAY_MS;
    _reconnect_next_time = 0;
}

bool WiFiManager::isConnected() {
    return _state == WIFI_STATE_CONNECTED && WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::checkGatewayReachable() {
    if (!isConnected()) return false;
    
    IPAddress gateway = WiFi.gatewayIP();
    if (gateway == IPAddress(0, 0, 0, 0)) return false;
    
    // Try to create a TCP connection to gateway on a common port (80)
    // This is faster and more reliable than ICMP ping on ESP32
    WiFiClient testClient;
    testClient.setTimeout(2000);  // 2 second timeout
    
    bool reachable = testClient.connect(gateway, 80);
    testClient.stop();
    
    if (!reachable) {
        // Try port 3000 (SignalK)
        reachable = testClient.connect(gateway, 3000);
        testClient.stop();
    }
    
    if (!reachable) {
        char gw_msg[96];
        snprintf(gw_msg, sizeof(gw_msg), "GATEWAY_UNREACHABLE gateway=%s", gateway.toString().c_str());
        logEvent(gw_msg);
    }
    
    return reachable;
}

void WiFiManager::forceReconnect() {
    logEvent("FORCE_RECONNECT", "Zombie WiFi detected - forcing reconnection");
    
    // Force disconnect even if WiFi.status() shows connected
    WiFi.disconnect(true);
    delay(500);
    
    _state = WIFI_STATE_DISCONNECTED;
    _reconnect_state = RECONNECT_IDLE;
    _metrics.recordDisconnect();
    
    // Trigger reconnection on next loop()
}

WiFiConnectionState WiFiManager::getState() {
    return _state;
}

IPAddress WiFiManager::getIP() {
    return WiFi.localIP();
}

int8_t WiFiManager::getRSSI() {
    return WiFi.RSSI();
}

uint8_t WiFiManager::getChannel() {
    return WiFi.channel();
}

String WiFiManager::getMacAddress() {
    return WiFi.macAddress();
}

WiFiMetrics& WiFiManager::getMetrics() {
    return _metrics;
}

void WiFiManager::printMetrics() {
    Serial.println("\n========== CONNECTION METRICS ==========");
    Serial.printf("Total Attempts:       %d\n", _metrics.total_attempts);
    Serial.printf("Successful:           %d\n", _metrics.successful_connections);
    Serial.printf("Failed:               %d\n", _metrics.failed_connections);
    Serial.printf("Success Rate:         %.1f%%\n", _metrics.getSuccessRate());
    Serial.println("-----------------------------------------");
    Serial.printf("Disconnects:          %d\n", _metrics.disconnects);
    Serial.printf("Reconnect Attempts:   %d\n", _metrics.reconnect_attempts);
    Serial.printf("Reconnect Successes:  %d\n", _metrics.reconnect_successes);
    Serial.println("-----------------------------------------");
    Serial.printf("Avg Connect Time:     %lu ms\n", _metrics.getAverageConnectTime());
    Serial.printf("Min Connect Time:     %lu ms\n", _metrics.min_connect_time == UINT32_MAX ? 0 : _metrics.min_connect_time);
    Serial.printf("Max Connect Time:     %lu ms\n", _metrics.max_connect_time);
    Serial.printf("Last Connect Time:    %lu ms\n", _metrics.last_connect_time);
    Serial.println("-----------------------------------------");
    Serial.printf("Total Connected Time: %lu s\n", _metrics.total_connected_time / 1000);
    Serial.printf("Longest Session:      %lu s\n", _metrics.longest_session / 1000);
    Serial.println("-----------------------------------------");
    Serial.printf("Avg RSSI:             %d dBm\n", _metrics.getAverageRSSI());
    Serial.printf("Min RSSI:             %d dBm\n", _metrics.min_rssi);
    Serial.printf("Max RSSI:             %d dBm\n", _metrics.max_rssi);
    Serial.printf("RSSI Samples:         %d\n", _metrics.rssi_samples);
    Serial.println("-----------------------------------------");
    Serial.printf("Free Heap:            %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Min Free Heap:        %d bytes\n", ESP.getMinFreeHeap());
    Serial.println("=========================================\n");
}

void WiFiManager::resetMetrics() {
    _metrics.reset();
}

void WiFiManager::setConnectionCallback(ConnectionCallback callback) {
    _connection_callback = callback;
}

void WiFiManager::logEvent(const char* event, const char* details) {
#if DEBUG_WIFI
    unsigned long elapsed = millis() - _boot_time;
    Serial.printf("[WiFi][%08lu] %s", elapsed, event);
    if (strlen(details) > 0) {
        Serial.printf(" %s", details);
    }
    Serial.println();
#endif
}

void WiFiManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (!_instance) return;
    
    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:
            _instance->logEvent("EVENT=WIFI_READY");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_START:
            _instance->logEvent("EVENT=STA_START");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_STOP:
            _instance->logEvent("EVENT=STA_STOP");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        {
            _instance->logEvent("EVENT=STA_CONNECTED");

            // Log AP info as seen by ESP-IDF (channel, authmode, BSSID, etc.)
            wifi_ap_record_t ap = {};
            esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
            if (err == ESP_OK) {
                char ap_msg[256];
                snprintf(ap_msg, sizeof(ap_msg),
                         "AP_INFO ssid=%s channel=%u rssi=%d authmode=%u bssid=%02X:%02X:%02X:%02X:%02X:%02X",
                         (const char*)ap.ssid,
                         ap.primary,
                         ap.rssi,
                         (unsigned)ap.authmode,
                         ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
                _instance->logEvent(ap_msg);
            } else {
                char ap_err[96];
                snprintf(ap_err, sizeof(ap_err), "AP_INFO error=%d", (int)err);
                _instance->logEvent(ap_err);
            }
            break;
        }
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        {
            // Log detailed disconnect reason to diagnose auth/handshake/beacon timeouts, etc.
            uint8_t reason = info.wifi_sta_disconnected.reason;
            char msg[128];
            snprintf(msg, sizeof(msg), "EVENT=STA_DISCONNECTED reason=%u (%s)",
                     reason, wifiDisconnectReasonToString(reason));
            _instance->logEvent(msg);
            if (reason == WIFI_REASON_AUTH_EXPIRE) {
                _instance->logEvent("HINT",
                    "AUTH_EXPIRE stale AP association - deauth from AP or wait ~300s");
            }
            if (_instance->_state == WIFI_STATE_CONNECTED) {
                _instance->_metrics.recordDisconnect();
                _instance->_state = WIFI_STATE_DISCONNECTED;
                if (_instance->_connection_callback) {
                    _instance->_connection_callback(WIFI_STATE_DISCONNECTED, "Disconnected");
                }
            }
            break;
        }
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
            char msg[128];
            snprintf(msg, sizeof(msg), "EVENT=GOT_IP ip=%s",
                WiFi.localIP().toString().c_str());
            _instance->logEvent(msg);

            // Confirm AP info again after IP (sometimes differs vs STA_CONNECTED timing)
            wifi_ap_record_t ap = {};
            esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
            if (err == ESP_OK) {
                char ap_msg[256];
                snprintf(ap_msg, sizeof(ap_msg),
                         "AP_INFO_POST_IP ssid=%s channel=%u rssi=%d authmode=%u bssid=%02X:%02X:%02X:%02X:%02X:%02X",
                         (const char*)ap.ssid,
                         ap.primary,
                         ap.rssi,
                         (unsigned)ap.authmode,
                         ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
                _instance->logEvent(ap_msg);
            } else {
                char ap_err[96];
                snprintf(ap_err, sizeof(ap_err), "AP_INFO_POST_IP error=%d", (int)err);
                _instance->logEvent(ap_err);
            }
            _instance->_state = WIFI_STATE_CONNECTED;
            break;
        }
        
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            _instance->logEvent("EVENT=LOST_IP");
            if (_instance->_state == WIFI_STATE_CONNECTED) {
                _instance->_metrics.recordDisconnect();
                _instance->_state = WIFI_STATE_DISCONNECTED;
            }
            break;
            
        default:
        {
            char evt_msg[64];
            snprintf(evt_msg, sizeof(evt_msg), "EVENT=UNKNOWN id=%d", (int)event);
            _instance->logEvent(evt_msg);
            break;
        }
    }
}

void WiFiManager::runStressTest(int cycles) {
    Serial.println("\n############################################");
    Serial.printf("# WIFI STRESS TEST - %d CYCLES\n", cycles);
    Serial.println("############################################\n");
    
    _metrics.reset();
    
    for (int i = 1; i <= cycles; i++) {
        Serial.printf("\n--- CYCLE %d/%d ---\n", i, cycles);
        
        // Disconnect if connected
        if (WiFi.status() == WL_CONNECTED || _state == WIFI_STATE_CONNECTED) {
            logEvent("STRESS_DISCONNECT", "Disconnecting for test cycle");
            WiFi.disconnect(true);
            _state = WIFI_STATE_DISCONNECTED;
            delay(1000);
        }
        
        // Wait before connect
        logEvent("STRESS_WAIT", "Waiting before connection attempt");
        delay(2000);
        
        // Attempt connection
        logEvent("STRESS_CONNECT", "Attempting connection");
        bool success = attemptConnection(false);
        
        if (success) {
            Serial.printf("Cycle %d: SUCCESS - %lu ms\n", i, _metrics.last_connect_time);
            
            // Hold connection and sample RSSI
            logEvent("STRESS_HOLD", "Holding connection");
            unsigned long hold_start = millis();
            while (millis() - hold_start < 5000) {
                if (WiFi.status() == WL_CONNECTED) {
                    _metrics.recordRSSI(WiFi.RSSI());
                }
                delay(500);
            }
        } else {
            Serial.printf("Cycle %d: FAILED\n", i);
        }
        
        // Log interim metrics
        char metrics_msg[256];
        snprintf(metrics_msg, sizeof(metrics_msg),
            "METRICS attempts=%d success=%d fail=%d rate=%.1f%% avg_time=%lums heap=%d",
            _metrics.total_attempts, _metrics.successful_connections,
            _metrics.failed_connections, _metrics.getSuccessRate(),
            _metrics.getAverageConnectTime(), ESP.getFreeHeap());
        logEvent(metrics_msg);
    }
    
    Serial.println("\n############################################");
    Serial.println("# STRESS TEST COMPLETE");
    Serial.println("############################################");
    
    printMetrics();
    
    float rate = _metrics.getSuccessRate();
    Serial.println("\n============ TEST RESULT ============");
    if (rate >= 95.0f) {
        Serial.printf("PASSED - Success rate: %.1f%% (target: >= 95%%)\n", rate);
    } else {
        Serial.printf("FAILED - Success rate: %.1f%% (target: >= 95%%)\n", rate);
    }
    Serial.println("=====================================\n");
}
