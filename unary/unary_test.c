/*
 * Self-tests for the bit-packed version.
 * Reference = native size_t arithmetic and an independent binary formatter.
 * Every result is also checked for the canonical-form invariant (all marks 1,
 * zero tail), which is independent of the arithmetic reference.
 * Operand values deliberately straddle 64-bit word boundaries (63/64/65, 127/128/129, ...).
 * The harness is validated: a deliberately wrong add (a+b+1) must be caught,
 * and a deliberately non-canonical numeral must be rejected by the invariant check.
 */
#include "unary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
static int verbose = 1;

#define CHECK(cond, ...) do { if (!(cond)) { fails++; if (verbose) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static size_t gcd_ref(size_t a, size_t b) { while (b) { size_t t = a % b; a = b; b = t; } return a; }

static void bin_ref(size_t n, char *buf) {
    char tmp[80]; int k = 0;
    if (n == 0) { strcpy(buf, "0"); return; }
    while (n) { tmp[k++] = (char)('0' + (n & 1)); n >>= 1; }
    for (int i = 0; i < k; i++) buf[i] = tmp[k - 1 - i];
    buf[k] = '\0';
}

/* status check + invariant check + read + free */
static size_t num(UStatus st, Unary *u) {
    if (st != U_OK) { fails++; if (verbose) printf("FAIL: status %s\n", u_strerror(st)); return (size_t)-1; }
    CHECK(u_check_invariant(u), "non-canonical result (len=%zu)", u->len);
    size_t n = u_to_size(u);
    u_free(u);
    return n;
}

static void add_sweep(size_t amax, int mutate) {
    for (size_t a = 0; a <= amax; a++)
        for (size_t b = 0; b <= amax; b++) {
            Unary ua, ub, r;
            u_from_size(&ua, a); u_from_size(&ub, b);
            UStatus st = u_add(&r, &ua, &ub);
            if (mutate && st == U_OK) { Unary t; u_succ(&t, &r); u_free(&r); r = t; }
            size_t got = num(st, &r);
            CHECK(got == a + b, "%zu+%zu: got %zu", a, b, got);
            u_free(&ua); u_free(&ub);
        }
}

static const size_t B_VALUES[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
                                   31,32,33, 62,63,64,65,66, 126,127,128,129,130, 191,192,193 };
#define NB (sizeof B_VALUES / sizeof B_VALUES[0])

static void sweep(size_t amax, size_t astep) {
    for (size_t a = 0; a <= amax; a += astep) {
        Unary ua; u_from_size(&ua, a);
        CHECK(u_check_invariant(&ua), "from_size(%zu) non-canonical", a);
        char ref[80], *got;

        CHECK(u_to_binary(&got, &ua) == U_OK, "to_binary status");
        bin_ref(a, ref);
        CHECK(strcmp(got, ref) == 0, "to_binary(%zu): got %s want %s", a, got, ref);
        { Unary back; CHECK(u_from_binary(&back, ref) == U_OK, "from_binary status");
          CHECK(u_check_invariant(&back), "from_binary(%s) non-canonical", ref);
          CHECK(u_to_size(&back) == a, "from_binary(%s)", ref); u_free(&back); }
        { Unary back; u_from_binary(&back, got);
          CHECK(u_to_size(&back) == a, "roundtrip(%zu)", a); u_free(&back); }
        free(got);

        for (size_t k = 0; k < NB; k++) {
            size_t b = B_VALUES[k];
            Unary ub, r, q;
            u_from_size(&ub, b);
            CHECK(num(u_add(&r, &ua, &ub), &r) == a + b, "%zu+%zu", a, b);
            CHECK(num(u_mul(&r, &ua, &ub), &r) == a * b, "%zu*%zu", a, b);
            CHECK(u_le(&ua, &ub) == (a <= b), "%zu<=%zu", a, b);
            CHECK(u_lt(&ua, &ub) == (a <  b), "%zu<%zu", a, b);
            CHECK(u_eq(&ua, &ub) == (a == b), "%zu==%zu", a, b);
            if (a >= b) CHECK(num(u_sub(&r, &ua, &ub), &r) == a - b, "%zu-%zu", a, b);
            else        CHECK(u_sub(&r, &ua, &ub) == U_ERR_UNDERFLOW, "%zu-%zu should underflow", a, b);
            if (b) {
                UStatus st = u_divmod(&q, &r, &ua, &ub);
                CHECK(st == U_OK, "divmod status");
                if (st == U_OK) {
                    CHECK(u_check_invariant(&q) && u_check_invariant(&r), "divmod non-canonical");
                    CHECK(u_to_size(&q) == a / b && u_to_size(&r) == a % b, "divmod(%zu,%zu)", a, b);
                    u_free(&q); u_free(&r);
                }
            } else {
                CHECK(u_divmod(&q, &r, &ua, &ub) == U_ERR_DIV0, "divmod by zero");
            }
            CHECK(num(u_gcd(&r, &ua, &ub), &r) == gcd_ref(a, b), "gcd(%zu,%zu)", a, b);
            u_free(&ub);
        }
        u_free(&ua);
    }
}

int main(void) {
    /* 1. harness validity */
    int saved = verbose; verbose = 0; fails = 0;
    add_sweep(5, 1);
    int caught = fails;
    fails = 0;
    { Unary bad; u_from_size(&bad, 70); bad.w[1] |= (uint64_t)1 << 40;   /* stray bit beyond len */
      CHECK(!u_check_invariant(&bad), "invariant check accepted a dirty tail"); u_free(&bad); }
    int inv_caught = (fails == 0);
    verbose = saved; fails = 0;
    printf("harness check: wrong add flagged %d times (%s); dirty tail rejected (%s)\n",
           caught, caught ? "ok" : "HARNESS IS TAUTOLOGICAL", inv_caught ? "ok" : "INVARIANT CHECK BROKEN");
    if (!caught || !inv_caught) return 2;

    /* 2. real tests */
    add_sweep(200, 0);   /* all pairs 0..200, crosses word boundaries */
    sweep(24, 1);
    sweep(300, 1);

    for (size_t a = 0; a < 6; a++)
        for (size_t b = 0; b < 5; b++) {
            Unary ua, ub, r; u_from_size(&ua, a); u_from_size(&ub, b);
            size_t want = 1; for (size_t i = 0; i < b; i++) want *= a;
            CHECK(num(u_pow(&r, &ua, &ub), &r) == want, "%zu**%zu", a, b);
            u_free(&ua); u_free(&ub);
        }
    { Unary a, e, r; u_from_size(&a, 3); u_from_size(&e, 5);     /* 3**5 = 243 spans 4 words */
      CHECK(num(u_pow(&r, &a, &e), &r) == 243, "3**5"); u_free(&a); u_free(&e); }

    /* invalid input */
    { Unary x; CHECK(u_from_str(&x, "|x|") == U_ERR_INVALID, "bad unary string accepted"); }
    { Unary x; CHECK(u_from_binary(&x, "10201") == U_ERR_INVALID, "bad binary string accepted"); }

    /* the examples from the Python __main__ */
    { Unary x, y; u_from_size(&x, 11); char *b, *s; u_to_binary(&b, &x); u_to_str(&s, &x);
      printf("11 = %s  ->  binary %s\n", s, b); free(b); free(s); u_free(&x);
      u_from_binary(&y, "1011"); u_to_str(&s, &y); printf("1011 -> %s\n", s); free(s); u_free(&y); }
    { Unary a, b, r; char *s; u_from_str(&a, "||"); u_from_str(&b, "|||"); u_mul(&r, &a, &b);
      u_to_str(&s, &r); printf("|| * ||| = %s\n", s); free(s); u_free(&a); u_free(&b); u_free(&r); }
    { Unary a, b, q, r; char *sq, *sr; u_from_size(&a, 7); u_from_str(&b, "||"); u_divmod(&q, &r, &a, &b);
      u_to_str(&sq, &q); u_to_str(&sr, &r);
      printf("7 divmod 2 = (%s, %s)\n", sq, sr[0] ? sr : "e");
      free(sq); free(sr); u_free(&a); u_free(&b); u_free(&q); u_free(&r); }

    printf("self-test: %s\n", fails == 0 ? "all passed" : "FAILURES");
    if (fails) printf("%d failures\n", fails);
    return fails != 0;
}
