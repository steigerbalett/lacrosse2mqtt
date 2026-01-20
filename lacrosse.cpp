#include "lacrosse.h"

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

#define LACROSSE_TX29_NOHUMIDSENSOR 0x6A  // Sensor without humidity (106 decimal)
#define LACROSSE_TX25_PROBE_FLAG    0x7D  // Second temperature probe channel (125 decimal)

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

    // Kanal-Erkennung - OHNE ID-Manipulation
    f->channel = 1;  // Standard ist Kanal 1
    
    if (humi_raw == LACROSSE_TX29_NOHUMIDSENSOR) {
        // TX29-IT / TX35-IT: Kein Feuchtigkeitssensor, nur Temperatur
        f->channel = 1;
        f->humi = -1;
    }
    else if (humi_raw == LACROSSE_TX25_PROBE_FLAG) {
        // TX25-U: Zweiter Temperatursensor (Sonde/Probe)
        f->channel = 2;
        // ENTFERNT: f->ID += 0x40;  // ← Kein ID-Offset mehr!
        f->humi = -1;
    }
    else if (humi_raw >= 0 && humi_raw <= 100) {
        // TX29DTH-IT / TX35DTH-IT: Gültige Luftfeuchtigkeit
        f->channel = 1;
        f->humi = humi_raw;
    }
    else {
        // Ungültiger Wert
        f->channel = 1;
        f->humi = -1;
    }
}

const char* LaCrosse::GetSensorType(struct Frame *f)
{
    byte humi_raw = f->humi;
    
    // Bei Channel 2: humi ist -1, aber wir wissen es ist 0x7D
    if (f->channel == 2) {
        humi_raw = 0x7D;
    } else if (f->humi == -1) {
        humi_raw = 0x6A;  // Annahme: kein Humidity = 0x6A
    }
    
    // Basierend auf Rate und Humidity-Byte
    if (f->rate == 17241) {
        if (f->humi > 0 && f->humi <= 100) {
            return "TX29DTH-IT";   // Temp + Humidity
        } else if (humi_raw == 0x6A) {
            return "TX29-IT";       // Nur Temp
        } else if (humi_raw == 0x7D) {
            return "TX25-U";        // Probe Sensor
        } else {
            return "TX27-IT";       // Alternativer Temp-Sensor
        }
    } else if (f->rate == 9579) {
        if (f->humi > 0 && f->humi <= 100) {
            return "TX35DTH-IT";    // Temp + Humidity (langsam)
        } else if (humi_raw == 0x6A) {
            return "TX35-IT";       // Nur Temp (langsam)
        } else {
            return "30.3155WD";     // WetterDirekt Sensor
        }
    } else if (f->rate == 8842) {
        return "TX38-IT";           // Spezielle Rate
    }
    
    return "LaCrosse";              // Fallback
}

bool LaCrosse::DisplayFrame(byte *data, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM];
    if (!f->valid) {
        Serial.println("LaCrosse::DisplayFrame FRAME INVALID");
        return false;
    }

    // ID bleibt unverändert - keine Offsets
    int displayID = f->ID;

    DisplayRaw(last[f->ID], "Sensor ", data, FRAME_LENGTH, f->rssi, f->rate);
    
    // Zeige ID + Kanal
    Serial.printf(" ID%-3d Ch%d", displayID, f->channel);
    Serial.printf(" Temp%-5.1f°C", f->temp);
    Serial.printf(" init%d batlo%d", f->init, f->batlo);
    
    // Hole Sensor-Typ
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
#if 0
    if ((data[0] & 0xF0) != 0x90)
        return false;
#endif
    DecodeFrame(data, f);
    return f->valid;
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
