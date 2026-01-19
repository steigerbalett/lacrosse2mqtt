#include "lacrosse.h"

/*
* Message Format:
*
* .- [0] -. .- [1] -. .- [2] -. .- [3] -. .- [4] -.
* SSSS.DDDD DDN_.TTTT TTTT.TTTT WHHH.HHHH CCCC.CCCC
* |  | |     ||  |  | |  | |  | ||      | |       |
* |  | |     ||  |  | |  | |  | ||      | `--------- CRC
* |  | |     ||  |  | |  | |  | |`-------- humidity%
* |  | |     ||  |  | |  | |  | `---- weak battery
* |  | |     ||  `--------------- Temperature BCD, T = X/10-40
* |  | |     | `--- new battery
* |  | `-------- sensor ID
* `---- start byte
*
* more details:
* [https://github.com/merbanan/rtl_433/blob/master/src/devices/lacrosse_tx35.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/lacrosse_tx35.c)
*/
void LaCrosse::DecodeFrame(byte *bytes, struct Frame *f)
{
    f->valid = true;

    if (bytes[4] != CalculateCRC(bytes, FRAME_LENGTH - 1))
        f->valid = false;

    if ((bytes[0] & 0xF0) != 0x90)
        f->valid = false;

    // SSSS.DDDD DDN_.TTTT TTTT.TTTT WHHH.HHHH CCCC.CCCC
    f->ID =  (bytes[0] & 0xF) << 2;
    f->ID |= (bytes[1] & 0xC0) >> 6;

    f->init = (bytes[1] & 0x20) ? 1 : 0;

    byte bcd[3];
    bcd[0] = bytes[1] & 0xF;
    bcd[1] = (bytes[2] & 0xF0) >> 4;
    bcd[2] = (bytes[2] & 0xF);
    f->temp  = ((bcd[0] * 100 + bcd[1] * 10 + bcd[2]) - 400) / 10.0;
    f->batlo = (bytes[3] & 0x80) ? 1 : 0;
    f->humi  = bytes[3] & 0x7f;
    
    // Kanal 2 Erkennung - verschiedene Sensoren verwenden verschiedene Magic Values:
    // - TX29-IT: 0x7d (125 dezimal)
    // - TX25-TH: 0x6A (106 dezimal)
    if (f->humi == 106 || f->humi == 0x7d) {
      f->ID += 64;
    f->humi = -1;  // Keine Luftfeuchtigkeit
} else if (f->humi > 100) {
    f->humi = -1;  // Markiere als "keine Humidity"
}
    
    // Rate-basierte ID-Offsets NACH Kanal-2-Erkennung
    if (f->rate == 9579)    // slow rate sensors => increase ID by 128
        f->ID |= 0x80;
    if (f->rate == 8842)
        f->ID |= 0x40;
}

bool LaCrosse::DisplayFrame(byte *data, struct Frame *f)
{
    static unsigned long last[SENSOR_NUM]; /* one for each sensor ID */

    if (!f->valid) {
        Serial.println("LaCrosse::DisplayFrame FRAME INVALID");
        return false;
    }

    DisplayRaw(last[f->ID], "Sensor ", data, FRAME_LENGTH, f->rssi, f->rate);

    Serial.printf(" ID:%-3d Temp:%-5.1f°C", f->ID, f->temp);
    Serial.printf(" init:%d batlo:%d", f->init, f->batlo);
    
    if (f->humi > 0 && f->humi <= 100)
        Serial.printf(" Humi:%d%%", f->humi);
    else if (f->humi == -1)
        Serial.print(" (Channel 2, no humidity)");
    
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
    Serial.printf("] rssi:%-4d rate:%-5d", rssi, rate);
}