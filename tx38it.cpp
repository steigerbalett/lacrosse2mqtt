#include "tx38it.h"
#include <Arduino.h>

/*
 * TX38IT Indoor Temperature Sensor Protocol
 * 
 * Similar to LaCrosse but with lower data rate (8842 bps)
 * 5-byte message format
 */

byte TX38IT::CalculateCRC(byte *data, byte len)
{
    byte crc = 0;
    for (int i = 0; i < len; i++) {
        for (int bit = 0; bit < 8; bit++) {
            byte tmp = (byte)((crc ^ data[i]) & 0x80);
            crc <<= 1;
            if (tmp != 0) {
                crc ^= 0x31;
            }
            data[i] <<= 1;
        }
    }
    return crc;
}

bool TX38IT::DecodeFrame(byte *bytes, struct Frame *f)
{
    f->valid = true;
    
    // CRC Prüfung
    byte data_copy[FRAME_LENGTH];
    memcpy(data_copy, bytes, FRAME_LENGTH);
    byte crc_calc = CalculateCRC(data_copy, FRAME_LENGTH - 1);
    
    if (bytes[4] != crc_calc) {
        Serial.printf(" [TX38IT CRC fail]");
        f->valid = false;
        return false;
    }
    
    // Header Prüfung - TX38IT verwendet oft 0x8X
    if ((bytes[0] & 0xF0) != 0x80 && (bytes[0] & 0xF0) != 0x90) {
        f->valid = false;
        return false;
    }

    // ID extrahieren
    f->ID = (bytes[0] & 0x0F) << 2;
    f->ID |= (bytes[1] & 0xC0) >> 6;
    
    // New battery flag
    f->init = (bytes[1] & 0x20) ? 1 : 0;

    // Temperatur (BCD Format)
    byte bcd[3];
    bcd[0] = bytes[1] & 0x0F;
    bcd[1] = (bytes[2] & 0xF0) >> 4;
    bcd[2] = (bytes[2] & 0x0F);
    f->temp = ((bcd[0] * 100 + bcd[1] * 10 + bcd[2]) - 400) / 10.0f;

    // Batteriezustand
    f->batlo = (bytes[3] & 0x80) ? 1 : 0;
    
    // Luftfeuchtigkeit (meistens nicht vorhanden bei TX38IT)
    byte humi_raw = bytes[3] & 0x7F;
    f->humi = (humi_raw == 0x6A) ? -1 : humi_raw;
    
    f->channel = 1;
    
    return true;
}

bool TX38IT::TryHandleData(byte *data, struct Frame *f)
{
    // TX38IT erkennen: 8842 bps Datenrate, 0x8X oder 0x9X Start
    if ((data[0] & 0xF0) == 0x80 || (data[0] & 0xF0) == 0x90) {
        return DecodeFrame(data, f);
    }
    return false;
}

bool TX38IT::DisplayFrame(byte *data, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("TX38IT::DisplayFrame FRAME INVALID");
        return false;
    }

    DisplayRaw(last[f->ID], "TX38IT  ", data, FRAME_LENGTH, f->rssi, f->rate);
    
    Serial.printf(" ID%-3d Ch%d", f->ID, f->channel);
    Serial.printf(" Temp%.1f°C", f->temp);
    Serial.printf(" init%d batlo%d", f->init, f->batlo);
    
    if (f->humi > 0 && f->humi <= 100) {
        Serial.printf(" Humi%d%%", f->humi);
    }
    
    Serial.println();
    
    return true;
}

void TX38IT::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate)
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