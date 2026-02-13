#ifndef HP1000_H
#define HP1000_H

#include <Arduino.h>

namespace HP1000 {
    struct Frame {
        uint8_t ID;
        uint8_t channel;
        float temp;
        uint8_t humi;
        float wind_speed;
        float wind_gust;
        int wind_direction;
        float pressure;
        float rain;
        uint8_t uv;              // UV-Index (0-15)
        float light_lux;         // Lichtintensität in Lux
        bool batlo;
        bool init;
        int8_t rssi;
        int rate;
    };

    bool TryHandleData(uint8_t *data, int len, Frame *frame);
    void DisplayFrame(uint8_t *data, int len, Frame *frame);
}

#endif