#ifndef FHEM_CONNECTOR_H
#define FHEM_CONNECTOR_H

#include <Arduino.h>

class FHEMConnector {
public:
    // Check if FHEM mode is enabled
    static bool isEnabled();
    
    // Handle incoming serial commands from FHEM
    static void handleSerialCommand();
    
    // Send version information to FHEM
    static void sendVersionInfo();
    
    // Send sensor data in FHEM format
    static void sendSensorData(const String& data);
    
private:
    // Process a single command character
    static void processCommandChar(char c);
    
    // Store numeric values from multi-digit commands
    static unsigned long commandValue;
};

#endif // FHEM_CONNECTOR_H