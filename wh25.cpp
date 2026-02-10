#include "wh25.h"
#include "globals.h"

bool WH25::TryHandleData(byte *payload, int payloadSize, Frame *frame) {
    // WH25: 10 Bytes, startet mit 0x25
    if (payloadSize != WH25_FRAME_LENGTH || payload[0] != 0x25) {
        return false;
    }
    
    // Einfache Summen-Prüfung
    byte sum = 0;
    for (int i = 0; i < WH25_FRAME_LENGTH - 1; i++) {
        sum += payload[i];
    }
    
    frame->crc = payload[WH25_FRAME_LENGTH - 1];
    
    if (sum != frame->crc) {
        Serial.println("WH25: CRC error");
        frame->valid = false;
        return false;
    }
    
    // ID extrahieren
    frame->ID = payload[1];
    
    // Battery Status
    frame->batlo = (payload[2] & 0x08) != 0;
    
    // Temperatur (Bytes 3-4, signed, 0.1°C Auflösung)
    int16_t temp_raw = (int16_t)((payload[3] << 8) | payload[4]);
    frame->temp = temp_raw / 10.0;
    
    // Luftfeuchtigkeit (Byte 5)
    frame->humi = payload[5];
    
    // Luftdruck (Bytes 6-7, hPa, 0.1 hPa Auflösung)
    uint16_t pressure_raw = (payload[6] << 8) | payload[7];
    frame->pressure = pressure_raw / 10.0;
    
    frame->valid = true;
    
    return true;
}

void WH25::DisplayFrame(byte *payload, int payloadSize, Frame *frame) {
    Serial.println();
    Serial.println("===== WH25 Pressure Sensor =====");
    Serial.print("ID: ");
    Serial.println(frame->ID, HEX);
    Serial.print("Temperature: ");
    Serial.print(frame->temp, 1);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(frame->humi);
    Serial.println(" %");
    Serial.print("Pressure: ");
    Serial.print(frame->pressure, 1);
    Serial.println(" hPa");
    Serial.print("Battery: ");
    Serial.println(frame->batlo ? "LOW" : "OK");
    Serial.print("RSSI: ");
    Serial.print(frame->rssi);
    Serial.println(" dBm");
    Serial.println("================================");
}

void WH25::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate) {
    unsigned long now = millis();
    if (now - last < 1000)
        return;
    last = now;
    
    Serial.print(dev);
    Serial.print(": ");
    for (int i = 0; i < len; i++) {
        if (data[i] < 0x10)
            Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" Rate: ");
    Serial.println(rate);
}