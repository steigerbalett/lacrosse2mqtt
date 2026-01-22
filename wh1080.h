#ifndef _WH1080_H
#define _WH1080_H

#include <Arduino.h>
#include "globals.h"

namespace WH1080 {
    
    struct Frame {
        byte ID;
        byte station_id;
        float temp;
        byte humi;
        float rain;
        float wind_speed;
        float wind_gust;
        byte wind_bearing;
        byte status;
        bool valid;
        int8_t rssi;
        uint16_t rate;
    };

    bool TryHandleData(byte *data, byte len, struct Frame *f);
    bool DecodeFrame(byte *bytes, byte len, struct Frame *f);
    bool DisplayFrame(byte *data, byte len, struct Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
    
    byte CalculateCRC(byte *data, byte len);
    const char* GetWindDirection(byte bearing);
}

#endif
