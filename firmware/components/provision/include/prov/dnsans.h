#ifndef PROV_DNSANS_H
#define PROV_DNSANS_H

/*
 * The DNS of the captive portal: every name is the board. Pure: a packet in, a packet out, nothing read past the end of
 * the input and nothing written past the end of the output. Anything that is not a plain single-question query is ignored.
 */

#include <stddef.h>
#include <stdint.h>

#define PROV_DNS_MAX 512 /* the largest query that is looked at (the classic UDP limit) */

/*
 * Answers the query `q` into `out` (capacity `cap`, at least len + 16 is needed). An A question in class IN gets one
 * answer with `ip`; any other type gets an empty answer, so that the phone falls back to the A record. Returns the
 * length of the answer, or 0 if the packet is to be ignored.
 */
size_t prov_dns_answer(const uint8_t *q, size_t len, uint8_t *out, size_t cap, const uint8_t ip[4]);

#endif /* PROV_DNSANS_H */
