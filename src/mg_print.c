#include "mg_internal.h"
#include "mg_font.h"



// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   Copy Start  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>


#ifdef _MSC_VER
#define vsnprintf _vsnprintf
#endif

MgFont mgStockFont;
MgFont* mgfont = &mgStockFont;

// Converts 8-bit codepage entries into Unicode code points.
static int cp1252[] = {
    0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152,
    0xfffd, 0x017d, 0xfffd, 0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122,
    0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178, 0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6,
    0x00a7, 0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af, 0x00b0, 0x00b1, 0x00b2, 0x00b3,
    0x00b4, 0x00b5, 0x00b6, 0x00b7, 0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf, 0x00c0,
    0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd,
    0x00ce, 0x00cf, 0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7, 0x00d8, 0x00d9, 0x00da,
    0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df, 0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4,
    0x00f5, 0x00f6, 0x00f7, 0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff,
};

static int border(MgSurface* surface, int x, int y) {
    MgPixel top = mgGet(surface, 0, 0);
    MgPixel c = mgGet(surface, x, y);
    return (c.r == top.r && c.g == top.g && c.b == top.b) || x >= surface->w || y >= surface->h;
}

static void scan(MgSurface* surface, int* x, int* y, int* rowh) {
    while (*y < surface->h) {
        if (*x >= surface->w) {
            *x = 0;
            (*y) += *rowh;
            *rowh = 1;
        }
        if (!border(surface, *x, *y))
            return;
        (*x)++;
    }
}

/*
 * Watermarks are encoded vertically in the alpha channel using seven pixels
 * starting at x, y. The first and last alpha values contain the magic values
 * 0b10101010 and 0b01010101 respectively.
 */
static int readWatermark(MgSurface* bmp, int x, int y, int* big, int* small) {
    const int magicHeader = 0xAA;
    const int magicFooter = 0x55;

    unsigned char watermark[7];

    for (int i = 0; i < 7; i++) {
        MgPixel c = mgGet(bmp, x, y + i);
        watermark[i] = c.a;
    }

    if (watermark[0] != magicHeader || watermark[6] != magicFooter) {
        return 0;
    }

    *big = watermark[1] | (watermark[2] << 8) | (watermark[3] << 16) | (watermark[4] << 24);
    *small = watermark[5];

    return 1;
}

int mgLoadGlyphs(MgFont* font, int codepage) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int rowh = 1;

    MgGlyph* g;
    switch (codepage) {
        case TCP_ASCII:
            font->numGlyphs = 128 - 32;
            break;
        case TCP_1252:
            font->numGlyphs = 256 - 32;
            break;
        case TCP_UTF32:
            if (!readWatermark(font->surface, 0, 0, &font->numGlyphs, &rowh)) {
                return 0;
            }
            h = rowh;
            x = 1;
            break;
        default:
            errno = EINVAL;
            return 0;
    }

    font->glyphs = (MgGlyph*)calloc(font->numGlyphs, sizeof(MgGlyph));

    for (int index = 0; index < font->numGlyphs; index++) {
        // Look up the Unicode code point.
        g = &font->glyphs[index];

        if (codepage != TCP_UTF32) {
            // Find the next glyph.
            scan(font->surface, &x, &y, &rowh);

            if (y >= font->surface->h) {
                errno = EINVAL;
                return 0;
            }

            // Scan the width and height
            w = h = 0;
            while (!border(font->surface, x + w, y)) {
                w++;
            }

            while (!border(font->surface, x, y + h)) {
                h++;
            }
        }

        switch (codepage) {
            case TCP_ASCII:
                g->code = index + 32;
                break;
            case TCP_1252:
                if (index < 96) {
                    g->code = index + 32;
                } else {
                    g->code = cp1252[index - 96];
                }
                break;
            case TCP_UTF32:
                if (!readWatermark(font->surface, x, y, &g->code, &w)) {
                    // Maybe we are at the end of a row?
                    x = 0;
                    y += rowh;
                    if (!readWatermark(font->surface, x, y, &g->code, &w)) {
                        return 0;
                    }
                }
                x++;
                break;
            default:
                return 0;
        }

        g->x = x;
        g->y = y;
        g->w = w;
        g->h = h;
        x += w;
        if (h != font->glyphs[0].h) {
            errno = EINVAL;
            return 0;
        }

        if (h > rowh) {
            rowh = h;
        }
    }

    // Sort by code point.
    for (int i = 1; i < font->numGlyphs; i++) {
        int j = i;
        MgGlyph g = font->glyphs[i];
        while (j > 0 && font->glyphs[j - 1].code > g.code) {
            font->glyphs[j] = font->glyphs[j - 1];
            j--;
        }
        font->glyphs[j] = g;
    }

    return 1;
}

MgFont* mgLoadFont(MgSurface* surface, int codepage) {
    MgFont* font = (MgFont*)calloc(1, sizeof(MgFont));
    font->surface = surface;
    if (!mgLoadGlyphs(font, codepage)) {
        mgFreeFont(font);
        return NULL;
    }
    return font;
}

void mgFreeFont(MgFont* font) {
    mgFree(font->surface);
    free(font->glyphs);
    free(font);
}

static MgGlyph* get(MgFont* font, int code) {
    unsigned lo = 0, hi = font->numGlyphs;
    while (lo < hi) {
        unsigned guess = (lo + hi) / 2;
        if (code < font->glyphs[guess].code)
            hi = guess;
        else
            lo = guess + 1;
    }

    if (lo == 0 || font->glyphs[lo - 1].code != code)
        return &font->glyphs['?' - 32];
    else
        return &font->glyphs[lo - 1];
}

void mgSetupFont(MgFont* font) {
    // Load the stock font if needed.
    if (font == mgfont && !mgfont->surface) {
        mgfont->surface = mgLoadImageMem(mg_font, mg_font_size);
        mgLoadGlyphs(mgfont, 1252);
    }
}

void mgPrint(MgSurface* dest, MgFont* font, int x, int y, MgPixel color, const char* text, ...) {
    char tmp[1024];
    MgGlyph* g;
    va_list args;
    const char* p;
    int start = x, c;

    mgSetupFont(font);

    // Expand the formatting string.
    va_start(args, text);
    vsnprintf(tmp, sizeof(tmp), text, args);
    tmp[sizeof(tmp) - 1] = 0;
    va_end(args);

    // Print each glyph.
    p = tmp;
    while (*p) {
        p = mgDecodeUTF8(p, &c);
        if (c == '\r')
            continue;
        if (c == '\n') {
            x = start;
            y += mgTextHeight(font, "");
            continue;
        }
        g = get(font, c);
        mgBlitTint(dest, font->surface, x, y, g->x, g->y, g->w, g->h, color);
        x += g->w;
    }
}

int mgTextWidth(MgFont* font, const char* text) {
    int x = 0, w = 0, c;
    mgSetupFont(font);

    while (*text) {
        text = mgDecodeUTF8(text, &c);
        if (c == '\n' || c == '\r') {
            x = 0;
        } else {
            x += get(font, c)->w;
            w = (x > w) ? x : w;
        }
    }
    return w;
}

int mgTextHeight(MgFont* font, const char* text) {
    int rowh, h, c;
    mgSetupFont(font);

    h = rowh = get(font, 0)->h;
    while (*text) {
        text = mgDecodeUTF8(text, &c);
        if (c == '\n' && *text)
            h += rowh;
    }
    return h;
}


// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   Copy End  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^