#include "matcher.h"

static inline int is_same_char(char a, char b)
{
    return (a == b);
}

int wc_matcher(const char* str, int len, const char* pt, int pt_len, int nwc)
{
    int i;
    int search_len;
    if ((len < pt_len - nwc) || (len > pt_len && nwc < 1))
        return 1;

    if (pt[0] == '*')
    {
        --nwc;
        for (i = 0; i < len - pt_len + nwc + 2; ++i)
        {
            if (wc_matcher(str + i, len - i, pt + 1, pt_len - 1, nwc) == 0)
                return 0;
        }
        return 1;
    }

    search_len = (len > pt_len) ? pt_len : len;
    for (i = 0; i < search_len - 1; ++i)
    {
        if (!is_same_char(str[i], pt[i]))
        {
            if (pt[i] == '*')
            {
                if (wc_matcher(str + i, len - i, pt + i, pt_len - i, nwc) == 0)
                    return 0;
                --nwc;
            }
            return str[i] - pt[i];
        }
    }

    if (len > pt_len && (pt[i] == '*' || is_same_char(str[i], pt[i])))
        return 0;
    else
        return str[i] - pt[i];
}

// no case sensitive version
static inline int is_same_char_i(char a, char b)
{
    if (a == b)
        return 1;

    if ((a ^ b) == 0x20) {
        char c = a & ~0x20;
        if ((c >= 'A') && (c <= 'Z'))
            return 1;
    };

    return 0;
}

int wc_matcher_i(const char* str, int len, const char* pt, int pt_len, int nwc)
{
    int i;
    int search_len;
    if ((len < pt_len - nwc) || (len > pt_len && nwc < 1))
        return 1;

    if (pt[0] == '*')
    {
        --nwc;
        for (i = 0; i < len - pt_len + nwc + 2; ++i)
        {
            if (wc_matcher_i(str + i, len - i, pt + 1, pt_len - 1, nwc) == 0)
                return 0;
        }
        return 1;
    }

    search_len = (len > pt_len) ? pt_len : len;
    for (i = 0; i < search_len - 1; ++i)
    {
        if (!is_same_char_i(str[i], pt[i]))
        {
            if (pt[i] == '*')
            {
                if (wc_matcher_i(str + i, len - i, pt + i, pt_len - i, nwc) == 0)
                    return 0;
                --nwc;
            }
            return str[i] - pt[i];
        }
    }

    if (len > pt_len && (pt[i] == '*' || is_same_char_i(str[i], pt[i])))
        return 0;
    else
        return str[i] - pt[i];
}