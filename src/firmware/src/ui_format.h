#ifndef UI_FORMAT_H
#define UI_FORMAT_H
#include <stddef.h>
/* Shared LCD/UART integer heading; -1 for nonfinite input. */
int ui_heading_degrees(float heading);
void ui_format_signed_tenths(char *dst, size_t size, int value);
/* Clip an inclusive line to an inclusive rectangle. Returns 0 when invisible. */
int ui_clip_line(int *x0, int *y0, int *x1, int *y1,
                 int left, int top, int right, int bottom);
#endif
