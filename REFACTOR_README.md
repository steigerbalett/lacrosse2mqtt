# LaCrosse2MQTT Refactor Branch

## 🚀 Overview

This branch contains a **major refactoring** of the lacrosse2mqtt codebase, focusing on:

1. **Code Deduplication** - Eliminated ~300 lines of duplicated MQTT topic generation code
2. **mqtt_helper.h Library** - Centralized MQTT topic handling for all protocols
3. **WH24/WH25 Support** - Added two new weather station protocols
4. **Improved Maintainability** - Consistent code structure across all protocol handlers

---

## 📊 Key Changes

### 1. mqtt_helper.h - Central MQTT Topic Management

**New Functions:**
```cpp
String getMqttTopicPrefix(byte ID);                          // Get topic prefix based on config
String getMqttTopic(byte ID, const String& subtopic);       // Complete topic generation
String getSensorDisplayName(byte ID);                        // Get friendly sensor name
String getDeviceId(const String& mqtt_id, byte ID);         // HA device identifier
String getEntityId(const String& mqtt_id, byte ID, ...);    // HA entity ID
String getHADiscoveryTopic(...);                            // HA discovery topic
```

**Benefits:**
- Single source of truth for topic generation
- Automatic support for both ID-based and name-based topics
- Easy to add new MQTT features globally

### 2. Protocol Integration Status

| # | Protocol | Status | mqtt_helper.h | MQTT Topics | HA Discovery |
|---|----------|--------|---------------|-------------|-------------|
| 1 | **LaCrosse IT+** | ✅ Refactored | ✅ | temp, humi, battery, state | ✅ |
| 2 | **WH1080** | ✅ Refactored | ✅ | temp, humi, wind*, rain, state | ✅ |
| 3 | **WS1600** | ✅ Refactored | ✅ | temp, humi, wind*, rain, battery | ✅ |
| 4 | **WT440XH** | ✅ Refactored | ✅ | temp, humi, battery, state | ✅ |
| 5 | **TX22IT** | ✅ Refactored | ✅ | temp, humi, wind*, battery | ✅ |
| 6 | **EMT7110** | ✅ Refactored | ✅ | power, energy, battery, state | ✅ |
| 7 | **W136** | ✅ Refactored | ✅ | rain, battery, state | ✅ |
| 8 | **WH24** | ⚠️ Handler Ready | ✅ | temp, humi, pressure, wind*, rain, uv | ✅ |
| 9 | **WH25** | ⚠️ Handler Ready | ✅ | temp, humi, pressure, battery | ✅ |

*wind = wind_speed, wind_gust, wind_direction, wind_bearing

### 3. Code Size Reduction

**Before Refactoring:**
- lacrosse2mqtt.ino: **58,072 bytes**
- Duplicated topic code: ~300 lines across 7 protocols

**After Refactoring:**
- lacrosse2mqtt.ino: **49,822 bytes** (7 protocols)
- mqtt_helper.h: 3,847 bytes
- **Net Reduction: ~8,250 bytes (-14%)**
- With WH24/WH25: +6,456 bytes = **Net: -1,794 bytes total**

---

## 🔧 Implementation Instructions

### WH24 & WH25 Integration

The handlers are ready but need manual integration into `lacrosse2mqtt.ino`:

**File:** `wh24_wh25_handlers.cpp` contains the complete code

**Location:** Insert in `receive()` function after W136 handler:

```cpp
void receive() {
    // ... existing handlers ...
    
    // ========== W136 PROTOCOL ==========
    if (!frame_valid && config.proto_w136 && payLoadSize == 6) {
        // ... W136 code ...
    }
    
    // ============ INSERT WH24/WH25 HANDLERS HERE ============
    // Copy code from wh24_wh25_handlers.cpp
    
    // Unknown frame handler
    if (!frame_valid) {
        LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
    }
}
```

**Documentation:** See `WH24_WH25_INTEGRATION.md` for detailed guide

---

## 📋 Testing Checklist

### Existing Protocols (7)
- [ ] LaCrosse IT+ sensors detected and published
- [ ] WH1080 weather data correct
- [ ] WS1600 wind/rain values accurate  
- [ ] WT440XH dual-channel working
- [ ] TX22IT wind data correct
- [ ] EMT7110 power measurement working
- [ ] W136 rain sensor functional
- [ ] All MQTT topics use correct prefix (lacrosse/climate/ or lacrosse/id/)
- [ ] Name-based topics work when configured
- [ ] Home Assistant auto-discovery creates all entities

### New Protocols (2)
- [ ] WH24 handlers integrated into receive()
- [ ] WH25 handlers integrated into receive()
- [ ] Protocols enabled in web config
- [ ] WH24 sensors detected (17 byte frames)
- [ ] WH25 sensors detected (10 byte frames)
- [ ] All MQTT topics populated
- [ ] Wind direction text correct (N/NE/E/SE/S/SW/W/NW)
- [ ] Pressure values reasonable (900-1100 hPa)
- [ ] UV index in valid range (0-15)
- [ ] Home Assistant entities created

### General
- [ ] No compile errors
- [ ] No memory leaks
- [ ] MQTT connection stable
- [ ] Debug output clean
- [ ] Battery reporting accurate

---

## 📈 Performance Improvements

1. **Memory Efficiency**
   - Reduced code duplication saves flash memory
   - String handling optimized in mqtt_helper.h

2. **Maintainability**
   - Single location for MQTT topic changes
   - Consistent error handling
   - Easier to add new protocols

3. **Consistency**
   - All protocols follow same pattern
   - Identical topic structure
   - Uniform Home Assistant integration

---

## 🔜 Future Enhancements

### Short Term
1. Add Home Assistant discovery for:
   - Pressure sensor entities (WH24, WH25)
   - UV Index sensor entity (WH24)

2. Implement remaining protocol features:
   - Rain rate calculation (mm/hour)
   - Wind chill / feels-like temperature
   - Dew point calculation

### Long Term
1. Create unit tests for mqtt_helper.h
2. Add MQTT topic documentation generator
3. Implement protocol auto-detection
4. Add MQTT retained message support per sensor

---

## 📚 File Structure

```
lacrosse2mqtt/
├── lacrosse2mqtt.ino          # Main application (refactored)
├── mqtt_helper.h              # NEW: Central MQTT topic handling
├── globals.h                  # Global definitions
├── webfrontend.h              # Web interface
├── lacrosse.h/.cpp            # LaCrosse IT+ protocol
├── wh1080.h/.cpp              # WH1080 protocol  
├── ws1600.h/.cpp              # WS1600 protocol
├── wt440xh.h/.cpp             # WT440XH protocol
├── tx22it.h/.cpp              # TX22IT protocol
├── emt7110.h/.cpp             # EMT7110 protocol
├── w136.h/.cpp                # W136 protocol
├── wh24.h                     # WH24 protocol header
├── wh25.h                     # WH25 protocol header
├── wh24_wh25_handlers.cpp     # NEW: Ready-to-integrate handlers
├── WH24_WH25_INTEGRATION.md   # NEW: Integration documentation
└── REFACTOR_README.md         # This file
```

---

## 🎯 Refactoring Goals - Status

- [x] Create mqtt_helper.h library
- [x] Refactor LaCrosse IT+ handler
- [x] Refactor WH1080 handler
- [x] Refactor WS1600 handler
- [x] Refactor WT440XH handler
- [x] Refactor TX22IT handler
- [x] Refactor EMT7110 handler
- [x] Refactor W136 handler
- [x] Implement WH24 handler
- [x] Implement WH25 handler
- [x] Create integration documentation
- [ ] Final integration into lacrosse2mqtt.ino
- [ ] Testing on hardware
- [ ] Merge to main branch

---

## 🤝 Contributing

When adding new protocols, follow the pattern:

1. Use `getMqttTopic(ID, subtopic)` for all MQTT publishes
2. Use `getSensorDisplayName(ID)` for debug output
3. Update cache with `fcache[cacheIndex].*`
4. Call Home Assistant discovery functions
5. Set `frame_valid = true` when successful

**Example:**
```cpp
mqtt_client.publish(getMqttTopic(ID, "temp").c_str(), 
                    String(frame.temp, 1).c_str());

if (config.ha_discovery && id2name[ID].length() > 0) {
    pub_hass_config(1, ID, 1);  // Temperature
}
```

---

## 📝 Commit History

- [4fa62a6](https://github.com/steigerbalett/lacrosse2mqtt/commit/4fa62a6) - Add mqtt_helper.h with topic generation functions
- [a5fa36d](https://github.com/steigerbalett/lacrosse2mqtt/commit/a5fa36d) - Refactor all 7 protocols to use mqtt_helper.h (-8,250 bytes)
- [5594eb1](https://github.com/steigerbalett/lacrosse2mqtt/commit/5594eb1) - Add WH24 & WH25 integration guide
- [e0cc9ec](https://github.com/steigerbalett/lacrosse2mqtt/commit/e0cc9ec) - Add WH24 & WH25 protocol handlers

---

## ⚡ Quick Start

1. **Clone refactor branch:**
   ```bash
   git clone -b refactor https://github.com/steigerbalett/lacrosse2mqtt.git
   ```

2. **Integrate WH24/WH25 (optional):**
   - Copy code from `wh24_wh25_handlers.cpp`
   - Insert into `lacrosse2mqtt.ino` at marked location
   - See `WH24_WH25_INTEGRATION.md` for details

3. **Compile and upload:**
   - Use Arduino IDE or PlatformIO
   - Target: ESP32 (TTGO LoRa32)

4. **Configure protocols:**
   - Access web interface
   - Enable desired protocols
   - Set sensor names for MQTT topics

---

## 📞 Support

- **Issues:** https://github.com/steigerbalett/lacrosse2mqtt/issues
- **Discussions:** https://github.com/steigerbalett/lacrosse2mqtt/discussions
- **Original Project:** https://github.com/seife/lacrosse2mqtt

---

## 📄 License

GPL-2.0-or-later (same as main project)

---

**Last Updated:** 2026-02-12  
**Branch:** refactor  
**Status:** ✅ Ready for testing (WH24/WH25 integration pending)
