#ifndef __TRANSPORT_H
#define __TRANSPORT_H

#include <stdint.h>

typedef struct {
    void     (*init)(void);
    void     (*write)(const uint8_t *data, uint16_t len);
} Output_t;

extern Output_t *g_output;

void Output_Init(void);
void Output_Write(const uint8_t *data, uint16_t len);

#endif
