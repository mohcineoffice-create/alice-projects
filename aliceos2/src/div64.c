/* 64-bit division helpers normally provided by libgcc (needed on 32-bit x86). */
#include "types.h"

static u64 udivmod(u64 n, u64 d, u64 *rem) {
    u64 q = 0, r = 0;
    if (d == 0) { *rem = 0; return 0; }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (u64)1 << i; }
    }
    *rem = r;
    return q;
}

u64 __udivdi3(u64 n, u64 d) { u64 r; return udivmod(n, d, &r); }
u64 __umoddi3(u64 n, u64 d) { u64 r; udivmod(n, d, &r); return r; }

s64 __divdi3(s64 n, s64 d) {
    int neg = (n < 0) != (d < 0);
    u64 r, q = udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 n, s64 d) {
    u64 r;
    udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
    return n < 0 ? -(s64)r : (s64)r;
}

s64 __divmoddi4(s64 n, s64 d, s64 *rem) {
    s64 q = __divdi3(n, d);
    if (rem) *rem = n - q * d;
    return q;
}
u64 __udivmoddi4(u64 n, u64 d, u64 *rem) {
    u64 r, q = udivmod(n, d, &r);
    if (rem) *rem = r;
    return q;
}
