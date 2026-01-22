#include "ws1600.h"
#include <Arduino.h>

/*
 * WS1600 Weather Sensor Protocol
 * 9-byte message format
 */

bool WS1600::DecodeFrame(byte *bytes, byte len, struct Frame *f)
{
    if (len != 9) {
        return false;
    }
    
    f->valid = true;
    
    // ID und Kanal
    f->ID = bytes[0] & 0x0F;
    f->channel = ((bytes[0] & 0xF0) >> 4);
    
    // Temperatur (12-bit signed)
    int16_t temp_raw = ((bytes[1] & 0x0F) << 8) | bytes[2];
    if (temp_raw & 0x800) {
        temp_raw |= 0xF000; // Sign extend
    }
    f->temp = temp_raw * 0.1f;
    
    // Luftfeuchtigkeit
    f->humi = bytes[3];
    
    // Regen
    int rain_raw = (bytes[4] << 8) | bytes[5];
    f->rain = rain_raw * 0.25f; // 0.25mm pro Count
    
    // Windgeschwindigkeit
    f->wind_speed = bytes[6] * 0.2f; // 0.2 m/s pro Count
    
    // Windrichtung
    f->wind_direction = bytes[7] & 0x0F;
    
    // Batterie
    f->batlo = (bytes[1] & 0x80) ? 1 : 0;
    
    return true;
}

bool WS1600::TryHandleData(byte *data, byte len, struct Frame *f)
{
    // WS1600 Pakete sind 9 Bytes lang
    if (len == 9) {
        return DecodeFrame(data, len, f);
    }
    return false;
}

bool WS1600::DisplayFrame(byte *data, byte len, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("WS1600::DisplayFrame FRAME INVALID");
        return false;
    }

    DisplayRaw(last[f->ID], "WS1600  ", data, len, f->rssi, f->rate);
    
    Serial.printf(" ID%d Ch%d", f->ID, f->channel);
    Serial.printf(" Temp%.1f°C", f->temp);
    Serial.printf(" Humi%d%%", f->humi);
    Serial.printf(" Rain%.1fmm", f->rain);
    Serial.printf(" Wind%.1fm/s", f->wind_speed);
    Serial.printf(" Dir%d", f->wind_direction);
    Serial.printf(" batlo%d", f->batlo);
    Serial.println();
    
    return true;
}

void WS1600::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate)
{
    unsigned long now = millis();
    if (last == 0)
        last = now;
    Serial.printf("%6ld %s [", (now - last), dev);
    last = now;
    for (uint8_t i = 0; i < len; i++)
        Serial.printf("%02X%s", data[i], (i == len - 1) ? "" : " ");
    Serial.printf("] rssi%-4d rate%-5d", rssi, rate);
}