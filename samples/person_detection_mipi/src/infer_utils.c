/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Person-detection inference utilities: preprocess and postprocess implementation
 */

#include "infer_utils.h"
#include <stdint.h>
#include <math.h>

#ifdef __ARM_FEATURE_MVE
#include <arm_mve.h>
#endif

int infer_preprocess(void *addr, int width, int height)
{
    if (!addr || width <= 0 || height <= 0)
        return -1;

    size_t   total = (size_t)width * (size_t)height * 3u;
    uint8_t *p     = (uint8_t *)addr;

#ifdef __ARM_FEATURE_MVE
    const uint8x16_t mask = vdupq_n_u8(0x80u);
    int32_t          n    = (int32_t)total;
    do {
        mve_pred16_t pred = vctp8q((uint32_t)n);
        uint8x16_t   v    = vldrbq_z_u8(p, pred);
        vstrbq_p_u8(p, veorq_u8(v, mask), pred);
        p += 16;
        n -= 16;
    } while (n > 0);
#else
    if (((uintptr_t)addr & 3u) == 0u) {
        uint32_t      *p32  = (uint32_t *)addr;
        size_t         n32  = total >> 2;
        size_t         tail = total & 3u;
        const uint32_t M    = 0x80808080u;
        size_t i = 0;

        for (; i + 3 < n32; i += 4) {
            p32[i    ] ^= M;
            p32[i + 1] ^= M;
            p32[i + 2] ^= M;
            p32[i + 3] ^= M;
        }
        for (; i < n32; ++i) {
            p32[i] ^= M;
        }
        p = (uint8_t *)addr + (n32 << 2);
        for (size_t j = 0; j < tail; ++j) {
            p[j] ^= 0x80u;
        }
    } else {
        for (size_t i = 0; i < total; ++i) {
            p[i] ^= 0x80u;
        }
    }
#endif

    return 0;
}

/* Postprocessing constants */
static const float k_scale_x2y2   = 2.2298152446746826f;
static const int   k_zp_x2y2      = -84;
static const float k_scale_x1y1   = 2.4782917499542236f;
static const int   k_zp_x1y1      = -128;
static const float k_scale_scores = 0.00390625f; /* 1/256 */
static const int   k_zp_scores    = -128;

static float iou_xywh(const float *a, const float *b)
{
    float ax1 = a[0], ay1 = a[1], ax2 = a[0]+a[2], ay2 = a[1]+a[3];
    float bx1 = b[0], by1 = b[1], bx2 = b[0]+b[2], by2 = b[1]+b[3];
    float inter_w  = fmaxf(0.f, fminf(ax2,bx2) - fmaxf(ax1,bx1));
    float inter_h  = fmaxf(0.f, fminf(ay2,by2) - fmaxf(ay1,by1));
    float inter    = inter_w * inter_h;
    float uni      = a[2]*a[3] + b[2]*b[3] - inter;
    return (uni <= 0.f) ? 0.f : inter / uni;
}

static void min_heap_swap(int *h, int a, int b)
{
    int t = h[a]; h[a] = h[b]; h[b] = t;
}

static void min_heap_sift_up(int *h, int cnt, const float *scores, int cur)
{
    while (cur > 0 && cur < cnt) {
        int parent = (cur - 1) >> 1;
        if (scores[h[cur]] < scores[h[parent]]) {
            min_heap_swap(h, cur, parent);
            cur = parent;
        } else {
            break;
        }
    }
}

static void min_heap_sift_down(int *h, int cnt, const float *scores, int cur)
{
    for (;;) {
        int left = (cur << 1) + 1;
        int right = left + 1;
        int smallest = cur;
        if (left < cnt && scores[h[left]] < scores[h[smallest]]) smallest = left;
        if (right < cnt && scores[h[right]] < scores[h[smallest]]) smallest = right;
        if (smallest != cur) {
            min_heap_swap(h, cur, smallest);
            cur = smallest;
        } else break;
    }
}

int infer_postprocess(const void *box1_addr, size_t box1_size,
                      const void *cls_addr,  size_t cls_size,
                      const void *box2_addr, size_t box2_size,
                      int max_out, detection_t *out_dets, int *out_count)
{
    if (!box1_addr || !cls_addr || !box2_addr || !out_dets || !out_count)
        return -1;

    const int N = MAX_BOXES;
    if (box1_size < (size_t)N*2 || box2_size < (size_t)N*2 || cls_size < (size_t)N)
        return -2;

    const int8_t *box1 = (const int8_t *)box1_addr;
    const int8_t *box2 = (const int8_t *)box2_addr;
    const int8_t *cls  = (const int8_t *)cls_addr;

    /* Static temporaries to avoid heap usage */
    static float x1[MAX_BOXES];
    static float y1[MAX_BOXES];
    static float x2[MAX_BOXES];
    static float y2[MAX_BOXES];
    static float scores[MAX_BOXES];
    static float boxes[MAX_BOXES * 4u];
    static uint8_t valid[MAX_BOXES];

    /* Scalar decode (portable) */
    for (int i = 0; i < N; ++i) {
        x2[i]     = (float)((int)box1[i*2+0] - k_zp_x2y2)  * k_scale_x2y2;
        y2[i]     = (float)((int)box1[i*2+1] - k_zp_x2y2)  * k_scale_x2y2;
        x1[i]     = (float)((int)box2[i*2+0] - k_zp_x1y1)  * k_scale_x1y1;
        y1[i]     = (float)((int)box2[i*2+1] - k_zp_x1y1)  * k_scale_x1y1;
        scores[i] = (float)((int)cls[i]       - k_zp_scores) * k_scale_scores;
    }

    for (int i = 0; i < N; ++i) {
        float xmin = fminf(x1[i], x2[i]);
        float xmax = fmaxf(x1[i], x2[i]);
        float ymin = fminf(y1[i], y2[i]);
        float ymax = fmaxf(y1[i], y2[i]);
        float bw   = xmax - xmin;
        float bh   = ymax - ymin;
        if (bw > 1.f && bh > 1.f) {
            boxes[4*i+0] = xmin;
            boxes[4*i+1] = ymin;
            boxes[4*i+2] = bw;
            boxes[4*i+3] = bh;
            valid[i] = 1u;
        } else {
            valid[i] = 0u;
        }
    }

    const float conf_thresh = 0.25f;
    const float iou_thresh  = 0.6f;

    const int nms_cap = (max_out < NMS_MAX_KEEP) ? max_out : NMS_MAX_KEEP;
    if (nms_cap <= 0) {
        *out_count = 0;
        return 0;
    }

    /* Bounded min-heap (by score) of candidate indices */
    int heap_cnt = 0;
    int heap_idx[NMS_MAX_KEEP];

    /* heap operations use static helpers defined above */

    for (int i = 0; i < N; ++i) {
        if (!valid[i]) continue;
        float s = scores[i];
        if (s <= conf_thresh) continue;
        if (heap_cnt < nms_cap) {
            heap_idx[heap_cnt] = i;
            min_heap_sift_up(heap_idx, heap_cnt + 1, scores, heap_cnt);
            heap_cnt++;
        } else if (s > scores[heap_idx[0]]) {
            heap_idx[0] = i;
            min_heap_sift_down(heap_idx, heap_cnt, scores, 0);
        }
    }

    if (heap_cnt == 0) {
        *out_count = 0;
        return 0;
    }

    /* Extract heap into candidates array in descending order */
    int candidates_count = heap_cnt;
    int candidates_arr[NMS_MAX_KEEP];
    for (int i = heap_cnt - 1; i >= 0; --i) {
        candidates_arr[i] = heap_idx[0];
        heap_idx[0] = heap_idx[heap_cnt - 1];
        heap_cnt--;
        if (heap_cnt > 0) min_heap_sift_down(heap_idx, heap_cnt, scores, 0);
    }

    /* Perform NMS on the top candidates */
    int num_kept = 0;
    int kept[NMS_MAX_KEEP];
    for (int i = 0; i < candidates_count && num_kept < nms_cap; ++i) {
        int idx = candidates_arr[i];
        float bi[4] = { boxes[4*idx], boxes[4*idx+1], boxes[4*idx+2], boxes[4*idx+3] };
        int suppress = 0;
        for (int j = 0; j < num_kept && !suppress; ++j) {
            int ki = kept[j];
            float bj[4] = { boxes[4*ki], boxes[4*ki+1], boxes[4*ki+2], boxes[4*ki+3] };
            if (iou_xywh(bi, bj) > iou_thresh)
                suppress = 1;
        }
        if (!suppress)
            kept[num_kept++] = idx;
    }

    /* Fill output detections */
    for (int i = 0; i < num_kept; ++i) {
        int idx = kept[i];
        out_dets[i].class_id = 0;
        out_dets[i].score    = scores[idx];
        out_dets[i].x        = boxes[4*idx+0];
        out_dets[i].y        = boxes[4*idx+1];
        out_dets[i].w        = boxes[4*idx+2];
        out_dets[i].h        = boxes[4*idx+3];
    }

    *out_count = num_kept;
    return 0;
}
