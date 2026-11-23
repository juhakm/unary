/*
 * unary.h -- unary numerals, one BIT per mark (bit-packed C port of unary.py).
 *
 * A natural number n is a string of n marks; each mark is a 1-bit.  Zero is the
 * empty string.  Operations stay string operations: addition is bit-string
 * concatenation, subtraction is suffix removal (truncation), multiplication is
 * substitution (each mark -> a copy of the other numeral), order is a prefix
 * test, binary conversion is repeated pairing-off.
 *
 * Representation invariant (canonical form, relied on by eq/le):
 *   bits 0 .. len-1 are all 1; every bit at position >= len (in the last
 *   word and beyond) is 0.  Bit i lives in w[i/64], position i%64.
 *
 * Assumptions flagged (nothing here is a physical claim):
 *   * The operations are conventions a reader applies to strings of marks;
 *     a bare collection of marks has no arithmetic of its own.
 *   * Binary conversion uses the pairing convention '11' -> one pair; this
 *     presupposes the reader can recognise "two" as a pair of marks.
 *   * `len` (number of marks) is bookkeeping a C buffer needs.  It is used for
 *     sizing, bit positions and overflow guards; u_to_size() exposes it as the
 *     external observer's count (was to_int() in Python).
 *   * Because every stored bit is 1, the buffer contents are fully determined
 *     by `len`.  The packing gives an 8x memory reduction vs. one byte per
 *     mark; it does not change the O(n) space of a unary numeral.
 *
 * Ownership: functions that fill an `out` Unary allocate it; release with
 * u_free().  `out` must not alias an input.  On error `out` is left empty
 * (w == NULL) and needs no freeing.
 */
#ifndef UNARY_H
#define UNARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t *w;    /* packed marks, canonical form (see above) */
    size_t    len;  /* number of marks */
} Unary;

typedef enum {
    U_OK = 0,
    U_ERR_NOMEM,
    U_ERR_INVALID,    /* non-mark char / non-binary char */
    U_ERR_UNDERFLOW,  /* a - b with b > a */
    U_ERR_DIV0,
    U_ERR_OVERFLOW    /* result would exceed addressable size */
} UStatus;

const char *u_strerror(UStatus st);

/* construction / reading (external observer) */
UStatus u_from_str (Unary *out, const char *marks);  /* '|' per mark; validates */
UStatus u_from_size(Unary *out, size_t n);
size_t  u_to_size  (const Unary *a);                 /* external count */
UStatus u_to_str   (char **out, const Unary *a);     /* '|' per mark; malloc'd */
void    u_free     (Unary *a);
bool    u_check_invariant(const Unary *a);           /* canonical-form check (tests) */

/* order: a <= b iff b starts with a */
bool u_eq(const Unary *a, const Unary *b);
bool u_le(const Unary *a, const Unary *b);
bool u_lt(const Unary *a, const Unary *b);
bool u_is_zero(const Unary *a);

/* arithmetic */
UStatus u_succ  (Unary *out, const Unary *a);
UStatus u_add   (Unary *out, const Unary *a, const Unary *b);
UStatus u_sub   (Unary *out, const Unary *a, const Unary *b);
UStatus u_mul   (Unary *out, const Unary *a, const Unary *b);
UStatus u_divmod(Unary *q, Unary *r, const Unary *a, const Unary *b);
UStatus u_pow   (Unary *out, const Unary *a, const Unary *e);
UStatus u_gcd   (Unary *out, const Unary *a, const Unary *b);

/* conversion: unary <-> binary */
UStatus u_to_binary  (char **out, const Unary *a);   /* malloc'd; caller free() */
UStatus u_from_binary(Unary *out, const char *bits);

#endif
