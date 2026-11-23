# unary (C)

Arithmetic on unary numerals using only string operations. A C port of `unary.py`.

A natural number *n* is a string of *n* marks. Zero is the empty string.

| operation      | string operation                                   |
|----------------|----------------------------------------------------|
| addition       | concatenation                                      |
| subtraction    | suffix removal (truncation)                        |
| multiplication | substitution (each mark → a copy of the other numeral) |
| order (`<=`)   | prefix test                                        |
| divmod         | repeated removal                                   |
| power          | repeated multiplication                            |
| gcd            | Euclid                                             |
| to binary      | repeated pairing-off                               |
| from binary    | Horner: double the string, append a mark if bit is 1 |

## Layout

```
unary/           one byte per mark ('|' characters, NUL-terminated)
unary_packed/    one bit per mark, packed into uint64_t words   <- preferred
    unary.h      public API
    unary.c      implementation
    unary_test.c self-tests
```

Both directories expose the same function names, so code written against one
compiles against the other (the `Unary` struct fields differ: `s` vs `w`).

## Build and test

```sh
cd unary_packed
gcc -std=c11 -O2 -Wall -Wextra unary.c unary_test.c -o unary_test
./unary_test
```

With sanitizers:

```sh
gcc -std=c11 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
    unary.c unary_test.c -o unary_test_san && ./unary_test_san
```

Use it in your own code by compiling `unary.c` alongside your sources and
including `unary.h`.

## API

```c
typedef struct { uint64_t *w; size_t len; } Unary;   /* packed version */

typedef enum { U_OK, U_ERR_NOMEM, U_ERR_INVALID, U_ERR_UNDERFLOW,
               U_ERR_DIV0, U_ERR_OVERFLOW } UStatus;
const char *u_strerror(UStatus st);

/* construction / reading (the external observer's counting) */
UStatus u_from_str (Unary *out, const char *marks);   /* '|' per mark */
UStatus u_from_size(Unary *out, size_t n);
size_t  u_to_size  (const Unary *a);
UStatus u_to_str   (char **out, const Unary *a);      /* malloc'd */
void    u_free     (Unary *a);

/* order */
bool u_eq(a, b);  bool u_le(a, b);  bool u_lt(a, b);  bool u_is_zero(a);

/* arithmetic */
UStatus u_succ  (Unary *out, const Unary *a);
UStatus u_add   (Unary *out, const Unary *a, const Unary *b);
UStatus u_sub   (Unary *out, const Unary *a, const Unary *b);   /* U_ERR_UNDERFLOW if b > a */
UStatus u_mul   (Unary *out, const Unary *a, const Unary *b);
UStatus u_divmod(Unary *q, Unary *r, const Unary *a, const Unary *b); /* U_ERR_DIV0 if b == 0 */
UStatus u_pow   (Unary *out, const Unary *a, const Unary *e);   /* 0**0 == 1 */
UStatus u_gcd   (Unary *out, const Unary *a, const Unary *b);

/* binary conversion */
UStatus u_to_binary  (char **out, const Unary *a);    /* malloc'd; caller free() */
UStatus u_from_binary(Unary *out, const char *bits);
```

Example:

```c
Unary a, b, r;
u_from_size(&a, 6);
u_from_size(&b, 7);
if (u_mul(&r, &a, &b) == U_OK) {
    printf("%zu\n", u_to_size(&r));   /* 42 */
    u_free(&r);
}
u_free(&a); u_free(&b);
```

### Ownership rules

- Every function that fills an `out` numeral allocates it; release it with `u_free()`.
- `out` must not alias an input.
- On error, `out` is left empty (`NULL` buffer) and needs no freeing.
- `u_to_binary` and `u_to_str` return `malloc`'d strings; release with `free()`.

## Packed representation

Bit *i* of the numeral lives in `w[i / 64]` at position `i % 64`.
Canonical form, relied on by `u_eq` and `u_le`:

- every bit below `len` is 1;
- every bit at or above `len` (in the last word and beyond) is 0.

Every function preserves this. `u_check_invariant()` verifies it and is used by
the tests.

## Assumptions and caveats

Nothing here is a physical claim. Flagged explicitly:

- **Conventions of the reader.** The operations are conventions a reader
  applies to strings of marks; a bare collection of marks has no arithmetic of
  its own.
- **Pairing convention.** Binary conversion treats two adjacent marks as one
  pair. This presupposes the reader can recognise "two" as a pair of marks.
- **`len` is bookkeeping.** A C buffer must carry its size. `len` is used for
  buffer sizing, bit positions and overflow guards; the arithmetic itself
  scans and copies marks. `u_to_size()` is the external observer's count (it
  was `to_int()` in Python).
- **Contents are determined by `len`.** Every stored mark is 1, so the buffer
  holds no information beyond its length. Packing reduces memory 8× relative
  to one byte per mark; it does not change the O(n) space of a unary numeral.
  It is also why `to_binary`'s "pairs become the new number" step is a
  truncation in the packed version.
- **Overflow guards are an addition.** Size computations return
  `U_ERR_OVERFLOW` where Python would simply run out of memory.
- **Growth.** Multiplication and exponentiation produce n·m and n^k marks.
  At one bit per mark this is cheaper, but it still exhausts memory quickly.
  A 64-bit-sized value is out of reach for any unary numeral.

## Testing

- Reference arithmetic is native `size_t` and an independent binary formatter,
  so the checks do not reuse the code under test.
- Each result is also checked for canonical form, independently of the
  arithmetic reference.
- Operand values straddle 64-bit word boundaries (63/64/65, 127/128/129,
  191/192/193).
- The harness validates itself: a deliberately wrong add (`a+b+1`) must be
  flagged, and a numeral with a stray bit past `len` must be rejected by the
  invariant check. If either check passes silently the test run aborts.
- Coverage: all pairs 0..200 for add; 0..300 against the boundary set for
  sub, mul, divmod, gcd, order, binary round trips and error cases; `**` over
  small ranges plus 3^5.

## Measured performance

Single runs, `-O2`, one machine, identical benchmark source for both versions.
Treat as indicative, not definitive.

| operation                    | byte-per-mark | bit-packed |
|------------------------------|--------------:|-----------:|
| mul 6000×6000 (36M marks), ×5 | 0.061 s       | 0.005 s    |
| add 36M + 36M, ×20           | 0.626 s       | 0.027 s    |
| divmod 36M / 7               | 0.031 s       | 0.019 s    |
| to_binary 36M                | 0.031 s       | 0.044 s    |

A 36M-mark numeral occupies 36 MB in the byte version and 4.5 MB when packed.
`to_binary` is slower when packed because its pair-finding loop reads two bits
per step; a word-parallel version would fix that.
