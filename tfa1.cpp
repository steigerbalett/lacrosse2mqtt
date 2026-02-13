#include "tfa1.h"

namespace TFA1 {

// CRC8 Berechnung für TFA_1 (Polynom 0x31)
uint8_t crc8(uint8_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool TryHandleData(uint8_t *data, int len, Frame *frame) {
    // TFA_1 Frame Format (5 bytes):
    // [0] high nibble: ID bits 14-11
    //     low nibble: Battery (1 bit) + Channel (2 bits) + ID bit 10
    // [1] ID bits 9-2
    // [2] ID bits 1-0 (high 2 bits) + Temp high 6 bits
    // [3] Temp low 4 bits (high nibble) + Humidity high 4 bits (low nibble)
    // [4] Humidity low 4 bits (high nibble) + CRC 4 bits (low nibble)
    
    if (len != 5) return false;
    
    // Sync-Pattern prüfen (sollte bereits vom SX127x gefiltert sein)
    // Sync ist 2d d4, aber das bekommen wir hier nicht mehr
    
    // ID extrahieren (15 bits)
    uint16_t id = 0;
    id |= ((uint16_t)(data[0] & 0xF0)) << 7;  // Bits 14-11
    id |= ((uint16_t)(data[0] & 0x01)) << 10; // Bit 10
    id |= ((uint16_t)data[1]) << 2;           // Bits 9-2
    id |= ((uint16_t)(data[2] & 0xC0)) >> 6;  // Bits 1-0
    
    frame->ID = id;
    
    // Battery Status (Bit in Byte 0)
    frame->batlo = (data[0] & 0x08) != 0;
    
    // Channel (2 bits)
    frame->channel = (data[0] & 0x06) >> 1;
    
    // Temperatur (10 bits, signed, 0.1°C Auflösung, -400 bis +600)
    int16_t temp_raw = 0;
    temp_raw |= ((int16_t)(data[2] & 0x3F)) << 4;  // 6 bits
    temp_raw |= ((int16_t)(data[3] & 0xF0)) >> 4;  // 4 bits
    
    // Sign-Extension für 10-bit signed
    if (temp_raw & 0x200) {
        temp_raw |= 0xFC00;
    }
    
    frame->temp = (temp_raw - 400) / 10.0;
    
    // Luftfeuchtigkeit (8 bits, 1% Auflösung)
    frame->humi = ((data[3] & 0x0F) << 4) | ((data[4] & 0xF0) >> 4);
    
    // CRC Check (4 bits)
    uint8_t expected_crc = data[4] & 0x0F;
    uint8_t calculated_crc = crc8(data, 4) & 0x0F;
    
    if (expected_crc != calculated_crc) {
        return false;
    }
    
    // Init-Message erkennen (b26-00-56 entspricht ID=0x1644, Temp=-40°C, Humi=0%)
    if (frame->ID == 0x1644 && frame->temp == -40.0 && frame->humi == 0) {
        frame->init = true;
    } else {
        frame->init = false;
    }
    
    // Plausibilitätschecks
    if (frame->temp < -40.0 || frame->temp > 60.0) return false;
    if (frame->humi < 1 || frame->humi > 99) return false;
    if (frame->channel > 3) return false;
    
    return true;
}

void DisplayFrame(uint8_t *data, int len, Frame *frame) {
    Serial.println("\n--- TFA_1 KlimaLogg Pro ---");
    Serial.print("ID: 0x");
    Serial.println(frame->ID, HEX);
    Serial.print("Channel: ");
    Serial.println(frame->channel);
    Serial.print("Temperature: ");
    Serial.print(frame->temp, 1);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(frame->humi);
    Serial.println(" %");
    Serial.print("Battery: ");
    Serial.println(frame->batlo ? "LOW" : "OK");
    if (frame->init) {
        Serial.println("*** INIT MESSAGE ***");
    }
    Serial.println("---------------------------");
}

} // namespace TFA1
