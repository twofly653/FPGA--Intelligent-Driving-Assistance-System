#include "ui_format.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>

int ui_heading_degrees(float heading)
{
    if (!isfinite(heading)) return -1;
    heading = fmodf(heading, 360.0f);
    if (heading < 0.0f) heading += 360.0f;
    return ((int)(heading + 0.5f)) % 360;
}


void ui_format_signed_tenths(char *dst, size_t size, int value)
{
    unsigned magnitude = value < 0 ? (unsigned)(-(int64_t)value) : (unsigned)value;
    if (!dst || size == 0) return;
    (void)snprintf(dst, size, "%c%u.%u", value < 0 ? '-' : '+',
                   magnitude / 10u, magnitude % 10u);
}

static int outcode(int x, int y, int l, int t, int r, int b)
{
    return (x < l ? 1 : x > r ? 2 : 0) | (y < t ? 4 : y > b ? 8 : 0);
}

int ui_clip_line(int *x0, int *y0, int *x1, int *y1,
                 int l, int t, int r, int b)
{
    int c0, c1, c, x, y, n;
    if (!x0 || !y0 || !x1 || !y1 || l > r || t > b) return 0;
    for (n = 0; n < 8; ++n) {
        c0 = outcode(*x0, *y0, l, t, r, b);
        c1 = outcode(*x1, *y1, l, t, r, b);
        if (!(c0 | c1)) return 1;
        if (c0 & c1) return 0;
        c = c0 ? c0 : c1;
        if (c & 8) {
            y = b;
            x = *x0 + (int)((int64_t)(*x1 - *x0) * (b - *y0) / (*y1 - *y0));
        } else if (c & 4) {
            y = t;
            x = *x0 + (int)((int64_t)(*x1 - *x0) * (t - *y0) / (*y1 - *y0));
        } else if (c & 2) {
            x = r;
            y = *y0 + (int)((int64_t)(*y1 - *y0) * (r - *x0) / (*x1 - *x0));
        } else {
            x = l;
            y = *y0 + (int)((int64_t)(*y1 - *y0) * (l - *x0) / (*x1 - *x0));
        }
        if (c == c0) { *x0 = x; *y0 = y; }
        else { *x1 = x; *y1 = y; }
    }
    return 0;
}
