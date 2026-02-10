#ifndef _WH25_H
#define _WH25_H

#include <Arduino.h>

namespace WH25 {
    static const byte WH25_FRAME_LENGTH = 10;
    
    struct Frame {
        byte ID;
        int rssi;
        int rate;
        bool valid;
        bool batlo;
        float temp;
        byte humi;
        float pressure;
        byte crc;    // NEU
    };
    
    bool TryHandleData(byte *payload, int payloadSize, Frame *frame);
    void DisplayFrame(byte *payload, int payloadSize, Frame *frame);
    void DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate);
}

#endif