#include "tx22it.h"
#include "globals.h"

extern Cache fcache[];

bool TX22IT::TryHandleData(byte *data, byte payLoadSize, Frame *f) {
    // TX22IT: 9 Bytes, startet mit 0x41
    if (payLoadSize != 9 || data[0] != 0x41) {
        return false;
    }

    // CRC Check (XOR über alle 9 Bytes muss 0 ergeben)
    byte crc = 0;
    for (int i = 0; i < 9; i++) {
        crc ^= data[i];
    }
    if (crc != 0) {
        return false;
    }

    // Decode
    f->ID = data[1] & 0x07;
    f->init = (data[1] & 0x80) >> 7;
    f->batlo = (data[1] & 0x40) >> 6;
    
    // Temperature (10 bit signed, 0.1°C resolution)
    int temp_raw = ((data[2] & 0x03) << 8) | data[3];
    if (temp_raw & 0x200) {  // Negative Temperatur
        temp_raw = temp_raw - 0x400;
    }
    f->temp = temp_raw * 0.1;
    
    // Humidity (7 bit, 1% resolution)
    f->humi = data[4] & 0x7F;
    
    // Wind Speed (8 bit, 0.1 m/s resolution)
    f->wind_speed = data[5] * 0.1;
    
    // Wind Gust (8 bit, 0.1 m/s resolution)
    f->wind_gust = data[6] * 0.1;
    
    // Wind Direction (9 bit, 0-360°)
    int windDir = ((data[7] & 0x01) << 8) | data[8];
    f->wind_direction = windDir * 360.0 / 512.0;

    // Update cache
    int idx = GetCacheIndex(f->ID, 1);
    fcache[idx].ID = f->ID;
    fcache[idx].temp = f->temp;
    fcache[idx].humi = f->humi;
    fcache[idx].batlo = f->batlo;
    fcache[idx].init = f->init;
    fcache[idx].wind_speed = f->wind_speed;
    fcache[idx].wind_gust = f->wind_gust;
    fcache[idx].wind_direction = f->wind_direction;
    fcache[idx].wind_timestamp = millis();
    fcache[idx].timestamp = millis();
    fcache[idx].valid = true;
    fcache[idx].rssi = f->rssi;
    fcache[idx].rate = f->rate;
    strncpy(fcache[idx].sensorType, "TX22IT", 15);
    fcache[idx].sensorType[15] = '\0';

    f->valid = true;
    return true;
}

bool TX22IT::DisplayFrame(byte *data, byte payLoadSize, Frame *f) {
    if (!f->valid) return false;
    
    static unsigned long last[SENSOR_NUM];
    DisplayRaw(last[f->ID], "TX22IT ", data, payLoadSize, f->rssi, f->rate);
    
    Serial.printf(" ID%-3d", f->ID);
    Serial.printf(" Temp%-5.1f°C", f->temp);
    Serial.printf(" Humi%d%%", f->humi);
    Serial.printf(" Wind%.1fm/s", f->wind_speed);
    Serial.printf(" Gust%.1fm/s", f->wind_gust);
    Serial.printf(" Dir%.0f°", f->wind_direction);
    Serial.printf(" init%d batlo%d", f->init, f->batlo);
    Serial.println();
    
    return true;
}

void TX22IT::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate) {
    unsigned long now = millis();
    if (last == 0) last = now;
    Serial.printf("%6ld %s [", (now - last), dev);
    last = now;
    for (uint8_t i = 0; i < len; i++)
        Serial.printf("%02X%s", data[i], (i == len - 1) ? "" : " ");
    Serial.printf("] rssi%-4d rate%-5d", rssi, rate);
}