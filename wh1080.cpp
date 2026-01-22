#include "wh1080.h"
#include <Arduino.h>

/*
 * WH1080 Weather Station Protocol
 * 
 * Message Format (10 bytes):
 * [0]: Station ID high nibble + Sensor type
 * [1]: Station ID low byte  
 * [2-3]: Temperature (BCD)
 * [4]: Humidity
 * [5-6]: Rain counter
 * [7]: Wind speed
 * [8]: Wind gust + direction
 * [9]: CRC
 *
 * Based on: http://www.sevenwatt.com/main/wh1080-protocol-v2-fsk/
 */

const char* WH1080::GetWindDirection(byte bearing)
{
    const char* directions[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    
    if (bearing < 16) {
        return directions[bearing];
    }
    return "Unknown";
}

byte WH1080::CalculateCRC(byte *data, byte len)
{
    byte crc = 0;
    for (int i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool WH1080::DecodeFrame(byte *bytes, byte len, struct Frame *f)
{
    if (len != 10) {
        return false;
    }
    
    // CRC Prüfung
    byte crc_calc = CalculateCRC(bytes, len - 1);
    if (crc_calc != bytes[len - 1]) {
        Serial.printf(" [WH1080 CRC fail: calc=%02X got=%02X]", crc_calc, bytes[len-1]);
        f->valid = false;
        return false;
    }
    
    f->valid = true;
    
    // Station ID extrahieren
    f->station_id = ((bytes[0] & 0x0F) << 4) | ((bytes[1] & 0xF0) >> 4);
    f->ID = f->station_id;
    
    // Temperatur dekodieren (BCD Format mit Offset)
    int temp_bcd = ((bytes[1] & 0x0F) << 8) | bytes[2];
    f->temp = (temp_bcd * 0.1f) - 40.0f;
    
    // Luftfeuchtigkeit
    f->humi = bytes[3];
    
    // Regen (0.3mm pro Count)
    int rain_count = (bytes[4] << 8) | bytes[5];
    f->rain = rain_count * 0.3f;
    
    // Windgeschwindigkeit (0.34 m/s pro Count)
    f->wind_speed = bytes[6] * 0.34f;
    
    // Windböe und Richtung
    f->wind_gust = (bytes[7] & 0xF0) >> 4;
    f->wind_gust *= 0.34f;
    f->wind_bearing = bytes[7] & 0x0F;
    
    // Status
    f->status = bytes[8];
    
    return true;
}

bool WH1080::TryHandleData(byte *data, byte len, struct Frame *f)
{
    // WH1080 Pakete beginnen typischerweise mit 0xAX
    if (len == 10 && (data[0] & 0xF0) == 0xA0) {
        return DecodeFrame(data, len, f);
    }
    return false;
}

bool WH1080::DisplayFrame(byte *data, byte len, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("WH1080::DisplayFrame FRAME INVALID");
        return false;
    }

    DisplayRaw(last[f->ID], "WH1080  ", data, len, f->rssi, f->rate);
    
    Serial.printf(" ID%02X", f->station_id);
    Serial.printf(" Temp%.1f°C", f->temp);
    Serial.printf(" Humi%d%%", f->humi);
    Serial.printf(" Rain%.1fmm", f->rain);
    Serial.printf(" Wind%.1fm/s", f->wind_speed);
    Serial.printf(" Gust%.1fm/s", f->wind_gust);
    Serial.printf(" Dir%s", GetWindDirection(f->wind_bearing));
    Serial.println();
    
    return true;
}

void WH1080::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate)
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