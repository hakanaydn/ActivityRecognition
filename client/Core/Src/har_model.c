#include "har_model.h"
#include "har_weights.h"
#include <string.h>

static float fast_expf(float x)
{
    if (x < -87.0f) return 0.0f;
    union { float f; int i; } u;
    u.i = (int)(12102203.0f * x + 1065353216.0f);
    return u.f;
}

void HAR_Init(HAR_RingBuf_t *rb)
{
    rb->count = 0;
}

void HAR_Push(HAR_RingBuf_t *rb, const int16_t *sample)
{
    if (rb->count < HAR_WINDOW) {
        int i = rb->count;
        for (int c = 0; c < HAR_CHANNELS; c++)
            rb->buf[i][c] = (float)sample[c];
        rb->count++;
    } else {
        for (int i = 1; i < HAR_WINDOW; i++)
            for (int c = 0; c < HAR_CHANNELS; c++)
                rb->buf[i-1][c] = rb->buf[i][c];
        for (int c = 0; c < HAR_CHANNELS; c++)
            rb->buf[HAR_WINDOW-1][c] = (float)sample[c];
    }
}

int HAR_Ready(const HAR_RingBuf_t *rb)
{
    return rb->count >= HAR_WINDOW;
}

static void normalize(float *x)
{
    for (int i = 0; i < HAR_WINDOW; i++)
        for (int c = 0; c < HAR_CHANNELS; c++)
            x[i * HAR_CHANNELS + c] = (x[i * HAR_CHANNELS + c] - har_mean[c]) / har_std[c];
}

static void conv1d_same(const float *in, int in_len, int in_ch,
                        const float *kernel, const float *bias,
                        int out_ch, int ksize,
                        float *out)
{
    int pad = (ksize - 1) / 2;
    for (int o = 0; o < out_ch; o++) {
        for (int t = 0; t < in_len; t++) {
            float sum = bias[o];
            for (int k = 0; k < ksize; k++) {
                int src_t = t + k - pad;
                if (src_t < 0 || src_t >= in_len) continue;
                for (int c = 0; c < in_ch; c++) {
                    sum += in[src_t * in_ch + c] * kernel[k * in_ch * out_ch + c * out_ch + o];
                }
            }
            out[t * out_ch + o] = sum;
        }
    }
}

static void relu(float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (x[i] < 0) x[i] = 0;
}

static void maxpool1d_2(const float *in, int in_len, int ch, float *out)
{
    int out_len = in_len / 2;
    for (int t = 0; t < out_len; t++) {
        for (int c = 0; c < ch; c++) {
            float a = in[(2*t) * ch + c];
            float b = in[(2*t+1) * ch + c];
            out[t * ch + c] = (a > b) ? a : b;
        }
    }
}

static void global_avg_pool(const float *in, int len, int ch, float *out)
{
    for (int c = 0; c < ch; c++) {
        float sum = 0;
        for (int t = 0; t < len; t++)
            sum += in[t * ch + c];
        out[c] = sum / (float)len;
    }
}

static void dense(const float *in, int in_size,
                  const float *kernel, const float *bias,
                  int out_size, float *out)
{
    for (int o = 0; o < out_size; o++) {
        float sum = bias[o];
        for (int i = 0; i < in_size; i++)
            sum += in[i] * kernel[i * out_size + o];
        out[o] = sum;
    }
}

static void softmax(float *x, int n)
{
    float maxv = x[0];
    for (int i = 1; i < n; i++)
        if (x[i] > maxv) maxv = x[i];
    float sum = 0;
    for (int i = 0; i < n; i++) {
        x[i] = fast_expf(x[i] - maxv);
        sum += x[i];
    }
    for (int i = 0; i < n; i++)
        x[i] /= sum;
}

int HAR_Run(const HAR_RingBuf_t *rb, float *output)
{
    float x[HAR_WINDOW * HAR_CHANNELS];
    for (int i = 0; i < HAR_WINDOW; i++)
        for (int c = 0; c < HAR_CHANNELS; c++)
            x[i * HAR_CHANNELS + c] = rb->buf[i][c];

    normalize(x);

    float c1[HAR_WINDOW * 8];
    conv1d_same(x, HAR_WINDOW, HAR_CHANNELS, conv1d_kernel, conv1d_bias, 8, 3, c1);
    relu(c1, HAR_WINDOW * 8);

    float p1[25 * 8];
    maxpool1d_2(c1, HAR_WINDOW, 8, p1);

    float c2[25 * 16];
    conv1d_same(p1, 25, 8, conv1d_1_kernel, conv1d_1_bias, 16, 3, c2);
    relu(c2, 25 * 16);

    float pooled[16];
    global_avg_pool(c2, 25, 16, pooled);

    float logits[HAR_NCLASSES];
    dense(pooled, 16, dense_kernel, dense_bias, HAR_NCLASSES, logits);

    softmax(logits, HAR_NCLASSES);

    int best = 0;
    for (int i = 0; i < HAR_NCLASSES; i++) {
        output[i] = logits[i];
        if (logits[i] > logits[best]) best = i;
    }
    return best;
}
