#ifndef TFA1_H
#define TFA1_H

#include <Arduino.h>

namespace TFA1 {
    struct Frame {
        uint16_t ID;         // 15-bit unique ID
        uint8_t channel;     // 0-3
        float temp;          // -40°C bis +60°C
        uint8_t humi;        // 1-99%
        bool batlo;          // Battery low
        bool init;           // Init message (b26-00-56)
        int8_t rssi;
        int rate;
    };

    bool TryHandleData(uint8_t *data, int len, Frame *frame);
    void DisplayFrame(uint8_t *data, int len, Frame *frame);
}

#endif
