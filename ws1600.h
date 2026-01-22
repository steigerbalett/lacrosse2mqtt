#ifndef _WS1600_H
#define _WS1600_H

#include <Arduino.h>
#include "globals.h"

namespace WS1600 {
    
    struct Frame {
        byte ID;
        byte channel;
        float temp;
        byte humi;
        float rain;
        float wind_speed;
        byte wind_direction;
        bool batlo;
        bool valid;
        int8_t rssi;
        uint16_t rate;
    };

    bool TryHandleData(byte *data, byte len, struct Frame *f);
    bool DecodeFrame(byte *bytes, byte len, struct Frame *f);
    bool DisplayFrame(byte *data, byte len, struct Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif