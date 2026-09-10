#include "common/utility.h"

static inline s32 sort_compare_int(const void* a, const void* b)
{
    s32 value_a = *(s32*)a;
    s32 value_b = *(s32*)b;
    
    if (value_a < value_b)
    {
        return -1;
    }
    
    if (value_a > value_b)
    {
        return 1;
    }
    
    return 0;
}

s32 sort_compare_canvas(const void* a, const void* b)
{
    s32 value_a = (*(Canvas**)a)->order;
    s32 value_b = (*(Canvas**)b)->order;
    
    if (value_a < value_b)
    {
        return -1;
    }
    
    if (value_a > value_b)
    {
        return 1;
    }
    
    return 0;
}

str8 color_to_str8(CF_Color c)
{
    str8 s = make_arena_string(&g_arena, 10);
    CF_Pixel pixel = cf_color_to_pixel(c);
    cf_string_fmt(s, "0x%02X%02X%02X%02X", pixel.r, pixel.g, pixel.b, pixel.a);
    return s;
}