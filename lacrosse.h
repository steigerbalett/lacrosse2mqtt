#ifndef _LACROSSE_H
#define _LACROSSE_H

#include <Arduino.h>
#include "globals.h"

namespace LaCrosse {

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

    void DecodeFrame(byte *data, struct Frame *f);
    bool TryHandleData(byte *data, struct Frame *f);
    bool DisplayFrame(byte *data, struct Frame *f);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
    
    byte UpdateCRC(byte res, uint8_t val);
    byte CalculateCRC(byte *data, byte len);
    
    const char* GetSensorType(struct Frame *f);
}

#endif