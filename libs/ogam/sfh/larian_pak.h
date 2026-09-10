/* larian_pak.h - v0.7 - Public Domain
 * 
 * A single-header C/C++ library for parsing Larian Studios .pak files, 
 * extracting game data, and loading meshes/textures for modern graphics APIs.
 * Supports Divinity: Original Sin 1 & 2, and Baldur's Gate 3.
 *
 * USAGE:
 *   #define LARIAN_PAK_IMPLEMENTATION
 *   #include "larian_pak.h"
 *
 * OPTIONAL BG3 SUPPORT:
 *   #define LARIAN_ENABLE_ZSTD // Requires linking with -lzstd
 *   #define LARIAN_ENABLE_ZLIB // Requires linking with -lz
 */

#ifndef LARIAN_PAK_H
#define LARIAN_PAK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" 
{
#endif
    
    /* ============================================================================
     * MATH & GRAPHICS DATA
     * ============================================================================ */
    
    typedef struct
    {
        float x;
        float y;
        float z;
    } larian_vec3;
    
    typedef struct
    {
        float x;
        float y;
        float z;
        float w;
    } larian_vec4;
    
    typedef struct
    {
        larian_vec3 translate;
        larian_vec4 rotation_quat;
        float scale;
    } larian_transform;
    
    typedef struct
    {
        larian_vec3 position;
        larian_vec3 normal;
        larian_vec4 tangent;
        float uv_x;
        float uv_y;
    } larian_vertex;
    
    typedef struct
    {
        larian_vertex* vertices;
        uint32_t vertex_count;
        uint32_t* indices;
        uint32_t index_count;
    } larian_mesh;
    
    typedef enum
    {
        LARIAN_TEX_FORMAT_UNKNOWN,
        LARIAN_TEX_FORMAT_RGBA8_UNORM,
        LARIAN_TEX_FORMAT_BC1_RGBA, 
        LARIAN_TEX_FORMAT_BC3_RGBA, 
        LARIAN_TEX_FORMAT_BC7_RGBA
    } larian_texture_format;
    
    typedef struct
    {
        uint32_t width;
        uint32_t height;
        uint32_t mip_levels;
        larian_texture_format format;
        uint8_t* pixel_data;
        size_t data_size;
    } larian_texture;
    
    /* ============================================================================
     * SCENE & GAME DATA
     * ============================================================================ */
    
    typedef struct
    {
        char name[64];
        char uuid[40];
        larian_transform transform;
        char script_name[128]; 
        bool has_script;
    } larian_trigger_volume;
    
    typedef struct
    {
        char template_uuid[40];  
        char visual_uuid[40];    
        larian_transform transform;
        char attached_script[128];
    } larian_object_ref;
    
    /* ============================================================================
     * PAK ARCHIVE STRUCTURES
     * ============================================================================ */
    
    typedef struct larian_pak larian_pak;
    
    typedef struct
    {
        char name[256];
        uint64_t offset;
        uint32_t size_compressed;
        uint32_t size_uncompressed;
        uint8_t compression_flags; 
    } larian_pak_entry;
    
    /* ============================================================================
     * API
     * ============================================================================ */
    
    larian_pak* larian_pak_open(const char* filepath);
    void larian_pak_close(larian_pak* pak);
    uint32_t larian_pak_get_file_count(larian_pak* pak);
    bool larian_pak_get_file_entry(larian_pak* pak, uint32_t index, larian_pak_entry* out_entry);
    
    uint8_t* larian_pak_extract_file(larian_pak* pak, const larian_pak_entry* entry);
    void larian_pak_free_file_data(uint8_t* data);
    
    bool larian_parse_mesh(const uint8_t* file_data, size_t data_size, larian_mesh* out_mesh);
    void larian_free_mesh(larian_mesh* mesh);
    
    bool larian_parse_texture(const uint8_t* file_data, size_t data_size, larian_texture* out_texture);
    void larian_free_texture(larian_texture* texture);
    
    /* Texture Utilities */
    uint32_t* larian_texture_get_rgba8(const larian_texture* texture);
    void larian_free_rgba8(uint32_t* pixels);
    
    uint32_t larian_parse_trigger_volumes(const uint8_t* file_data, size_t data_size, larian_trigger_volume** out_triggers);
    uint32_t larian_parse_object_refs(const uint8_t* file_data, size_t data_size, larian_object_ref** out_objects);
    void larian_free_scene_data(void* ptr);
    
#ifdef __cplusplus
}
#endif
#endif // LARIAN_PAK_H

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */
#ifdef LARIAN_PAK_IMPLEMENTATION

#include <stdio.h>
#include <string.h>

#ifndef LARIAN_MALLOC
#include <stdlib.h>
#define LARIAN_MALLOC(sz) malloc(sz)
#define LARIAN_FREE(p)    free(p)
#endif

// Optional Compression Libraries
#ifdef LARIAN_ENABLE_ZSTD
#include <zstd.h>
#endif

#ifdef LARIAN_ENABLE_ZLIB
#include <zlib.h>
#endif

/* ----------------------------------------------------------------------------
 * UTILS & INLINE LZ4 DECOMPRESSOR
 * ---------------------------------------------------------------------------- */

static int larian_lz4_decompress(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_capacity)
{
    const uint8_t* ip = src;
    const uint8_t* const iend = src + src_size;
    uint8_t* op = dst;
    uint8_t* const oend = dst + dst_capacity;
    
    while (ip < iend)
    {
        uint8_t token = *ip++;
        size_t length = (token >> 4);
        
        if (length == 15)
        {
            unsigned s;
            do
            {
                if (ip >= iend)
                {
                    return -1;
                }
                s = *ip++;
                length += s;
            }
            while (s == 255);
        }
        
        if ((size_t)(oend - op) < length || (size_t)(iend - ip) < length)
        {
            return -1;
        }
        
        memcpy(op, ip, length);
        op += length;
        ip += length;
        
        if (ip >= iend)
        {
            break;
        }
        
        if (iend - ip < 2)
        {
            return -1;
        }
        
        uint16_t offset = ip[0] | (ip[1] << 8);
        ip += 2;
        
        const uint8_t* match = op - offset;
        if (match < dst || match >= op)
        {
            return -1;
        }
        
        length = (token & 0x0F);
        if (length == 15)
        {
            unsigned s;
            do
            {
                if (ip >= iend)
                {
                    return -1;
                }
                s = *ip++;
                length += s;
            }
            while (s == 255);
        }
        length += 4; 
        
        if ((size_t)(oend - op) < length)
        {
            return -1;
        }
        
        for (size_t i = 0; i < length; i++)
        {
            *op++ = *match++;
        }
    }
    return (int)(op - dst);
}

/* ----------------------------------------------------------------------------
 * PAK ARCHIVE PARSER
 * ---------------------------------------------------------------------------- */

struct larian_pak
{
    FILE* file_handle;
    uint32_t version;
    uint32_t entry_count;
    larian_pak_entry* entries;
};

#pragma pack(push, 1)
typedef struct 
{
    char name[256];
    uint64_t offset;
    uint32_t size_on_disk;
    uint32_t size_uncompressed;
    uint32_t archive_part;
    uint32_t flags;
    uint32_t crc;
    uint32_t unknown2;
} larian_pak_v18_fat_entry;
#pragma pack(pop)

larian_pak* larian_pak_open(const char* filepath)
{
    if (!filepath)
    {
        return NULL;
    }
    
    FILE* f = fopen(filepath, "rb");
    if (!f)
    {
        return NULL;
    }
    
    uint32_t magic;
    uint32_t version;
    
    if (fread(&magic, 4, 1, f) != 1 || fread(&version, 4, 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    
    if (magic != 0x4B50534C) // "LSPK"
    {
        fclose(f);
        return NULL;
    }
    
    uint64_t file_list_offset;
    uint32_t file_list_size;
    fread(&file_list_offset, 8, 1, f);
    fread(&file_list_size, 4, 1, f);
    
    larian_pak* pak = (larian_pak*)LARIAN_MALLOC(sizeof(larian_pak));
    if (!pak)
    {
        fclose(f);
        return NULL;
    }
    
    pak->file_handle = f;
    pak->version = version;
    pak->entry_count = 0;
    pak->entries = NULL;
    
    fseek(f, (int)file_list_offset, SEEK_SET);
    
    uint32_t num_files = 0;
    uint32_t compressed_fat_size = 0;
    
    if (fread(&num_files, sizeof(uint32_t), 1, f) != 1 || 
        fread(&compressed_fat_size, sizeof(uint32_t), 1, f) != 1)
    {
        fclose(f);
        LARIAN_FREE(pak);
        return NULL;
    }
    
    uint8_t* comp_fat = (uint8_t*)LARIAN_MALLOC(compressed_fat_size);
    if (!comp_fat || fread(comp_fat, 1, compressed_fat_size, f) != compressed_fat_size)
    {
        if (comp_fat)
        {
            LARIAN_FREE(comp_fat);
        }
        fclose(f);
        LARIAN_FREE(pak);
        return NULL;
    }
    
    size_t uncomp_fat_cap = num_files * 288; // Max size for v18 layout
#ifdef LARIAN_ENABLE_ZSTD
    if (pak->version >= 18) 
    {
        unsigned long long zstd_size = ZSTD_getFrameContentSize(comp_fat, compressed_fat_size);
        if (zstd_size != ZSTD_CONTENTSIZE_ERROR && zstd_size != ZSTD_CONTENTSIZE_UNKNOWN)
        {
            uncomp_fat_cap = zstd_size;
        }
    }
#endif
    
    uint8_t* uncomp_fat = (uint8_t*)LARIAN_MALLOC(uncomp_fat_cap);
    if (!uncomp_fat)
    {
        LARIAN_FREE(comp_fat);
        fclose(f);
        LARIAN_FREE(pak);
        return NULL;
    }
    
    int decomp_res = -1;
    if (pak->version >= 18) 
    {
#ifdef LARIAN_ENABLE_ZSTD
        size_t res = ZSTD_decompress(uncomp_fat, uncomp_fat_cap, comp_fat, compressed_fat_size);
        decomp_res = ZSTD_isError(res) ? -1 : 1;
#else
        fprintf(stderr, "Error: Archive uses ZSTD but LARIAN_ENABLE_ZSTD is not defined.\n");
#endif
    }
    else 
    {
        decomp_res = larian_lz4_decompress(comp_fat, compressed_fat_size, uncomp_fat, uncomp_fat_cap);
    }
    
    LARIAN_FREE(comp_fat);
    
    if (decomp_res > 0)
    {
        pak->entries = (larian_pak_entry*)LARIAN_MALLOC(sizeof(larian_pak_entry) * num_files);
        if (!pak->entries)
        {
            LARIAN_FREE(uncomp_fat);
            fclose(f);
            LARIAN_FREE(pak);
            return NULL;
        }
        
        pak->entry_count = num_files;
        uint8_t* ptr = uncomp_fat;
        
        for (uint32_t i = 0; i < num_files; ++i)
        {
            if (pak->version == 18) 
            {
                strncpy(pak->entries[i].name, (char*)ptr, 255);
                pak->entries[i].name[255] = '\0';
                ptr += 256;
                
                memcpy(&pak->entries[i].offset, ptr, 8); ptr += 8;
                memcpy(&pak->entries[i].size_compressed, ptr, 4); ptr += 4;
                memcpy(&pak->entries[i].size_uncompressed, ptr, 4); ptr += 4;
                
                uint32_t archive_part;
                memcpy(&archive_part, ptr, 4); ptr += 4;
                
                uint32_t flags;
                memcpy(&flags, ptr, 4); ptr += 4;
                pak->entries[i].compression_flags = flags & 0x0F;
                
                ptr += 8; 
            }
            else if (pak->version == 15 || pak->version == 16) 
            {
                strncpy(pak->entries[i].name, (char*)ptr, 255);
                pak->entries[i].name[255] = '\0';
                ptr += 256;
                
                memcpy(&pak->entries[i].offset, ptr, 8); ptr += 8;
                
                uint32_t archive_part;
                memcpy(&archive_part, ptr, 4); ptr += 4;
                
                uint32_t flags;
                memcpy(&flags, ptr, 4); ptr += 4;
                pak->entries[i].compression_flags = flags & 0x0F;
                
                memcpy(&pak->entries[i].size_compressed, ptr, 4); ptr += 4;
                memcpy(&pak->entries[i].size_uncompressed, ptr, 4); ptr += 4;
            }
            else 
            {
                size_t name_len = strlen((char*)ptr);
                strncpy(pak->entries[i].name, (char*)ptr, 255);
                pak->entries[i].name[255] = '\0';
                ptr += (name_len + 1);
                
                memcpy(&pak->entries[i].offset, ptr, 8); ptr += 8;
                memcpy(&pak->entries[i].size_uncompressed, ptr, 4); ptr += 4;
                memcpy(&pak->entries[i].size_compressed, ptr, 4); ptr += 4;
                
                if (pak->version >= 10)
                {
                    ptr += 4;
                }
                
                pak->entries[i].compression_flags = 2; 
            }
        }
    }
    
    LARIAN_FREE(uncomp_fat);
    return pak;
}

void larian_pak_close(larian_pak* pak)
{
    if (!pak)
    {
        return;
    }
    
    if (pak->file_handle)
    {
        fclose(pak->file_handle);
    }
    
    if (pak->entries)
    {
        LARIAN_FREE(pak->entries);
    }
    
    LARIAN_FREE(pak);
}

uint32_t larian_pak_get_file_count(larian_pak* pak)
{
    return pak ? pak->entry_count : 0;
}

bool larian_pak_get_file_entry(larian_pak* pak, uint32_t index, larian_pak_entry* out_entry)
{
    if (!pak || !pak->entries || index >= pak->entry_count || !out_entry)
    {
        return false;
    }
    
    *out_entry = pak->entries[index];
    return true;
}

uint8_t* larian_pak_extract_file(larian_pak* pak, const larian_pak_entry* entry)
{
    if (!pak || !entry || !pak->file_handle)
    {
        return NULL;
    }
    
    fseek(pak->file_handle, (int)entry->offset, SEEK_SET);
    
    uint8_t* buffer = (uint8_t*)LARIAN_MALLOC(entry->size_uncompressed);
    if (!buffer)
    {
        return NULL;
    }
    
    if (entry->compression_flags == 0)
    {
        fread(buffer, 1, entry->size_uncompressed, pak->file_handle);
        return buffer;
    }
    
    uint8_t* comp_buffer = (uint8_t*)LARIAN_MALLOC(entry->size_compressed);
    if (!comp_buffer)
    {
        LARIAN_FREE(buffer);
        return NULL;
    }
    
    fread(comp_buffer, 1, entry->size_compressed, pak->file_handle);
    bool success = false;
    
    if (entry->compression_flags == 1) // Zlib (or mislabeled DOS1 LZ4)
    {
#ifdef LARIAN_ENABLE_ZLIB
        unsigned long dest_len = entry->size_uncompressed;
        if (uncompress(buffer, &dest_len, comp_buffer, entry->size_compressed) == Z_OK)
        {
            success = true;
        }
#else
        // Fallback: Some older PAKs use flag 1 for LZ4. 
        if (larian_lz4_decompress(comp_buffer, entry->size_compressed, buffer, entry->size_uncompressed) >= 0)
        {
            success = true;
        }
#endif
    }
    else if (entry->compression_flags == 2) // LZ4
    {
        if (larian_lz4_decompress(comp_buffer, entry->size_compressed, buffer, entry->size_uncompressed) >= 0)
        {
            success = true;
        }
    }
    else if (entry->compression_flags == 3) // ZSTD
    {
#ifdef LARIAN_ENABLE_ZSTD
        size_t res = ZSTD_decompress(buffer, entry->size_uncompressed, comp_buffer, entry->size_compressed);
        if (!ZSTD_isError(res))
        {
            success = true;
        }
#else
        fprintf(stderr, "Error: File requires ZSTD. Recompile with #define LARIAN_ENABLE_ZSTD\n");
#endif
    }
    
    LARIAN_FREE(comp_buffer);
    
    if (!success)
    {
        LARIAN_FREE(buffer);
        return NULL;
    }
    
    return buffer;
}

void larian_pak_free_file_data(uint8_t* data)
{
    if (data)
    {
        LARIAN_FREE(data);
    }
}

/* ----------------------------------------------------------------------------
 * TEXTURE DECODING UTILS (BC1 / BC3)
 * ---------------------------------------------------------------------------- */

static void larian_unpack_rgb565(uint16_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (color >> 11) & 0x1F;
    *g = (color >> 5) & 0x3F;
    *b = color & 0x1F;
    
    *r = (*r << 3) | (*r >> 2);
    *g = (*g << 2) | (*g >> 4);
    *b = (*b << 3) | (*b >> 2);
}

static uint32_t larian_pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static void larian_decode_bc1_block(const uint8_t* block, uint32_t* out_pixels, uint32_t stride, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height)
{
    uint16_t color0;
    uint16_t color1;
    memcpy(&color0, block, 2);
    memcpy(&color1, block + 2, 2);
    
    uint32_t bits;
    memcpy(&bits, block + 4, 4);
    
    uint8_t r[4];
    uint8_t g[4];
    uint8_t b[4];
    uint8_t a[4];
    
    larian_unpack_rgb565(color0, &r[0], &g[0], &b[0]);
    larian_unpack_rgb565(color1, &r[1], &g[1], &b[1]);
    
    a[0] = 255;
    a[1] = 255;
    
    if (color0 > color1)
    {
        r[2] = (2 * r[0] + r[1]) / 3;
        g[2] = (2 * g[0] + g[1]) / 3;
        b[2] = (2 * b[0] + b[1]) / 3;
        a[2] = 255;
        
        r[3] = (r[0] + 2 * r[1]) / 3;
        g[3] = (g[0] + 2 * g[1]) / 3;
        b[3] = (b[0] + 2 * b[1]) / 3;
        a[3] = 255;
    }
    else
    {
        r[2] = (r[0] + r[1]) / 2;
        g[2] = (g[0] + g[1]) / 2;
        b[2] = (b[0] + b[1]) / 2;
        a[2] = 255;
        
        r[3] = 0;
        g[3] = 0;
        b[3] = 0;
        a[3] = 0;
    }
    
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            uint32_t index = (bits >> (2 * (y * 4 + x))) & 0x03;
            uint32_t px = x_offset + x;
            uint32_t py = y_offset + y;
            
            if (px < width && py < height)
            {
                out_pixels[py * stride + px] = larian_pack_rgba(r[index], g[index], b[index], a[index]);
            }
        }
    }
}

static void larian_decode_bc3_block(const uint8_t* block, uint32_t* out_pixels, uint32_t stride, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height)
{
    uint8_t alpha0 = block[0];
    uint8_t alpha1 = block[1];
    
    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    
    if (alpha0 > alpha1)
    {
        alphas[2] = (6 * alpha0 + 1 * alpha1) / 7;
        alphas[3] = (5 * alpha0 + 2 * alpha1) / 7;
        alphas[4] = (4 * alpha0 + 3 * alpha1) / 7;
        alphas[5] = (3 * alpha0 + 4 * alpha1) / 7;
        alphas[6] = (2 * alpha0 + 5 * alpha1) / 7;
        alphas[7] = (1 * alpha0 + 6 * alpha1) / 7;
    }
    else
    {
        alphas[2] = (4 * alpha0 + 1 * alpha1) / 5;
        alphas[3] = (3 * alpha0 + 2 * alpha1) / 5;
        alphas[4] = (2 * alpha0 + 3 * alpha1) / 5;
        alphas[5] = (1 * alpha0 + 4 * alpha1) / 5;
        alphas[6] = 0;
        alphas[7] = 255;
    }
    
    uint64_t alpha_indices = 0;
    for (int i = 0; i < 6; ++i)
    {
        alpha_indices |= ((uint64_t)block[2 + i]) << (8 * i);
    }
    
    uint8_t final_alphas[16];
    for (int i = 0; i < 16; ++i)
    {
        final_alphas[i] = alphas[(alpha_indices >> (3 * i)) & 0x07];
    }
    
    uint32_t temp_pixels[16];
    larian_decode_bc1_block(block + 8, temp_pixels, 4, 0, 0, 4, 4);
    
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            uint32_t px = x_offset + x;
            uint32_t py = y_offset + y;
            
            if (px < width && py < height)
            {
                uint32_t color = temp_pixels[y * 4 + x];
                uint8_t r = color & 0xFF;
                uint8_t g = (color >> 8) & 0xFF;
                uint8_t b = (color >> 16) & 0xFF;
                uint8_t a = final_alphas[y * 4 + x];
                
                out_pixels[py * stride + px] = larian_pack_rgba(r, g, b, a);
            }
        }
    }
}

uint32_t* larian_texture_get_rgba8(const larian_texture* texture)
{
    if (!texture || !texture->pixel_data)
    {
        return NULL;
    }
    
    uint32_t pixel_count = texture->width * texture->height;
    uint32_t* rgba_data = (uint32_t*)LARIAN_MALLOC(pixel_count * sizeof(uint32_t));
    
    if (!rgba_data)
    {
        return NULL;
    }
    
    if (texture->format == LARIAN_TEX_FORMAT_RGBA8_UNORM)
    {
        memcpy(rgba_data, texture->pixel_data, pixel_count * sizeof(uint32_t));
        return rgba_data;
    }
    
    if (texture->format == LARIAN_TEX_FORMAT_BC1_RGBA || texture->format == LARIAN_TEX_FORMAT_BC3_RGBA)
    {
        uint32_t block_size = (texture->format == LARIAN_TEX_FORMAT_BC1_RGBA) ? 8 : 16;
        const uint8_t* ptr = texture->pixel_data;
        
        for (uint32_t y = 0; y < texture->height; y += 4)
        {
            for (uint32_t x = 0; x < texture->width; x += 4)
            {
                if (texture->format == LARIAN_TEX_FORMAT_BC1_RGBA)
                {
                    larian_decode_bc1_block(ptr, rgba_data, texture->width, x, y, texture->width, texture->height);
                }
                else
                {
                    larian_decode_bc3_block(ptr, rgba_data, texture->width, x, y, texture->width, texture->height);
                }
                ptr += block_size;
            }
        }
        return rgba_data;
    }
    
    if (texture->format == LARIAN_TEX_FORMAT_BC7_RGBA)
    {
        for (uint32_t i = 0; i < pixel_count; ++i)
        {
            rgba_data[i] = 0xFFFF00FF; 
        }
        return rgba_data;
    }
    
    return rgba_data;
}

void larian_free_rgba8(uint32_t* pixels)
{
    if (pixels)
    {
        LARIAN_FREE(pixels);
    }
}

/* ----------------------------------------------------------------------------
 * TEXTURE / MESH PARSERS
 * ---------------------------------------------------------------------------- */

bool larian_parse_mesh(const uint8_t* file_data, size_t data_size, larian_mesh* out_mesh)
{
    if (!file_data || !out_mesh)
    {
        return false;
    }
    
    out_mesh->vertex_count = 0;
    out_mesh->index_count = 0;
    out_mesh->vertices = NULL;
    out_mesh->indices = NULL;
    
    return false; 
}

void larian_free_mesh(larian_mesh* mesh)
{
    if (mesh)
    {
        if (mesh->vertices)
        {
            LARIAN_FREE(mesh->vertices);
        }
        if (mesh->indices)
        {
            LARIAN_FREE(mesh->indices);
        }
    }
}

bool larian_parse_texture(const uint8_t* file_data, size_t data_size, larian_texture* out_texture)
{
    if (!file_data || !out_texture || data_size < 128)
    {
        return false;
    }
    
    uint32_t magic;
    memcpy(&magic, file_data, 4);
    
    if (magic != 0x20534444)
    {
        return false; 
    }
    
    const uint8_t* header = file_data + 4;
    
    uint32_t height;
    uint32_t width;
    uint32_t mip_map_count;
    
    memcpy(&height, header + 8, 4);
    memcpy(&width, header + 12, 4);
    memcpy(&mip_map_count, header + 24, 4);
    
    out_texture->width = width;
    out_texture->height = height;
    out_texture->mip_levels = (mip_map_count == 0) ? 1 : mip_map_count;
    
    uint32_t pixel_format_flags;
    uint32_t four_cc;
    
    memcpy(&pixel_format_flags, header + 76, 4);
    memcpy(&four_cc, header + 80, 4);
    
    out_texture->format = LARIAN_TEX_FORMAT_UNKNOWN;
    
    if (pixel_format_flags & 0x4) 
    {
        if (four_cc == 0x31545844)
        {
            out_texture->format = LARIAN_TEX_FORMAT_BC1_RGBA;
        }
        else if (four_cc == 0x35545844)
        {
            out_texture->format = LARIAN_TEX_FORMAT_BC3_RGBA;
        }
        else if (four_cc == 0x30315844)
        {
            out_texture->format = LARIAN_TEX_FORMAT_BC7_RGBA;
        }
    }
    else if (pixel_format_flags & 0x40) 
    {
        out_texture->format = LARIAN_TEX_FORMAT_RGBA8_UNORM;
    }
    
    uint32_t header_size = 128;
    if (four_cc == 0x30315844)
    {
        header_size += 20; 
    }
    
    out_texture->data_size = data_size - header_size;
    out_texture->pixel_data = (uint8_t*)LARIAN_MALLOC(out_texture->data_size);
    
    if (!out_texture->pixel_data)
    {
        return false;
    }
    
    memcpy(out_texture->pixel_data, file_data + header_size, out_texture->data_size);
    return true;
}

void larian_free_texture(larian_texture* texture)
{
    if (texture && texture->pixel_data)
    {
        LARIAN_FREE(texture->pixel_data);
    }
}

/* ----------------------------------------------------------------------------
 * SCENE DATA PARSERS
 * ---------------------------------------------------------------------------- */

static const uint8_t* larian_find_pattern(const uint8_t* data, size_t size, const char* pattern)
{
    size_t pattern_len = strlen(pattern);
    if (pattern_len > size)
    {
        return NULL;
    }
    
    for (size_t i = 0; i <= size - pattern_len; ++i)
    {
        if (memcmp(data + i, pattern, pattern_len) == 0)
        {
            return data + i;
        }
    }
    return NULL;
}

uint32_t larian_parse_trigger_volumes(const uint8_t* file_data, size_t data_size, larian_trigger_volume** out_triggers)
{
    if (!file_data || !out_triggers)
    {
        return 0;
    }
    
    uint32_t capacity = 16;
    uint32_t count = 0;
    larian_trigger_volume* array = (larian_trigger_volume*)LARIAN_MALLOC(sizeof(larian_trigger_volume) * capacity);
    
    if (!array)
    {
        return 0;
    }
    
    const uint8_t* cursor = file_data;
    size_t remaining_size = data_size;
    
    while (remaining_size > 0)
    {
        const uint8_t* match = larian_find_pattern(cursor, remaining_size, "TriggerVolume");
        if (!match)
        {
            break;
        }
        
        if (count >= capacity)
        {
            capacity *= 2;
            larian_trigger_volume* new_array = (larian_trigger_volume*)LARIAN_MALLOC(sizeof(larian_trigger_volume) * capacity);
            
            if (!new_array)
            {
                LARIAN_FREE(array);
                return 0;
            }
            
            memcpy(new_array, array, sizeof(larian_trigger_volume) * count);
            LARIAN_FREE(array);
            array = new_array;
        }
        
        memset(&array[count], 0, sizeof(larian_trigger_volume));
        strncpy(array[count].name, "TriggerVolume", 63);
        array[count].transform.scale = 1.0f;
        
        count++;
        
        size_t advanced = (match - cursor) + 13;
        cursor += advanced;
        remaining_size -= advanced;
    }
    
    if (count > 0)
    {
        *out_triggers = array;
    }
    else
    {
        LARIAN_FREE(array);
        *out_triggers = NULL;
    }
    
    return count;
}

uint32_t larian_parse_object_refs(const uint8_t* file_data, size_t data_size, larian_object_ref** out_objects)
{
    if (!file_data || !out_objects)
    {
        return 0;
    }
    
    uint32_t capacity = 16;
    uint32_t count = 0;
    larian_object_ref* array = (larian_object_ref*)LARIAN_MALLOC(sizeof(larian_object_ref) * capacity);
    
    if (!array)
    {
        return 0;
    }
    
    const uint8_t* cursor = file_data;
    size_t remaining_size = data_size;
    
    while (remaining_size > 0)
    {
        const uint8_t* match = larian_find_pattern(cursor, remaining_size, "Item");
        if (!match)
        {
            break;
        }
        
        if (count >= capacity)
        {
            capacity *= 2;
            larian_object_ref* new_array = (larian_object_ref*)LARIAN_MALLOC(sizeof(larian_object_ref) * capacity);
            
            if (!new_array)
            {
                LARIAN_FREE(array);
                return 0;
            }
            
            memcpy(new_array, array, sizeof(larian_object_ref) * count);
            LARIAN_FREE(array);
            array = new_array;
        }
        
        memset(&array[count], 0, sizeof(larian_object_ref));
        array[count].transform.scale = 1.0f;
        
        count++;
        
        size_t advanced = (match - cursor) + 4;
        cursor += advanced;
        remaining_size -= advanced;
    }
    
    if (count > 0)
    {
        *out_objects = array;
    }
    else
    {
        LARIAN_FREE(array);
        *out_objects = NULL;
    }
    
    return count;
}

void larian_free_scene_data(void* ptr)
{
    if (ptr)
    {
        LARIAN_FREE(ptr);
    }
}

#endif // LARIAN_PAK_IMPLEMENTATION