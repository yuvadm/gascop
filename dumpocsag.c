#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
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


int hammingWeight(int n) {
    unsigned int c;
    for (c = 0; n; c++)
        n &= n - 1;
    return c;
}

int evenParity(int n) {
    return hammingWeight(n) & 1;
}

int bchSyndrome(int data, int poly, int n, int k) {
    int mask = 1 << (n - 1);
    int coeff = poly << (k - 1);
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

int bchFix(int data, int poly, int n, int k) {
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

int main(int argc, char **argv) {
    printf("Dumpocsag v0.1\n");
    exit(0);
}
