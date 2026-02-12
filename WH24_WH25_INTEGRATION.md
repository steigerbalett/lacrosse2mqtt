# WH24 & WH25 Protocol Integration Guide

## Overview

This document describes the implementation of **WH24** (outdoor weather station) and **WH25** (indoor sensor) protocol support in lacrosse2mqtt.

## Protocols

### WH24 - Outdoor Weather Station
- **Payload Size**: 17 bytes
- **Sensors**: Temperature, Humidity, Pressure, Wind Speed, Wind Gust, Wind Direction, Rain, UV Index
- **Manufacturer**: Fine Offset / Ecowitt
- **Models**: WH24, WH65B compatible sensors

### WH25 - Indoor Sensor
- **Payload Size**: 10 bytes  
- **Sensors**: Temperature, Humidity, Pressure
- **Manufacturer**: Fine Offset / Ecowitt

## Implementation Location

The handlers must be inserted in `lacrosse2mqtt.ino` within the `receive()` function:

```
receive() {
    // ... LaCrosse handler
    // ... WH1080, WS1600, WT440XH, TX22IT, EMT7110, W136 handlers
    
    // === INSERT WH24 HANDLER HERE ===
    // === INSERT WH25 HANDLER HERE ===
    
    if (!frame_valid) {
        LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
    }
}
```

## Key Features

### ✅ mqtt_helper.h Integration
- All MQTT topics use `getMqttTopic(ID, subtopic)`
- Consistent with refactored codebase
- Supports both ID-based and name-based topics

### ✅ Cache Management  
- Updates `fcache[cacheIndex]` with all sensor values
- Stores sensor type as "WH24" or "WH25"
- Tracks RSSI, data rate, battery status

### ✅ Home Assistant Auto-Discovery
- **WH24**: Temperature, Humidity, Wind (speed/direction/gust), Rain, Battery
- **WH25**: Temperature, Humidity, Battery
- Pressure and UV Index ready for future HA entities

### ✅ MQTT Topics Published

#### WH24 Topics:
- `temp` - Temperature (°C)
- `humi` - Humidity (%)
- `pressure` - Air pressure (hPa)
- `wind_speed` - Wind speed (m/s)
- `wind_gust` - Wind gust (m/s)
- `wind_bearing` - Wind direction (degrees)
- `wind_direction` - Wind direction (text: N, NE, E, ...)
- `rain` - Rain (mm)
- `uv_index` - UV Index (0-15)
- `battery` - Battery (10% = low, 100% = ok)
- `state` - JSON with RSSI, batlo, type

#### WH25 Topics:
- `temp` - Temperature (°C)
- `humi` - Humidity (%)
- `pressure` - Air pressure (hPa)
- `battery` - Battery (10% = low, 100% = ok)
- `state` - JSON with RSSI, batlo, type

## Configuration

Enable protocols in web interface or `config.json`:

```json
{
  "proto_wh24": true,
  "proto_wh25": true
}
```

## Debug Output

With `config.debug_mode = true`:

```
[MQTT] WH24 ID=42 Name=Outdoor_Station
[MQTT] UV Index: 5, Pressure: 1013.2 hPa

[MQTT] WH25 ID=17 Name=Indoor_Sensor  
[MQTT] Pressure: 1015.8 hPa
```

## Handler Code Size

- **WH24 Handler**: ~3900 characters
- **WH25 Handler**: ~2500 characters
- **Total Addition**: ~6400 characters

## Testing Checklist

- [ ] Protocols enabled in web config
- [ ] Sensors detected in debug log
- [ ] MQTT topics populated correctly
- [ ] Home Assistant entities appear
- [ ] Battery status correct
- [ ] Wind direction text correct (N/NE/E/SE/S/SW/W/NW)
- [ ] Pressure values reasonable (900-1100 hPa)
- [ ] UV index in valid range (0-15)

## Future Enhancements

1. Add Home Assistant discovery for:
   - Pressure sensor entity
   - UV Index sensor entity

2. Add rain rate calculation (rain/hour)

3. Add wind chill / feels-like temperature calculation

## References

- [Fine Offset Protocol Documentation](https://wxtools.sourceforge.io/doc/wh2900.html)
- [rtl_433 WH24 Implementation](https://github.com/merbanan/rtl_433/issues/844)
- [Ecowitt Protocol Discussion](https://groups.google.com/g/rtl_433/c/OcJ_Uvlnq0w)

## Refactoring Status

✅ **COMPLETE** - WH24 and WH25 handlers fully integrated with mqtt_helper.h

## Commit Information

**Branch**: `refactor`  
**Integration Date**: 2026-02-12  
**Refactoring**: Part of complete mqtt_helper.h migration
