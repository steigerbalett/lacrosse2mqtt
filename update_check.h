#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>

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
};

UpdateInfo updateInfo;
bool updateCheckInProgress = false;
bool updateInstallInProgress = false;
int updateProgress = 0;

// Funktion: Prüfe auf neue Version
bool checkForUpdate() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        updateInfo.errorMessage = "WiFi not connected";
        return false;
    }
    
    updateCheckInProgress = true;
    updateInfo.errorMessage = "";
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        // Verwende das Certificate Bundle für bessere Kompatibilität
        client->setCACertBundle(ca_cert_bundle_start);
        
        HTTPClient http;
        
        Serial.println("Checking for updates...");
        Serial.println("URL: " + String(GITHUB_API_URL));
        
        // Verwende HTTPS mit Certificate Bundle
        if (http.begin(*client, GITHUB_API_URL)) {
            http.addHeader("Accept", "application/vnd.github+json");
            http.addHeader("User-Agent", "LaCrosse2MQTT-Updater");
            http.setTimeout(10000); // 10 Sekunden Timeout
            
            int httpCode = http.GET();
            
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
                        // ✓ NEU: Gleiche Version erkannt
                        updateInfo.available = false;
                        updateInfo.errorMessage = "Kein Update verfügbar";
                        Serial.println("Update check completed:");
                        Serial.println("Current: " + updateInfo.currentVersion);
                        Serial.println("Latest: " + updateInfo.latestVersion);
                        Serial.println("Kein Update verfügbar");
                    } else if (!updateInfo.downloadUrl.isEmpty()) {
                        // Update verfügbar
                        updateInfo.available = true;
                        Serial.println("Update check completed:");
                        Serial.println("Current: " + updateInfo.currentVersion);
                        Serial.println("Latest: " + updateInfo.latestVersion);
                        Serial.println("Update available!");
                    } else {
                        // Keine .bin Datei gefunden
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
bool installUpdate() {
    if (updateInfo.downloadUrl.isEmpty()) {
        Serial.println("No download URL available");
        return false;
    }
    
    updateInstallInProgress = true;
    updateProgress = 0;
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        // Verwende das Certificate Bundle
        client->setCACertBundle(ca_cert_bundle_start);
        
        HTTPClient http;
        
        Serial.println("Starting firmware download...");
        Serial.println("URL: " + updateInfo.downloadUrl);
        
        if (http.begin(*client, updateInfo.downloadUrl)) {
            http.setTimeout(30000); // 30 Sekunden
            
            int httpCode = http.GET();
            
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