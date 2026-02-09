#ifndef _TX22IT_H
#define _TX22IT_H

#include <Arduino.h>

namespace TX22IT {
    struct Frame {
        byte ID;
        byte rssi;
        int rate;
        bool valid;
        bool batlo;
        bool init;
        float temp;
        byte humi;
        float wind_speed;
        float wind_gust;
        float wind_direction;
    };
    
    bool TryHandleData(byte *data, byte payLoadSize, Frame *f);
    bool DisplayFrame(byte *data, byte payLoadSize, Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif