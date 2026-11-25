#include "prov/dnsans.h"

#include <string.h>

#define HDR 12

size_t prov_dns_answer(const uint8_t *q, size_t len, uint8_t *out, size_t cap, const uint8_t ip[4])
{
    size_t pos = HDR, qend, total;
    unsigned flags, qtype, qclass, name_len = 0;
    int is_a;

    if (!q || !out || !ip || len < HDR + 5 || len > PROV_DNS_MAX) {
        return 0;
    }
    flags = (unsigned)(q[2] << 8 | q[3]);
    if ((flags & 0x8000u) != 0 || ((flags >> 11) & 0xFu) != 0) { /* a response, or not a standard query */
        return 0;
    }
    if (q[4] != 0 || q[5] != 1) { /* exactly one question */
        return 0;
    }
    for (;;) { /* the name: labels of at most 63 bytes, no compression in a question, 255 bytes in all */
        unsigned l;
        if (pos >= len) {
            return 0;
        }
        l = q[pos++];
        if (l == 0) {
            break;
        }
        if (l > 63 || pos + l > len) { /* 0x40 and 0x80 are reserved, 0xC0 is a pointer */
            return 0;
        }
        name_len += l + 1;
        if (name_len > 254) {
            return 0;
        }
        pos += l;
    }
    if (pos + 4 > len) {
        return 0;
    }
    qtype = (unsigned)(q[pos] << 8 | q[pos + 1]);
    qclass = (unsigned)(q[pos + 2] << 8 | q[pos + 3]);
    qend = pos + 4;
    is_a = qtype == 1 && qclass == 1;
    total = qend + (is_a ? 16 : 0);
    if (total > cap) {
        return 0;
    }
    memcpy(out, q, qend); /* the id, the question */
    out[2] = (uint8_t)(0x84 | (q[2] & 0x01)); /* response, authoritative, recursion desired as asked */
    out[3] = 0x80;                            /* recursion available, no error */
    out[4] = 0;
    out[5] = 1;
    out[6] = 0;
    out[7] = is_a ? 1 : 0;
    out[8] = out[9] = out[10] = out[11] = 0;
    if (is_a) {
        static const uint8_t rr[10] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04}; /* A IN ttl 60 len 4 */
        out[qend] = 0xC0; /* the name, by pointer to the question */
        out[qend + 1] = 0x0C;
        memcpy(out + qend + 2, rr, sizeof(rr));
        memcpy(out + qend + 12, ip, 4);
    }
    return total;
}
