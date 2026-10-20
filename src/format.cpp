#include "format.h"

namespace dm {

namespace {

void emitChars(char* out, size_t cap, size_t* len, char c, int count) {
    for (int i = 0; i < count; ++i) {
        if (*len + 1 < cap) out[*len] = c;
        ++*len;
    }
}

void emitNumber(char* out, size_t cap, size_t* len, unsigned long magnitude,
                bool negative, int base, bool upper, char pad, int width) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = digits[magnitude % base];
        magnitude /= base;
    } while (magnitude != 0);

    const int total = n + (negative ? 1 : 0);
    const int padCount = width > total ? width - total : 0;
    if (pad != '0') emitChars(out, cap, len, ' ', padCount);
    if (negative) {
        if (*len + 1 < cap) out[*len] = '-';
        ++*len;
    }
    if (pad == '0') emitChars(out, cap, len, '0', padCount);
    for (int i = n - 1; i >= 0; --i) {
        if (*len + 1 < cap) out[*len] = tmp[i];
        ++*len;
    }
}

}  // namespace

int formatStringV(char* out, size_t cap, const char* fmt, va_list args) {
    if (cap == 0) return 0;
    size_t len = 0;
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            if (len + 1 < cap) out[len] = *p;
            ++len;
            continue;
        }
        ++p;
        if (*p == '%') {
            if (len + 1 < cap) out[len] = '%';
            ++len;
            continue;
        }

        char pad = ' ';
        int width = 0;
        int isLong = 0;
        if (*p == '0') {
            pad = '0';
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        if (*p == 'l') {
            isLong = 1;
            ++p;
        }

        switch (*p) {
            case 'c': {
                char c = char(va_arg(args, int));
                if (len + 1 < cap) out[len] = c;
                ++len;
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s) {
                    if (len + 1 < cap) out[len] = *s;
                    ++len;
                    ++s;
                }
                break;
            }
            case 'd':
            case 'i': {
                long v = isLong ? va_arg(args, long) : long(va_arg(args, int));
                bool neg = v < 0;
                unsigned long mag =
                    neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
                emitNumber(out, cap, &len, mag, neg, 10, false, pad, width);
                break;
            }
            case 'u': {
                unsigned long v =
                    isLong ? va_arg(args, unsigned long)
                           : (unsigned long)va_arg(args, unsigned int);
                emitNumber(out, cap, &len, v, false, 10, false, pad, width);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long v =
                    isLong ? va_arg(args, unsigned long)
                           : (unsigned long)va_arg(args, unsigned int);
                emitNumber(out, cap, &len, v, false, 16, *p == 'X', pad, width);
                break;
            }
            case 'p': {
                unsigned long v = (unsigned long)va_arg(args, void*);
                emitNumber(out, cap, &len, v, false, 16, false, '0', 8);
                break;
            }
            default:
                if (len + 1 < cap) out[len] = '%';
                ++len;
                if (*p) {
                    if (len + 1 < cap) out[len] = *p;
                    ++len;
                }
                break;
        }
        if (*p == '\0') break;
    }
    out[len < cap ? len : cap - 1] = '\0';
    return int(len);
}

int formatString(char* out, size_t cap, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = formatStringV(out, cap, fmt, args);
    va_end(args);
    return n;
}

}  // namespace dm
