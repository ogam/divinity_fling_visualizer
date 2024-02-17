#pragma once
#pragma warning(disable:4996)
#include <cstdio>
#include <string>
#include <stdarg.h>

#define KILOBYTES(X) (X * 1024)
#define MEGABYTES(X) KILOBYTES(X * 1024)

#define ARRAY_SIZE(ARR) (sizeof(ARR) / sizeof(ARR[0]))
#define ASSERT(EXPRESSION, ...) { if (!(EXPRESSION)) *(int*)0 = 0; }
#define EPSILON 0.0001f
#define STRCMP(A, B) (!(strcmp(A, B)))

#define MIN(A, B) A > B ? B : A
#define MAX(A, B) A < B ? B : A
#define CLAMP(V, A, B) MIN(MAX(V,A),B)
//#define ABS(VALUE) (VALUE < 0 ? -VALUE : VALUE)

#define BEGIN_ENUM(ENUM) enum ENUM {
#define ADD_ENUM(ENUM, KEY) ENUM##_##KEY,
#define END_ENUM() };
#define BEGIN_ENUM_NAMES(ENUM) static const char* s_##ENUM##_names[] = {
#define ADD_ENUM_NAME(ENUM, NAME) #NAME,
#define END_ENUM_NAMES() };
#define STRINGIZE(X) #X

//  @todo:  build.bat and increment on every build
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 0
#define APP_VERSION_BUILD 5

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef char s8;
typedef short s16;
typedef long s32;
typedef long long s64;
typedef float f32;
typedef double f64;
typedef int b32;

typedef void (*ParseBlockFn)(const char *block_start, const char *block_end, void* udata);

//  @todo:  translate OEM keys properly
//          todo: need to define enum values
//                specify values to each one, we're missing some or isn't in proper order
// https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
enum
{
    KeyCode_MouseLeft = 0x1,
    KeyCode_MouseRight = 0x2,
    KeyCode_MouseMiddle = 0x4,
    KeyCode_Mouse4 = 0x5,
    KeyCode_Mouse5 = 0x6,
    KeyCode_Escape = 0x1B,
    KeyCode_Space = 0x20,
    KeyCode_1 = 0x31,
    KeyCode_2 = 0x32,
    KeyCode_I = 0x49,
    KeyCode_F1 = 0x70,
    KeyCode_F2 = 0x71,
    KeyCode_F3 = 0x72,
    KeyCode_F4 = 0x73,
    KeyCode_F5 = 0x74,
    KeyCode_F6 = 0x75,
    KeyCode_F7 = 0x76,
    KeyCode_F8 = 0x77,
    KeyCode_F9 = 0x78,
    KeyCode_F10 = 0x79,
    KeyCode_F11 = 0x7A,
    KeyCode_F12 = 0x7B,
    KeyCode_LShift = 0xA0,
    KeyCode_RShift = 0xA1,
    KeyCode_LCtrl = 0xA2,
    KeyCode_RCtrl = 0xA3,
    KeyCode_LAlt = 0xA4,
    KeyCode_RAlt = 0xA5,
    KeyCode_Plus = 0xBB,
    KeyCode_Minus = 0xBD,
    KeyCode_Count = 0xFF
};

enum
{
    KeyMode_None = 0,
    KeyMod_LShift = 1 << 0,
    KeyMod_RShift = 1 << 1,
    KeyMod_LCtrl = 1 << 2,
    KeyMod_RCtrl = 1 << 3,
    KeyMod_LAlt = 1 << 4,
    KeyMod_RAlt = 1 << 5,
    KeyMod_Shift = KeyMod_LShift | KeyMod_RShift,
    KeyMod_Ctrl = KeyMod_LCtrl | KeyMod_RCtrl,
    KeyMod_Alt = KeyMod_LAlt | KeyMod_RAlt,
};

const char **get_key_code_names();

u64 hash_fnv1a(void *data, u64 size);

enum Game
{
    // Divinity: Original Sin 1
    // probably not needed since camera is free and easy to fling anywhere
    Game_DOS1,
    Game_DOS1_EE,
    // Divinity: Original Sin 2
    // mod tools makes this one a bit redudnant, camera is a bit more restricted but might still be useful for low angle?
    Game_DOS2,
    Game_DOS2_DE,
    // Baldur's Gate 3
    Game_BG3,
    Game_Count
};

enum RebindType
{
    RebindType_None,
    RebindType_Fling,
    RebindType_Scan,
    RebindType_Cancel,
    RebindType_Inventory,
    RebindType_Inventory_Position,
    RebindType_Set_Inventory_Position
};

//  @todo:  `help` window or something
enum
{
    WindowType_None = 0,
    WindowType_Node = 1 << 0,
    WindowType_Map = 1 << 1,
    WindowType_Settings = 1 << 2,
    WindowType_About = 1 << 3,
};

enum
{
    LevelDrawType_None = 0,
    LevelDrawType_Regions = 1,
    LevelDrawType_Objects = 2
};

enum Action
{
    Action_Set_World_Position,
    Action_Scan_Regions,
    Action_Cancel,
    Action_Screenshot,
    Action_Set_Inventory_Position,
    Action_Get_Clipboard_Image,
    Action_Count,
};

enum AutoMapAction
{
    AutoMapAction_Set_1,
    AutoMapAction_Set_2,
    AutoMapAction_Set_Count,
    AutoMapAction_Save,
    AutoMapAction_New,
    AutoMapAction_Count,
};

enum WorldFilter
{
    WorldFilter_None,
    WorldFilter_In,
    WorldFilter_Out,
    WorldFilter_Selected,
};

typedef struct GlobalButton
{
    b32 previous_state : 1;
    b32 state : 1;
    b32 key : 16;
    b32 mod : 12;
} GlobalButton;

typedef struct String
{
    u8 length;
    char str[255];
} String;

typedef struct V2i
{
    s16 x;
    s16 y;
} V2i;

typedef struct Color32
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} Color32;

struct ClipboardImage
{
    u8 *image;
    u64 image_size;
    u32 bit_count;
    s32 width;
    s32 height;
};

typedef struct V3f
{
    f32 x;
    f32 y;
    f32 z;
} V3f;

typedef struct Aabbi
{
    V2i min;
    V2i max;
} Aabbi;

typedef struct Aabbf
{
    V3f min;
    V3f max;
} Aabbf;

struct StringCollection
{
    const char **strings;
    s32 count;
};

struct V2iCollection
{
    V2i *points;
    s32 count;
    s32 capacity;
};

struct V3fCollection
{
    V3f *points;
    s32 count;
    s32 capacity;
};

struct AddressCollection
{
    u64 *addresses;
    s32 count;
};

struct AddressVersionInfo
{
    Game game;
    b32 is_gog;
    const char *version;
    u64 node_addresses[32];
    u64 level_addresses[32];
    s32 node_count;
    s32 level_count;
    s32 additional_info;
};

struct AddressVersionInfoCollection
{
    AddressVersionInfo *items;
    s32 count;
    s32 capacity;
};

struct Signature
{
    u8 signature[32];
    s32 byte_count;
    s32 search_start;
    s32 search_length;
};

struct SignatureInfo
{
    Game game;
    b32 is_gog;
    
    Signature node_signature;
    Signature level_signature;
    
    // minimum supported version
    u32 version_major;
    u32 version_minor;
    u32 version_build;
    u32 version_private;
    
    // expects first index to be overwritten
    u64 node_address_offsets[32];
    u64 level_address_offsets[32];
    
    s32 node_address_offset_count;
    s32 level_address_offset_count;
    s32 additional_info;
};

struct SignatureInfoCollection
{
    SignatureInfo *items;
    s32 count;
    s32 capacity;
};

// workaround for raylib texture2d
typedef struct AppTexture 
{
    unsigned int id;
    int width;
    int height;
    int mipmaps;
    int format;
} AppTexture;

// work around for raylib render texture 2d
typedef struct AppRenderTexture
{
    unsigned int id;
    AppTexture texture;
    AppTexture depth;
} AppRenderTexture;

struct PointOfInterest
{
    String name;
    String image_file;
    
    AppTexture texture;
    
    Aabbf region;
    b32 is_selected;
    b32 draw;
};

struct PointOfInterestCollection
{
    PointOfInterest *items;
    s32 count;
    s32 capacity;
};

#define POI_CAPACITY 1024

struct Level
{
    PointOfInterest *regions;
    PointOfInterest *objects;
    s32 regions_count;
    s32 objects_count;
    Aabbf bounds;
};

enum
{
    PointOfInterestUIResult_None,
    PointOfInterestUIResult_Export,
    PointOfInterestUIResult_Auto_Map,
};

enum
{
    AutoMapWindow_None,
    AutoMapWindow_Region,
    AutoMapWindow_Object,
};

// fatty hashmap
struct Hashmap
{
    u64 **keys;
    void **values;
    u64 *hashed_indices;
    
    u64 element_size;
    s32 capacity;
    s32 count;
};

struct mco_coro;

struct Arena
{
    u8 *memory;
    u8 *memory_end;
    u8 *current;
};

struct VisualizerCtx
{
    //  @todo: serialize this to text on save? currently it's binary blob
    union
    {
        struct 
        {
            GlobalButton buttons[Action_Count];
            u32 game_inventory_key_code;
            b32 mouse_wheel_invert;
            Color32 background_color;
            u32 circle_color;
            u32 selected_circle_color;
            s16 scan_step_rate;
            s32 wiggle_amount;
        };
        
        struct
        {
            GlobalButton buttons[Action_Count];
            u32 game_inventory_key_code;
            b32 mouse_wheel_invert;
            Color32 background_color;
            u32 circle_color;
            u32 selected_circle_color;
            s16 scan_step_rate;
            s32 wiggle_amount;
        } config;
    };
    b32 stop_on_first_filtered;
    
    V2i raw_mouse_position;
    V2i inventory_screen_position;
    RebindType previous_rebind_type;
    RebindType rebind_type;
    f64 rebind_wait_time;
    
    s32 memory_addresses_dirty_counter;
    u64 position_address;
    u64 level_name_address;
    
    // hooked game info
    V2i game_screen_position;
    Aabbi game_screen_bounds;
    V3f position;
    String level_name;
    
    Game game_type;
    mco_coro *action_co;
    Action next_action;
    Action ui_next_action;
    b32 global_hotkeys_enabled;
    
    // delay between each process search
    f64 next_process_search_time;
    
    // screen regions to scan
    Aabbi regions[8];
    s32 region_count;
    s32 ui_region_selection;
    b32 is_redrawing_region;
    b32 show_region_outline;
    
    V3fCollection recorded_world_positions;
    V2iCollection recorded_screen_positions;
    s32 selected_record_index;
    
    Aabbf filter_world_region;
    s32 filter_world_input_box;
    WorldFilter filter_type;
    
    // ui
    u8 *image;
    b32 image_queued;
    AppTexture texture;
    AppRenderTexture map_texture;
    V2i focus_point;
    s32 zoom_level;
    b32 window_type;
    b32 level_details_draw_type;
    V3f world_sample_point;
    b32 use_world_bounds;
    b32 map_draw_scan_samples;
    V3f map_mouse_position;
    Level *previous_tab_level;
    Level *current_tab_level;
    Aabbf *current_drawing_aabbf;
    
    b32 auto_map_window_state;
    V3f auto_map_sample_points[AutoMapAction_Set_Count];
    V2i auto_map_image_sample_points[AutoMapAction_Set_Count];
    GlobalButton auto_map_buttons[AutoMapAction_Count];
    String clipboard_image_name;
    PointOfInterest *current_auto_map_poi;
    b32 auto_map_image_state;
    V2i auto_map_image_position;
    
    // tool representation of the world in respects to level
    Hashmap *world;
    Aabbf max_world_region;
    
    Arena arena;
    
    u8 *memory;
    u8 *memory_end;
    
    AddressVersionInfoCollection address_version_collection;
    SignatureInfoCollection signature_collection;
};

// very minimal amount of data structures, utility functions and minimal amount of testing
struct StringTable
{
    String *table;
    u64 *hashes;
    s32 count = 0;
    s32 capacity;
};

static StringTable interned_strings;

static inline const char *string_table_intern(const char *string)
{
    u64 length = strlen(string);
    ASSERT(length < sizeof(String::str));
    u64 hash = hash_fnv1a((void*)string, length);
    
    if (!string)
    {
        return nullptr;
    }
    
    if (interned_strings.capacity == 0)
    {
        interned_strings.capacity = 4096;
        u64 allocation_size = (sizeof(String) + sizeof(u64)) * interned_strings.capacity;
        u8 *memory = (u8*)malloc(allocation_size);
        interned_strings.table = (String*)memory;
        interned_strings.hashes = (u64*)(memory + sizeof(String) * interned_strings.capacity);
        memset(memory, 0, allocation_size);
    }
    
    s32 table_index = interned_strings.count;
    
    for (s32 index = 0; index < interned_strings.count; ++index)
    {
        if (interned_strings.hashes[index] == hash)
        {
            table_index = index;
            break;
        }
    }
    
    if (table_index == interned_strings.count)
    {
        interned_strings.count++;
    }
    
    if (interned_strings.table[table_index].str && interned_strings.table[table_index].length)
    {
        if (!STRCMP(interned_strings.table[table_index].str, string))
        {
            printf("String table updating - %s -> %s\n", interned_strings.table[table_index].str, string);
        }
    }
    
    interned_strings.table[table_index].length = snprintf(interned_strings.table[table_index].str, sizeof(String::str), string);
    interned_strings.hashes[table_index] = hash;
    
    return interned_strings.table[table_index].str;
}

static inline Arena arena_make(void *memory, u64 size)
{
    ASSERT(memory);
    Arena arena = {};
    arena.memory = (u8*)memory;
    arena.memory_end = arena.memory + size;
    arena.current = arena.memory;
    
    return arena;
}

static inline void arena_clear(Arena *arena)
{
    arena->current = arena->memory;
}

static inline void* arena_alloc(Arena *arena, u64 size)
{
    ASSERT(arena->current + size < arena->memory_end);
    void *allocation = (void*)arena->current;
    arena->current += size;
    return allocation;
}

static inline Hashmap* hashmap_make(Arena *arena, u64 element_size, s32 capacity)
{
    ASSERT(arena);
    Hashmap *map = (Hashmap*)arena_alloc(arena, sizeof(Hashmap));
    map->keys = (u64**)arena_alloc(arena, sizeof(u64*) * capacity);
    map->values = (void**)arena_alloc(arena, sizeof(void*) * capacity);
    map->hashed_indices = (u64*)arena_alloc(arena, sizeof(u64) * capacity);
    u8 *data = (u8*)arena_alloc(arena, element_size * capacity);
    
    memset(map->hashed_indices, 0, sizeof(u64) * capacity);
    
    for (s32 index = 0; index < capacity; ++index)
    {
        map->values[index] = data + element_size * index;
    }
    map->count = 0;
    map->capacity = capacity;
    map->element_size = element_size;
    return map;
}

static inline void hashmap_clear(Hashmap *map)
{
    ASSERT(map);
    memset(map->hashed_indices, 0, sizeof(u64) * map->capacity);
    map->count = 0;
}

static inline void* hashmap_set(Hashmap *map, const char *key, void *data)
{
    ASSERT(map);
    u64 key_length = strlen(key);
    u64 hash = hash_fnv1a((void*)key, key_length);
    s32 index = map->count;
    
    for (s32 i = 0; i < map->count; ++i)
    {
        if (map->hashed_indices[i] == hash)
        {
            index = i;
            break;
        }
    }
    
    if (map->hashed_indices[index] == 0)
    {
        map->hashed_indices[index] = hash;
        map->count++;
    }
    
    map->keys[index] = (u64*)string_table_intern(key);
    memcpy(map->values[index], data, map->element_size);
    return map->values[index];
}

static inline void* hashmap_get(Hashmap *map, const char *key)
{
    ASSERT(map);
    u64 key_length = strlen(key);
    u64 hash = hash_fnv1a((void*)key, key_length);
    void* value = nullptr;
    
    for (s32 index = 0; index < map->count; ++index)
    {
        if (map->hashed_indices[index] == hash)
        {
            value = map->values[index];
            break;
        }
    }
    
    return value;
}

static inline const char* util_string_skip_spaces(const char *string)
{
    const char *walker = string;
    while (*walker)
    {
        if (*walker == ' ' || *walker == '\t' || *walker == '\n' || *walker == '\r')
        {
            walker++;
        }
        else
        {
            break;
        }
    }
    return walker;
}

static inline const char* util_string_skip_special_characters(const char *string)
{
    const char *walker = string;
    while (*walker)
    {
        if (*walker == ' ' || *walker == '\t' || *walker == '\n' || *walker == '\r' || *walker == '{' || *walker == '}' || *walker == '=' || *walker == ',')
        {
            walker++;
        }
        else
        {
            break;
        }
    }
    return walker;
}


static inline s32 util_string_word_length(const char *string)
{
    const char *walker = string;
    while (*walker)
    {
        if (*walker == ' ' || *walker == '\t' || *walker == '\n' || *walker == '\r')
        {
            break;
        }
        walker++;
    }
    
    return (s32)(walker - string);
}

static inline s32 util_string_line_length(const char *string)
{
    const char *walker = string;
    while (*walker)
    {
        if (*walker == '\n' || *walker == '\r')
        {
            break;
        }
        walker++;
    }
    
    return (s32)(walker - string);
}

static inline b32 util_string_starts_with(const char *search, const char *string)
{
    u64 search_length = strlen(search);
    u64 string_length = strlen(string);
    b32 result = false;
    
    if (search_length <= string_length)
    {
        u64 search_hash = hash_fnv1a((void*)search, (s32)search_length);
        u64 string_hash = hash_fnv1a((void*)string, (s32)string_length);
        result = search_hash == string_hash;
    }
    
    return result;
}

static inline const char* util_string_find(const char *string, char token)
{
    const char *walker = string;
    const char *found = nullptr;
    while (*walker)
    {
        if (*walker == token)
        {
            found = walker;
            break;
        }
        walker++;
    }
    return found;
}

static inline const char* util_string_find_reverse(const char *string, char token)
{
    u64 length = strlen(string);
    const char *walker = string + length - 1;
    const char *found = nullptr;
    while (walker > string)
    {
        if (*walker == token)
        {
            found = walker;
            break;
        }
        walker--;
    }
    return found;
}

static inline void string_pop(String *str)
{
    if (str->length)
    {
        str->length--;
        str->str[str->length] = 0;
    }
}

static inline s32 string_printf(String *str, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    str->length = vsnprintf(str->str, sizeof(str->str), fmt, args);
    va_end(args);
    return str->length;
}

static inline String string_from_address_offsets(u64 *offsets, s32 count)
{
    String str;
    memset(&str, 0, sizeof(String));
    for (s32 index = 0; index < count; ++index)
    {
        string_printf(&str, "%s 0x%X,", str.str, offsets[index]);
    }
    string_pop(&str);
    return str;
}

static inline f32 clamp(f32 value, f32 min, f32 max)
{
    value = MIN(value, max);
    value = MAX(value, min);
    return value;
}

static inline V2i v2i_add(V2i a, V2i b)
{
    V2i v = {a.x + b.x, a.y + b.y};
    return v;
}

static inline V2i v2i_sub(V2i a, V2i b)
{
    V2i v = {a.x - b.x, a.y - b.y};
    return v;
}

static inline V2i v2i_min(V2i a, V2i b)
{
    V2i v = a;
    v.x = MIN(a.x, b.x);
    v.y = MIN(a.y, b.y);
    return v;
}

static inline V2i v2i_max(V2i a, V2i b)
{
    V2i v = a;
    v.x = MAX(a.x, b.x);
    v.y = MAX(a.y, b.y);
    return v;
}


static inline V3f v3f_add(V3f a, V3f b)
{
    V3f v = {a.x + b.x, a.y + b.y, a.z + b.z};
    return v;
}


static inline V3f v3f_sub(V3f a, V3f b)
{
    V3f v = {a.x - b.x, a.y - b.y, a.z - b.z};
    return v;
}

static inline f32 v3f_dot(V3f a, V3f b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline f32 v3f_len_sq(V3f a)
{
    return a.x * a.x + a.y * a.y + a.z * a.z;
}

static inline V3f v3f_min(V3f a, V3f b)
{
    V3f v = a;
    v.x = MIN(a.x, b.x);
    v.y = MIN(a.y, b.y);
    v.z = MIN(a.z, b.z);
    return v;
}

static inline V3f v3f_max(V3f a, V3f b)
{
    V3f v = a;
    v.x = MAX(a.x, b.x);
    v.y = MAX(a.y, b.y);
    v.z = MAX(a.z, b.z);
    return v;
}

static inline V3f v3f_mul(V3f v, f32 s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
    return v;
}

static inline Aabbi aabbi_make(V2i min, V2i max)
{
    Aabbi aabb;
    aabb.min = v2i_min(min, max);
    aabb.max = v2i_max(min, max);
    return aabb;
}

static inline V2i aabbi_size(Aabbi aabb)
{
    V2i size = v2i_sub(aabb.max, aabb.min);
    return size;
}

static inline s32 aabbi_area(Aabbi aabb)
{
    V2i size = v2i_sub(aabb.max, aabb.min);
    return size.x * size.y;
}

static inline Aabbf aabbf_make(V3f min, V3f max)
{
    Aabbf aabb = {};
    aabb.min = v3f_min(min, max);
    aabb.max = v3f_max(min, max);
    return aabb;
}

static inline Aabbf aabbf_make_center_radius(V3f center, f32 radius)
{
    Aabbf aabb = {};
    aabb.min = center;
    aabb.max = center;
    aabb.min.x -= radius;
    aabb.min.y -= radius;
    aabb.min.z -= radius;
    aabb.max.x -= radius;
    aabb.max.y -= radius;
    aabb.max.z -= radius;
    
    return aabb;
}

static inline Aabbf aabbf_expand(Aabbf aabb, f32 radius)
{
    aabb.min.x -= radius;
    aabb.min.y -= radius;
    aabb.min.z -= radius;
    aabb.max.x -= radius;
    aabb.max.y -= radius;
    aabb.max.z -= radius;
    
    return aabb;
}

static inline V3f aabbf_size(Aabbf aabb)
{
    V3f size = v3f_sub(aabb.max, aabb.min);
    return size;
}

static inline f32 aabbf_volume(Aabbf aabb)
{
    V3f size = aabbf_size(aabb);
    return size.x * size.y * size.z;
}

static inline b32 aabbf_contains(Aabbf aabb, V3f v)
{
    return v.x >= aabb.min.x && v.y >= aabb.min.y && v.z >= aabb.min.z &&
        v.x <= aabb.max.x && v.y <= aabb.max.y && v.z <= aabb.max.z;
}

static inline Aabbf aabbf_combine(Aabbf a, Aabbf b)
{
    Aabbf aabb = a;
    aabb.min = v3f_min(a.min, b.min);
    aabb.max = v3f_max(a.max, b.max);
    return aabb;
}

static inline b32 aabbf_filtered(Aabbf aabb, V3f v, WorldFilter filter, b32 is_selected)
{
    b32 filtered = filter == WorldFilter_None;
    if (filter == WorldFilter_In && aabbf_contains(aabb, v))
    {
        filtered = true;
    }
    else if (filter == WorldFilter_Out && !aabbf_contains(aabb, v))
    {
        filtered = true;
    }
    else if (filter == WorldFilter_Selected && is_selected)
    {
        filtered = true;
    }
    return filtered;
}

static inline b32 f32_is_zero(f32 value)
{
    return value < EPSILON && value > -EPSILON;
}

static inline void v2i_collection_clear(V2iCollection *collection)
{
    collection->count = 0;
}

static inline void v2i_collection_push(V2iCollection *collection, V2i point)
{
    if (collection->count < collection->capacity)
    {
        collection->points[collection->count++] = point;
    }
}

static inline void v3f_collection_clear(V3fCollection *collection)
{
    collection->count = 0;
}

static inline void v3f_collection_push(V3fCollection *collection, V3f point)
{
    if (collection->count < collection->capacity)
    {
        collection->points[collection->count++] = point;
    }
}

static inline b32 global_button_is_down(GlobalButton *button)
{
    return button->state;
}

static inline b32 global_button_is_pressed(GlobalButton *button)
{
    return button->state && !button->previous_state;
}

static inline b32 global_button_is_released(GlobalButton *button)
{
    return !button->state && button->previous_state;
}

static inline b32 global_button_is_up(GlobalButton *button)
{
    return !button->state;
}

static inline void* read_entire_file(const char *path, u64 *out_size)
{
    void *buffer = nullptr;
    if (FILE *file_handle = fopen(path, "rb"))
    {
        fseek(file_handle, 0, SEEK_END);
        u64 buffer_size = ftell(file_handle);
        rewind(file_handle);
        buffer = malloc(buffer_size);
        fread(buffer, 1, buffer_size, file_handle);
        fclose(file_handle);
        
        if (out_size)
        {
            *out_size = buffer_size;
        }
    }
    
    return buffer;
}

static inline u64 hash_fnv1a(void *data, u64 size)
{
    u8 *data_buf = (u8*)data;
    u64 result = 0xcbf29ce484222325;
    u8 *value = (u8*)&result;
    while (size--) {
        *value = *value ^ *data_buf;
        result = result * 0x100000001b3;
        data_buf++;
    }
    return result;
}

static inline s32 parse_file_get_block_count(const char *buffer, u64 length)
{
    s32 open_block_count = 0;
    s32 close_block_count = 0;
    const char *buffer_end = buffer + length;
    const char *buffer_walker = buffer;
    s32 line_number = length ? 1 : 0;
    
    while (buffer_walker < buffer_end)
    {
        if (*buffer_walker == '{')
        {
            open_block_count++;
        }
        else if (*buffer_walker == '}')
        {
            close_block_count++;
        }
        if (*buffer_walker == '\n')
        {
            line_number++;
        }
        
        if (abs(open_block_count - close_block_count) > 1)
        {
            printf("[ERROR] Extra close or open brace\n");
            printf("line - %d\t\tbyte - %llu\n", line_number, buffer_walker - buffer);
            break;
        }
        buffer_walker++;
    }
    
    s32 count = 0;
    if (open_block_count == close_block_count)
    {
        count = open_block_count;
    }
    
    return count;
}

// probably the first and last time i use a callback for configs, doesn't seem worth it
static inline void parse_file(const char *buffer, u64 buffer_length, ParseBlockFn handle_block_fn, void* udata)
{
    s32 open_block_count = 0;
    s32 close_block_count = 0;
    const char *buffer_end = buffer + buffer_length;
    const char *buffer_walker = buffer;
    const char *block_start = nullptr;
    
    while (buffer_walker < buffer_end)
    {
        if (*buffer_walker == '{')
        {
            block_start = buffer_walker + 1;
            open_block_count++;
        }
        else if (*buffer_walker == '}')
        {
            handle_block_fn(block_start, buffer_walker, udata);
            close_block_count++;
        }
        
        buffer_walker++;
    }
}

//  @todo:  address offsets
//  @todo:  signatures
static inline V3f parse_v3f(const char *start, const char *end)
{
    V3f v = {};
    const char *line_walker = start;
    s32 element_index = 0;
    String value;
    s32 value_name_length;
    
    while (line_walker < end && element_index < 3)
    {
        value_name_length = util_string_word_length(line_walker);
        memcpy(&value.str, line_walker, value_name_length);
        value.length = (u8)value_name_length;
        value.str[value.length] = '\0';
        
        if (value.str[value.length - 1] == ',')
        {
            value.str[--value.length] = '\0';
        }
        
        ((f32*)&v)[element_index++] = strtof(value.str, nullptr);
        line_walker += value_name_length;
        line_walker = util_string_skip_special_characters(line_walker);
    }
    return v;
}

static inline s32 parse_s32(const char *start, const char *end)
{
    String value;
    value.length = (u8)(MIN((u64)(end - start), sizeof(value.str)));
    memcpy(&value.str, start, value.length);
    value.str[value.length] = '\0';
    
    if (value.str[value.length - 1] == ',')
    {
        value.str[--value.length] = '\0';
    }
    
    return strtol(value.str, nullptr, 10);
}

static inline u32 parse_u32(const char *start, const char *end)
{
    String value;
    value.length = (u8)(MIN((u64)(end - start), sizeof(value.str)));
    memcpy(&value.str, start, value.length);
    value.str[value.length] = '\0';
    
    if (value.str[value.length - 1] == ',')
    {
        value.str[--value.length] = '\0';
    }
    
    return strtoul(value.str, nullptr, 10);
}

static inline void handle_point_of_interest_block(const char *block_start, const char *block_end, void* udata)
{
    ASSERT(block_start && block_end);
    ASSERT(udata);
    PointOfInterestCollection *collection = (PointOfInterestCollection*)udata;
    PointOfInterest *current = nullptr;
    
    if (collection->count >= collection->capacity)
    {
        return;
    }
    
    current = &collection->items[collection->count];
    *current = {};
    
    const char *walker = block_start;
    String type = {};
    String value = {};
    
    b32 is_using_radius = false;
    f32 radius = 0;
    
    while (walker < block_end)
    {
        const char *line_walker = util_string_skip_special_characters(walker);
        
        s32 line_length = util_string_line_length(walker);
        walker += line_length;
        walker = util_string_skip_special_characters(walker);
        
        s32 type_name_length = util_string_word_length(line_walker);
        
        memcpy(&type.str, line_walker, type_name_length);
        type.length = (u8)type_name_length;
        type.str[type.length] = '\0';
        
        line_walker += type_name_length;
        line_walker = util_string_skip_special_characters(line_walker);
        
        s32 value_name_length = 0;
        const char *interned_type = string_table_intern(type.str);
        
        if (interned_type == string_table_intern("image"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            memcpy(&current->image_file, &value, sizeof(String));
        }
        else if (interned_type == string_table_intern("name"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            memcpy(&current->name, &value, sizeof(String));
        }
        else if (interned_type == string_table_intern("location"))
        {
            V3f location = parse_v3f(line_walker, walker);
            current->region = aabbf_make(location, location);
        }
        else if (interned_type == string_table_intern("radius"))
        {
            f32 radius = 0.0f;
            value_name_length = util_string_word_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            radius = strtof(value.str, nullptr);
            
            radius = radius;
            is_using_radius = true;
        }
        else if (interned_type == string_table_intern("min"))
        {
            V3f min = parse_v3f(line_walker, walker);
            current->region.min = min;
        }
        else if (interned_type == string_table_intern("max"))
        {
            V3f max = parse_v3f(line_walker, walker);
            current->region.max = max;
        }
    }
    
    if (is_using_radius)
    {
        current->region = aabbf_expand(current->region, radius);
    }
    else
    {
        current->region = aabbf_make(current->region.min, current->region.max);
    }
    
    if (current->name.length)
    {
        collection->count++;
    }
}

static inline void dump_point_of_interests_to_file(const char *path, PointOfInterest *interests, s32 count)
{
    ASSERT(interests);
    ASSERT(path);
    if (FILE *file_handle = fopen(path, "wb"))
    {
        String buffer;
        for (s32 index = 0; index < count; ++index)
        {
            PointOfInterest *interest = interests + index;
            
            fwrite("{\n", 1, 2, file_handle);
            
            string_printf(&buffer, "\timage = %s\n", interest->image_file.str);
            fwrite(buffer.str, 1, buffer.length, file_handle);
            
            string_printf(&buffer, "\tname = %s\n", interest->name.str);
            fwrite(buffer.str, 1, buffer.length, file_handle);
            
            string_printf(&buffer, "\tmin = %.2f, %.2f, %.2f\n", interest->region.min.x, interest->region.min.y, interest->region.min.z);
            fwrite(buffer.str, 1, buffer.length, file_handle);
            
            string_printf(&buffer, "\tmax = %.2f, %.2f, %.2f\n", interest->region.max.x, interest->region.max.y, interest->region.max.z);
            fwrite(buffer.str, 1, buffer.length, file_handle);
            
            fwrite("}\n", 1, 2, file_handle);
        }
        
        fclose(file_handle);
    }
}