#ifndef HAR_MODEL_H
#define HAR_MODEL_H

#include <stdint.h>

#define HAR_WINDOW  50
#define HAR_CHANNELS 6
#define HAR_NCLASSES 6

typedef struct {
    uint16_t count;
    float    buf[HAR_WINDOW][HAR_CHANNELS];
} HAR_RingBuf_t;

void HAR_Init(HAR_RingBuf_t *rb);
void HAR_Push(HAR_RingBuf_t *rb, const int16_t *sample);
int  HAR_Ready(const HAR_RingBuf_t *rb);
int  HAR_Run(const HAR_RingBuf_t *rb, float *output);

extern const char *HAR_ClassNames[];

#endif
