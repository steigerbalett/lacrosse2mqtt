#ifndef _WEBFRONTEND_H
#define _WEBFRONTEND_H

#include <Arduino.h>

void setup_web();
void handle_index();
void handle_config();
void handle_client();
void handle_licenses();
void add_debug_log(uint8_t *data, int8_t rssi, int datarate, bool valid);

#endif