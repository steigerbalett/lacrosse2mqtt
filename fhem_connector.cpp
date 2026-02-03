#include "fhem_connector.h"
#include "SX127x.h"
#include "globals.h"

// External variables from main program
extern SX127x SX;
extern int freq;
extern Config config;

unsigned long FHEMConnector::commandValue = 0;

bool FHEMConnector::isEnabled() {
    return config.fhem_mode;
}

void FHEMConnector::handleSerialCommand() {
    if (!isEnabled()) return;
    
    if (Serial.available()) {
        char c = Serial.read();
        processCommandChar(c);
    }
}

void FHEMConnector::processCommandChar(char c) {
    if ('0' <= c && c <= '9') {
        commandValue = 10 * commandValue + c - '0';
    }
    else if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {
        switch (c) {
            case 'v':
                sendVersionInfo();
                break;
                
            case 'r':
                // Data rate switching
                // 'r' or '0r' or '17241r' -> 17.241 kbps (LaCrosse IT+)
                // '1r' or '9579r' -> 9.579 kbps (TX35-IT)
                // '2r' or '8842r' -> 8.842 kbps (TX38-IT)
                if (commandValue == 0 || commandValue == 17241) {
                    SX.NextDataRate(0);  // Index 0 = 17.241 kbps
                } else if (commandValue == 1 || commandValue == 9579) {
                    SX.NextDataRate(1);  // Index 1 = 9.579 kbps
                } else if (commandValue == 2 || commandValue == 8842) {
                    SX.NextDataRate(2);  // Index 2 = 8.842 kbps
                } else {
                    SX.NextDataRate(-1);  // Cycle through rates
                }
                
                if (config.fhem_verbose) {
                    Serial.print("[INFO] DataRate: ");
                    Serial.print(SX.GetDataRate());
                    Serial.println(" kbps");
                }
                break;
                
            case 'f':
                // Frequency query
                if (config.fhem_verbose) {
                    Serial.print("[INFO] Frequency: ");
                    Serial.print(freq);
                    Serial.println(" kHz");
                }
                break;
                
            case 'd':
                // Debug mode (not supported in FHEM mode)
                if (config.fhem_verbose) {
                    Serial.println("[INFO] Debug: Use web interface");
                }
                break;
                
            case 'h':
            case '?':
                // Help
                if (config.fhem_verbose) {
                    Serial.println("[INFO] LaCrosse2MQTT FHEM Commands:");
                    Serial.println("[INFO]   v       - Version info");
                    Serial.println("[INFO]   r       - Cycle data rates");
                    Serial.println("[INFO]   0r      - 17.241 kbps (IT+)");
                    Serial.println("[INFO]   1r      - 9.579 kbps (TX35)");
                    Serial.println("[INFO]   2r      - 8.842 kbps (TX38)");
                    Serial.println("[INFO]   f       - Show frequency");
                    Serial.println("[INFO]   h or ?  - This help");
                }
                break;
                
            default:
                sendVersionInfo();
                break;
        }
        commandValue = 0;
    }
}

void FHEMConnector::sendVersionInfo() {
    if (!isEnabled()) return;
    
    Serial.print("\n[LaCrosse2MQTT.");
    Serial.print(LACROSSE2MQTT_VERSION);
    Serial.print("-fhem (SX127x) @");
    Serial.print(SX.GetDataRate());
    Serial.print(" kbps / ");
    Serial.print(freq);
    Serial.println(" kHz]");
}

void FHEMConnector::sendSensorData(const String& data) {
    if (!isEnabled()) return;
    
    Serial.println(data);
}