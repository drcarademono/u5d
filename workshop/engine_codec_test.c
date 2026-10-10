/* Compatibility oracle: run Workshop's encoded output through the real engine
 * decoder without linking the game's GUI or its 16-bit typedefs into C++. */
#include "common/common.h"
#include "common/file.h"
#include "common/lzw.h"
#include <string.h>
void debug(const char *format, ...) { (void)format; }
int FILE_ReadU8(FILE *f, u8 *out) {
    int value = fgetc(f);
    if (value == EOF)
        return 0;
    *out = (u8)value;
    return 1;
}
int FILE_ReadU32LE(FILE *f, u32 *out) {
    u8 b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    *out = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return 1;
}
int WorkshopEngineDecode(const void *input, size_t size, void **result, unsigned *length) {
    FILE *f = tmpfile();
    u8 *out = NULL;
    u32 decoded = 0;
    if (!f)
        return 0;
    if (fwrite(input, 1, size, f) != size || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return 0;
    }
    int ok = LzwDecompressFile(f, &out, &decoded) == 0;
    fclose(f);
    *result = out;
    *length = decoded;
    return ok;
}

#include "common/signs.h"
int WorkshopEngineFindSign(const unsigned char *bytes, unsigned size,
                          unsigned map, unsigned floor, unsigned x, unsigned y)
{
    return U5_FindSign(bytes, size, map, floor, x, y);
}
