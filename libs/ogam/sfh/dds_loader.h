#ifndef DDS_LOADER_H
#define DDS_LOADER_H

/* Force POSIX features before standard includes on Linux/GCC/Clang */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

/* MSVC CRT Security Warning Suppression */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Windows-specific allocation headers for MinGW / MSVC */
#if defined(_WIN32) || defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#endif

/* Detect SSE2 Intrinsics Across Compilers */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define DDS_USE_SSE2 1
#endif

#ifdef __cplusplus
extern "C" {
#endif
    
    /*
        Public API
        - channels: 1 (Grayscale), 2 (RGB packed in 32-bit), 3 (RGBA 32-bit)
    */
    void* dds_load(const char* filename, int mipmap_level, int channels, int* out_width, int* out_height);
    void  dds_free(void* data);
    
#ifdef DDS_LOADER_IMPLEMENTATION
    
    /* -------------------------------------------------------------------------- */
    /* Memory Allocation Macros Setup                                             */
    /* -------------------------------------------------------------------------- */
    
#ifndef DDS_ALLOC
#define DDS_ALLOC(size) malloc(size)
#define DDS__USING_DEFAULT_ALLOC 1
#endif
    
#ifndef DDS_FREE
#define DDS_FREE(ptr) free(ptr)
#define DDS__USING_DEFAULT_FREE 1
#endif
    
    /* DDS Constants & Structures */
#define DDS_MAGIC 0x20534444
    
#define DDSD_CAPS        0x1
#define DDSD_HEIGHT      0x2
#define DDSD_WIDTH       0x4
#define DDSD_PITCH       0x8
#define DDSD_PIXELFORMAT 0x1000
#define DDSD_MIPMAPCOUNT 0x20000
#define DDSD_LINEARSIZE  0x80000
    
#define DDPF_ALPHAPIXELS 0x1
#define DDPF_ALPHA      0x2
#define DDPF_FOURCC     0x4
#define DDPF_RGB        0x40
#define DDPF_LUMINANCE  0x20000
    
#define FOURCC_DXT1 0x31545844
#define FOURCC_DXT3 0x33545844
#define FOURCC_DXT5 0x35545844
#define FOURCC_DX10 0x30315844
    
#define DXGI_FORMAT_BC1_UNORM 71
#define DXGI_FORMAT_BC2_UNORM 77
#define DXGI_FORMAT_BC3_UNORM 83
    
#pragma pack(push, 1)
    typedef struct 
    {
        uint32_t size;
        uint32_t flags;
        uint32_t fourCC;
        uint32_t rgb_bit_count;
        uint32_t r_bit_mask;
        uint32_t g_bit_mask;
        uint32_t b_bit_mask;
        uint32_t a_bit_mask;
    } DDS_PIXELFORMAT;
    
    typedef struct 
    {
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        uint32_t pitch_or_linear_size;
        uint32_t depth;
        uint32_t mipmap_count;
        uint32_t reserved1[11];
        DDS_PIXELFORMAT ddspf;
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    } DDS_HEADER;
    
    typedef struct 
    {
        uint32_t dxgi_format;
        uint32_t resource_dimension;
        uint32_t misc_flags;
        uint32_t array_size;
        uint32_t misc_flags2;
    } DDS_HEADER_DX10;
#pragma pack(pop)
    
    /* Portable Cross-Compiler 16-Byte Aligned Allocator Handling Custom Allocators */
    static inline void* dds__aligned_malloc(size_t size, size_t alignment) 
    {
#if defined(DDS_ALIGNED_ALLOC)
        return DDS_ALIGNED_ALLOC(size, alignment);
#elif defined(DDS__USING_DEFAULT_ALLOC) && (defined(_MSC_VER) || defined(__MINGW32__) || defined(_WIN32))
        return _aligned_malloc(size, alignment);
#elif defined(DDS__USING_DEFAULT_ALLOC) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
        size_t padded_size = (size + alignment - 1) & ~(alignment - 1);
        return aligned_alloc(alignment, padded_size);
#elif defined(DDS__USING_DEFAULT_ALLOC) && defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
        void* ptr = NULL;
        if (posix_memalign(&ptr, alignment, size) != 0) return NULL;
        return ptr;
#else
        /* Manual Alignment Fallback for Custom Allocators (e.g., custom DDS_ALLOC) */
        void* raw = DDS_ALLOC(size + alignment + sizeof(void*));
        if (!raw) return NULL;
        void** ptr = (void**)(((uintptr_t)raw + sizeof(void*) + alignment - 1) & ~(alignment - 1));
        ptr[-1] = raw;
        return ptr;
#endif
    }
    
    static inline void dds__aligned_free(void* ptr) 
    {
        if (!ptr) return;
#if defined(DDS_ALIGNED_FREE)
        DDS_ALIGNED_FREE(ptr);
#elif defined(DDS__USING_DEFAULT_FREE) && (defined(_MSC_VER) || defined(__MINGW32__) || defined(_WIN32))
        _aligned_free(ptr);
#elif defined(DDS__USING_DEFAULT_FREE) && ((defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L))
        free(ptr);
#else
        DDS_FREE(((void**)ptr)[-1]);
#endif
    }
    
    /* Helpers */
    static inline uint8_t dds__scale_bits(uint32_t val, uint32_t bits) 
    {
        if (bits == 0) return 0;
        if (bits >= 8) return (uint8_t)(val >> (bits - 8));
        return (uint8_t)((val * 255) / ((1U << bits) - 1));
    }
    
    static inline uint32_t dds__count_bits(uint32_t mask, uint32_t* shift) 
    {
        if (mask == 0) { *shift = 0; return 0; }
        *shift = 0;
        while ((mask & 1) == 0) { mask >>= 1; *shift += 1; }
        uint32_t count = 0;
        while (mask & 1) { count++; mask >>= 1; }
        return count;
    }
    
    static inline void dds__unpack_rgb565(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) 
    {
        *r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
        *g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
        *b = (uint8_t)((c         & 0x1F) * 255 / 31);
    }
    
    /* Decompressors */
    static void dds__decompress_bc1_block(const uint8_t* block, uint8_t* out_rgba, int stride) 
    {
        uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
        uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
        uint32_t lookup = (uint32_t)(block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24));
        
        uint8_t colors[4][4];
        dds__unpack_rgb565(c0, &colors[0][0], &colors[0][1], &colors[0][2]); colors[0][3] = 255;
        dds__unpack_rgb565(c1, &colors[1][0], &colors[1][1], &colors[1][2]); colors[1][3] = 255;
        
        if (c0 > c1) 
        {
            colors[2][0] = (uint8_t)((2 * colors[0][0] + colors[1][0]) / 3);
            colors[2][1] = (uint8_t)((2 * colors[0][1] + colors[1][1]) / 3);
            colors[2][2] = (uint8_t)((2 * colors[0][2] + colors[1][2]) / 3);
            colors[2][3] = 255;
            
            colors[3][0] = (uint8_t)((colors[0][0] + 2 * colors[1][0]) / 3);
            colors[3][1] = (uint8_t)((colors[0][1] + 2 * colors[1][1]) / 3);
            colors[3][2] = (uint8_t)((colors[0][2] + 2 * colors[1][2]) / 3);
            colors[3][3] = 255;
        } else 
        {
            colors[2][0] = (uint8_t)((colors[0][0] + colors[1][0]) / 2);
            colors[2][1] = (uint8_t)((colors[0][1] + colors[1][1]) / 2);
            colors[2][2] = (uint8_t)((colors[0][2] + colors[1][2]) / 2);
            colors[2][3] = 255;
            
            colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
        }
        
        for (int y = 0; y < 4; y++) 
        {
            for (int x = 0; x < 4; x++) 
            {
                uint8_t idx = (uint8_t)((lookup >> (2 * (y * 4 + x))) & 0x03);
                uint8_t* dst = out_rgba + (y * stride) + (x * 4);
                memcpy(dst, colors[idx], 4);
            }
        }
    }
    
    static void dds__decompress_bc2_block(const uint8_t* block, uint8_t* out_rgba, int stride) 
    {
        dds__decompress_bc1_block(block + 8, out_rgba, stride);
        for (int y = 0; y < 4; y++) 
        {
            uint16_t row = (uint16_t)(block[y * 2] | (block[y * 2 + 1] << 8));
            for (int x = 0; x < 4; x++) 
            {
                uint8_t a = (uint8_t)((row >> (x * 4)) & 0x0F);
                out_rgba[(y * stride) + (x * 4) + 3] = (uint8_t)((a * 255) / 15);
            }
        }
    }
    
    static void dds__decompress_bc3_block(const uint8_t* block, uint8_t* out_rgba, int stride) 
    {
        dds__decompress_bc1_block(block + 8, out_rgba, stride);
        uint8_t a0 = block[0], a1 = block[1];
        uint8_t alphas[8];
        alphas[0] = a0;
        alphas[1] = a1;
        
        if (a0 > a1) 
        {
            for (int i = 1; i < 7; i++) alphas[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
        } else 
        {
            for (int i = 1; i < 5; i++) alphas[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
            alphas[6] = 0; alphas[7] = 255;
        }
        
        uint64_t a_bits = 0;
        for (int i = 0; i < 6; i++) a_bits |= ((uint64_t)block[2 + i]) << (i * 8);
        
        for (int y = 0; y < 4; y++) 
        {
            for (int x = 0; x < 4; x++) 
            {
                uint8_t idx = (uint8_t)((a_bits >> (3 * (y * 4 + x))) & 0x07);
                out_rgba[(y * stride) + (x * 4) + 3] = alphas[idx];
            }
        }
    }
    
    void dds_free(void* data) 
    {
        if (data) dds__aligned_free(data);
    }
    
    void* dds_load(const char* filename, int mipmap_level, int channels, int* out_width, int* out_height) 
    {
        if (!filename || channels < 1 || channels > 3) return NULL;
        
        FILE* f = fopen(filename, "rb");
        if (!f) return NULL;
        
        uint32_t magic;
        if (fread(&magic, 1, 4, f) != 4 || magic != DDS_MAGIC) 
        {
            fclose(f); return NULL;
        }
        
        DDS_HEADER header;
        if (fread(&header, 1, sizeof(header), f) != sizeof(header)) 
        {
            fclose(f); return NULL;
        }
        
        uint32_t fourCC = header.ddspf.fourCC;
        if ((header.ddspf.flags & DDPF_FOURCC) && fourCC == FOURCC_DX10) 
        {
            DDS_HEADER_DX10 dx10;
            if (fread(&dx10, 1, sizeof(dx10), f) != sizeof(dx10)) 
            {
                fclose(f); return NULL;
            }
            if (dx10.dxgi_format == DXGI_FORMAT_BC1_UNORM) fourCC = FOURCC_DXT1;
            else if (dx10.dxgi_format == DXGI_FORMAT_BC2_UNORM) fourCC = FOURCC_DXT3;
            else if (dx10.dxgi_format == DXGI_FORMAT_BC3_UNORM) fourCC = FOURCC_DXT5;
        }
        
        uint32_t w = header.width;
        uint32_t h = header.height;
        uint32_t mip_count = (header.flags & DDSD_MIPMAPCOUNT) ? header.mipmap_count : 1;
        if (mipmap_level >= (int)mip_count) 
        {
            fclose(f); return NULL;
        }
        
        int is_compressed = (header.ddspf.flags & DDPF_FOURCC);
        for (int i = 0; i < mipmap_level; i++) 
        {
            size_t mip_size = is_compressed ? (((w + 3) / 4) * ((h + 3) / 4) * ((fourCC == FOURCC_DXT1) ? 8 : 16))
                : ((size_t)w * h * (header.ddspf.rgb_bit_count / 8));
            if (fseek(f, (long)mip_size, SEEK_CUR) != 0) 
            {
                fclose(f); return NULL;
            }
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
        }
        
        if (w > 0x7FFF || h > 0x7FFF) 
        {
            fclose(f); return NULL;
        }
        
        uint8_t* rgba_buf = (uint8_t*)dds__aligned_malloc((size_t)w * h * 4, 16);
        if (!rgba_buf) 
        {
            fclose(f); return NULL;
        }
        
        if (is_compressed) 
        {
            size_t block_size = (fourCC == FOURCC_DXT1) ? 8 : 16;
            uint32_t blocks_x = (w + 3) / 4;
            uint32_t blocks_y = (h + 3) / 4;
            uint8_t* block_buf = (uint8_t*)DDS_ALLOC(blocks_x * block_size);
            
            if (!block_buf) { dds__aligned_free(rgba_buf); fclose(f); return NULL; }
            
            for (uint32_t by = 0; by < blocks_y; by++) 
            {
                if (fread(block_buf, 1, blocks_x * block_size, f) != blocks_x * block_size) 
                {
                    DDS_FREE(block_buf); dds__aligned_free(rgba_buf); fclose(f); return NULL;
                }
                for (uint32_t bx = 0; bx < blocks_x; bx++) 
                {
                    uint8_t block_rgba[4 * 4 * 4];
                    const uint8_t* src_block = block_buf + (bx * block_size);
                    
                    if (fourCC == FOURCC_DXT1)      dds__decompress_bc1_block(src_block, block_rgba, 16);
                    else if (fourCC == FOURCC_DXT3) dds__decompress_bc2_block(src_block, block_rgba, 16);
                    else if (fourCC == FOURCC_DXT5) dds__decompress_bc3_block(src_block, block_rgba, 16);
                    
                    for (int py = 0; py < 4; py++) 
                    {
                        uint32_t pixel_y = by * 4 + py;
                        if (pixel_y >= h) break;
                        for (int px = 0; px < 4; px++) 
                        {
                            uint32_t pixel_x = bx * 4 + px;
                            if (pixel_x >= w) break;
                            memcpy(rgba_buf + (pixel_y * w + pixel_x) * 4, block_rgba + (py * 4 + px) * 4, 4);
                        }
                    }
                }
            }
            DDS_FREE(block_buf);
        } else 
        {
            uint32_t bytes_per_pixel = header.ddspf.rgb_bit_count / 8;
            if (bytes_per_pixel == 0) { dds__aligned_free(rgba_buf); fclose(f); return NULL; }
            
            size_t raw_size = (size_t)w * h * bytes_per_pixel;
            uint8_t* raw_buf = (uint8_t*)DDS_ALLOC(raw_size);
            if (!raw_buf) { dds__aligned_free(rgba_buf); fclose(f); return NULL; }
            
            if (fread(raw_buf, 1, raw_size, f) != raw_size) 
            {
                DDS_FREE(raw_buf); dds__aligned_free(rgba_buf); fclose(f); return NULL;
            }
            
            uint32_t r_shift, g_shift, b_shift, a_shift;
            uint32_t r_bits = dds__count_bits(header.ddspf.r_bit_mask, &r_shift);
            uint32_t g_bits = dds__count_bits(header.ddspf.g_bit_mask, &g_shift);
            uint32_t b_bits = dds__count_bits(header.ddspf.b_bit_mask, &b_shift);
            uint32_t a_bits = dds__count_bits(header.ddspf.a_bit_mask, &a_shift);
            
            for (uint32_t i = 0; i < w * h; i++) 
            {
                uint32_t pixel = 0;
                memcpy(&pixel, raw_buf + (i * bytes_per_pixel), bytes_per_pixel);
                rgba_buf[i * 4 + 0] = r_bits ? dds__scale_bits((pixel & header.ddspf.r_bit_mask) >> r_shift, r_bits) : 0;
                rgba_buf[i * 4 + 1] = g_bits ? dds__scale_bits((pixel & header.ddspf.g_bit_mask) >> g_shift, g_bits) : 0;
                rgba_buf[i * 4 + 2] = b_bits ? dds__scale_bits((pixel & header.ddspf.b_bit_mask) >> b_shift, b_bits) : 0;
                rgba_buf[i * 4 + 3] = a_bits ? dds__scale_bits((pixel & header.ddspf.a_bit_mask) >> a_shift, a_bits) : 255;
            }
            DDS_FREE(raw_buf);
        }
        fclose(f);
        
        /* Output Channel Pipeline */
        void* final_buf = NULL;
        if (channels == 3) 
        {
            final_buf = rgba_buf;
        } else if (channels == 2) 
        {
            uint8_t* rgb_data = (uint8_t*)dds__aligned_malloc((size_t)w * h * 4, 16);
            if (!rgb_data) { dds__aligned_free(rgba_buf); return NULL; }
            
            uint32_t num_pixels = w * h;
            uint32_t i = 0;
            
#ifdef DDS_USE_SSE2
            __m128i alpha_mask = _mm_set1_epi32((int)0xFF000000);
            for (; i + 3 < num_pixels; i += 4) 
            {
                __m128i pixels = _mm_load_si128((const __m128i*)(rgba_buf + i * 4));
                pixels = _mm_or_si128(pixels, alpha_mask);
                _mm_store_si128((__m128i*)(rgb_data + i * 4), pixels);
            }
#endif
            for (; i < num_pixels; i++) 
            {
                rgb_data[i * 4 + 0] = rgba_buf[i * 4 + 0];
                rgb_data[i * 4 + 1] = rgba_buf[i * 4 + 1];
                rgb_data[i * 4 + 2] = rgba_buf[i * 4 + 2];
                rgb_data[i * 4 + 3] = 0xFF;
            }
            dds__aligned_free(rgba_buf);
            final_buf = rgb_data;
        } else if (channels == 1) 
        {
            uint8_t* gray_data = (uint8_t*)dds__aligned_malloc((size_t)w * h * 4, 16);
            if (!gray_data) { dds__aligned_free(rgba_buf); return NULL; }
            
            uint32_t num_pixels = w * h;
            uint32_t i = 0;
            
#ifdef DDS_USE_SSE2
            __m128i weights = _mm_set_epi16(0, 29, 150, 77, 0, 29, 150, 77);
            __m128i zero    = _mm_setzero_si128();
            
            for (; i + 3 < num_pixels; i += 4) 
            {
                __m128i pix = _mm_load_si128((const __m128i*)(rgba_buf + i * 4));
                
                __m128i p01 = _mm_unpacklo_epi8(pix, zero);
                __m128i p23 = _mm_unpackhi_epi8(pix, zero);
                
                __m128i m01 = _mm_madd_epi16(p01, weights);
                __m128i m23 = _mm_madd_epi16(p23, weights);
                
                __m128i sum01 = _mm_add_epi32(m01, _mm_srli_si128(m01, 8));
                __m128i sum23 = _mm_add_epi32(m23, _mm_srli_si128(m23, 8));
                
                uint32_t y0 = (uint32_t)_mm_cvtsi128_si32(sum01) >> 8;
                uint32_t y1 = (uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(sum01, 4)) >> 8;
                uint32_t y2 = (uint32_t)_mm_cvtsi128_si32(sum23) >> 8;
                uint32_t y3 = (uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(sum23, 4)) >> 8;
                
                ((uint32_t*)gray_data)[i + 0] = (y0 * 0x00010101) | 0xFF000000;
                ((uint32_t*)gray_data)[i + 1] = (y1 * 0x00010101) | 0xFF000000;
                ((uint32_t*)gray_data)[i + 2] = (y2 * 0x00010101) | 0xFF000000;
                ((uint32_t*)gray_data)[i + 3] = (y3 * 0x00010101) | 0xFF000000;
            }
#endif
            for (; i < num_pixels; i++) 
            {
                uint8_t lum = (uint8_t)(0.299f * rgba_buf[i * 4 + 0] + 0.587f * rgba_buf[i * 4 + 1] + 0.114f * rgba_buf[i * 4 + 2]);
                gray_data[i * 4 + 0] = lum;
                gray_data[i * 4 + 1] = lum;
                gray_data[i * 4 + 2] = lum;
                gray_data[i * 4 + 3] = 0xFF;
            }
            dds__aligned_free(rgba_buf);
            final_buf = gray_data;
        }
        
        if (out_width) *out_width = (int)w;
        if (out_height) *out_height = (int)h;
        
        return final_buf;
    }
    
#ifdef __cplusplus
}
#endif

#endif /* DDS_LOADER_IMPLEMENTATION */
#endif /* DDS_LOADER_H */