#ifndef IMPERA_MOD_RUNTIME_H
#define IMPERA_MOD_RUNTIME_H
#include <stdio.h>
#include <stdint.h>
void MOD_MountDirectory(const char *directory);
FILE *MOD_OpenResource(const char *name, int *found);
unsigned MOD_LoadedCount(void);
unsigned MOD_Revision(void);
/* Snapshot resets on each mount; never mutates the original compiled index. */
const uint8_t *MOD_BritanniaIndex(void);
/* Build fresh worlds from resources, never from persisted SAVEGAME lists. */
int MOD_InitializeWorldObjects(void *worlds512);
#endif
