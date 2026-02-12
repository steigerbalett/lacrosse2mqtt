#ifndef _WH65B_H
#define _WH65B_H

#include <Arduino.h>

/*
 * Fine Offset WH65B / HP1000 Protocol Handler
 * 
 * 868 MHz / 915 MHz Outdoor Weather Station
 * Compatible models:
 *  - Fine Offset WH65B (868 MHz Europe, 915 MHz USA)
 *  - Fine Offset HP1000 (868 MHz Europe)
 *  - Ecowitt WH65B
 *  - Ambient Weather WH65B
 * 
 * Frame: 17 bytes
 * Sensors: Temperature, Humidity, Pressure, Wind Speed, Wind Gust, 
 *          Wind Direction, Rain, UV Index
 * 
 * Protocol differences vs WH24 (433 MHz):
 *  - Wind factor: 0.51 m/s (WH65B) vs 1.12 m/s (WH24)
 *  - Rain factor: 0.254 mm (WH65B) vs 0.3 mm (WH24)
 *  - Postamble: 12 bits (WH65B) vs 3 bits (WH24)
 */

namespace WH65B {
    struct Frame {
        uint8_t ID;
        uint8_t channel;
        float temp;
        uint8_t humi;
        float wind_speed;
        float wind_gust;
        int wind_direction;
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