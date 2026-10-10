#ifndef U5_SIGNS_H
#define U5_SIGNS_H
#include <stddef.h>
/* SIGNS.DAT records: map, signed floor, x, y, NUL-terminated glyph bytes.
 * The caller passes the buffer starting at this location's header offset. */
static inline int U5_FindSign(const unsigned char *data, size_t size,
                             unsigned map, unsigned floor, unsigned x, unsigned y)
{
    size_t pos = 0;
    while (pos < size && size - pos > 4 && data[pos] == map) {
        size_t end = pos + 4;
        while (end < size && data[end] != 0)
            ++end;
        if (end == size)
            return -1;
        if (data[pos + 1] == (unsigned char)floor &&
            data[pos + 2] == x && data[pos + 3] == y)
            return (int)pos;
        pos = end + 1;
    }
    return -1;
}
#endif
