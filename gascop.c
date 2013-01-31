/* Gascop, a lightweight POCSAG decoder for rtl-sdr devices.
 *
 * Copyright (c) 2013 by
 *     Yuval Adam <yuv.adm@gmail.com>
 *     Salvatore Sanfilippo <antirez@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  -  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  -  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "rtl-sdr.h"

/* POCSAG parameters */
#define POCSAG_ID "POCSAG"
#define POCSAG_SYM_RATE 1200
#define POCSAG_FM_DEV 4500
#define POCSAG_SPS 8
#define POCSAG_STD_SYNC 0x7cd215d8
#define POCSAG_STD_IDLE 0x7a89c197
#define POCSAG_WORDSIZE 32
#define POCSAG_SOFTTHRESHOLD 2
#define POCSAG_MAXWORD 16
#define POCSAG_MSG_LEN 256

#define POCSAG_DEFAULT_RATE 2000000
#define POCSAG_DEFAULT_WIDTH 1000
#define POCSAG_DEFAULT_HEIGHT 700
#define POCSAG_ASYNC_BUF_NUMBER 12
#define POCSAG_DATA_LEN (16*16384) /* 256K */
#define POCSAG_AUTO_GAIN -100 /* Use automatic gain. */
#define POCSAG_MAX_GAIN 999999 /* Use max available gain. */

/* BCH parameters */
#define POCSAG_BCH_POLY 0x769
#define POCSAG_BCH_N 31
#define POCSAG_BCH_K 21

/* POCASG states */
#define POCSAG_SEARCH_PREAMBLE_START 0
#define POCSAG_SEARCH_PREAMBLE_END 1
#define POCSAG_SYNC 2
#define POCSAG_SEARCH_SYNC 3
#define POCSAG_SYNCHED 4

#define IGNORE(V) ((void) V)

struct {
    pthread_t reader_thread;
    pthread_mutex_t data_mutex;     /* Mutex to synchronize buffer access. */
    pthread_cond_t data_cond;       /* Conditional variable associated. */
    unsigned char *data;            /* Raw IQ samples buffer */
    uint16_t *magnitude;            /* Magnitude vector */
    uint32_t data_len;              /* Buffer length. */
    int data_ready;                 /* Data ready to be processed. */
    uint16_t *maglut;               /* I/Q -> Magnitude lookup table. */
    int exit; 

    /* rtlsdr */
    int dev_index;
    int gain;
    int enable_agc;
    rtlsdr_dev_t *dev;
    int freq;
} Gascop;

struct pocsag_msg
{
    uint32_t bits;
    int nb;
    int nc;
    char buf[POCSAG_MSG_LEN];
};

void pocsag_msg_init(struct pocsag_msg *msg) {
    msg->bits = 0;
    msg->nb = 0;
    msg->nc = 0;
    memset(msg->buf, 0x00, POCSAG_MSG_LEN);
}

int hammingWeight(uint32_t n) {
    int c;
    for (c = 0; n; c++)
        n &= n - 1;
    return c;
}

uint8_t evenParity(uint32_t n) {
    return hammingWeight(n) & 1;
}

uint32_t bchSyndrome(uint32_t data, int poly, int n, int k) {
    uint32_t mask = 1 << (n - 1);
    uint32_t coeff = poly << (k - 1);
    n = k;

    int s = data >> 1;
    while (n > 0) {
        if (s & mask)
            s ^= coeff;
        n -= 1;
        mask >>= 1;
        coeff >>= 1;
    }

    if (evenParity(data))
        s |= 1 << (n - k);

    return s;
}

uint32_t bchFix(uint32_t data, int poly, int n, int k) {
    int i, j;
    for (i=0; i<32; i++) {
        int t = data ^ (1 << i);
        if (!bchSyndrome(t, poly, n, k))
            return t;
    }
    for (i=0; i<32; i++) {
        for (j=i+1; j<32; j++) {
            int t = data ^ ((1 << i) | (1 << j));
            if (!bchSyndrome(t, poly, n, k))
                return t;
        }
    }
    return data;
}

void gascopInit(void) {
    pthread_mutex_init(&Gascop.data_mutex, NULL);
    pthread_cond_init(&Gascop.data_cond, NULL);
    Gascop.data_len = POCSAG_DATA_LEN;
    Gascop.data_ready = 0;
    if ((Gascop.data = malloc(Gascop.data_len)) == NULL) {
        fprintf(stderr, "Out of memory allocating data buffer.\n");
        exit(1);
    }
}

void rtlsdrInit(void) {
    int device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No rtlsdr devices found.\n");
        exit(1);
    }

    printf("Found %d device(s):\n", device_count);
    char vendor[256], product[256], serial[256];
    int j;
    for (j = 0; j < device_count; j++) {
        rtlsdr_get_device_usb_strings(j, vendor, product, serial);
        fprintf(stderr, "%d: %s, %s, SN: %s %s\n", j, vendor, product, serial,
            (j == Gascop.dev_index) ? "(currently selected)" : "");
    }

    if (rtlsdr_open(&Gascop.dev, Gascop.dev_index) < 0) {
        fprintf(stderr, "Error opening rtlsdr device: %s\n", strerror(errno));
        exit(1);
    }

    rtlsdr_set_tuner_gain_mode(Gascop.dev, 0);  /* auto gain */
    rtlsdr_set_freq_correction(Gascop.dev, 0);
    rtlsdr_set_agc_mode(Gascop.dev, 1);
    rtlsdr_set_center_freq(Gascop.dev, Gascop.freq);
    rtlsdr_set_sample_rate(Gascop.dev, POCSAG_DEFAULT_RATE);
    rtlsdr_reset_buffer(Gascop.dev);
    printf("Gain reported by device: %.2f\n",
        rtlsdr_get_tuner_gain(Gascop.dev) / 10.0);
}

void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    IGNORE(ctx);

    pthread_mutex_lock(&Gascop.data_mutex);
    if (len > Gascop.data_len)
        len = Gascop.data_len;
    memcpy(Gascop.data, buf, len);
    Gascop.data_ready = 1;
    pthread_cond_signal(&Gascop.data_cond);
    pthread_mutex_unlock(&Gascop.data_mutex);
}

void *readerThreadEntryPoint(void *arg) {
    IGNORE(arg);

    rtlsdr_read_async(Gascop.dev, rtlsdrCallback, NULL,
        POCSAG_ASYNC_BUF_NUMBER, Gascop.data_len);
    return NULL;
}

void printUsage() {
    printf("Usage: ./gascop [options] <frequency Hz>\n\n");
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        printUsage();
        exit(-1);
    }

    Gascop.freq = strtoll(argv[1], NULL, 10);

    gascopInit();
    rtlsdrInit();
    pthread_create(&Gascop.reader_thread, NULL, readerThreadEntryPoint, NULL);
    pthread_mutex_lock(&Gascop.data_mutex);

    while (1) {
        if (!Gascop.data_ready) {
            pthread_cond_wait(&Gascop.data_cond, &Gascop.data_mutex);
            continue;
        }
        Gascop.data_ready = 0;
        pthread_cond_signal(&Gascop.data_cond);
        pthread_mutex_unlock(&Gascop.data_mutex);

        uint32_t j;
        int i, q;
        for (j = 0; j < Gascop.data_len; j += 2) {
            i = Gascop.data[j] - 127;
            q = Gascop.data[j+1] - 127;
            if (i < 0) i = -i;
            if (q < 0) q = -q;
            printf("I:%d, Q:%d\n", i, q);
        }

        pthread_mutex_lock(&Gascop.data_mutex);
        if (Gascop.exit) break;
    }

    rtlsdr_close(Gascop.dev);
    return 0;
}
