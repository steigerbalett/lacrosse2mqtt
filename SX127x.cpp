#include "globals.h"
#include "SX127x.h"
#include <SPI.h>

bool SX127x::ready()
{
    byte flags = ReadReg(REG_IRQFLAGS2);
    if (!(flags & RF_IRQFLAGS2_FIFOLEVEL))
        return false;
    if (!(flags & RF_IRQFLAGS2_PAYLOADREADY))
        return false;
    return true;
}

bool SX127x::Receive(byte &length)
{
    byte len = FRAME_LENGTH;
    byte i = 0;
    if (! ready())
        return false;

    while (ReadReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY) {
        byte bt = GetByteFromFifo();
        m_payload[i] = bt;
        i++;
        if (ReadReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_FIFOEMPTY)
            break;
    }
    if (i >= len) {
        m_rssi = ReadReg(REG_RSSIVALUE);
        m_payloadready = true;
    }

    if (!m_payloadready)
        return false;

    m_payloadready = false;
    length = i;

    EnableReceiver(false);
    return true;
}

byte *SX127x::GetPayloadPointer()
{
    return &m_payload[0];
}

void SX127x::SetFrequency(unsigned long kHz)
{
    m_frequency = kHz;
    unsigned long f = (((kHz * 1000) << 2) / (32000000L >> 11)) << 6;
    WriteReg(REG_FRFMSB, f >> 16);
    WriteReg(REG_FRFMID, f >> 8);
    WriteReg(REG_FRFLSB, f);
}

void SX127x::EnableReceiver(bool enable, int len)
{
    if (!enable) { /* disable... */
        WriteReg(REG_OPMODE, (ReadReg(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_STANDBY);
        return;
    }
    /* enable... */
    WriteReg(REG_OPMODE, (ReadReg(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_RECEIVER);
    WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | (len - 1));
    WriteReg(REG_PAYLOADLENGTH, len);
    ClearFifo();
}

byte SX127x::GetByteFromFifo()
{
    return ReadReg(0x00);
}

void SX127x::ClearFifo()
{
    WriteReg(REG_IRQFLAGS2, 16);
}

void SX127x::SetupForLaCrosse()
{
    digitalWrite(m_ss, HIGH);
    EnableReceiver(false);

    /* MODULATIONSHAPING_00 == bit 3 and 4 of REG_OPMODE, bit 4 is "reserved", bit 3 is "LowFrequencyModeOn"
     * according to the datasheet. "LowFrequency" => 433MHz, so having it off here is OK. */
    /* 0x01 */ WriteReg(REG_OPMODE, RF_OPMODE_LONGRANGEMODE_OFF | RF_OPMODE_MODULATIONTYPE_FSK |
                                    RF_OPMODE_MODULATIONSHAPING_00 | RF_OPMODE_STANDBY);
    /* 0x04 */ WriteReg(REG_FDEVMSB, RF_FDEVMSB_30000_HZ);
    /* 0x05 */ WriteReg(REG_FDEVLSB, RF_FDEVLSB_30000_HZ);
//  /* 0x0d */ WriteReg(REG_RXCONFIG, RF_RXCONFIG_AFCAUTO_ON|RF_RXCONFIG_AGCAUTO_ON|RF_RXCONFIG_RXTRIGER_PREAMBLEDETECT);
//  /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_2); // 100kHz
    /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_16 | RF_RXBW_EXP_2); // 125kHz
//  /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_24 | RF_RXBW_EXP_1); // 166kHz
    /* 0x3F */ WriteReg(REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN);
    /* 0x29 */ WriteReg(REG_RSSITHRESH, 210);
    /* 0x27 */ WriteReg(REG_SYNCCONFIG, RF_SYNCCONFIG_AUTORESTARTRXMODE_WAITPLL_ON |
                                        RF_SYNCCONFIG_SYNC_ON | RF_SYNCCONFIG_SYNCSIZE_2);
    /* 0x28 */ WriteReg(REG_SYNCVALUE1, 0x2D);
    /* 0x29 */ WriteReg(REG_SYNCVALUE2, 0xD4);
    /* 0x30 */ WriteReg(REG_PACKETCONFIG1, RF_PACKETCONFIG1_CRCAUTOCLEAR_OFF);
    /* 0x31 */ WriteReg(REG_PACKETCONFIG2, RF_PACKETCONFIG2_DATAMODE_PACKET);
//  /* 0x35 */ WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | RF_FIFOTHRESH_FIFOTHRESHOLD_THRESHOLD);
//  /* 0x35 */ WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | 4);
    /* 0x35 */ WriteReg(REG_FIFOTHRESH, 4);
    /* 0x38 */ WriteReg(REG_PAYLOADLENGTH, 6 /*PAYLOADSIZE*/);
    /* 0x40 */ WriteReg(REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_00);
    /* 0x41 */ WriteReg(REG_DIOMAPPING2, RF_DIOMAPPING2_MAP_PREAMBLEDETECT);

}

byte SX127x::ReadReg(byte addr)
{
    SPI.beginTransaction(SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0));
    digitalWrite(m_ss, LOW);
    SPI.transfer(addr & 0x7F);
    uint8_t regval = SPI.transfer(0);
    digitalWrite(m_ss, HIGH);
    SPI.endTransaction();
    return regval;
}

void SX127x::WriteReg(byte addr, byte value)
{
    SPI.beginTransaction(SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0));
    digitalWrite(m_ss, LOW);
    SPI.transfer(addr | 0x80);
    SPI.transfer(value);
    digitalWrite(m_ss, HIGH);
    SPI.endTransaction();
}

bool SX127x::init()
{
    pinMode(m_ss, OUTPUT);
    delay(10);
    digitalWrite(m_ss, HIGH);
    if (m_reset != (byte)-1) {
        pinMode(m_reset, OUTPUT);
        digitalWrite(m_reset, LOW);
        delay(10);
        digitalWrite(m_reset, HIGH);
        delay(10);
    }
    SPI.begin();
    uint8_t version = ReadReg(REG_VERSION);
    if (version != 0x12)
        return false;
    return true;
}
SX127x::SX127x(byte ss, byte reset)

{
    m_reset = reset;
    m_ss = ss;
    m_datarate = 0;
    m_frequency = 868250;
    m_payloadready = false;
    num_active_rates = 0;
    current_rate_index = 0;
    for (int i = 0; i < 6; i++) {
        active_rates[i] = false;
    }
}

int SX127x::GetDataRate()
{
    return m_datarate;  // m_datarate enthält jetzt direkt den Wert (z.B. 17241)
}

int8_t SX127x::GetRSSI()
{
    return -(m_rssi / 2);
}

void SX127x::SetActiveDataRates(bool rate_17241, bool rate_9579, bool rate_8842, bool rate_6618, bool rate_4800, bool use_38400)
{
    active_rates[0] = rate_17241;  // 17.241 kbps
    active_rates[1] = rate_9579;   // 9.579 kbps
    active_rates[2] = rate_8842;   // 8.842 kbps
    active_rates[3] = rate_6618;   // 6.618 kbps
    active_rates[4] = rate_4800;   // 4.800 kbps
    active_rates[5] = use_38400;   // 38.400 kbps
    
    // Zähle aktive Raten
    num_active_rates = 0;
    for (int i = 0; i < 6; i++) {
        if (active_rates[i]) num_active_rates++;
    }
}

void SX127x::SetDataRate(int bitrate)
{
    // Berechne BitRate Register
    unsigned long br = 32000000L / bitrate;
    WriteReg(REG_BITRATEMSB, br >> 8);
    WriteReg(REG_BITRATELSB, br);
    
    // Setze Frequenzabweichung und RX-Bandbreite je nach Bitrate
    switch (bitrate) {
        case 17241:  // LaCrosse IT+, WH24, WH25, WH65B, HP1000
            // FDEV = 9579 Hz -> Register = (9579 * 2^19) / 32000000 = 156
            WriteReg(REG_FDEVMSB, 0x00);
            WriteReg(REG_FDEVLSB, 0x9C);
            WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_3); // ~25 kHz
            break;
            
        case 9579:   // TX35-IT
            // FDEV = 9579 Hz
            WriteReg(REG_FDEVMSB, 0x00);
            WriteReg(REG_FDEVLSB, 0x9C);
            WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_3); // ~25 kHz
            break;
            
        case 8842:   // TX38-IT
            // FDEV = 30000 Hz
            WriteReg(REG_FDEVMSB, RF_FDEVMSB_30000_HZ);
            WriteReg(REG_FDEVLSB, RF_FDEVLSB_30000_HZ);
            WriteReg(REG_RXBW, RF_RXBW_MANT_16 | RF_RXBW_EXP_2); // ~50 kHz
            break;
            
        case 6618:   // WH1080, WS1600, WT440XH, TX22-IT, EMT7110
            // FDEV = 11000 Hz -> Register = (11000 * 2^19) / 32000000 = 179
            WriteReg(REG_FDEVMSB, 0x00);
            WriteReg(REG_FDEVLSB, 0xB3);
            WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_3); // ~25 kHz
            break;
            
        case 4800:   // W136
            // FDEV = 2500 Hz -> Register = (2500 * 2^19) / 32000000 = 41
            WriteReg(REG_FDEVMSB, 0x00);
            WriteReg(REG_FDEVLSB, 0x29);
            WriteReg(REG_RXBW, RF_RXBW_MANT_24 | RF_RXBW_EXP_4); // ~10.4 kHz
            break;
            
        case 38400:  // TFA_1 KlimaLogg Pro
            // FDEV = 19200 Hz -> Register = (19200 * 2^19) / 32000000 = 315
            WriteReg(REG_FDEVMSB, 0x01);
            WriteReg(REG_FDEVLSB, 0x3B);
            WriteReg(REG_RXBW, RF_RXBW_MANT_16 | RF_RXBW_EXP_1); // ~83.3 kHz
            break;
            
        default:     // Fallback zu 17.241 kbps
            WriteReg(REG_FDEVMSB, 0x00);
            WriteReg(REG_FDEVLSB, 0x9C);
            WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_3);
            break;
    }
    
    m_datarate = bitrate;
    
    if (config.debug_mode) {
        Serial.printf("SetDataRate: %d bps (BR=0x%04X)\n", bitrate, (unsigned int)br);
    }
}

void SX127x::NextDataRate(int useRate)
{
    if (useRate < 0) {
        // Auto-Modus: Wechsle zur nächsten aktiven Rate
        int start_index = current_rate_index;
        do {
            current_rate_index = (current_rate_index + 1) % 6;
            
            // Verhindere Endlosschleife wenn keine Rate aktiv ist
            if (current_rate_index == start_index && num_active_rates == 0) {
                current_rate_index = 0;  // Fallback
                break;
            }
        } while (!active_rates[current_rate_index] && current_rate_index != start_index);
    } else {
        // Explizite Rate setzen
        if (useRate >= 0 && useRate < 6) {
            current_rate_index = useRate;
        }
    }
    
    // Konfiguriere die Hardware für die gewählte Rate
    switch (current_rate_index) {
        case 0: SetDataRate(17241); break;
        case 1: SetDataRate(9579); break;
        case 2: SetDataRate(8842); break;
        case 3: SetDataRate(6618); break;
        case 4: SetDataRate(4800); break;
        case 5: SetDataRate(38400); break;
        default: SetDataRate(17241); break;
    }
    
    if (config.debug_mode) {
        Serial.printf("Switched to data rate index %d (%d bps)\n", current_rate_index, m_datarate);
    }
}