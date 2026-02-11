/*
* Message Format:
*
* .- [0] -. .- [1] -. .- [2] -. .- [3] -. .- [4] -.
* SSSS.DDDD DDN_.TTTT TTTT.TTTT WHHH.HHHH CCCC.CCCC
* | | | || | | | | | | || | | |
* | | | || | | | | | | || | `--------- CRC
* | | | || | | | | | | |`-------- humidity%
* | | | || | | | | | | `---- weak battery
* | | | || `--------------- Temperature BCD, T = X/10-40
* | | | | `--- new battery
* | | `-------- sensor ID
* `---- start byte
*
* Special humidity values:
* 0x6A (106): No humidity sensor (TX29-IT, TX35-IT)
* 0x7D (125): Second temperature probe channel (TX25-U)
*
* Data rates:
* 17.241 kbps: TX25-IT, TX27-IT, TX29-IT, TX29DTH-IT, TX37, 30.3143.IT, 30.3144.IT
* 9.579 kbps:  TX35TH-IT, TX35DTH-IT, TX38-IT, 30.3155WD, 30.3156WD
*
* Based on: https://github.com/merbanan/rtl_433/blob/master/src/devices/lacrosse_tx35.c
*/
#include "lacrosse.h"
#include <Arduino.h>

#define LACROSSE_TX29_NOHUMIDSENSOR 0x6A
#define LACROSSE_TX25_PROBE_FLAG    0x7D

void LaCrosse::DecodeFrame(byte *bytes, struct Frame *f)
{
    f->valid = true;
    if (bytes[4] != CalculateCRC(bytes, FRAME_LENGTH - 1))
        f->valid = false;
    if ((bytes[0] & 0xF0) != 0x90)
        f->valid = false;

    // SSSS.DDDD DDN_.TTTT TTTT.TTTT WHHH.HHHH CCCC.CCCC
    f->ID = (bytes[0] & 0xF) << 2;
    f->ID |= (bytes[1] & 0xC0) >> 6;
    f->init = (bytes[1] & 0x20) ? 1 : 0;

    byte bcd[3];
    bcd[0] = bytes[1] & 0xF;
    bcd[1] = (bytes[2] & 0xF0) >> 4;
    bcd[2] = (bytes[2] & 0xF);
    f->temp = ((bcd[0] * 100 + bcd[1] * 10 + bcd[2]) - 400) / 10.0;

    f->batlo = (bytes[3] & 0x80) ? 1 : 0;
    byte humi_raw = bytes[3] & 0x7f;

    f->channel = 1;
    
    if (humi_raw == LACROSSE_TX29_NOHUMIDSENSOR) {
        f->channel = 1;
        f->humi = -1;
    }
    else if (humi_raw == LACROSSE_TX25_PROBE_FLAG) {
        f->channel = 2;
        f->humi = -1;
    }
    else if (humi_raw >= 0 && humi_raw <= 100) {
        f->channel = 1;
        f->humi = humi_raw;
    }
    else {
        f->channel = 1;
        f->humi = -1;
    }
}

// TX141/TX145 Protokoll Dekodierung
bool LaCrosse::DecodeTX141Frame(byte *bytes, struct Frame *f)
{
    memset(f, 0, sizeof(*f));
    
    // Einfache XOR-Checksum prüfen
    byte crc_calc = 0;
    for (int i = 0; i < 4; i++) {
        crc_calc ^= bytes[i];
    }
    
    // CRC muss passen (toleranz ±1 wegen möglicher Bitfehler)
    if (abs((int)crc_calc - (int)bytes[4]) > 1) {
        Serial.printf(" [CRC fail: calc=%02X got=%02X]", crc_calc, bytes[4]);
        return false;
    }
    
    // ID extrahieren
    f->ID = (bytes[0] >> 2) & 0x3F;
    
    if (f->ID > 63) {
        f->ID = bytes[1] & 0x3F;
    }
    
    f->channel = 1;
    
    // Temperatur-Dekodierung (mehrere Formate probieren)
    int16_t temp_raw_a = ((bytes[1] & 0x0F) << 8) | bytes[2];
    float temp_a = (temp_raw_a - 500) / 10.0f;
    
    int16_t temp_raw_b = ((bytes[2] << 4) | (bytes[3] >> 4));
    float temp_b = (temp_raw_b - 500) / 10.0f;
    
    int16_t temp_raw_c = (bytes[2] << 8) | bytes[3];
    float temp_c = temp_raw_c / 10.0f - 40.0f;
    
    int bcd1 = (bytes[2] >> 4) & 0x0F;
    int bcd2 = bytes[2] & 0x0F;
    int bcd3 = (bytes[3] >> 4) & 0x0F;
    float temp_d = (bcd1 * 10 + bcd2 + bcd3 / 10.0f) - 40.0f;
    
    float chosen_temp = 999.9f;
    
    if (temp_a >= -40 && temp_a <= 60) chosen_temp = temp_a;
    else if (temp_b >= -40 && temp_b <= 60) chosen_temp = temp_b;
    else if (temp_c >= -40 && temp_c <= 60) chosen_temp = temp_c;
    else if (temp_d >= -40 && temp_d <= 60) chosen_temp = temp_d;
    
    if (chosen_temp == 999.9f) {
        Serial.print(" [No valid temp]");
        return false;
    }
    
    f->temp = chosen_temp;
    f->batlo = (bytes[3] & 0x08) ? 1 : 0;
    f->init = (bytes[0] & 0x80) ? 1 : 0;
    f->humi = -1;
    f->valid = true;
    
    Serial.printf(" [TX141: A=%.1f B=%.1f C=%.1f D=%.1f -> %.1f]", 
                  temp_a, temp_b, temp_c, temp_d, chosen_temp);
    
    return true;
}

const char* LaCrosse::GetSensorType(struct Frame *f)
{
    byte humi_raw = f->humi;
    
    if (f->channel == 2) {
        humi_raw = 0x7D;
    } else if (f->humi == -1) {
        humi_raw = 0x6A;
    }
    
    // KORRIGIERT: TX141 nur erkennen wenn ID >= 64 (wurde von DecodeTX141Frame gesetzt)
    // ODER wenn es explizit als alternatives Protokoll dekodiert wurde
    // Normal dekodierte Sensoren (0x9X) sind NIEMALS TX141!
    
    if (f->rate == 17241) {
        if (f->humi > 0 && f->humi <= 100) {
            return "TX29DTH-IT";
        } else if (humi_raw == 0x6A) {
            return "TX29-IT";       // Nur Temp, kein Humidity
        } else if (humi_raw == 0x7D) {
            return "TX25-U";        // Probe Sensor (Channel 2)
        } else {
            return "TX27-IT";
        }
    } else if (f->rate == 9579) {
        if (f->humi > 0 && f->humi <= 100) {
            return "TX35DTH-IT";
        } else if (humi_raw == 0x6A) {
            return "TX35-IT";
        } else {
            return "30.3155WD";
        }
    } else if (f->rate == 8842) {
        return "TX38-IT";
    }
    
    return "LaCrosse";
}

bool LaCrosse::DisplayFrame(byte *data, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("LaCrosse::DisplayFrame FRAME INVALID");
        return false;
    }

    int displayID = f->ID;

    DisplayRaw(last[f->ID], "Sensor ", data, FRAME_LENGTH, f->rssi, f->rate);
    
    Serial.printf(" ID%-3d Ch%d", displayID, f->channel);
    Serial.printf(" Temp%-5.1f°C", f->temp);
    Serial.printf(" init%d batlo%d", f->init, f->batlo);
    
    const char* sensor_type = GetSensorType(f);
    
    if (f->humi > 0 && f->humi <= 100) {
        Serial.printf(" Humi%d%% (%s)", f->humi, sensor_type);
    } else {
        Serial.printf(" (%s)", sensor_type);
    }
    
    Serial.println();
    
    return true;
}

bool LaCrosse::TryHandleData(byte *data, struct Frame *f)
{
    // Zuerst: Standard 0x9X Protokoll (LaCrosse IT+)
    if ((data[0] & 0xF0) == 0x90) {
        DecodeFrame(data, f);
        return f->valid;
    }
    
    // TX141/TX145 oder andere alternative Protokolle (NICHT 0x9X)
    // Diese werden als TX141 behandelt
    return DecodeTX141Frame(data, f);
}

byte LaCrosse::UpdateCRC(byte res, uint8_t val)
{
    for (int i = 0; i < 8; i++) {
        uint8_t tmp = (uint8_t)((res ^ val) & 0x80);
        res <<= 1;
        if (0 != tmp) {
            res ^= 0x31;
        }
        val <<= 1;
    }
    return res;
}

byte LaCrosse::CalculateCRC(byte *data, byte len)
{
    byte res = 0;
    for (int j = 0; j < len; j++) {
        uint8_t val = data[j];
        res = UpdateCRC(res, val);
    }
    return res;
}

void LaCrosse::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate)
{
    unsigned long now = millis();
    if (last == 0)
        last = now;
    Serial.printf("%6ld %s [", (now - last), dev);
    last = now;
    for (uint8_t i = 0; i < len; i++)
        Serial.printf("%02X%s", data[i],(i==len-1)?"":" ");
    Serial.printf("] rssi%-4d rate%-5d", rssi, rate);
}