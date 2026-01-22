#ifndef _WT440XH_H
#define _WT440XH_H

#include <Arduino.h>
#include "globals.h"

namespace WT440XH {
    
    struct Frame {
        byte ID;
        byte channel;
        float temp;
        byte humi;
        bool batlo;
        bool valid;
        int8_t rssi;
        uint16_t rate;
    };

    bool TryHandleData(byte *data, struct Frame *f);
    bool DecodeFrame(byte *bytes, struct Frame *f);
    bool DisplayFrame(byte *data, struct Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif