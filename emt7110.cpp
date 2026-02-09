#include "emt7110.h"
#include "globals.h"

extern Cache fcache[];

bool EMT7110::TryHandleData(byte *data, byte payLoadSize, Frame *f) {
    // EMT7110: 9 Bytes, startet mit 0x43
    if (payLoadSize != 9 || data[0] != 0x43) {
        return false;
    }

    // CRC Check
    byte crc = 0;
    for (int i = 0; i < 9; i++) {
        crc ^= data[i];
    }
    if (crc != 0) {
        return false;
    }

    // Decode
    f->ID = data[1] & 0x0F;
    f->init = (data[1] & 0x80) >> 7;
    f->batlo = (data[1] & 0x40) >> 6;
    
    // Power in Watt (16 bit, 0.1 W resolution)
    unsigned int power_raw = (data[2] << 8) | data[3];
    f->power = power_raw * 0.1;
    
    // Energy in kWh (24 bit, 0.001 kWh resolution)
    unsigned long energy_raw = ((unsigned long)data[4] << 16) | 
                               ((unsigned long)data[5] << 8) | 
                               data[6];
    f->energy = energy_raw * 0.001;

    // Update cache
    int idx = GetCacheIndex(f->ID, 1);
    fcache[idx].ID = f->ID;
    fcache[idx].batlo = f->batlo;
    fcache[idx].init = f->init;
    fcache[idx].power = f->power;
    fcache[idx].energy = f->energy;
    fcache[idx].power_timestamp = millis();
    fcache[idx].timestamp = millis();
    fcache[idx].valid = true;
    fcache[idx].rssi = f->rssi;
    fcache[idx].rate = f->rate;
    strncpy(fcache[idx].sensorType, "EMT7110", 15);
    fcache[idx].sensorType[15] = '\0';

    f->valid = true;
    return true;
}

bool EMT7110::DisplayFrame(byte *data, byte payLoadSize, Frame *f) {
    if (!f->valid) return false;
    
    static unsigned long last[SENSOR_NUM];
    DisplayRaw(last[f->ID], "EMT7110", data, payLoadSize, f->rssi, f->rate);
    
    Serial.printf(" ID%-3d", f->ID);
    Serial.printf(" Power%.1fW", f->power);
    Serial.printf(" Energy%.3fkWh", f->energy);
    Serial.printf(" init%d batlo%d", f->init, f->batlo);
    Serial.println();
    
    return true;
}

void EMT7110::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate) {
    unsigned long now = millis();
    if (last == 0) last = now;
    Serial.printf("%6ld %s [", (now - last), dev);
    last = now;
    for (uint8_t i = 0; i < len; i++)
        Serial.printf("%02X%s", data[i], (i == len - 1) ? "" : " ");
    Serial.printf("] rssi%-4d rate%-5d", rssi, rate);
}