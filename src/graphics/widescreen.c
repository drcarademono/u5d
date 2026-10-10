#include "common/common.h"
#include "common/file.h"
#include "mod/runtime.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "grap_buf.h"
#include "tileset.h"
#include "widescreen.h"
#include <string.h>

static int s_actorTiles[32];
int WIDE_ActorTile(int actor) { return s_actorTiles[actor]; }

WideLayout WIDE_Layout(int width, int height)
{
    WideLayout l;
    /* Integer horizontal scaling, with the historical 6:5 vertical pixel
     * aspect. Extra logical width becomes map columns, never wider pixels. */
    l.scale = height / 240;
    if (width / 320 < l.scale) l.scale = width / 320;
    if (l.scale < 1) l.scale = 1;
    l.width = width / l.scale;
    l.scaleY = l.scale * 1.2f;
    l.height = height * 5 / (l.scale * 6);
    if(l.height < 200) l.height = 200;
    l.sidebarX = l.width - 128;
    l.columns = (l.sidebarX - 16) / 16;
    l.rows = (l.height - 16 + 15) / 16;
    /* The fixed 11-row combat board needs an odd row count to center its
     * middle tile. World maps retain partial rows to fill the viewport. */
    if (D_5893_map_id >= 128 && !(l.rows & 1)) l.rows--;
    l.mapX = 8 + ((l.sidebarX - 16) - l.columns * 16) / 2;
    l.mapY = 8 + ((l.height - 16) - l.rows * 16) / 2;
    return l;
}

/* Additional world blocks are read separately: looking beyond the resident
 * 32x32 map must not wrap back into the four blocks used by gameplay. */
static byte WorldTile(int x, int y)
{
    static byte blocks[256][256];
    static byte loaded[256];
    static int level = -1;
    static unsigned revision;
    x &= 255;
    y &= 255;
    int block = (y / 16) * 16 + x / 16;
    if (level != D_5895_map_level || revision != MOD_Revision()) {
        memset(loaded, 0, sizeof(loaded));
        level = D_5895_map_level;
        revision = MOD_Revision();
    }
    if (((x - D_589b) & 255) < 32 && ((y - D_589c) & 255) < 32)
        return *ULTIMA_4402_GetTileAddr(x, y);
    if (!loaded[block]) {
        if (!level && MOD_BritanniaIndex()[block] == 255)
            memset(blocks[block], 1, 256);
        else {
            FILE* file = FILE_Open(level ? "UNDER.DAT" : "BRIT.DAT", "rb");
            if (!file || fseek(file, (level ? block : MOD_BritanniaIndex()[block]) * 256, SEEK_SET) != 0 ||
                fread(blocks[block], 1, 256, file) != 256)
                memset(blocks[block], 255, 256);
            if (file) fclose(file);
        }
        loaded[block] = 1;
    }
    byte tile = blocks[block][(y & 15) * 16 + (x & 15)];
    if (tile >= 0x16 && tile <= 0x18) {
        bool open = true;
        for (int i = 0; i < 8; i++)
            if (D_3866[i] == block) open = D_58d0[i] == 0;
        if (open) tile = 0xdf;
    }
    if (tile == 0x19) {
        bool ruined = false;
        for (int i = 0; i < 8; i++)
            if (D_386e[i] == block) ruined = D_58d8[i] > 127;
        if (ruined) tile = 0x1a;
    }
    return tile;
}

byte WIDE_MapTile(int dx, int dy)
{
    int x = D_5896_map_x + dx, y = D_5897_map_y + dy;
    if (D_5893_map_id >= 128) {
        if (x < 0 || y < 0 || x >= 11 || y >= 11) return 255;
        return *ULTIMA_4402_GetTileAddr(x,y);
    }
    if (D_5893_map_id == 0) return WorldTile(x, y);
    /* Vanilla ULTIMA_4402_GetTileAddr uses the location's final map byte
     * as background outside town bounds. Extend the same backdrop rather
     * than turning the extra fullscreen columns/rows into black void. */
    if (x < 0 || y < 0 || x >= 32 || y >= 32) return D_6a07;
    return *ULTIMA_4402_GetTileAddr(x, y);
}

/* Audited against TILES.16 and LOOK2.DAT: freestanding objects with plain
 * black backgrounds. Other furniture with baked brick/grass and solid structural tiles
 * must not be treated as cutout sprites. */
static bool HasObjectBackground(byte tile)
{
    return tile == TILE_MAP_WELL || (tile & 0xfc) == TILE_MAP_FOUNTAIN ||
        tile == TILE_MAP_BRAZIER || tile == TILE_MAP_FLAME || tile == TILE_MAP_DE /* virtue flame */ ||
        tile == TILE_MAP_59 ||
        tile == TILE_MAP_BELLOWS_FC || tile == TILE_MAP_BELLOWS_FD ||
        (tile >= 0x80 && tile <= 0x83) /* animated pendulum */ ||
        tile == TILE_MAP_84 /* stocks */ ||
        tile == TILE_MAP_86 /* metal grate */ || tile == 0x8b /* torture rack */ ||
        tile == 0x8e /* guillotine */ || tile == 0x99 /* portcullis */ ||
        tile == 0xaa /* carpet */ || tile == TILE_MAP_LADDER_UP ||
        tile == TILE_MAP_LADDER_DOWN ||
        tile == 0x88 /* cannonballs */ || tile == 0xa3 /* stack of logs */ ||
        (tile >= TILE_MAP_CANNON_B4 && tile <= TILE_MAP_CANNON_B7);
}

byte WIDE_GroundTile(int dx, int dy)
{
    byte object = WIDE_MapTile(dx,dy);
    if (!GRAP_BUF_TransparentSprites() ||
        !HasObjectBackground(object)) return object;
    /* Maps have no lower terrain layer. Infer only recognized ground from
     * immediate cardinal neighbors; keep isolated objects opaque. */
    const byte floors[] = {TILE_MAP_44, TILE_MAP_GRASS, TILE_MAP_45, 0x40 /* wooden floor */};
    const int offsets[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    int best=0;
    byte ground=object;
    for (size_t i=0;i<sizeof(floors);i++) {
        int count=0;
        for (int j=0;j<4;j++)
            count += WIDE_MapTile(dx+offsets[j][0],dy+offsets[j][1]) == floors[i];
        if(count>best) {best=count;ground=floors[i];}
    }
    return ground;
}

int WIDE_SpritePixel(int tile, int dx, int dy, int x, int y)
{
    /* Southern foreground scenery owns its pixels. Sprite art is 16x16;
     * only the outline margin can cross into the following map row. */
    if (GRAP_BUF_TransparentSprites() && y >= 16) {
        int neighbor = x < 0 ? -1 : x >= 16 ? 1 : 0;
        byte south = WIDE_MapTile(dx + neighbor, dy + 1);
        if (memchr(D_6a86, south, sizeof(D_6a86))) return -1;
    }
    return GRAP_BUF_SpritePixel(tile,x,y);
}

byte WIDE_TerrainPixel(int dx, int dy, int x, int y)
{
    byte color=GRAP_BUF_TilePixel(D_b11e[WIDE_GroundTile(dx,dy)],x,y);
    if(!GRAP_BUF_TransparentSprites()) return color;
    unsigned ref=TILESET_Sample();
    /* Include outlines from adjacent static objects in restored terrain. */
    for(int oy=-1;oy<=1;oy++) for(int ox=-1;ox<=1;ox++) {
        byte tile=WIDE_MapTile(dx+ox,dy+oy);
        if(WIDE_GroundTile(dx+ox,dy+oy)==tile) continue;
        int pixel=WIDE_SpritePixel(D_b11e[tile],dx+ox,dy+oy,x-ox*16,y-oy*16);
        if(pixel>=0) {color=(byte)pixel;ref=TILESET_Sample();}
    }
    TILESET_SetSample(ref);return color;
}

static bool Transparent(byte tile, int distance)
{
    if (tile == 0x4b || tile == 0x4a || tile == 0xba || tile == 0xbb || tile == 0x98)
        return distance != 1;
    return memchr(D_6a86, tile, sizeof(D_6a86)) == NULL;
}

bool WIDE_Visible(int dx, int dy)
{
    if (D_58a5 == 0) return dx == 0 && dy == 0;
    /* Daylight illuminates the larger viewport; torch/night light keeps the
     * original squared-distance limit. Opaque intervening tiles block sight. */
    if (D_58a5 < 50 && dx * dx + dy * dy > D_58a5) return false;
    bool expanded=abs(dx)>5 || abs(dy)>5;
    int x = 0, y = 0, ax = abs(dx), ay = abs(dy);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, error = ax - ay;
    while (x != dx || y != dy) {
        int previousX=x,previousY=y;
        int twice = error * 2;
        if (twice > -ay) { error -= ay; x += sx; }
        if (twice < ax) { error += ax; y += sy; }
        /* A ray cannot squeeze diagonally between two opaque corner tiles. */
        if(x!=previousX && y!=previousY &&
            !Transparent(WIDE_MapTile(x,previousY),x*x+previousY*previousY) &&
            !Transparent(WIDE_MapTile(previousX,y),previousX*previousX+y*y)) return false;
        /* The original view is authoritative. Expanded sightlines must not
         * emerge on the far side of a room it classified as unseen. */
        if(expanded && abs(x)<=5 && abs(y)<=5 && GetMapViewport(x+5,y+5)==255) return false;
        if (x == dx && y == dy) break;
        if (!Transparent(WIDE_MapTile(x, y), x * x + y * y)) return false;
    }
    return true;
}

static void Copy(byte* dst, int stride, int dx, int dy, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++) {
        TILESET_Copy(dst+(dy+y)*stride+dx,g_linearEgaBuffer0+(sy+y)*320+sx,w);
        memcpy(dst + (dy + y) * stride + dx,g_linearEgaBuffer0 + (sy + y) * 320 + sx,w);
    }
}

bool WIDE_Compose(byte* pixels, WideLayout l)
{
    if (!D_58a4 || (D_5893_map_id >= 33 && D_5893_map_id < 128) ||
        l.columns < 11 || l.rows < 11 || !GRAP_BUF_HasTileset()) return false;
    TILESET_Copy(pixels,NULL,(size_t)l.width*l.height);
    memset(pixels, 0, (size_t)l.width * l.height);
    /* Relocate the original status and command column intact. */
    Copy(pixels, l.width, l.sidebarX, 0, 192, 0, 128, 200);
    for (int y = 8; y < l.height - 8; y++) {
        Copy(pixels, l.width, 0, y, 0, 16, 8, 1);
        Copy(pixels, l.width, l.sidebarX - 8, y, 184, 16, 8, 1);
    }
    /* Extend only the frame's blue strips; copy ornaments and wind text. */
    for (int x = 0; x < l.sidebarX; x++) {
        for (int y = 0; y < 8; y++)
            pixels[y * l.width + x] = g_linearEgaBuffer0[y * 320 + 16];
        for (int y = 0; y < 8; y++)
            pixels[(l.height - 8 + y) * l.width + x] = g_linearEgaBuffer0[(184 + y) * 320 + 16];
    }
    /* Corners stay at the edges. Each central opening travels with both of
     * its arrow tips, leaving uninterrupted blue strips between them. */
    Copy(pixels, l.width, 0, 0, 0, 0, 8, 8);
    Copy(pixels, l.width, l.sidebarX - 8, 0, 184, 0, 8, 8);
    Copy(pixels, l.width, l.sidebarX / 2 - 56, 0, 40, 0, 112, 8);
    Copy(pixels, l.width, 0, l.height - 8, 0, 184, 8, 8);
    Copy(pixels, l.width, l.sidebarX - 8, l.height - 8, 184, 184, 8, 8);
    Copy(pixels, l.width, l.sidebarX / 2 - 56, l.height - 8, 40, 184, 112, 8);

    for (int i = 0; i < 32; i++) s_actorTiles[i] = -1;
    int cx = l.columns / 2, cy = l.rows / 2;
    int* sprites = malloc((size_t)l.columns * l.rows * sizeof(int));
    if (!sprites) return false;
    for (int i = 0; i < l.columns * l.rows; i++) sprites[i] = -1;
    if (D_5893_map_id < 33) {
        for (int row = 0; row < l.rows; row++) {
            for (int col = 0; col < l.columns; col++) {
                int dx = col - cx, dy = row - cy;
                if (abs(dx) <= 5 && abs(dy) <= 5) continue;
                if (!WIDE_Visible(dx, dy)) continue;
                byte tile = WIDE_MapTile(dx, dy);
                if (tile == 255) continue;
                int idx = D_b11e[WIDE_GroundTile(dx,dy)];
                for (int actor = 31; actor >= 0; actor--) {
                    ActorFmt* a = &D_5c5a[actor];
                    int ax = a->_2_x - D_5896_map_x, ay = a->_3_y - D_5897_map_y;
                    if (D_5893_map_id == 0) {
                        ax = ((ax + 128) & 255) - 128;
                        ay = ((ay + 128) & 255) - 128;
                    }
                    if (a->_0_tile && a->_1_animTile && a->_4_z == D_5895_map_level && ax == dx && ay == dy) {
                        int sprite = a->_1_animTile;
                        bool reflection = false;
                        if (tile == TILE_MAP_87) continue;
                        if ((a->_0_tile & 0xfc) != TILE_ACTOR_E8 &&
                            a->_0_tile != TILE_ACTOR_SLEEP && a->_0_tile != TILE_ACTOR_DEAD &&
                            sprite != TILE_ACTOR_1D && sprite != TILE_ACTOR_SLEEP &&
                            !(a->_0_tile == TILE_ACTOR_BARD && tile == TILE_MAP_CHAIR_92)) {
                            if (a->_0_tile == TILE_ACTOR_BARD) sprite -= 8;
                            sprite = ULTIMA_ResolveActorTile(sprite, tile,
                                WIDE_MapTile(dx, dy - 1), WIDE_MapTile(dx, dy + 1), &reflection);
                        }
                        if (sprite == -1) continue;
                        if (sprite == -2) idx = D_b11e[TILE_MAP_38];
                        else sprites[row * l.columns + col] = s_actorTiles[actor] = 256 + sprite;
                        if (reflection && row > 0 && WIDE_Visible(dx, dy - 1)) {
                            for (int y = 0; y < 16; y++)
                                if(l.mapY+(row-1)*16+y>=8 && l.mapY+(row-1)*16+y<l.height-8)
                                for (int x = 0; x < 16; x++)
                                    { byte* pixel=pixels+(l.mapY+(row-1)*16+y)*l.width+l.mapX+col*16+x;
                                      int color=GRAP_BUF_TilePixel(D_b11e[TILE_MAP_MIRROR_9E],x,y);
                                      TILESET_Record(pixel,color);*pixel=(byte)color; }
                        }
                    }
                }
                for (int y = 0; y < 16; y++)
                    if(l.mapY+row*16+y>=8 && l.mapY+row*16+y<l.height-8)
                    for (int x = 0; x < 16; x++)
                        { byte* pixel=pixels+(l.mapY+row*16+y)*l.width+l.mapX+col*16+x;
                          int color=GRAP_BUF_TilePixel(idx,x,y);TILESET_Record(pixel,color);*pixel=(byte)color; }
            }
        }
    }
    /* Keep the engine's central tiles, effects, targeting and modal overlays. */
    Copy(pixels, l.width, l.mapX + (cx - 5) * 16, l.mapY + (cy - 5) * 16,
         8, 8, 176, l.height-8-(l.mapY+(cy-5)*16)<176 ?
         l.height-8-(l.mapY+(cy-5)*16) : 176);
    for(int row=0;row<l.rows;row++) for(int col=0;col<l.columns;col++) {
        int dx=col-cx,dy=row-cy;
        if(abs(dx)<=5 && abs(dy)<=5 || !WIDE_Visible(dx,dy)) continue;
        int actorSprite=sprites[row*l.columns+col];
        if(actorSprite>=0 && !GRAP_BUF_SpriteIsTransparent(actorSprite)) continue;
        byte tile=WIDE_MapTile(dx,dy);
        if(WIDE_GroundTile(dx,dy)!=tile)
            GRAP_BUF_DrawSprite(pixels,l.width,l.mapX+col*16,l.mapY+row*16,
                D_b11e[tile],l.mapX,8,l.mapX+l.columns*16,l.height-8,dx,dy);
    }
    for (int row = 0; row < l.rows; row++)
        for (int col = 0; col < l.columns; col++) {
            int sprite = sprites[row * l.columns + col];
            if (sprite >= 0)
                GRAP_BUF_DrawSprite(pixels, l.width, l.mapX + col * 16, l.mapY + row * 16,
                    sprite, l.mapX, 8, l.mapX + l.columns * 16, l.height - 8,col-cx,row-cy);
        }
    free(sprites);
    /* Restore outline margins at the seam after copying central effects. */
    for (int row = 0; row < l.rows; row++)
        for (int col = 0; col < l.columns; col++) {
            int dx = col - cx, dy = row - cy;
            if (abs(dx) > 5 || abs(dy) > 5 || (abs(dx) != 5 && abs(dy) != 5)) continue;
            int tile;
            if (GetMapViewport(dx+5,dy+5)==0 && GetActorMap(dx+5,dy+5)!=0x16)
                tile=256+GetActorMap(dx+5,dy+5);
            else if(GetMapViewport(dx+5,dy+5)!=255 && WIDE_GroundTile(dx,dy)!=WIDE_MapTile(dx,dy))
                tile=D_b11e[WIDE_MapTile(dx,dy)];
            else continue;
            for (int sy = -1; sy <= 16; sy++)
                for (int sx = -1; sx <= 16; sx++) {
                    int px = col * 16 + sx, py = row * 16 + sy;
                    if (px < 0 || py < 0 || px >= l.columns * 16 || py >= l.rows * 16) continue;
                    if (px >= (cx - 5) * 16 && px < (cx + 6) * 16 &&
                        py >= (cy - 5) * 16 && py < (cy + 6) * 16) continue;
                    if (l.mapY+py>=8 && l.mapY+py<l.height-8 && WIDE_SpritePixel(tile, dx, dy, sx, sy) == 0)
                        pixels[(l.mapY + py) * l.width + l.mapX + px] = 0;
                }
        }
    return true;
}

static bool s_ditheredDarkness;
void WIDE_SetDitheredDarkness(bool enabled) { s_ditheredDarkness=enabled; }
bool WIDE_DitheredDarkness(void) { return s_ditheredDarkness; }

/* Structural wall artwork audited against TILES.16 and LOOK2.DAT.
 * This is a fade barrier, not the gameplay collision/LOS classification.
 * See docs/masonry-tiles.md for inclusions and intentionally excluded tiles. */
static bool SolidMasonry(byte tile)
{
    return tile==TILE_MAP_4A || tile==TILE_MAP_4B /* arrow slit / window */ ||
        (tile>=0x4d && tile<=0x57) /* stone walls and crenellations */ ||
        tile==TILE_MAP_SHELF /* window shelf */ ||
        (tile>=0x70 && tile<=0x7f) /* strange walls */ ||
        tile==TILE_MAP_87 /* archway */ ||
        tile==TILE_MAP_97 || tile==TILE_MAP_98 /* odd doors */ ||
        (tile>=TILE_MAP_DOOR_B8 && tile<=TILE_MAP_FIREPLACE) ||
        tile==TILE_MAP_SIGN_F8 /* wall-mounted inscription */ ||
        tile==0xfe; /* wall */
}

/* Darkness coverage, 0..16, at original game-pixel resolution. Visible tiles
 * fade inward over 24 pixels; unseen terrain stays completely hidden.
 * Visible masonry (including concealed doors) keeps its solid silhouette. */
bool WIDE_DarknessMask(byte* mask,int columns,int rows)
{
    int stride=columns+4;
    bool* lit=malloc((size_t)stride*(rows+4)*sizeof(bool));
    bool* solid=malloc((size_t)stride*(rows+4)*sizeof(bool));
    if(!lit || !solid) { free(lit);free(solid);return false; }
    for(int row=-2;row<rows+2;row++) for(int col=-2;col<columns+2;col++) {
        int dx=col-columns/2,dy=row-rows/2;
        byte tile=WIDE_MapTile(dx,dy);
        /* Unavailable terrain is not a source of an inward fade. Town
         * backgrounds are valid tiles, just like the original viewport. */
        solid[(row+2)*stride+col+2]=tile==255 || SolidMasonry(tile);
        lit[(row+2)*stride+col+2]=tile!=255 &&
            (abs(dx)<=5 && abs(dy)<=5 ? GetMapViewport(dx+5,dy+5)!=255 : WIDE_Visible(dx,dy));
    }
    for(int row=0;row<rows;row++) for(int col=0;col<columns;col++) {
        bool visible=lit[(row+2)*stride+col+2];
        byte tile=visible ? WIDE_MapTile(col-columns/2,row-rows/2) : 255;
        bool masonry=SolidMasonry(tile);
        /* Tile visibility is constant across its pixels. Collect only nearby
         * exposed darkness once, rather than scanning 25 tiles per pixel. */
        int candidateX[25],candidateY[25],candidateCount=0;
        if(visible && !masonry && (col!=columns/2 || row!=rows/2))
            for(int oy=-2;oy<=2;oy++) for(int ox=-2;ox<=2;ox++) {
                int i=(row+oy+2)*stride+col+ox+2;
                if(!lit[i] && !solid[i]) {
                    candidateX[candidateCount]=ox;candidateY[candidateCount++]=oy;
                }
            }
        if(!visible || candidateCount==0) {
            for(int py=0;py<16;py++)
                memset(mask+(row*16+py)*columns*16+col*16,visible?0:16,16);
            continue;
        }
        for(int py=0;py<16;py++) for(int px=0;px<16;px++) {
            int shade=0;
            if(!visible) shade=16;
            else if(!masonry && (col!=columns/2 || row!=rows/2)) {
                int nearest=24*24;
                for(int candidate=0;candidate<candidateCount;candidate++) {
                    int ox=candidateX[candidate],oy=candidateY[candidate];
                    int x=px-ox*16,y=py-oy*16;
                    int dx=x<0?-x:x>15?x-15:0,dy=y<0?-y:y>15?y-15:0;
                    int distance=dx*dx+dy*dy;
                    if(distance>=nearest) continue;
                    /* Darkness beyond masonry cannot fade through it onto the
                     * lit side. Trace the short pixel segment to this boundary. */
                    int endX=ox*16+(x<0?0:x>15?15:x);
                    int endY=oy*16+(y<0?0:y>15?15:y);
                    int steps=abs(endX-px)>abs(endY-py)?abs(endX-px):abs(endY-py);
                    bool blocked=false;
                    for(int step=1;step<steps;step++) {
                        int rx=px+(endX-px)*step/steps;
                        int ry=py+(endY-py)*step/steps;
                        int cx=rx>=0?rx/16:(rx-15)/16;
                        int cy=ry>=0?ry/16:(ry-15)/16;
                        if(solid[(row+cy+2)*stride+col+cx+2]) { blocked=true;break; }
                    }
                    if(!blocked) nearest=distance;
                }
                int distance=0;while(distance<24 && (distance+1)*(distance+1)<=nearest) distance++;
                shade=(24-distance)*16/24;if(shade>15) shade=15;
            }
            mask[(row*16+py)*columns*16+col*16+px]=(byte)shade;
        }
    }
    free(solid);free(lit);return true;
}
