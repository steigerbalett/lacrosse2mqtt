#ifndef _SX127X_H
#define _SX127X_H

#include <Arduino.h>
#include "sx1276Regs-Fsk.h"

#define PAYLOAD_SIZE 64

class SX127x {
private:
    byte m_ss;
    byte m_reset;
    byte m_datarate;
    unsigned long m_frequency;
    uint8_t m_payload[PAYLOAD_SIZE];
    bool m_payloadready;
    byte m_rssi;
    
    // Active data rates configuration
    int active_rates[3];
    int active_rate_count;
    int current_rate_index;

    bool ready();
    byte GetByteFromFifo();
    void ClearFifo();
    byte ReadReg(byte addr);
    void WriteReg(byte addr, byte value);
    void SetRate(int rate);

public:
    SX127x(byte ss, byte reset = (byte)-1);
    bool init();
    void SetFrequency(unsigned long kHz);
    void EnableReceiver(bool enable, int len = 5);
    bool Receive(byte &length);
    byte *GetPayloadPointer();
    void SetupForLaCrosse();
    int GetDataRate();
    int8_t GetRSSI();
    void SetActiveDataRates(bool rate_17241, bool rate_9579, bool rate_8842);
    void NextDataRate(byte idx = 0xff);
};

#endif
