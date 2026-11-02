#ifndef ENVIRONMENT_SERVICE_H
#define ENVIRONMENT_SERVICE_H

#include "veml3328.h"

typedef struct {
    int temperature_x10;
    int humidity_x10;
    int pressure_hpa;
    veml_sample_t light;
    int environment_fresh;
    int pressure_fresh;
    int light_fresh;
    int hdc_detected, spa_detected, light_detected;
} environment_snapshot_t;

/* Single owner of HDC1080/SPA06/VEML3328. UI and UART only read this cache. */
void environment_service_init(void);
void environment_service_poll(void);
void environment_service_get_snapshot(environment_snapshot_t *out);

#endif
