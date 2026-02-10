#ifndef _EMT7110_H
#define _EMT7110_H

#include <Arduino.h>

namespace EMT7110 {
    struct Frame {
        byte ID;
        byte rssi;
        int rate;
        bool valid;
        bool batlo;
        bool init;
        float power;   // Watt
        float energy;  // kWh
    };
    
    bool TryHandleData(byte *data, byte payLoadSize, Frame *f);
    bool DisplayFrame(byte *data, byte payLoadSize, Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif