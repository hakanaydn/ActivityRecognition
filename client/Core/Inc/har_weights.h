#ifndef HAR_WEIGHTS_H
#define HAR_WEIGHTS_H

#include <stdint.h>

/* Auto-generated from Keras Tiny 1D CNN */
/* Input: (50,6), Classes: 6, Accuracy: ~94% */

#define HAR_WINDOW    50
#define HAR_CHANNELS  6
#define HAR_NCLASSES  6

extern const float har_mean[6];
extern const float har_std[6];
extern const float conv1d_kernel[144];
extern const float conv1d_bias[8];
extern const float conv1d_1_kernel[384];
extern const float conv1d_1_bias[16];
extern const float dense_kernel[96];
extern const float dense_bias[6];

#endif
