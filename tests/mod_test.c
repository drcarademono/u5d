#undef NDEBUG
#include "common/common.h"
#include "common/file.h"
#include "mod/package.h"
#include "mod/runtime.h"
#include "mod/world_resources.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buffer {
    unsigned char bytes[131072];
    size_t size;
} Buffer;
static void bytes(Buffer *b, const void *p, size_t n) {
    assert(b->size + n <= sizeof(b->bytes));
    memcpy(b->bytes + b->size, p, n);
    b->size += n;
}
static void number(Buffer *b, uint32_t v) {
    unsigned char p[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16),
                          (unsigned char)(v >> 24)};
    bytes(b, p, 4);
}
static void header(Buffer *b, unsigned count) {
    b->size = 0;
    bytes(b, "IMOD0001", 8);
    number(b, 4);
    number(b, count);
    bytes(b, "Test", 4);
}
static void rawEntry(Buffer *b, const char *name, const void *base, size_t baseSize,
                     const void *changed, size_t changedSize) {
    number(b, (uint32_t)strlen(name));
    bytes(b, name, strlen(name));
    number(b, (uint32_t)baseSize);
    number(b, (uint32_t)changedSize);
    number(b, MOD_Crc32(base, baseSize));
    number(b, MOD_Crc32(changed, changedSize));
    number(b, 1);
    number(b, 0);
    number(b, (uint32_t)changedSize);
    bytes(b, changed, changedSize);
}
static void entry(Buffer *b, const char *name, const char *base, const char *changed) {
    rawEntry(b, name, base, strlen(base), changed, strlen(changed));
}
static void put(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, size, f) == size);
    assert(!fclose(f));
}
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    (void)context;
    (void)name;
    *size = 4;
    *data = malloc(4);
    assert(*data);
    memcpy(*data, "base", 4);
    return 1;
}
static void value(const char *path, const char *expected, int useMod) {
    FILE *f = useMod ? FILE_Open(path, "rb") : fopen(path, "rb");
    assert(f);
    char b[32] = {0};
    assert(fread(b, 1, sizeof(b) - 1, f) == strlen(expected));
    assert(!strcmp(b, expected));
    fclose(f);
}
static void worldResources(void) {
    static unsigned char overlay[U5_WORLD_INDEX_OFFSET + 256], altered[sizeof(overlay)];
    static unsigned char map[65536], worlds[512], seed[256], brit[256], under[256];
    Buffer b;
    memset(map, 5, sizeof(map));
    memset(map + 256, 7, 256);
    memcpy(overlay + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
    memcpy(altered, overlay, sizeof(overlay));
    memset(altered + U5_WORLD_INDEX_OFFSET, 255, 256);
    altered[U5_WORLD_INDEX_OFFSET] = 1;
    altered[U5_WORLD_INDEX_OFFSET + 255] = 0;
    put("game/DATA.OVL", overlay, sizeof(overlay));
    put("game/BRIT.DAT", map, sizeof(map));
    memset(seed, 0xab, 256); memset(brit, 0x23, 256); memset(under, 0x45, 256);
    put("game/INIT.OOL", seed, 256);
    put("game/BRIT.OOL", seed, 256); put("game/UNDER.OOL", seed, 256);
    put("SAVEGAME/BRIT.OOL", "old world", 9);
    put("SAVEGAME/UNDER.OOL", "old world", 9);
    header(&b, 1);rawEntry(&b, "DATA.OVL", overlay, sizeof(overlay), altered, sizeof(altered));
    put("game/Mods/10-index.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    assert(MOD_BritanniaIndex()[0] == 1 && MOD_BritanniaIndex()[1] == 255 && MOD_BritanniaIndex()[255] == 0);
    assert(MOD_InitializeWorldObjects(worlds));
    for (unsigned i=0;i<512;++i) assert(worlds[i] == (i<256?0:0xab));
    value("SAVEGAME/BRIT.OOL", "old world", 0);
    /* A second package may supply the map paired with the earlier index. */
    header(&b, 1);rawEntry(&b, "BRIT.DAT", map, sizeof(map), map, 512);
    put("game/Mods/11-map.imperamod", b.bytes, b.size);
    header(&b, 2);
    rawEntry(&b, "BRIT.OOL", seed, 256, brit, 256);
    rawEntry(&b, "INIT.OOL", seed, 256, under, 256);
    put("game/Mods/12-objects.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==3);
    assert(MOD_InitializeWorldObjects(worlds));
    assert(!memcmp(worlds,brit,256) && !memcmp(worlds+256,under,256));
    /* Explicit UNDER.OOL overrides INIT.OOL, regardless of existing save files. */
    header(&b, 1);rawEntry(&b, "UNDER.OOL", seed, 256, brit, 256);
    put("game/Mods/13-under.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==4);
    assert(MOD_InitializeWorldObjects(worlds));assert(!memcmp(worlds+256,brit,256));
    remove("SAVEGAME/BRIT.OOL");remove("SAVEGAME/UNDER.OOL");
    assert(MOD_InitializeWorldObjects(worlds));assert(!memcmp(worlds,brit,256));
    /* Invalid object records reject their entire package. */
    remove("game/Mods/13-under.imperamod");
    header(&b, 2);rawEntry(&b, "UNDER.OOL", seed, 256, brit, 255);
    entry(&b,"STORY.DAT","base","partial");
    put("game/Mods/13-under.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==3);value("STORY.DAT","base",1);
    remove("game/Mods/13-under.imperamod");remove("game/Mods/12-objects.imperamod");
    /* Invalid effective index/map pair rejects the map package atomically. */
    header(&b, 2);rawEntry(&b,"BRIT.DAT",map,sizeof(map),map,256);
    entry(&b,"STORY.DAT","base","partial");
    put("game/Mods/11-map.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==1);value("STORY.DAT","base",1);
    remove("game/Mods/11-map.imperamod");
    header(&b, 1);rawEntry(&b,"DATA.OVL",overlay,sizeof(overlay),altered,256);
    put("game/Mods/10-index.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==0);
    assert(!memcmp(MOD_BritanniaIndex(),U5_BritanniaDefaultIndex,256));
    remove("game/Mods/10-index.imperamod");
    /* Compiled defaults work without a base overlay. Failed initialization is atomic. */
    remove("game/DATA.OVL");FILE_SetDataDirectory("game");
    assert(!memcmp(MOD_BritanniaIndex(),U5_BritanniaDefaultIndex,256));
    memset(worlds,0xee,512);put("game/INIT.OOL",seed,255);
    assert(!MOD_InitializeWorldObjects(worlds));
    for(unsigned i=0;i<512;++i) assert(worlds[i]==0xee);
    FILE_SetDataDirectory(NULL);
    const char *files[]={"game/BRIT.DAT","game/INIT.OOL","game/BRIT.OOL","game/UNDER.OOL"};
    for(unsigned i=0;i<4;++i) remove(files[i]);
}
int main(void) {
    assert(MOD_AllowedResource("INIT.GAM"));
    assert(MOD_AllowedResource("BRIT.CBT"));
    assert(!MOD_AllowedResource("../INIT.GAM"));
    assert(!MOD_AllowedResource("ENGINE.CFG"));
    assert(!MOD_AllowedResource("SAVED.GAM"));
    assert(!MOD_AllowedResource("ULTIMA.EXE"));
    assert(MOD_Crc32("123456789", 9) == 0xcbf43926u);
    Buffer b;
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "modded");
    ModPackage p;
    char error[256];
    assert(MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    assert(p.count == 1 && p.entries[0].size == 6 && !memcmp(p.entries[0].data, "modded", 6));
    MOD_Free(&p);
    for (size_t n = 0; n < b.size; n++) {
        assert(!MOD_Decode(b.bytes, n, readBase, NULL, &p, error, sizeof(error)));
        assert(p.count == 0 && error[0]);
    }
    b.bytes[b.size - 1] ^= 1;
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    b.bytes[b.size - 1] ^= 1;
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "good");
    entry(&b, "STORY.DAT", "xxxx", "bad");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    assert(!p.count);
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "good");
    entry(&b, "INIT.GAM", "base", "bad");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    header(&b, 1);
    entry(&b, "../INIT.GAM", "base", "good");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "grow");
    unsigned char saved = b.bytes[b.size - 12];
    b.bytes[b.size - 12] = 255;
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    b.bytes[b.size - 12] = saved;
    put("game/init.gam", "base", 4);
    put("game/story.dat", "base", 4);
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "first");
    put("game/Mods/01-first.imperamod", b.bytes, b.size);
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "second");
    put("game/Mods/02-conflict.imperamod", b.bytes, b.size);
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "third");
    entry(&b, "STORY.DAT", "base", "partial");
    put("game/Mods/03-paired-conflict.imperamod", b.bytes, b.size);
    header(&b, 1);
    entry(&b, "STORY.DAT", "wrong", "bad");
    put("game/Mods/04-invalid.IMPERAMOD", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    value("INIT.GAM", "first", 1);
    value("init.gam", "first", 1);
    value("STORY.DAT", "base", 1);
    value("game/init.gam", "base", 0);
    /* Relative writable saves and explicit paths never go through overlays. */
    put("SAVEGAME/INIT.GAM", "saved", 5);
    value("SAVEGAME/INIT.GAM", "saved", 1);
    value("game/init.gam", "base", 1);
    FILE *f = FILE_Open("INIT.GAM", "wb");
    assert(f);
    assert(fputs("local", f) >= 0);
    fclose(f);
    value("INIT.GAM", "first", 1);
    value("INIT.GAM", "local", 0);
    value("game/init.gam", "base", 0);
    FILE_SetDataDirectory(NULL);
    assert(MOD_LoadedCount() == 0);
    value("INIT.GAM", "local", 1);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    remove("game/Mods/01-first.imperamod");
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    value("INIT.GAM", "second", 1);
    value("STORY.DAT", "base", 1);
    FILE_SetDataDirectory(NULL);
    remove("INIT.GAM");
    remove("game/Mods/02-conflict.imperamod");
    remove("game/Mods/03-paired-conflict.imperamod");
    remove("game/Mods/04-invalid.IMPERAMOD");
    remove("SAVEGAME/INIT.GAM");
    worldResources();
    puts("Mod package validation, atomic mounting, order, conflicts, reloads and original preservation "
         "passed");
    return 0;
}
