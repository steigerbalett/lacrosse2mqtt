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
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        updateInfo.errorMessage = "WiFi not connected";
        return false;
    }
    
    // Prüfe NTP-Synchronisation
    bool ntpOk = isNTPSynced();
    if (!ntpOk && !forceInsecure) {
        Serial.println("WARNING: System time not synchronized!");
        Serial.println("Current system time: " + getCurrentTimeString());
        Serial.println("Certificate validation will likely fail.");
    } else if (ntpOk) {
        Serial.println("System time OK: " + getCurrentTimeString());
    }
    
    updateCheckInProgress = true;
    updateInfo.errorMessage = "";
    updateInfo.certBundleFailed = false;
    updateInfo.requiresUserConfirmation = false;
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        bool useCertBundle = true;
        
        if (!forceInsecure) {
            // Versuche das eingebaute CA Bundle zu nutzen
            // In ESP32 Core 3.x: attach_ssl_client() nutzt automatisch das Bundle
            client->setCACert(NULL);  // NULL = nutzt eingebautes Mozilla CA Bundle
            Serial.println("Using built-in Mozilla CA Bundle for certificate validation");
            
            if (!ntpOk) {
                Serial.println("WARNING: Certificate validation may fail due to incorrect system time");
            }
        } else {
            Serial.println("User confirmed: Using insecure connection (no certificate validation)");
            client->setInsecure();
            useCertBundle = false;
        }
        
        HTTPClient http;
        
        Serial.println("Checking for updates...");
        Serial.println("URL: " + String(GITHUB_API_URL));
        
        if (http.begin(*client, GITHUB_API_URL)) {
            http.addHeader("Accept", "application/vnd.github+json");
            http.addHeader("User-Agent", "LaCrosse2MQTT-Updater");
            http.setTimeout(10000);
            
            int httpCode = http.GET();
            
            // Falls mit Zertifikat fehlgeschlagen
            if (httpCode < 0 && useCertBundle && !forceInsecure) {
                Serial.println("ERROR: Connection with certificate validation failed!");
                Serial.println("HTTP Error Code: " + String(httpCode));
                
                // Bessere Fehlerdiagnose
                if (!ntpOk) {
                    Serial.println("ROOT CAUSE: System time not synchronized via NTP");
                    Serial.println("Current system time: " + getCurrentTimeString());
                    updateInfo.errorMessage = "Certificate validation failed: System time not synchronized. Please wait for NTP sync.";
                } else {
                    Serial.println("ROOT CAUSE: Certificate validation failed despite correct system time");
                    Serial.println("Possible reasons: CA bundle not available, network issues, or certificate mismatch");
                    updateInfo.errorMessage = "Certificate validation failed. This may be due to missing CA bundle in firmware.";
                }
                
                Serial.println("User confirmation required to proceed without certificate validation");
                updateInfo.certBundleFailed = true;
                updateInfo.requiresUserConfirmation = true;
                
                http.end();
                delete client;
                updateCheckInProgress = false;
                return false;
            }
            
            Serial.println("HTTP Code: " + String(httpCode));
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                Serial.println("Response received, parsing JSON...");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error) {
                    updateInfo.latestVersion = doc["tag_name"].as<String>();
                    updateInfo.releaseNotes = doc["body"].as<String>();
                    updateInfo.publishedAt = doc["published_at"].as<String>();
                    updateInfo.currentVersion = String(LACROSSE2MQTT_VERSION);
                    
                    // Suche nach .bin Asset
                    JsonArray assets = doc["assets"];
                    for (JsonObject asset : assets) {
                        String name = asset["name"].as<String>();
                        if (name.endsWith(".bin")) {
                            updateInfo.downloadUrl = asset["browser_download_url"].as<String>();
                            updateInfo.fileSize = asset["size"].as<int>();
                            break;
                        }
                    }
                    
                    // Vergleiche Versionen
                    if (updateInfo.latestVersion == updateInfo.currentVersion) {
                        updateInfo.available = false;
                        updateInfo.errorMessage = "Kein Update verfügbar";
                        Serial.println("Update check completed:");
                        Serial.println("Current: " + updateInfo.currentVersion);
                        Serial.println("Latest: " + updateInfo.latestVersion);
                        Serial.println("No update available");
                    } else if (!updateInfo.downloadUrl.isEmpty()) {
                        updateInfo.available = true;
                        Serial.println("Update check completed:");
                        Serial.println("Current: " + updateInfo.currentVersion);
                        Serial.println("Latest: " + updateInfo.latestVersion);
                        Serial.println("Update available!");
                    } else {
                        updateInfo.available = false;
                        updateInfo.errorMessage = "No .bin file found in release";
                        Serial.println("No .bin file found in latest release");
                    }
                    
                    http.end();
                    delete client;
                    updateCheckInProgress = false;
                    return true;
                } else {
                    Serial.println("JSON parse error: " + String(error.c_str()));
                    updateInfo.errorMessage = "JSON parse error";
                }
            } else if (httpCode == 403) {
                Serial.println("GitHub API rate limit exceeded");
                updateInfo.errorMessage = "Rate limit exceeded. Try again later.";
            } else {
                Serial.println("HTTP Error: " + String(httpCode));
                updateInfo.errorMessage = "HTTP Error: " + String(httpCode);
            }
            
            http.end();
        } else {
            Serial.println("Unable to connect to GitHub API");
            updateInfo.errorMessage = "Unable to connect to GitHub API";
        }
        
        delete client;
    } else {
        Serial.println("Unable to create secure client");
        updateInfo.errorMessage = "Unable to create secure client";
    }
    
    updateCheckInProgress = false;
    return false;
}

// Funktion: Installiere Update
bool installUpdate(bool forceInsecure = false) {
    if (updateInfo.downloadUrl.isEmpty()) {
        Serial.println("No download URL available");
        return false;
    }
    
    updateInstallInProgress = true;
    updateProgress = 0;
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        if (!forceInsecure) {
            client->setCACert(NULL);  // NULL = nutzt eingebautes Mozilla CA Bundle
            Serial.println("Using built-in Mozilla CA Bundle for secure firmware download");
            
            if (!isNTPSynced()) {
                Serial.println("WARNING: System time not synchronized - certificate validation may fail");
            }
        } else {
            Serial.println("User confirmed: Downloading firmware without certificate validation");
            Serial.println("SECURITY WARNING: Server identity will not be verified!");
            client->setInsecure();
        }
        
        HTTPClient http;
        
        Serial.println("Starting firmware download...");
        Serial.println("URL: " + updateInfo.downloadUrl);
        
        if (http.begin(*client, updateInfo.downloadUrl)) {
            http.setTimeout(30000);
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            
            int httpCode = http.GET();
            
            // Falls mit Certificate Bundle fehlgeschlagen
            if (httpCode < 0 && !forceInsecure) {
                Serial.println("ERROR: Firmware download with certificate validation failed!");
                Serial.println("HTTP Error Code: " + String(httpCode));
                
                if (!isNTPSynced()) {
                    Serial.println("ROOT CAUSE: System time not synchronized");
                }
                
                http.end();
                delete client;
                updateInstallInProgress = false;
                return false;
            }
            
            Serial.println("Download HTTP Code: " + String(httpCode));
            
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                int contentLength = http.getSize();
                Serial.println("Content-Length: " + String(contentLength));
                
                if (contentLength > 0) {
                    bool canBegin = Update.begin(contentLength);
                    
                    if (canBegin) {
                        WiFiClient *stream = http.getStreamPtr();
                        size_t written = 0;
                        uint8_t buff[128] = { 0 };
                        
                        Serial.println("Starting update...");
                        
                        while (http.connected() && (written < contentLength)) {
                            size_t available = stream->available();
                            
                            if (available) {
                                int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
                                written += Update.write(buff, c);
                                updateProgress = (written * 100) / contentLength;
                                
                                if (written % 10240 == 0) {
                                    Serial.printf("Progress: %d%% (%d / %d bytes)\n", updateProgress, written, contentLength);
                                }
                            }
                            delay(1);
                        }
                        
                        if (Update.end()) {
                            Serial.println("Update successfully completed!");
                            if (Update.isFinished()) {
                                Serial.println("Rebooting in 3 seconds...");
                                http.end();
                                delete client;
                                updateInstallInProgress = false;
                                delay(3000);
                                ESP.restart();
                                return true;
                            } else {
                                Serial.println("Update not finished");
                            }
                        } else {
                            Serial.println("Update Error: " + String(Update.getError()));
                        }
                    } else {
                        Serial.println("Not enough space for update");
                    }
                } else {
                    Serial.println("Invalid content length");
                }
            } else {
                Serial.println("HTTP Error: " + String(httpCode));
            }
            
            http.end();
        }
        
        delete client;
    }
    
    updateInstallInProgress = false;
    return false;
}

#endif