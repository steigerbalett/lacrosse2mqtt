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
    bool certBundleFailed;
    bool requiresUserConfirmation;
};

UpdateInfo updateInfo;
bool updateCheckInProgress = false;
bool updateInstallInProgress = false;
int updateProgress = 0;
bool allowInsecureMode = false;  // Benutzer muss explizit zustimmen

// Funktion: Prüfe auf neue Version
bool checkForUpdate(bool forceInsecure = false) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        updateInfo.errorMessage = "WiFi not connected";
        return false;
    }
    
    updateCheckInProgress = true;
    updateInfo.errorMessage = "";
    updateInfo.certBundleFailed = false;
    updateInfo.requiresUserConfirmation = false;
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        bool useCertBundle = true;
        
        // Wenn Benutzer forceInsecure nicht erlaubt hat, versuche Certificate Bundle
        if (!forceInsecure) {
            // Versuche das Certificate Bundle zu laden
            // In ESP32 Arduino Core 3.x ist das Bundle eingebaut
            #ifdef ESP_IDF_VERSION_MAJOR
                #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
                    // Für neuere ESP-IDF Versionen (ESP32 Core 3.x+)
                    client->setCACert(NULL); // Nutzt das eingebaute Bundle
                    Serial.println("Using built-in certificate bundle for secure connection");
                #else
                    // Fallback für ältere Versionen
                    Serial.println("WARNING: Certificate bundle not available in this ESP32 core version!");
                    Serial.println("Falling back to insecure connection");
                    client->setInsecure();
                    useCertBundle = false;
                #endif
            #else
                // Wenn ESP_IDF_VERSION nicht definiert, nutze insecure
                Serial.println("WARNING: Cannot determine ESP32 core version!");
                Serial.println("Falling back to insecure connection");
                client->setInsecure();
                useCertBundle = false;
            #endif
        } else {
            // Benutzer hat explizit unsichere Verbindung bestätigt
            Serial.println("User confirmed: Using insecure connection (no certificate validation)");
            client->setInsecure();
            useCertBundle = false;
        }
        
        HTTPClient http;
        
        Serial.println("Checking for updates...");
        Serial.println("URL: " + String(GITHUB_API_URL));
        
        // Verwende HTTPS
        if (http.begin(*client, GITHUB_API_URL)) {
            http.addHeader("Accept", "application/vnd.github+json");
            http.addHeader("User-Agent", "LaCrosse2MQTT-Updater");
            http.setTimeout(10000); // 10 Sekunden Timeout
            
            int httpCode = http.GET();
            
            // Falls mit Certificate Bundle fehlgeschlagen, informiere Benutzer
            if (httpCode < 0 && useCertBundle && !forceInsecure) {
                Serial.println("ERROR: Connection with certificate bundle failed!");
                Serial.println("HTTP Error Code: " + String(httpCode));
                Serial.println("User confirmation required to proceed without certificate validation");
                
                updateInfo.certBundleFailed = true;
                updateInfo.requiresUserConfirmation = true;
                updateInfo.errorMessage = "Certificate validation failed. User confirmation required.";
                
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
bool installUpdate(bool forceInsecure = false) {
    if (updateInfo.downloadUrl.isEmpty()) {
        Serial.println("No download URL available");
        return false;
    }
    
    updateInstallInProgress = true;
    updateProgress = 0;
    
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (client) {
        bool useCertBundle = true;
        
        // Wenn forceInsecure nicht gesetzt, versuche Certificate Bundle
        if (!forceInsecure) {
            #ifdef ESP_IDF_VERSION_MAJOR
                #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
                    client->setCACert(NULL); // Nutzt das eingebaute Bundle
                    Serial.println("Using built-in certificate bundle for secure firmware download");
                #else
                    Serial.println("WARNING: Certificate bundle not available!");
                    Serial.println("Downloading firmware without certificate validation");
                    Serial.println("SECURITY WARNING: Unable to verify server identity!");
                    client->setInsecure();
                    useCertBundle = false;
                #endif
            #else
                Serial.println("WARNING: Cannot determine ESP32 core version!");
                Serial.println("Downloading firmware without certificate validation");
                client->setInsecure();
                useCertBundle = false;
            #endif
        } else {
            Serial.println("User confirmed: Downloading firmware without certificate validation");
            Serial.println("SECURITY WARNING: Server identity will not be verified!");
            client->setInsecure();
            useCertBundle = false;
        }
        
        HTTPClient http;
        
        Serial.println("Starting firmware download...");
        Serial.println("URL: " + updateInfo.downloadUrl);
        
        if (http.begin(*client, updateInfo.downloadUrl)) {
            http.setTimeout(30000); // 30 Sekunden
            
            int httpCode = http.GET();
            
            // Falls mit Certificate Bundle fehlgeschlagen
            if (httpCode < 0 && useCertBundle && !forceInsecure) {
                Serial.println("ERROR: Firmware download with certificate bundle failed!");
                Serial.println("HTTP Error Code: " + String(httpCode));
                
                http.end();
                delete client;
                updateInstallInProgress = false;
                return false;
            }
            
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