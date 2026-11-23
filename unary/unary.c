#include "unary.h"

#include <stdlib.h>
#include <string.h>

#define MARK_CHAR '|'
#define ONE ((uint64_t)1)

const char *u_strerror(UStatus st) {
    switch (st) {
    case U_OK:            return "ok";
    case U_ERR_NOMEM:     return "out of memory";
    case U_ERR_INVALID:   return "invalid character";
    case U_ERR_UNDERFLOW: return "a - b undefined for b > a in the naturals";
    case U_ERR_DIV0:      return "division by zero";
    case U_ERR_OVERFLOW:  return "result too large";
    }
    return "unknown error";
}

/* ---- internal helpers ---- */

static size_t words_of(size_t len) { return (len + 63) >> 6; }   /* len <= SIZE_MAX-64 */

/* zero-filled buffer with room for `len` marks; len recorded, bits NOT yet set */
static UStatus alloc_u(Unary *o, size_t len) {
    o->w = NULL;
    o->len = 0;
    if (len > SIZE_MAX - 64) return U_ERR_OVERFLOW;
    size_t nw = words_of(len);
    if (nw == 0) nw = 1;                       /* keep w non-NULL for the empty numeral */
    o->w = calloc(nw, sizeof(uint64_t));
    if (!o->w) return U_ERR_NOMEM;
    o->len = len;
    return U_OK;
}

static inline bool get_bit(const Unary *a, size_t i) { return (a->w[i >> 6] >> (i & 63)) & 1; }
static inline void set_bit(uint64_t *w, size_t i)    { w[i >> 6] |= ONE << (i & 63); }

/* set the first n bits of w to 1 (w zero beyond is the caller's business) */
static void fill_ones(uint64_t *w, size_t n) {
    size_t fw = n >> 6, rem = n & 63;
    for (size_t i = 0; i < fw; i++) w[i] = ~(uint64_t)0;
    if (rem) w[fw] = (ONE << rem) - 1;
}

/* Concatenate: copy the first slen bits of src into dst starting at bit dlen.
 * dst must be zero at and beyond bit dlen, and large enough for dlen+slen bits.
 * src must be canonical (zero beyond slen). */
static void append_bits(uint64_t *dst, size_t dlen, const uint64_t *src, size_t slen) {
    size_t sw = words_of(slen);
    size_t wi = dlen >> 6, off = dlen & 63;
    if (off == 0) {
        if (sw) memcpy(dst + wi, src, sw * sizeof(uint64_t));
        return;
    }
    for (size_t i = 0; i < sw; i++) {
        dst[wi + i] |= src[i] << off;
        uint64_t hi = src[i] >> (64 - off);    /* spill into next word, only if nonzero */
        if (hi) dst[wi + i + 1] |= hi;
    }
}

/* Keep the first nl marks of r, clearing the rest (suffix removal). */
static void truncate_u(Unary *r, size_t nl) {
    size_t ow = words_of(r->len), fw = nl >> 6, rem = nl & 63, start = fw;
    if (rem) { r->w[fw] &= (ONE << rem) - 1; start = fw + 1; }
    for (size_t i = start; i < ow; i++) r->w[i] = 0;
    r->len = nl;
}

static UStatus copy_u(Unary *o, const Unary *a) {
    UStatus st = alloc_u(o, a->len);
    if (st != U_OK) return st;
    size_t nw = words_of(a->len);
    if (nw) memcpy(o->w, a->w, nw * sizeof(uint64_t));
    return U_OK;
}

void u_free(Unary *a) {
    free(a->w);
    a->w = NULL;
    a->len = 0;
}

/* ---- construction / reading ---- */

UStatus u_from_str(Unary *out, const char *marks) {
    out->w = NULL; out->len = 0;
    size_t n = 0;
    for (const char *p = marks; *p; p++) {
        if (*p != MARK_CHAR) return U_ERR_INVALID;
        n++;
    }
    return u_from_size(out, n);
}

UStatus u_from_size(Unary *out, size_t n) {
    UStatus st = alloc_u(out, n);
    if (st != U_OK) return st;
    fill_ones(out->w, n);
    return U_OK;
}

size_t u_to_size(const Unary *a) { return a->len; }   /* external count */

UStatus u_to_str(char **out, const Unary *a) {
    *out = malloc(a->len + 1);
    if (!*out) return U_ERR_NOMEM;
    memset(*out, MARK_CHAR, a->len);
    (*out)[a->len] = '\0';
    return U_OK;
}

bool u_check_invariant(const Unary *a) {
    if (!a->w) return false;
    size_t nw = words_of(a->len);
    for (size_t i = 0; i < a->len; i++)
        if (!get_bit(a, i)) return false;
    size_t rem = a->len & 63;
    if (rem && (a->w[nw - 1] >> rem) != 0) return false;
    return true;
}

/* ---- order: a <= b iff b starts with a ---- */

bool u_le(const Unary *a, const Unary *b) {
    if (a->len > b->len) return false;
    size_t fw = a->len >> 6, rem = a->len & 63;
    if (fw && memcmp(a->w, b->w, fw * sizeof(uint64_t)) != 0) return false;
    if (rem) {
        uint64_t mask = (ONE << rem) - 1;
        if ((a->w[fw] & mask) != (b->w[fw] & mask)) return false;
    }
    return true;
}

bool u_eq(const Unary *a, const Unary *b) {
    if (a->len != b->len) return false;
    size_t nw = words_of(a->len);
    return nw == 0 || memcmp(a->w, b->w, nw * sizeof(uint64_t)) == 0;   /* tails are canonical zero */
}

bool u_lt(const Unary *a, const Unary *b) { return u_le(a, b) && !u_eq(a, b); }
bool u_is_zero(const Unary *a) { return a->len == 0; }

/* ---- arithmetic ---- */

UStatus u_succ(Unary *out, const Unary *a) {
    if (a->len >= SIZE_MAX - 64) { out->w = NULL; out->len = 0; return U_ERR_OVERFLOW; }
    UStatus st = alloc_u(out, a->len + 1);
    if (st != U_OK) return st;
    size_t nw = words_of(a->len);
    if (nw) memcpy(out->w, a->w, nw * sizeof(uint64_t));
    set_bit(out->w, a->len);
    return U_OK;
}

/* concatenation */
UStatus u_add(Unary *out, const Unary *a, const Unary *b) {
    if (a->len > SIZE_MAX - 64 - b->len) { out->w = NULL; out->len = 0; return U_ERR_OVERFLOW; }
    UStatus st = alloc_u(out, a->len + b->len);
    if (st != U_OK) return st;
    size_t nw = words_of(a->len);
    if (nw) memcpy(out->w, a->w, nw * sizeof(uint64_t));
    append_bits(out->w, a->len, b->w, b->len);
    return U_OK;
}

/* cancel suffix: b <= a (prefix test), so what remains is a's leading
 * (a.len - b.len) marks. */
UStatus u_sub(Unary *out, const Unary *a, const Unary *b) {
    out->w = NULL; out->len = 0;
    if (!u_le(b, a)) return U_ERR_UNDERFLOW;
    UStatus st = copy_u(out, a);
    if (st != U_OK) return st;
    truncate_u(out, a->len - b->len);
    return U_OK;
}

/* substitution: each mark of a is replaced by a copy of b */
UStatus u_mul(Unary *out, const Unary *a, const Unary *b) {
    out->w = NULL; out->len = 0;
    if (a->len && b->len > (SIZE_MAX - 64) / a->len) return U_ERR_OVERFLOW;
    UStatus st = alloc_u(out, a->len * b->len);
    if (st != U_OK) return st;
    size_t dlen = 0, nw = words_of(a->len);
    for (size_t i = 0; i < nw; i++) {
        for (uint64_t x = a->w[i]; x; x &= x - 1) {      /* one step per mark of a */
            append_bits(out->w, dlen, b->w, b->len);
            dlen += b->len;
        }
    }
    return U_OK;
}

/* repeated removal: r is truncated in place (suffix removal), q gains one mark
 * per removal. */
UStatus u_divmod(Unary *q, Unary *r, const Unary *a, const Unary *b) {
    q->w = NULL; q->len = 0;
    r->w = NULL; r->len = 0;
    if (u_is_zero(b)) return U_ERR_DIV0;

    UStatus st = copy_u(r, a);
    if (st != U_OK) return st;
    st = alloc_u(q, a->len);          /* capacity only: quotient <= a since b >= 1 */
    if (st != U_OK) { u_free(r); return st; }
    q->len = 0;

    while (u_le(b, r)) {
        truncate_u(r, r->len - b->len);
        set_bit(q->w, q->len);
        q->len++;
    }
    return U_OK;
}

/* repeated multiplication; 0**0 == 1, matching the Python version */
UStatus u_pow(Unary *out, const Unary *a, const Unary *e) {
    out->w = NULL; out->len = 0;
    Unary result;
    UStatus st = u_from_size(&result, 1);
    if (st != U_OK) return st;
    size_t nw = words_of(e->len);
    for (size_t i = 0; i < nw; i++) {
        for (uint64_t x = e->w[i]; x; x &= x - 1) {      /* one multiplication per mark of e */
            Unary t;
            st = u_mul(&t, &result, a);
            u_free(&result);
            if (st != U_OK) return st;
            result = t;
        }
    }
    *out = result;
    return U_OK;
}

/* Euclid */
UStatus u_gcd(Unary *out, const Unary *x, const Unary *y) {
    out->w = NULL; out->len = 0;
    Unary a, b;
    UStatus st = copy_u(&a, x);
    if (st != U_OK) return st;
    st = copy_u(&b, y);
    if (st != U_OK) { u_free(&a); return st; }

    while (!u_is_zero(&b)) {
        Unary q, r;
        st = u_divmod(&q, &r, &a, &b);   /* b != 0 here, so only NOMEM possible */
        if (st != U_OK) { u_free(&a); u_free(&b); return st; }
        u_free(&q);
        u_free(&a);
        a = b;
        b = r;
    }
    u_free(&b);
    *out = a;
    return U_OK;
}

/* ---- conversion: unary <-> binary ---- */

/* Pair off marks; the leftover mark (or its absence) is the next bit
 * (least significant first); the pairs become the new number; repeat.
 * Pairs are found by a bit-position cursor (pair = two adjacent marks).
 * Since every stored mark is 1, the surviving pairs already sit in the low
 * positions of the buffer, so "pairs become the new number" is a truncation. */
UStatus u_to_binary(char **out, const Unary *a) {
    *out = NULL;
    char bits[sizeof(size_t) * 8 + 2];   /* bit count bounded by address-space width */
    size_t nb = 0;

    if (u_is_zero(a)) {
        char *z = malloc(2);
        if (!z) return U_ERR_NOMEM;
        z[0] = '0'; z[1] = '\0';
        *out = z;
        return U_OK;
    }

    Unary n;
    UStatus st = copy_u(&n, a);
    if (st != U_OK) return st;

    while (n.len) {
        size_t rd = 0, pairs = 0;
        while (rd + 1 < n.len && get_bit(&n, rd) && get_bit(&n, rd + 1)) {
            pairs++;                     /* one more pair */
            rd += 2;
        }
        bits[nb++] = (rd < n.len && get_bit(&n, rd)) ? '1' : '0';   /* leftover mark */
        truncate_u(&n, pairs);           /* pairs -> new number */
    }
    u_free(&n);

    char *res = malloc(nb + 1);
    if (!res) return U_ERR_NOMEM;
    for (size_t i = 0; i < nb; i++) res[i] = bits[nb - 1 - i];
    res[nb] = '\0';
    *out = res;
    return U_OK;
}

/* Horner: for each bit, double the string; append one mark if bit is 1. */
UStatus u_from_binary(Unary *out, const char *bits) {
    out->w = NULL; out->len = 0;
    Unary u;
    UStatus st = alloc_u(&u, 0);
    if (st != U_OK) return st;

    for (const char *c = bits; *c; c++) {
        if (*c != '0' && *c != '1') { u_free(&u); return U_ERR_INVALID; }
        size_t bit = (*c == '1');
        if (u.len > (SIZE_MAX - 64 - 1) / 2) { u_free(&u); return U_ERR_OVERFLOW; }
        Unary t;
        st = alloc_u(&t, 2 * u.len + bit);
        if (st != U_OK) { u_free(&u); return st; }
        size_t nw = words_of(u.len);
        if (nw) memcpy(t.w, u.w, nw * sizeof(uint64_t));   /* u */
        append_bits(t.w, u.len, u.w, u.len);               /* + u */
        if (bit) set_bit(t.w, 2 * u.len);                  /* succ */
        u_free(&u);
        u = t;
    }
    *out = u;
    return U_OK;
}
