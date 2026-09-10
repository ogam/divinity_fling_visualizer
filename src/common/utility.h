#ifndef UTILITY_H
#define UTILITY_H

#define CAT2(A, B) A##B
// used for variables in macros to avoid clashing with variables in same or outer scope
#define UNIQUE_NAME(NAME) CAT2(_##__LINE__, NAME)

#define arena_array_fit(ARENA, ARR, COUNT) \
{ \
s32 UNIQUE_NAME(size) = sizeof(*ARR) * COUNT + sizeof(CK_ArrayHeader); \
void* UNIQUE_NAME(memory) = cf_arena_alloc(ARENA, UNIQUE_NAME(size)); \
typeof(ARR) UNIQUE_NAME(old_arr) = ARR; \
cf_array_static(ARR, UNIQUE_NAME(memory), UNIQUE_NAME(size)); \
if (UNIQUE_NAME(old_arr)) \
{ \
cf_array_set(ARR, UNIQUE_NAME(old_arr)); \
} \
}
#define arena_push_struct(ARENA, DATA) _arena_push(ARENA, &DATA, sizeof(DATA))

// stable array remove
#define array_remove(ARR, INDEX) \
{ \
if ((INDEX) >= 0 && (INDEX) < cf_array_count(ARR)) \
{ \
void* DST = (ARR) + (INDEX); \
void* SRC = (ARR) + (INDEX) + 1; \
size_t COPY_SIZE = sizeof(*(ARR)) * (cf_array_count(ARR) - (INDEX) - 1); \
CF_MEMCPY(DST, SRC, COPY_SIZE); \
cf_array_pop(ARR); \
} \
}

#define MEMZERO(X) CF_MEMSET(X, 0, sizeof(*X))

#define SWAP(A, B) \
{ \
typeof(A) __temp = (A); \
(A) = (B); \
(B) = (__temp); \
}

static str8 make_arena_string(CF_Arena* arena, s32 length)
{
    s32 size = sizeof(CK_ArrayHeader) + length;
    void* memory = cf_arena_alloc(arena, size);
    
    str8 s = NULL;
    cf_string_static(s, memory, size);
    
    return s;
}

static str8 arena_vfmt(CF_Arena* arena, const char* fmt, va_list args)
{
    s32 length = vsnprintf(NULL, 0, fmt, args);
    s32 size = sizeof(CK_ArrayHeader) + length + 1;
    void* memory = cf_arena_alloc(arena, size);
    
    str8 s = NULL;
    cf_string_static(s, memory, size);
    
    cf_string_vfmt(s, fmt, args);
    return s;
}

static str8 arena_fmt(CF_Arena* arena, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    str8 s = arena_vfmt(arena, fmt, args);
    
    va_end(args);
    
    return s;
}

static void* _arena_push(CF_Arena* arena, void* data, s32 size)
{
    void* dst = cf_arena_alloc(arena, size);
    if (dst)
    {
        CF_MEMCPY(dst, data, size);
    }
    return dst;
}

static inline s32 align(s32 value, s32 alignment) 
{
    return (value + (alignment - 1)) & -alignment;
}

static inline CF_ARRAY(u8) array_int_to_u8(CF_ARRAY(s32) arr)
{
    CF_ARRAY(u8) u8_arr = NULL;
    arena_array_fit(&g_arena, u8_arr, cf_array_count(arr));
    
    for (s32 index = 0; index < cf_array_count(arr); ++index)
    {
        cf_array_push(u8_arr, (u8)arr[index]);
    }
    
    return u8_arr;
}

s32 sort_compare_int(const void* a, const void* b);
s32 sort_compare_canvas(const void* a, const void* b);

static str8 https_request(const char* host_name, s32 port, const char* uri)
{
    CF_HttpsRequest request = cf_https_get(host_name, port, uri, true);
    while (true)
    {
        CF_HttpsResult request_state = cf_https_process(request);
        if (request_state < 0)
        {
            printf("Failed to retrieve game_signatures.txt\n");
            break;
        }
        if (request_state == CF_HTTPS_RESULT_OK)
        {
            break;
        }
    }
    
    CF_HttpsResponse response = cf_https_response(request);
    const char* content = cf_https_response_content(response);
    s32 length = cf_https_response_content_length(response);
    
    str8 result = NULL;
    if (length)
    {
        result = cf_string_dup(content);
    }
    
    cf_https_destroy(request);
    
    return result;
}

static CF_ARRAY(str8) v3_to_string_list(CF_V3 v)
{
    CF_ARRAY(str8) list = NULL;
    arena_array_fit(&g_arena, list, 3);
    
    cf_array_push(list, arena_fmt(&g_arena, "%.2f", v.x));
    cf_array_push(list, arena_fmt(&g_arena, "%.2f", v.y));
    cf_array_push(list, arena_fmt(&g_arena, "%.2f", v.z));
    
    return list;
}

str8 color_to_str8(CF_Color c);

static b32 move_file(const char* src, const char* dst)
{
    b32 has_file_moved = false;
    CF_Stat file_info = { 0 };
    CF_Result result = cf_fs_stat(src, &file_info);
    
    if (result.code == CF_RESULT_SUCCESS && file_info.size > 0)
    {
        size_t size = 0;
        void* data = cf_fs_read_entire_file_to_memory(src, &size);
        
        cf_fs_write_entire_buffer_to_file(dst, data, size);
        
        cf_free(data);
        cf_fs_remove(src);
        
        has_file_moved = true;
    }
    
    return has_file_moved;
}

static b32 array_str_contains(CF_ARRAY(const char*) list, const char* item)
{
    b32 has_item = false;
    
    for (s32 index = 0; index < cf_array_count(list); ++index)
    {
        if (list[index] == item)
        {
            has_item = true;
            break;
        }
    }
    
    return has_item;
}

static b32 coroutine_is_alive(CF_Coroutine co)
{
    if (co.id && cf_coroutine_state(co) != CF_COROUTINE_STATE_DEAD)
    {
        return true;
    }
    return false;
}

static void destroy_coroutine(CF_Coroutine* co)
{
    if (coroutine_is_alive(*co))
    {
        cf_destroy_coroutine(*co);
    }
    *co = (CF_Coroutine){ 0 };
}

static CF_Color get_pulsing_color(CF_Color color)
{
    color.a = cf_remap(cf_sin_f((f32)CF_SECONDS * 2.0f), -1.0f, 1.0f, 0.0f, 1.0f);
    return color;
}

static CF_Color get_pulsing_color2(CF_Color a, CF_Color b)
{
    f32 t = cf_remap(cf_sin_f((f32)CF_SECONDS * 2.0f), -1.0f, 1.0f, 0.0f, 1.0f);
    return cf_color_lerp(a, b, t);
}

#endif //UTILITY_H
