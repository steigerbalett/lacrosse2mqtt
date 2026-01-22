#ifndef _SX127x_H
#define _SX127x_H

#include <Arduino.h>
#include "sx1276Regs-Fsk.h"

class SX127x {
public:
  void SetupForLaCrosse();
  void SetFrequency(unsigned long f);
  bool Receive(byte &payLoadSize);
  int GetDataRate();
  int8_t GetRSSI();
  byte *GetPayloadPointer();
  void EnableReceiver(bool enable, int len = 5);
  void SetRate(int rate);
  void NextDataRate(byte idx = 0xff);
  void SetActiveDataRates(bool rate_17241, bool rate_9579, bool rate_8842);
  bool init();
  
  SX127x(byte ss, byte reset);

private:
  void WriteReg(byte addr, byte data);
  byte ReadReg(byte addr);
  byte GetByteFromFifo();
  void ClearFifo();
  bool ready();
  
  byte m_ss;
  byte m_reset;
  byte m_datarate;
  unsigned long m_frequency;
  bool m_payloadready;
  byte m_payload[32];
  int8_t m_rssi;
  
  int active_rates[3];      
  int active_rate_count;    
  int current_rate_index;   
};

#endif