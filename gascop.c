#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
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

void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    IGNORE(ctx);

    pthread_mutex_lock(&Gascop.data_mutex);
    if (len > Gascop.data_len) len = Gascop.data_len;
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

int main(int argc, char **argv) {
    IGNORE(argc);
    IGNORE(argv);

    printf("Gascop...\n");
    exit(0);
}
