#ifndef MQTT_HELPER_H
#define MQTT_HELPER_H

#include <Arduino.h>
#include "globals.h"

/**
 * @brief Generiert den MQTT Topic-Präfix für einen Sensor
 * 
 * Verwendet entweder den Sensor-Namen (falls gesetzt und mqtt_use_names=true)
 * oder die Sensor-ID als Fallback.
 * 
 * @param id Sensor ID (0-255)
 * @return String MQTT Topic-Präfix (z.B. "climate/Wohnzimmer" oder "lacrosse/id/30")
 */
inline String getMqttTopicPrefix(uint8_t id) {
    if (config.mqtt_use_names && id < SENSOR_NUM && id2name[id].length() > 0) {
        return "climate/" + id2name[id];
    }
    return "lacrosse/id/" + String(id);
}

/**
 * @brief Generiert vollständigen MQTT Topic mit Subtopic
 * 
 * @param id Sensor ID
 * @param subtopic Subtopic (z.B. "temp", "humi", "battery")
 * @return String Vollständiger MQTT Topic
 */
inline String getMqttTopic(uint8_t id, const String& subtopic) {
    return getMqttTopicPrefix(id) + "/" + subtopic;
}

/**
 * @brief Generiert MQTT Availability Topic für Home Assistant
 * 
 * @param id Sensor ID
 * @return String Availability Topic (z.B. "lacrosse/30/available")
 */
inline String getMqttAvailabilityTopic(uint8_t id) {
    return getMqttTopicPrefix(id) + "/available";
}

/**
 * @brief Generiert eindeutige Device ID für Home Assistant Discovery
 * 
 * @param id Sensor ID
 * @return String Device ID (z.B. "lacrosse_30")
 */
inline String getDeviceId(uint8_t id) {
    return "lacrosse_" + String(id);
}

/**
 * @brief Generiert eindeutige Entity ID für Home Assistant Discovery
 * 
 * @param id Sensor ID
 * @param entity_type Entity-Typ (z.B. "temp", "humi", "battery")
 * @return String Entity ID (z.B. "lacrosse_30_temp")
 */
inline String getEntityId(uint8_t id, const String& entity_type) {
    return getDeviceId(id) + "_" + entity_type;
}

/**
 * @brief Generiert Home Assistant Discovery Config Topic
 * 
 * @param component Component-Typ ("sensor", "binary_sensor", etc.)
 * @param id Sensor ID
 * @param entity_type Entity-Typ (z.B. "temp", "humi")
 * @return String Discovery Config Topic
 */
inline String getHADiscoveryTopic(const String& component, uint8_t id, const String& entity_type) {
    return "homeassistant/" + component + "/" + getEntityId(id, entity_type) + "/config";
}

/**
 * @brief Generiert Display-Namen für Sensor
 * 
 * Verwendet Sensor-Namen falls gesetzt, sonst "Sensor ID"
 * 
 * @param id Sensor ID
 * @return String Display-Name
 */
inline String getSensorDisplayName(uint8_t id) {
    if (id < SENSOR_NUM && id2name[id].length() > 0) {
        return id2name[id];
    }
    return "Sensor " + String(id);
}

#endif // MQTT_HELPER_H
