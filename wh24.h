#ifndef _WH24_H
#define _WH24_H

#include <Arduino.h>

namespace WH24 {
    static const byte WH24_FRAME_LENGTH = 17;
    
    struct Frame {
        byte ID;
        int rssi;
        int rate;
        bool valid;
        bool batlo;
        float temp;
        byte humi;
        float pressure;
        float wind_speed;      // NEU
        float wind_gust;       // NEU
        int wind_bearing;      // NEU
        float rain;            // NEU
        byte uv_index;         // NEU
        byte crc;              // NEU
    };
    
    bool TryHandleData(byte *payload, int payloadSize, Frame *frame);
    void DisplayFrame(byte *payload, int payloadSize, Frame *frame);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif