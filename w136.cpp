#include "w136.h"
#include "globals.h"

extern Cache fcache[];

bool W136::TryHandleData(byte *data, byte payLoadSize, Frame *f) {
    // W136: 6 Bytes, startet mit 0x47
    if (payLoadSize != 6 || data[0] != 0x47) {
        return false;
    }

    // CRC Check
    byte crc = 0;
    for (int i = 0; i < 6; i++) {
        crc ^= data[i];
    }
    if (crc != 0) {
        return false;
    }

    // Decode
    f->ID = data[1] & 0x0F;
    f->batlo = (data[1] & 0x80) >> 7;
    
    // Rain in mm (16 bit, 0.1mm resolution)
    unsigned int rain_raw = (data[2] << 8) | data[3];
    f->rain = rain_raw * 0.1;

    // Update cache
    int idx = GetCacheIndex(f->ID, 1);
    fcache[idx].ID = f->ID;
    fcache[idx].batlo = f->batlo;
    fcache[idx].rain = f->rain;
    fcache[idx].rain_timestamp = millis();
    fcache[idx].timestamp = millis();
    fcache[idx].valid = true;
    fcache[idx].rssi = f->rssi;
    fcache[idx].rate = f->rate;
    strncpy(fcache[idx].sensorType, "W136", 15);
    fcache[idx].sensorType[15] = '\0';

    f->valid = true;
    return true;
}

bool W136::DisplayFrame(byte *data, byte payLoadSize, Frame *f) {
    if (!f->valid) return false;
    
    static unsigned long last[SENSOR_NUM];
    DisplayRaw(last[f->ID], "W136   ", data, payLoadSize, f->rssi, f->rate);
    
    Serial.printf(" ID%-3d", f->ID);
    Serial.printf(" Rain%.1fmm", f->rain);
    Serial.printf(" batlo%d", f->batlo);
    Serial.println();
    
    return true;
}

void W136::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate) {
    unsigned long now = millis();
    if (last == 0) last = now;
    Serial.printf("%6ld %s [", (now - last), dev);
    last = now;
    for (uint8_t i = 0; i < len; i++)
        Serial.printf("%02X%s", data[i], (i == len - 1) ? "" : " ");
    Serial.printf("] rssi%-4d rate%-5d", rssi, rate);
}