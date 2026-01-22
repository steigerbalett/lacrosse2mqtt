#include "wt440xh.h"
#include <Arduino.h>

/*
 * WT440XH Temperature/Humidity Sensor Protocol
 * Compact 4-byte message format
 */

bool WT440XH::DecodeFrame(byte *bytes, struct Frame *f)
{
    f->valid = true;
    
    // ID extrahieren (6 bits)
    f->ID = (bytes[0] >> 2) & 0x3F;
    
    // Kanal
    f->channel = (bytes[0] & 0x03) + 1;
    
    // Temperatur (10-bit)
    int16_t temp_raw = ((bytes[1] & 0x03) << 8) | bytes[2];
    if (temp_raw & 0x200) {
        temp_raw |= 0xFC00; // Sign extend
    }
    f->temp = temp_raw * 0.1f;
    
    // Luftfeuchtigkeit (7 bits)
    f->humi = bytes[3] & 0x7F;
    
    // Batterie
    f->batlo = (bytes[1] & 0x80) ? 1 : 0;
    
    // Plausibilitätsprüfung
    if (f->temp < -40.0f || f->temp > 85.0f) {
        f->valid = false;
        return false;
    }
    
    if (f->humi > 100) {
        f->valid = false;
        return false;
    }
    
    return true;
}

bool WT440XH::TryHandleData(byte *data, struct Frame *f)
{
    // WT440XH hat 4-Byte Pakete
    // Erkennung über Paketlänge und Plausibilitätsprüfung
    return DecodeFrame(data, f);
}

bool WT440XH::DisplayFrame(byte *data, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("WT440XH::DisplayFrame FRAME INVALID");
        return false;
    }

    DisplayRaw(last[f->ID], "WT440XH ", data, 4, f->rssi, f->rate);
    
    Serial.printf(" ID%d Ch%d", f->ID, f->channel);
    Serial.printf(" Temp%.1f°C", f->temp);
    Serial.printf(" Humi%d%%", f->humi);
    Serial.printf(" batlo%d", f->batlo);
    Serial.println();
    
    return true;
}

void WT440XH::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate)
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