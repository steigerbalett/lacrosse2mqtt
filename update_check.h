#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <time.h>

// GitHub Repository Info
#define GITHUB_REPO_OWNER "steigerbalett"
#define GITHUB_REPO_NAME "lacrosse2mqtt"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/releases/latest"

// DigiCert Global Root CA
// Gültig bis: 10.11.2031
const char* github_root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n" \
"QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n" \
"CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n" \
"nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n" \
"43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n" \
"T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n" \
"gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n" \
"BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n" \
"TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n" \
"DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n" \
"hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n" \
"06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n" \
"PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n" \
"YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n" \
"CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n" \
"-----END CERTIFICATE-----\n";

struct UpdateInfo {
    bool available;
    String latestVersion;
    String currentVersion;
    String downloadUrl;
    String releaseNotes;
    String publishedAt;
    int fileSize;
    String errorMessage;
    bool certBundleFailed;
    bool requiresUserConfirmation;
};

UpdateInfo updateInfo;
bool updateCheckInProgress = false;
bool updateInstallInProgress = false;
int updateProgress = 0;

// Hilfsfunktion: Prüfe ob NTP synchronisiert ist
bool isNTPSynced() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;
    }
    
    // Prüfe ob das Jahr plausibel ist (zwischen 2020 und 2040)
    int year = timeinfo.tm_year + 1900;
    return (year >= 2020 && year <= 2040);
}

// Hilfsfunktion: Gebe aktuelle Zeit als String zurück
String getCurrentTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time not available";
    }
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// Funktion: Prüfe auf neue Version
bool checkForUpdate(bool forceInsecure = false) {
    Serial.println("\n========== UPDATE CHECK START ==========");
    
    // WiFi Check
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        updateInfo.errorMessage = "WiFi not connected";
        return false;
    }
    Serial.println("WiFi connected");
    Serial.println("SSID: " + WiFi.SSID());
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Memory Check
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.println("Free heap: " + String(freeHeap) + " bytes");
    if (freeHeap < 40000) {
        Serial.println("ERROR: Insufficient memory for HTTPS (need ~40KB)");
        updateInfo.errorMessage = "Insufficient memory: " + String(freeHeap) + " bytes";
        return false;
    }
    
    // DNS Check
    Serial.println("Resolving api.github.com...");
    IPAddress githubIP;
    if (!WiFi.hostByName("api.github.com", githubIP)) {
        Serial.println("ERROR: DNS resolution failed");
        updateInfo.errorMessage = "DNS resolution failed";
        return false;
    }
    Serial.println("DNS resolved to: " + githubIP.toString());
    
    // NTP Check
    bool ntpOk = isNTPSynced();
    if (!ntpOk) {
        Serial.println("WARNING: System time not synchronized!");
        Serial.println("Current time: " + getCurrentTimeString());
        Serial.println("Forcing insecure mode due to time sync issue");
        forceInsecure = true;
    } else {
        Serial.println("System time synchronized: " + getCurrentTimeString());
    }
    
    updateCheckInProgress = true;
    updateInfo.errorMessage = "";
    updateInfo.certBundleFailed = false;
    updateInfo.requiresUserConfirmation = false;
    
    // Stack-Variable statt Heap-Allokation
    WiFiClientSecure client;
    
    if (!forceInsecure) {
        client.setCACert(github_root_ca);
        Serial.println("Using certificate validation");
    } else {
        client.setInsecure();
        Serial.println("Using insecure mode (no certificate validation)");
    }
    
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    http.setReuse(false);
    
    Serial.println("\nConnecting to GitHub API...");
    Serial.println("URL: " + String(GITHUB_API_URL));
    
    if (!http.begin(client, GITHUB_API_URL)) {
        Serial.println("ERROR: http.begin() failed");
        updateInfo.errorMessage = "Failed to initialize HTTP client";
        updateCheckInProgress = false;
        return false;
    }
    
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-LaCrosse2MQTT");
    
    Serial.println("Sending GET request...");
    Serial.println("Free heap before request: " + String(ESP.getFreeHeap()));
    
    int httpCode = http.GET();
    
    Serial.println("HTTP Response Code: " + String(httpCode));
    Serial.println("Free heap after request: " + String(ESP.getFreeHeap()));
    
    if (httpCode < 0) {
        Serial.println("ERROR: Connection failed!");
        Serial.println("Error: " + http.errorToString(httpCode));
        
        // Wenn mit Zertifikat fehlgeschlagen, automatisch mit insecure versuchen
        if (!forceInsecure) {
            Serial.println("\nRetrying with insecure mode...");
            http.end();
            updateCheckInProgress = false;
            return checkForUpdate(true);  // Rekursiver Aufruf mit forceInsecure=true
        } else {
            updateInfo.errorMessage = "Connection failed: " + http.errorToString(httpCode);
            http.end();
            updateCheckInProgress = false;
            Serial.println("========== UPDATE CHECK END ==========\n");
            return false;
        }
    }
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Response received (" + String(payload.length()) + " bytes)");
        Serial.println("Parsing JSON...");
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            updateInfo.latestVersion = doc["tag_name"].as<String>();
            updateInfo.releaseNotes = doc["body"].as<String>();
            updateInfo.publishedAt = doc["published_at"].as<String>();
            updateInfo.currentVersion = String(LACROSSE2MQTT_VERSION);
            
            Serial.println("JSON parsed successfully");
            Serial.println("Current version: " + updateInfo.currentVersion);
            Serial.println("Latest version:  " + updateInfo.latestVersion);
            
            // Suche nach .bin Asset
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();
                if (name.endsWith(".bin")) {
                    updateInfo.downloadUrl = asset["browser_download_url"].as<String>();
                    updateInfo.fileSize = asset["size"].as<int>();
                    Serial.println("Firmware file: " + name);
                    Serial.println("Size: " + String(updateInfo.fileSize) + " bytes");
                    Serial.println("URL: " + updateInfo.downloadUrl);
                    break;
                }
            }
            
            // Vergleiche Versionen
            if (updateInfo.latestVersion == updateInfo.currentVersion) {
                updateInfo.available = false;
                updateInfo.errorMessage = "No update available";
                Serial.println("\nSoftware is up to date");
            } else if (!updateInfo.downloadUrl.isEmpty()) {
                updateInfo.available = true;
                Serial.println("\nUPDATE AVAILABLE!");
                Serial.println(updateInfo.currentVersion + " -> " + updateInfo.latestVersion);
            } else {
                updateInfo.available = false;
                updateInfo.errorMessage = "No .bin file found in release";
                Serial.println("\nNo .bin file found in latest release");
            }
            
            http.end();
            updateCheckInProgress = false;
            Serial.println("========== UPDATE CHECK END ==========\n");
            return true;
        } else {
            Serial.println("JSON parse error: " + String(error.c_str()));
            updateInfo.errorMessage = "JSON parse error";
        }
    } else if (httpCode == 403) {
        Serial.println("GitHub API rate limit exceeded");
        updateInfo.errorMessage = "Rate limit exceeded. Try again later.";
    } else if (httpCode == 404) {
        Serial.println("Repository or release not found");
        updateInfo.errorMessage = "Repository not found";
    } else {
        Serial.println("HTTP Error: " + String(httpCode));
        updateInfo.errorMessage = "HTTP Error: " + String(httpCode);
    }
    
    http.end();
    updateCheckInProgress = false;
    Serial.println("========== UPDATE CHECK END ==========\n");
    return false;
}

// Funktion: Installiere Update
bool installUpdate(bool forceInsecure = false) {
    if (updateInfo.downloadUrl.isEmpty()) {
        Serial.println("No download URL available");
        return false;
    }
    
    Serial.println("\n========== FIRMWARE UPDATE START ==========");
    Serial.println("Downloading from: " + updateInfo.downloadUrl);
    Serial.println("Expected size: " + String(updateInfo.fileSize) + " bytes");
    
    updateInstallInProgress = true;
    updateProgress = 0;
    
    // Memory Check
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.println("Free heap: " + String(freeHeap) + " bytes");
    if (freeHeap < 40000) {
        Serial.println("ERROR: Insufficient memory for download");
        updateInstallInProgress = false;
        return false;
    }
    
    // Stack-Variable statt Heap-Allokation
    WiFiClientSecure client;
    
    // Immer insecure für Download verwenden (GitHub redirects zu objects.githubusercontent.com)
    client.setInsecure();
    Serial.println("Using insecure mode for firmware download (GitHub CDN redirect)");
    
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(60000);
    http.setReuse(false);
    
    if (!http.begin(client, updateInfo.downloadUrl)) {
        Serial.println("ERROR: Failed to initialize download");
        updateInstallInProgress = false;
        return false;
    }
    
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    
    Serial.println("Starting download...");
    int httpCode = http.GET();
    
    Serial.println("Download HTTP Code: " + String(httpCode));
    
    if (httpCode < 0) {
        Serial.println("ERROR: Download connection failed!");
        Serial.println("Error: " + http.errorToString(httpCode));
        http.end();
        updateInstallInProgress = false;
        return false;
    }
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        int contentLength = http.getSize();
        Serial.println("Download started");
        Serial.println("Content-Length: " + String(contentLength) + " bytes");
        
        if (contentLength > 0) {
            bool canBegin = Update.begin(contentLength);
            
            if (canBegin) {
                WiFiClient *stream = http.getStreamPtr();
                size_t written = 0;
                uint8_t buff[512] = { 0 };
                
                Serial.println("Writing firmware...");
                unsigned long lastPrint = 0;
                
                while (http.connected() && (written < contentLength)) {
                    size_t available = stream->available();
                    
                    if (available) {
                        int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
                        written += Update.write(buff, c);
                        updateProgress = (written * 100) / contentLength;
                        
                        // Zeige Fortschritt alle 5%
                        if (updateProgress >= lastPrint + 5) {
                            Serial.printf("Progress: %d%% (%d / %d bytes)\n", 
                                        updateProgress, written, contentLength);
                            lastPrint = updateProgress;
                        }
                    }
                    delay(1);
                }
                
                Serial.println("Progress: 100% (" + String(written) + " / " + String(contentLength) + " bytes)");
                
                if (Update.end()) {
                    Serial.println("Firmware written successfully!");
                    
                    if (Update.isFinished()) {
                        Serial.println("Update completed successfully!");
                        Serial.println("\n========== REBOOTING IN 3 SECONDS ==========\n");
                        
                        http.end();
                        updateInstallInProgress = false;
                        
                        delay(3000);
                        ESP.restart();
                        return true;
                    } else {
                        Serial.println("Update not finished properly");
                    }
                } else {
                    Serial.println("Update Error: " + String(Update.getError()));
                    Serial.print("Error details: ");
                    Update.printError(Serial);
                }
            } else {
                Serial.println("Not enough space for update");
                Serial.println("Required: " + String(contentLength) + " bytes");
                Serial.println("Available: " + String(ESP.getFreeSketchSpace()) + " bytes");
            }
        } else {
            Serial.println("Invalid content length");
        }
    } else if (httpCode == 404) {
        Serial.println("Firmware file not found (404)");
    } else {
        Serial.println("HTTP Error: " + String(httpCode));
    }
    
    http.end();
    updateInstallInProgress = false;
    Serial.println("========== FIRMWARE UPDATE FAILED ==========\n");
    return false;
}

#endif