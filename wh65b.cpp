#include "wh65b.h"

namespace WH65B {

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
    // WH65B Frame Format (16 bytes mit UV/Lux):
    // [0] = Preamble (0xFF)
    // [1] = Sensor ID
    // [2-3] = Temperature (signed, 0.1°C)
    // [4] = Humidity (%)
    // [5-6] = Wind Speed (0.1 m/s)
    // [7-8] = Wind Gust (0.1 m/s)
    // [9-10] = Wind Direction (degrees)
    // [11] = Rain (0.3mm per tip)
    // [12] = UV Index (0-15)
    // [13-14] = Light (Lux, 16-bit)
    // [15] = CRC8
    
    if (len != 16) return false;
    if (data[0] != 0xFF) return false;
    
    // CRC Check
    if (crc8(data, 15) != data[15]) return false;
    
    frame->ID = data[1];
    frame->channel = 1;
    
    // Temperatur
    int16_t temp_raw = (data[2] << 8) | data[3];
    frame->temp = temp_raw / 10.0;
    
    // Luftfeuchtigkeit
    frame->humi = data[4];
    
    // Windgeschwindigkeit
    uint16_t wind_speed_raw = (data[5] << 8) | data[6];
    frame->wind_speed = wind_speed_raw / 10.0;
    
    // Windböe
    uint16_t wind_gust_raw = (data[7] << 8) | data[8];
    frame->wind_gust = wind_gust_raw / 10.0;
    
    // Windrichtung
    frame->wind_direction = (data[9] << 8) | data[10];
    
    // Regen
    frame->rain = data[11] * 0.3;
    
    // UV-Index
    frame->uv = data[12];
    
    // Lichtintensität (Lux)
    uint16_t light_raw = (data[13] << 8) | data[14];
    frame->light_lux = light_raw * 10.0;  // 10 Lux Auflösung
    
    // Batteriestatus
    frame->batlo = (data[1] & 0x80) != 0;
    frame->init = false;
    
    return true;
}

void DisplayFrame(uint8_t *data, int len, Frame *frame) {
    Serial.println("\n--- WH65B Weather Sensor ---");
    Serial.print("ID: ");
    Serial.println(frame->ID, HEX);
    Serial.print("Temperature: ");
    Serial.print(frame->temp, 1);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(frame->humi);
    Serial.println(" %");
    Serial.print("Wind Speed: ");
    Serial.print(frame->wind_speed, 2);
    Serial.println(" m/s");
    Serial.print("Wind Gust: ");
    Serial.print(frame->wind_gust, 2);
    Serial.println(" m/s");
    Serial.print("Wind Direction: ");
    Serial.print(frame->wind_direction);
    Serial.println("°");
    Serial.print("Rain: ");
    Serial.print(frame->rain, 1);
    Serial.println(" mm");
    Serial.print("UV Index: ");
    Serial.println(frame->uv);
    Serial.print("Light: ");
    Serial.print(frame->light_lux, 0);
    Serial.println(" lux");
    Serial.print("Battery: ");
    Serial.println(frame->batlo ? "LOW" : "OK");
    Serial.println("-------------------------");
}

} // namespace WH65B