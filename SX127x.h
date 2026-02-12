#ifndef _SX127X_H
#define _SX127X_H

#include <Arduino.h>
#include "sx1276Regs-Fsk.h"

#define PAYLOAD_SIZE 64

class SX127x {
private:
    int m_datarate;
    unsigned long m_frequency;
    int8_t m_rssi;
    byte m_reset, m_ss;
    byte m_payload[FRAME_LENGTH];
    bool m_payloadready;
    
    // Datenraten-Verwaltung
    bool active_rates[6];
    int num_active_rates;
    int current_rate_index;
    
    byte ReadReg(byte addr);
    void WriteReg(byte addr, byte value);
    void ClearFifo();
    byte GetByteFromFifo();
    void SetDataRate(int bitrate);

public:
    SX127x(byte ss, byte reset = -1);
    bool init();
    void SetFrequency(unsigned long kHz);
    void SetupForLaCrosse();
    void EnableReceiver(bool enable, int len = FRAME_LENGTH);
    void SetActiveDataRates(bool rate_17241, bool rate_9579, bool rate_8842, bool rate_6618, bool rate_4800, bool use_38400 = false);
    void NextDataRate(int useRate = -1);
    bool Receive(byte &length);
    byte *GetPayloadPointer();
    int GetDataRate();
    int8_t GetRSSI();
    bool ready();
};

#endif