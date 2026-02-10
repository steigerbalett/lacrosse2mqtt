#ifndef _W136_H
#define _W136_H

#include <Arduino.h>

namespace W136 {
    struct Frame {
        byte ID;
        byte rssi;
        int rate;
        bool valid;
        bool batlo;
        float rain;  // mm
    };
    
    bool TryHandleData(byte *data, byte payLoadSize, Frame *f);
    bool DisplayFrame(byte *data, byte payLoadSize, Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif