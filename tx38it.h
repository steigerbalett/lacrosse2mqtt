#ifndef _TX38IT_H
#define _TX38IT_H

#include <Arduino.h>
#include "globals.h"

namespace TX38IT {
    
    struct Frame {
        byte ID;
        byte channel;
        float temp;
        int8_t humi;
        bool batlo;
        bool init;
        bool valid;
        int8_t rssi;
        uint16_t rate;
    };

    bool TryHandleData(byte *data, struct Frame *f);
    bool DecodeFrame(byte *bytes, struct Frame *f);
    bool DisplayFrame(byte *data, struct Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
    
    byte CalculateCRC(byte *data, byte len);
}

#endif