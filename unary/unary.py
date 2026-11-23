"""
unary.py -- arithmetic on unary numerals using only string operations.

A natural number n is a string of n unary digits '|'.  Zero is the empty string.
No operation below converts to int or counts with len(): addition is
concatenation, subtraction is suffix removal, multiplication is substitution,
order is a prefix test, and conversion to binary is repeated pairing-off.

The only place a count appears is `to_int`, which is the *external* observer
reading the result (used by the tests and for display).  We keep it out of the
arithmetic.

Assumptions flagged (nothing here is a physical claim):
  * The operations are conventions a reader applies to strings of digits;
    a bare collection of unary digits has no arithmetic of its own.
  * Binary conversion uses the pairing convention '||' -> one pair; this
    presupposes the reader can recognise "two" as a pair of marks.
"""

D = "|"  # the unary digit


class Unary:
    __slots__ = ("s",)

    def __init__(self, s: str = ""):
        if any(c != D for c in s):
            raise ValueError(f"unary numeral may contain only {D!r}")
        self.s = s

    # ---- construction / reading (the external observer's counting) ----
    @classmethod
    def from_int(cls, n: int) -> "Unary":
        if n < 0:
            raise ValueError("naturals only")
        return cls(D * n)

    def to_int(self) -> int:
        return len(self.s)  # external count; not used by any operation below

    def __repr__(self) -> str:
        return self.s if self.s else "ε"

    # ---- order: a <= b  iff  b starts with a  (both are runs of D) ----
    def __eq__(self, o): return self.s == o.s
    def __hash__(self): return hash(self.s)
    def __le__(self, o): return o.s.startswith(self.s)
    def __ge__(self, o): return o <= self
    def __lt__(self, o): return self <= o and self != o
    def __gt__(self, o): return o < self
    def __bool__(self): return bool(self.s)

    # ---- arithmetic ----
    def succ(self) -> "Unary":
        return Unary(self.s + D)

    def __add__(self, o): return Unary(self.s + o.s)            # concatenation

    def __sub__(self, o):                                        # cancel suffix
        if not o <= self:
            raise ValueError("a - b undefined for b > a in the naturals")
        return Unary(self.s.removesuffix(o.s))

    def __mul__(self, o): return Unary(self.s.replace(D, o.s))   # each mark -> copy of o

    def __divmod__(self, o):                                     # repeated removal
        if not o:
            raise ZeroDivisionError("division by zero")
        q, r = Unary(), self
        while o <= r:
            r = r - o
            q = q.succ()
        return q, r

    def __floordiv__(self, o): return divmod(self, o)[0]
    def __mod__(self, o): return divmod(self, o)[1]

    def __pow__(self, o):                                        # repeated multiplication
        result = Unary(D)
        for _ in o.s:
            result = result * self
        return result

    def gcd(self, o) -> "Unary":                                 # Euclid
        a, b = self, o
        while b:
            a, b = b, a % b
        return a

    # ---- conversion: unary <-> binary ----
    def to_binary(self) -> str:
        """Pair off marks; the leftover mark (or its absence) is the next bit
        (least significant first); the pairs become the new number; repeat."""
        n, bits = self.s, []
        if not n:
            return "0"
        while n:
            t = n.replace(D + D, "#")        # '#' = one pair, D = leftover
            bits.append("1" if D in t else "0")
            n = t.replace(D, "").replace("#", D)
        return "".join(reversed(bits))

    @classmethod
    def from_binary(cls, bits: str) -> "Unary":
        """Horner: for each bit, double the string; append one mark if bit is 1."""
        u = Unary()
        for c in bits:
            if c not in "01":
                raise ValueError("binary string expected")
            u = u + u
            if c == "1":
                u = u.succ()
        return u

