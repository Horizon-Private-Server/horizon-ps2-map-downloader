#include <stdint.h>
#include <string.h>
#include "sha1.h"

static uint32_t rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(sha1_ctx *c, const uint8_t *p) {
    uint32_t w[80], a,b,c2,d,e, t;

    for (int i=0;i<16;i++)
        w[i] = (p[i*4]<<24) | (p[i*4+1]<<16) | (p[i*4+2]<<8) | p[i*4+3];

    for (int i=16;i<80;i++)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    a=c->h[0]; b=c->h[1]; c2=c->h[2]; d=c->h[3]; e=c->h[4];

    for (int i=0;i<80;i++) {
        uint32_t f,k;
        if (i<20)      { f=(b&c2)|((~b)&d); k=0x5A827999; }
        else if (i<40) { f=b^c2^d;         k=0x6ED9EBA1; }
        else if (i<60) { f=(b&c2)|(b&d)|(c2&d); k=0x8F1BBCDC; }
        else           { f=b^c2^d;         k=0xCA62C1D6; }

        t = rol(a,5) + f + e + k + w[i];
        e = d;
        d = c2;
        c2 = rol(b,30);
        b = a;
        a = t;
    }

    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += c2;
    c->h[3] += d;
    c->h[4] += e;
}

void sha1_init(sha1_ctx *c) {
    c->h[0]=0x67452301; c->h[1]=0xEFCDAB89; c->h[2]=0x98BADCFE;
    c->h[3]=0x10325476; c->h[4]=0xC3D2E1F0;
    c->len = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t len) {
    const uint8_t *p = data;
    size_t idx = c->len & 63; // modulo 64
    c->len += len;

    size_t fill = 64 - idx;
    if (len >= fill) {
        memcpy(c->buf + idx, p, fill);
        sha1_block(c, c->buf);
        p += fill; len -= fill;
        while (len >= 64) {
            sha1_block(c, p);
            p += 64; len -= 64;
        }
        idx = 0;
    }
    memcpy(c->buf + idx, p, len);
}

void sha1_final(sha1_ctx *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    size_t idx = c->len & 63;

    c->buf[idx++] = 0x80;
    if (idx > 56) {
        while (idx < 64) c->buf[idx++] = 0;
        sha1_block(c, c->buf);
        idx = 0;
    }
    while (idx < 56) c->buf[idx++] = 0;
    for (int i=7;i>=0;i--) c->buf[idx++] = (bits >> (i*8)) & 0xFF;

    sha1_block(c, c->buf);

    for (int i=0;i<5;i++) {
        out[i*4+0] = c->h[i] >> 24;
        out[i*4+1] = c->h[i] >> 16;
        out[i*4+2] = c->h[i] >> 8;
        out[i*4+3] = c->h[i];
    }
}
