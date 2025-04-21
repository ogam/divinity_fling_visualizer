//  @todo:  1 worker thread for loading assets, can reuse worker thread to do what the current coroutine is doing

#define MINICORO_IMPL
#include "minicoro.h"

#include "common.h"
#include "rl.h"
#include "platform.h"
#include <string>

#define INPUT_DELAY (1.0 / 30.0)
#define YIELD_WAIT(CO, DELAY) { f64 delay = get_time() +  DELAY; while (delay > get_time()) mco_yield(CO); } 

const char *s_process_names[] = {
    "EoCApp.exe",
    "bg3.exe",
    "bg3_dx11.exe",
};

VisualizerCtx ctx;

void handle_address_block(const char *block_start, const char *block_end, void* udata);
void handle_signature_block(const char *block_start, const char *block_end, void* udata);

void init();

b32 process_handle_is_valid();
b32 process_try_to_attach();

void global_button_update(GlobalButton *button);

Game get_game_type(const char *window_title);
StringCollection get_game_process_names(Game game);
const char* get_game_name(Game game, const char *version);
AddressCollection get_game_node_position_address_offsets(Game game, const char *version);
AddressCollection get_game_level_address_offsets(Game game, const char *version);

void co_bg3_do_set_world_position(mco_coro *co);
void setup_do_set_world_position(Game game, V2i screen_position, mco_coro **co);

void co_utility_open_inventory(mco_coro *co, u8 inventory_vk, u8 inventory_sc);
void co_utility_close_inventory(mco_coro *co, u8 inventory_vk, u8 inventory_sc);
void co_utility_begin_item_drag(mco_coro *co, s16 x, s16 y);
void co_utility_do_wiggle(mco_coro *co, s32 wiggle_amount, s32 wiggle_distance);

void co_do_scan_regions(mco_coro *co);
void setup_do_scan_regions(Game game, mco_coro **co);

void co_do_screenshot(mco_coro *co);
void setup_do_screenshot(mco_coro **co);

b32 scan_memory_addresses();

void save_settings();
void load_settings();
void game_actions();
void ui_actions();
void update_inputs();
void update();

void handle_address_block(const char *block_start, const char *block_end, void* udata)
{
    ASSERT(block_start && block_end);
    ASSERT(udata);
    AddressVersionInfoCollection *collection = (AddressVersionInfoCollection*)udata;
    AddressVersionInfo *current = nullptr;
    
    if (collection->count >= collection->capacity)
    {
        return;
    }
    
    current = &collection->items[collection->count];
    *current = {};
    
    const char *walker = block_start;
    String type = {};
    String value = {};
    
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
        
        if (interned_type == string_table_intern("game"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            const char *game_name = string_table_intern(value.str);
            string_printf(&current->game_name, game_name);
            for (s32 game_index = 0; game_index < Game_Count; ++game_index)
            {
                if (string_table_intern(s_game_short_names[game_index]) == game_name)
                {
                    current->game = (Game)game_index;
                    break;
                }
            }
        }
        else if (interned_type == string_table_intern("platform"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            current->is_gog = string_table_intern("gog") == string_table_intern(value.str);
        }
        else if (interned_type == string_table_intern("version"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            current->version = string_table_intern(value.str);
        }
        else if (interned_type == string_table_intern("graphics"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            current->additional_info = string_table_intern("dx11") == string_table_intern(value.str);
        }
        else if (interned_type == string_table_intern("node_offsets"))
        {
            u64 *address = current->node_addresses;
            
            while (line_walker < walker && current->node_count < ARRAY_SIZE(current->node_addresses))
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                *address = strtoll(value.str, nullptr, 16);
                current->node_count++;
                address++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
        else if (interned_type == string_table_intern("level_offsets"))
        {
            u64 *address = current->level_addresses;
            
            while (line_walker < walker && current->level_count < ARRAY_SIZE(current->level_addresses))
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                *address = strtoll(value.str, nullptr, 16);
                current->level_count++;
                address++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
    }
    
    if (current->node_count && current->level_count && current->version)
    {
        collection->count++;
    }
}

void handle_signature_block(const char *block_start, const char *block_end, void* udata)
{
    ASSERT(block_start && block_end);
    ASSERT(udata);
    SignatureInfoCollection *collection = (SignatureInfoCollection*)udata;
    SignatureInfo *current = nullptr;
    
    if (collection->count >= collection->capacity)
    {
        return;
    }
    
    current = &collection->items[collection->count];
    *current = {};
    
    const char *walker = block_start;
    String type = {};
    String value = {};
    
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
        
        if (interned_type == string_table_intern("game"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            const char *game_name = string_table_intern(value.str);
            string_printf(&current->game_name, game_name);
            
            if (string_table_intern("bg3") == game_name)
            {
                current->game = Game_BG3;
            }
            else if (string_table_intern("dos2") == game_name)
            {
                current->game = Game_DOS2_DE;
            }
            else if (string_table_intern("dos1") == game_name)
            {
                current->game = Game_DOS1_EE;
            }
        }
        else if (interned_type == string_table_intern("platform"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            current->is_gog = string_table_intern("gog") == string_table_intern(value.str);
        }
        else if (interned_type == string_table_intern("graphics"))
        {
            value_name_length = util_string_line_length(line_walker);
            memcpy(&value.str, line_walker, value_name_length);
            value.length = (u8)value_name_length;
            value.str[value.length] = '\0';
            
            current->additional_info = string_table_intern("dx11") == string_table_intern(value.str);
        }
        else if (interned_type == string_table_intern("node_signature"))
        {
            u8 *signature = current->node_signature.signature;
            s32 *signature_byte_count = &current->node_signature.byte_count;
            s32 capacity = (s32)ARRAY_SIZE(current->node_signature.signature);
            
            while (line_walker < walker && *signature_byte_count < capacity)
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                s64 signature_value = strtoll(value.str, nullptr, 16);
                if (signature_value < 0 || signature_value > 0xFF)
                {
                    *signature_byte_count = 0;
                    break;
                }
                
                *signature = (u8)signature_value;
                (*signature_byte_count)++;
                signature++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
        else if (interned_type == string_table_intern("level_signature"))
        {
            u8 *signature = current->level_signature.signature;
            s32 *signature_byte_count = &current->level_signature.byte_count;
            s32 capacity = (s32)ARRAY_SIZE(current->level_signature.signature);
            
            while (line_walker < walker && *signature_byte_count < capacity)
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                s64 signature_value = strtoll(value.str, nullptr, 16);
                if (signature_value < 0 || signature_value > 0xFF)
                {
                    *signature_byte_count = 0;
                    break;
                }
                
                *signature = (u8)signature_value;
                (*signature_byte_count)++;
                signature++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
        else if (interned_type == string_table_intern("node_search_start"))
        {
            current->node_signature.search_start = parse_s32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("node_search_length"))
        {
            current->node_signature.search_length = parse_s32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("level_search_start"))
        {
            current->level_signature.search_start = parse_s32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("level_search_length"))
        {
            current->level_signature.search_length = parse_s32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("node_address_offsets"))
        {
            u64 *address = current->node_address_offsets;
            s32 *address_offset_count = &current->node_address_offset_count;
            s32 capacity = (s32)ARRAY_SIZE(current->node_address_offsets);
            
            while (line_walker < walker && *address_offset_count < capacity)
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                *address = strtoll(value.str, nullptr, 16);
                (*address_offset_count)++;
                address++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
        else if (interned_type == string_table_intern("level_address_offsets"))
        {
            u64 *address = current->level_address_offsets;
            s32 *address_offset_count = &current->level_address_offset_count;
            s32 capacity = (s32)ARRAY_SIZE(current->level_address_offsets);
            
            while (line_walker < walker && *address_offset_count < capacity)
            {
                value_name_length = util_string_word_length(line_walker);
                memcpy(&value.str, line_walker, value_name_length);
                value.length = (u8)value_name_length;
                value.str[value.length] = '\0';
                
                if (value.str[value.length - 1] == ',')
                {
                    value.str[--value.length] = '\0';
                }
                
                *address = strtoll(value.str, nullptr, 16);
                (*address_offset_count)++;
                address++;
                line_walker += value_name_length;
                line_walker = util_string_skip_special_characters(line_walker);
            }
        }
        else if (interned_type == string_table_intern("version_major"))
        {
            current->version_major = parse_u32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("version_minor"))
        {
            current->version_minor = parse_u32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("version_build"))
        {
            current->version_build = parse_u32(line_walker, line_walker + util_string_word_length(line_walker));
        }
        else if (interned_type == string_table_intern("version_private"))
        {
            current->version_private = parse_u32(line_walker, line_walker + util_string_word_length(line_walker));
        }
    }
    
    b32 has_signatures = current->node_signature.byte_count && current->level_signature.byte_count;
    b32 has_search_lengths = current->node_signature.search_length && current->level_signature.search_length;
    b32 has_offsets = current->node_address_offset_count && current->level_address_offset_count;
    
    if (has_signatures && has_search_lengths && has_offsets)
    {
        collection->count++;
    }
}

void init()
{
    memset(&ctx, 0, sizeof(VisualizerCtx));
    Arena platform_arena;
    //  @todo:  remove screenshot buffer memory `ctx.image` since this should be reused from platform_arena
    //          since images are essentially sent to gpu either and sits idle
    // memory
    {
        // for a 4k resolution, 3840x2160
        // 50mb for screenshots 32bit image (8R8G8B8A)
        s32 max_screen_resolution_width = 3840;
        s32 max_screen_resolution_height = 2160;
        s32 max_screen_size = max_screen_resolution_width * max_screen_resolution_height;
        
        ctx.memory = (u8*)malloc(MEGABYTES(300));
        ctx.memory_end = (u8*)ctx.memory + MEGABYTES(300);
        memset(ctx.memory, 0, ctx.memory_end - ctx.memory);
        
        u8 *memory_walker = ctx.memory;
        ctx.image = memory_walker;
        memory_walker += MEGABYTES(50);
        
        ctx.recorded_world_positions.points = (V3f*)memory_walker;
        ctx.recorded_world_positions.capacity = max_screen_size;
        ctx.recorded_world_positions.count = 0;
        ASSERT(max_screen_size * sizeof(V3f) < MEGABYTES(100));
        memory_walker += MEGABYTES(100);
        
        ctx.recorded_screen_positions.points = (V2i*)memory_walker;
        ctx.recorded_screen_positions.capacity = max_screen_size;
        ctx.recorded_screen_positions.count = 0;
        ASSERT(max_screen_size * sizeof(V2i) < MEGABYTES(50));
        memory_walker += MEGABYTES(50);
        ctx.arena = arena_make(memory_walker, MEGABYTES(50));
        memory_walker += MEGABYTES(50);
        platform_arena = arena_make(memory_walker, MEGABYTES(50));
        memory_walker += MEGABYTES(50);
        
        ASSERT((memory_walker - ctx.memory_end) == 0);
    }
    
    // bindings
    {
        ctx.buttons[Action_Set_World_Position].key = KeyCode_F9;
        ctx.buttons[Action_Scan_Regions].key = KeyCode_F11;
        ctx.buttons[Action_Cancel].key = KeyCode_Escape;
        ctx.buttons[Action_Set_Inventory_Position].key = KeyCode_Space;
        ctx.game_inventory_key_code = KeyCode_I;
        ctx.next_action = Action_Count;
        
        ctx.rebind_type = RebindType_None;
        ctx.previous_rebind_type = ctx.rebind_type;
        ctx.rebind_wait_time = 0.0;
        
        const char **key_code_names = get_key_code_names();
        
        String key_name;
        for (s32 index = 0; index < KeyCode_Mouse5 + 1; ++index)
        {
            key_code_names[index] = STRINGIZE(EMPTY);
        }
        
        for (s32 index = KeyCode_Mouse5 + 1; index < KeyCode_Count; ++index)
        {
            platform_key_name(index, &key_name);
            if (key_name.length)
            {
                key_code_names[index] = string_table_intern(key_name.str);
            }
            else
            {
                key_code_names[index] = STRINGIZE(EMPTY);
            }
        }
    }
    
    {
        ctx.scan_step_rate = 10;
        ctx.wiggle_amount = 10;
        ctx.wiggle_distance = 10;
    }
    // ui
    {
        ctx.background_color = {255, 255, 255, 255};
        ctx.circle_color = 0xFFFFFFFF;
        ctx.selected_circle_color = 0xFF7F7FFF;
        ctx.level_details_draw_type = ~0;
        
        ctx.auto_map_buttons[AutoMapAction_Set_1].key = KeyCode_1;
        ctx.auto_map_buttons[AutoMapAction_Set_2].key = KeyCode_2;
        ctx.auto_map_buttons[AutoMapAction_Set_1].mod = KeyMod_Ctrl;
        ctx.auto_map_buttons[AutoMapAction_Set_2].mod = KeyMod_Ctrl;
        //ctx.auto_map_buttons[AutoMapAction_Save].key = KeyCode_2;
        //ctx.auto_map_buttons[AutoMapAction_New].key = KeyCode_2;
    }
    {
        //  @todo:  reuse arena
        u64 file_size;
        String path;
        const char *current_path = directory_get();
        string_printf(&path, "%s/game_addresses.txt", current_path);
        
        if (const char *file_buffer = (const char *)read_entire_file(path.str, &file_size))
        {
            s32 block_count = parse_file_get_block_count(file_buffer, file_size);
            ctx.address_version_collection.items = (AddressVersionInfo*)malloc(sizeof(AddressVersionInfo) * 128);
            ctx.address_version_collection.capacity = block_count;
            
            parse_file(file_buffer, file_size, handle_address_block, &ctx.address_version_collection);
            ctx.address_version_collection.capacity = 128;
            free((void*)file_buffer);
        }
        
        string_printf(&path, "%s/game_signatures.txt", current_path);
        if (const char *file_buffer = (const char *)read_entire_file(path.str, &file_size))
        {
            s32 block_count = parse_file_get_block_count(file_buffer, file_size);
            ctx.signature_collection.items = (SignatureInfo*)malloc(sizeof(SignatureInfo) * 128);
            ctx.signature_collection.capacity = block_count;
            
            parse_file(file_buffer, file_size, handle_signature_block, &ctx.signature_collection);
            ctx.signature_collection.capacity = 128;
            free((void*)file_buffer);
        }
    }
    
    {
        platform_init(platform_arena);
        process_info_init();
    }
}

void global_button_update(GlobalButton *button)
{
    button->previous_state = button->state;
    s16 state = key_is_down(button->key);
    s16 mod = true;
    
    s32 mod_keys[] = {
        KeyCode_LShift,
        KeyCode_RShift,
        KeyCode_LCtrl,
        KeyCode_RCtrl,
        KeyCode_LAlt,
        KeyCode_RAlt,
    };
    
    if (button->mod)
    {
        mod = false;
        b32 mod_value = button->mod;
        s32 mod_index = 0;
        while (mod_value)
        {
            if (mod_value & 1)
            {
                mod |= key_is_down(mod_keys[mod_index]);
            }
            
            mod_index++;
            mod_value >>= 1;
        }
    }
    
    button->state = !!state && !!mod;
}

Game get_game_type(const char *window_title)
{
    if (strstr(window_title, "DOS EE"))
    {
        return Game_DOS1_EE;
    }
    else if (strstr(window_title, "DOS II : DE"))
    {
        return Game_DOS2_DE;
    }
    else if (strstr(window_title, "Baldur's Gate 3"))
    {
        return Game_BG3;
    }
    return Game_None;
}

StringCollection get_game_process_names(Game game)
{
    StringCollection collection = {};
    collection.strings = s_process_names;
    collection.count = ARRAY_SIZE(s_process_names);
    ASSERT(collection.strings && collection.count);
    
    return collection;
}

const char* get_game_name(Game game, const char *version)
{
    const char *game_name = NULL;
    
    const char *interned_version = string_table_intern(version);
    b32 is_process_gog = process_is_gog();
    b32 is_dx11 = process_is_dx11();
    
    if (game == Game_BG3)
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            b32 is_same_graphics_api = address_version_info->additional_info == is_dx11;
            
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform &&
                is_same_graphics_api)
            {
                game_name = address_version_info->game_name.str;
                break;
            }
        }
    }
    else
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform)
            {
                game_name = address_version_info->game_name.str;
                break;
            }
        }
    }
    
    return game_name;
}

AddressCollection get_game_node_position_address_offsets(Game game, const char *version)
{
    AddressCollection collection = {};
    
    const char *interned_version = string_table_intern(version);
    b32 is_process_gog = process_is_gog();
    b32 is_dx11 = process_is_dx11();
    
    if (game == Game_BG3)
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            b32 is_same_graphics_api = address_version_info->additional_info == is_dx11;
            
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform &&
                is_same_graphics_api)
            {
                collection.addresses = address_version_info->node_addresses;
                collection.count = address_version_info->node_count;
                break;
            }
        }
    }
    else
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform)
            {
                collection.addresses = address_version_info->node_addresses;
                collection.count = address_version_info->node_count;
                break;
            }
        }
    }
    
    return collection;
}

AddressCollection get_game_level_address_offsets(Game game, const char *version)
{
    AddressCollection collection = {};
    
    const char *interned_version = string_table_intern(version);
    b32 is_process_gog = process_is_gog();
    b32 is_dx11 = process_is_dx11();
    
    if (game == Game_BG3)
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            b32 is_same_graphics_api = address_version_info->additional_info == is_dx11;
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform &&
                is_same_graphics_api)
            {
                collection.addresses = address_version_info->level_addresses;
                collection.count = address_version_info->level_count;
                break;
            }
        }
    }
    else
    {
        for (s32 index = 0; index < ctx.address_version_collection.count; ++index)
        {
            AddressVersionInfo *address_version_info = ctx.address_version_collection.items + index;
            b32 is_same_game = address_version_info->game == game;
            b32 is_same_version = address_version_info->version == interned_version;
            b32 is_same_store_platform = address_version_info->is_gog == is_process_gog;
            
            if (is_same_game && 
                is_same_version && 
                is_same_store_platform)
            {
                collection.addresses = address_version_info->level_addresses;
                collection.count = address_version_info->level_count;
                break;
            }
        }
    }
    return collection;
}

void co_utility_open_inventory(mco_coro *co, u8 inventory_vk, u8 inventory_sc)
{
    push_keyboard_event(inventory_vk, inventory_sc, 0, 0);
    YIELD_WAIT(co, INPUT_DELAY);
    push_keyboard_event(inventory_vk, inventory_sc, KeyEvent_KeyUp, 0);
    YIELD_WAIT(co, INPUT_DELAY);
}

void co_utility_close_inventory(mco_coro *co, u8 inventory_vk, u8 inventory_sc)
{
    push_keyboard_event(inventory_vk, inventory_sc, 0, 0);
    YIELD_WAIT(co, INPUT_DELAY);
    push_keyboard_event(inventory_vk, inventory_sc, KeyEvent_KeyUp, 0);
    YIELD_WAIT(co, INPUT_DELAY);
}

void co_utility_begin_item_drag(mco_coro *co, s16 x, s16 y)
{
    push_mouse_move_absolute({x, y});
    YIELD_WAIT(co, INPUT_DELAY);
    push_mouse_button(MouseButton_Left, true);
    YIELD_WAIT(co, INPUT_DELAY);
}

void co_utility_do_wiggle(mco_coro *co, s32 wiggle_amount, s32 wiggle_distance)
{
    s32 wiggle_counter = wiggle_amount;
    while (wiggle_counter--)
    {
        push_mouse_move_relative({(s16)wiggle_distance, 0});
        mco_yield(co);
    }
    wiggle_counter = wiggle_amount;
    while (wiggle_counter--)
    {
        push_mouse_move_relative({(s16)-wiggle_distance, 0});
        mco_yield(co);
    }
}

void co_do_set_world_position(mco_coro *co)
{
    VisualizerCtx *ctx = (VisualizerCtx*)mco_get_user_data(co);
    V2i screen_position;
    u8 inventory_vk;
    u8 inventory_sc;
    s16 x;
    s16 y;
    mco_pop(co, &y, sizeof(s16));
    mco_pop(co, &x, sizeof(s16));
    mco_pop(co, &inventory_sc, sizeof(u8));
    mco_pop(co, &inventory_vk, sizeof(u8));
    mco_pop(co, &screen_position, sizeof(V2i));
    
    process_focus();
    YIELD_WAIT(co, INPUT_DELAY);
    
    co_utility_open_inventory(co, inventory_vk, inventory_sc);
    co_utility_begin_item_drag(co, x, y);
    co_utility_do_wiggle(co, ctx->wiggle_amount, ctx->wiggle_distance);
    co_utility_close_inventory(co, inventory_vk, inventory_sc);
    
    // move to fixed position
    push_mouse_move_absolute(screen_position);
    YIELD_WAIT(co, INPUT_DELAY);
    
    co_utility_do_wiggle(co, ctx->wiggle_amount, ctx->wiggle_distance);
    
    push_mouse_button(MouseButton_Left, false);
}

void setup_do_set_world_position(Game game, V2i screen_position, mco_coro **co)
{
    ASSERT(co);
    void (*co_fn)(mco_coro*) = nullptr;
    
    co_fn = co_do_set_world_position;
    
    if (co_fn)
    {
        if (*co)
        {
            mco_destroy(*co);
        }
        
        mco_desc desc = mco_desc_init(co_fn, 0);
        desc.user_data = &ctx;
        mco_coro *_co;
        mco_result res = mco_create(&_co, &desc);
        ASSERT(res == MCO_SUCCESS);
        screen_position = v2i_add(ctx.game_screen_bounds.min, screen_position);
        mco_push(_co, &screen_position, sizeof(V2i));
        
        u8 inventory_vk = (u8)ctx.game_inventory_key_code;
        u8 inventory_sc = (u8)get_scan_code(ctx.game_inventory_key_code);
        mco_push(_co, &inventory_vk, sizeof(u8));
        mco_push(_co, &inventory_sc, sizeof(u8));
        
        V2i item_screen_position = v2i_add(ctx.game_screen_bounds.min, ctx.inventory_screen_position);
        mco_push(_co, &item_screen_position.x, sizeof(s16));
        mco_push(_co, &item_screen_position.y, sizeof(s16));
        *co = _co;
    }
}

void co_do_scan_regions(mco_coro *co)
{
    VisualizerCtx *ctx = (VisualizerCtx*)mco_get_user_data(co);
    u8 inventory_vk;
    u8 inventory_sc;
    s16 x;
    s16 y;
    mco_pop(co, &y, sizeof(s16));
    mco_pop(co, &x, sizeof(s16));
    mco_pop(co, &inventory_sc, sizeof(u8));
    mco_pop(co, &inventory_vk, sizeof(u8));
    
    Aabbi *regions = ctx->regions;
    s32 region_count = ctx->region_count;
    
    s16 scan_step_rate = ctx->scan_step_rate;
    
    process_focus();
    YIELD_WAIT(co, INPUT_DELAY);
    
    if (process_screenshot((void**)&ctx->image, ctx->game_screen_bounds, true))
    {
        ctx->image_queued = true;
    }
    
    YIELD_WAIT(co, INPUT_DELAY);
    
    co_utility_open_inventory(co, inventory_vk, inventory_sc);
    co_utility_begin_item_drag(co, x, y);
    co_utility_do_wiggle(co, ctx->wiggle_amount, ctx->wiggle_distance);
    co_utility_close_inventory(co, inventory_vk, inventory_sc);
    
    V3f previous_node_position = ctx->node_position;
    Aabbi screen_bounds = ctx->game_screen_bounds;
    v2i_collection_clear(&ctx->recorded_screen_positions);
    v3f_collection_clear(&ctx->recorded_world_positions);
    ctx->selected_record_index = 0;
    
    if (ctx->stop_on_first_filtered)
    {
        WorldFilter filter_type = ctx->filter_type;
        Aabbf filter_world_region = ctx->filter_world_region;
        for (s32 index = 0; index < region_count; ++index)
        {
            Aabbi region = regions[index];
            V2i absolute_min = v2i_add(region.min, screen_bounds.min);
            V2i absolute_max = v2i_add(region.max, screen_bounds.min);
            
            for (s16 y = absolute_min.y; y < absolute_max.y; y += scan_step_rate)
            {
                for (s16 x = absolute_min.x; x < absolute_max.x; x += scan_step_rate)
                {
                    V2i screen_position = {x, y};
                    push_mouse_move_absolute(screen_position);
                    mco_yield(co);
                    V3f current_node_position = ctx->node_position;
                    V3f dp = v3f_sub(current_node_position, previous_node_position);
                    if (!f32_is_zero(v3f_len_sq(dp)) && aabbf_filtered(filter_world_region, current_node_position, filter_type, false))
                    {
                        v2i_collection_push(&ctx->recorded_screen_positions, ctx->game_screen_position);
                        v3f_collection_push(&ctx->recorded_world_positions, current_node_position);
                        previous_node_position = current_node_position;
                        goto EXIT_SCAN;
                    }
                }
            }
        }
        EXIT_SCAN:
        (void)scan_step_rate;
    }
    else
    {
        for (s32 index = 0; index < region_count; ++index)
        {
            Aabbi region = regions[index];
            V2i absolute_min = v2i_add(region.min, screen_bounds.min);
            V2i absolute_max = v2i_add(region.max, screen_bounds.min);
            
            for (s16 y = absolute_min.y; y < absolute_max.y; y += scan_step_rate)
            {
                for (s16 x = absolute_min.x; x < absolute_max.x; x += scan_step_rate)
                {
                    V2i screen_position = {x, y};
                    push_mouse_move_absolute(screen_position);
                    mco_yield(co);
                    V3f current_node_position = ctx->node_position;
                    V3f dp = v3f_sub(current_node_position, previous_node_position);
                    if (!f32_is_zero(v3f_len_sq(dp)))
                    {
                        v2i_collection_push(&ctx->recorded_screen_positions, ctx->game_screen_position);
                        v3f_collection_push(&ctx->recorded_world_positions, current_node_position);
                        previous_node_position = current_node_position;
                    }
                }
            }
        }
    }
    push_mouse_button(MouseButton_Left, false);
}

void setup_do_scan_regions(Game game, mco_coro **co)
{
    ASSERT(co);
    void (*co_fn)(mco_coro*) = nullptr;
    
    co_fn = co_do_scan_regions;
    
    if (co_fn)
    {
        if (*co)
        {
            mco_destroy(*co);
        }
        
        mco_desc desc = mco_desc_init(co_fn, 0);
        desc.user_data = &ctx;
        mco_coro *_co;
        mco_result res = mco_create(&_co, &desc);
        ASSERT(res == MCO_SUCCESS);
        
        u8 inventory_vk = (u8)ctx.game_inventory_key_code;
        u8 inventory_sc = (u8)get_scan_code(ctx.game_inventory_key_code);
        mco_push(_co, &inventory_vk, sizeof(u8));
        mco_push(_co, &inventory_sc, sizeof(u8));
        
        V2i item_screen_position = v2i_add(ctx.game_screen_bounds.min, ctx.inventory_screen_position);
        mco_push(_co, &item_screen_position.x, sizeof(s16));
        mco_push(_co, &item_screen_position.y, sizeof(s16));
        *co = _co;
    }
}

void co_do_screenshot(mco_coro *co)
{
    VisualizerCtx *ctx = (VisualizerCtx*)mco_get_user_data(co);
    process_focus();
    YIELD_WAIT(co, INPUT_DELAY);
    
    if (process_screenshot((void**)&ctx->image, ctx->game_screen_bounds, true))
    {
        ctx->image_queued = true;
    }
}

void setup_do_screenshot(mco_coro **co)
{
    ASSERT(co);
    if (*co)
    {
        mco_destroy(*co);
    }
    
    mco_desc desc = mco_desc_init(co_do_screenshot, 0);
    desc.user_data = &ctx;
    mco_coro *_co;
    mco_result res = mco_create(&_co, &desc);
    ASSERT(res == MCO_SUCCESS);
    *co = _co;
}

b32 scan_memory_addresses()
{
    b32 memory_scan_failed = true;
    u32 version_major;
    u32 version_minor;
    u32 version_build;
    u32 version_private;
    const char *window_title = process_get_window_title();
    const char *version = process_get_version(&version_major, &version_minor, &version_build, &version_private);
    
    ctx.game_type = get_game_type(window_title);
    
    if (ctx.game_type == Game_None)
    {
        return false;
    }
    
    AddressCollection node_position_address_collection = get_game_node_position_address_offsets(ctx.game_type, version);
    AddressCollection level_address_collection = get_game_level_address_offsets(ctx.game_type, version);
    
    if (!node_position_address_collection.count && !level_address_collection.count)
    {
        process_signature_scanner_begin();
        SignatureInfo *signature = nullptr;
        b32 is_process_gog = process_is_gog();
        b32 is_dx11 = process_is_dx11();
        
        for (s32 index = 0; index < ctx.signature_collection.count; ++index)
        {
            SignatureInfo *current_signature_info = ctx.signature_collection.items + index;
            if (current_signature_info->game == ctx.game_type &&
                current_signature_info->is_gog == is_process_gog &&
                current_signature_info->additional_info == is_dx11)
            {
                if (current_signature_info->version_major <= version_major && 
                    current_signature_info->version_minor <= version_minor && 
                    current_signature_info->version_build <= version_build &&
                    current_signature_info->version_private <= version_private)
                {
                    signature = current_signature_info; 
                }
            }
        }
        
        if (signature)
        {
            u64 node_address_offset = process_signature_scan(&signature->node_signature);
            u64 level_address_offset = process_signature_scan(&signature->level_signature);
            if (node_address_offset && level_address_offset)
            {
                if (ctx.address_version_collection.count >= ctx.address_version_collection.capacity)
                {
                    ctx.address_version_collection.capacity = ctx.address_version_collection.capacity == 0 ? 128 : ctx.address_version_collection.capacity * 2;
                    ctx.address_version_collection.items = (AddressVersionInfo*) realloc(ctx.address_version_collection.items, sizeof(AddressVersionInfo) * ctx.address_version_collection.capacity);
                }
                
                AddressVersionInfo *new_address_version_info = ctx.address_version_collection.items + ctx.address_version_collection.count;
                memcpy(new_address_version_info->node_addresses, signature->node_address_offsets, sizeof(u64) * signature->node_address_offset_count);
                memcpy(new_address_version_info->level_addresses, signature->level_address_offsets, sizeof(u64) * signature->level_address_offset_count);
                new_address_version_info->node_addresses[0] = node_address_offset;
                new_address_version_info->level_addresses[0] = level_address_offset;
                new_address_version_info->node_count = signature->node_address_offset_count;
                new_address_version_info->level_count = signature->level_address_offset_count;
                new_address_version_info->version = string_table_intern(version);
                new_address_version_info->game = signature->game;
                string_printf(&new_address_version_info->game_name, signature->game_name.str);
                new_address_version_info->is_gog = signature->is_gog;
                new_address_version_info->additional_info = signature->additional_info;
                
                ctx.address_version_collection.count++;
                
                node_position_address_collection.addresses = new_address_version_info->node_addresses;
                node_position_address_collection.count = new_address_version_info->node_count;
                
                level_address_collection.addresses = new_address_version_info->level_addresses;
                level_address_collection.count = new_address_version_info->level_count;
                
                printf("Signature Scan successful\n");
                String node_offset_str = string_from_address_offsets(node_position_address_collection.addresses, node_position_address_collection.count);
                String level_offset_str = string_from_address_offsets(level_address_collection.addresses, level_address_collection.count);
                char output_buffer[1024];
                u64 output_buffer_length = snprintf(output_buffer, sizeof(output_buffer), 
                                                    "{\n" \
                                                    "\tgame = %s\n" \
                                                    "\tplatform = %s\n" \
                                                    "\tversion = %s\n" \
                                                    "\tgraphics = %s\n" \
                                                    "\tnode_offsets = %s\n" \
                                                    "\tlevel_offsets = %s\n" \
                                                    "}\n",
                                                    new_address_version_info->game_name.str,
                                                    signature->is_gog ? "gog" : "steam",
                                                    new_address_version_info->version,
                                                    new_address_version_info->additional_info ? "dx11" : "vulkan",
                                                    node_offset_str.str,
                                                    level_offset_str.str
                                                    );
                
                printf("Dumping signature to game_addresses.txt\n%s\n", output_buffer);
                
                const char *current_path = directory_get();
                String path;
                string_printf(&path, "%s/game_addresses.txt", current_path);
                
                if (FILE *file_handle = fopen(path.str, "a"))
                {
                    fwrite("\n", 1, 1, file_handle);
                    fwrite(output_buffer, 1, output_buffer_length, file_handle);
                    fclose(file_handle);
                }
            }
        }
        
        string_printf(&ctx.game_name, signature->game_name.str);
        process_signature_scanner_end();
    }
    if (node_position_address_collection.count)
    {
        if (process_scan_memory_ex(node_position_address_collection.addresses, node_position_address_collection.count - 1, &ctx.node_position_address, sizeof(u64)))
        {
            ctx.node_position_address += node_position_address_collection.addresses[node_position_address_collection.count - 1];
            memory_scan_failed = false;
        }
    }
    
    if (level_address_collection.count)
    {
        if (process_scan_memory_ex(level_address_collection.addresses, level_address_collection.count - 1, &ctx.level_name_address, sizeof(u64)))
        {
            ctx.level_name_address += level_address_collection.addresses[level_address_collection.count - 1];
            memory_scan_failed = false;
        }
    }
    
    if (!memory_scan_failed)
    {
        const char *game_name = get_game_name(ctx.game_type, version);
        string_printf(&ctx.game_name, game_name);
    }
    
    return !memory_scan_failed;
}

void save_settings()
{
    if (FILE *file_handle = fopen("settings", "wb"))
    {
        u64 cookie = 0xEEA11FEAAC87621B;
        fwrite(&cookie, 1, sizeof(u64), file_handle);
        fwrite(&ctx.config, 1, sizeof(ctx.config), file_handle);
        fclose(file_handle);
    }
}

void load_settings()
{
    u64 buffer_size = 0;
    u8 *buffer = (u8*)read_entire_file("settings", &buffer_size);
    
    if (buffer)
    {
        //  @todo:  convert to same file format as everything else than binary
        b32 can_parse = buffer_size >= sizeof(ctx.config) + sizeof(u64);
        u8 *buffer_walker = buffer;
        
        if (can_parse)
        {
            u64 cookie = *(u64*)buffer_walker;
            buffer_walker += sizeof(u64);
            
            can_parse = cookie == 0xEEA11FEAAC87621B;
        }
        if (can_parse)
        {
            memcpy(&ctx.config, buffer_walker, sizeof(ctx.config));
        }
        free(buffer);
    }
}

void game_actions()
{
    static u64 previous_node_position_address = 0;
    if (ctx.force_rescan_memory)
    {
        if (ctx.memory_addresses_dirty_counter == 0)
        {
            ctx.memory_addresses_dirty_counter = 1;
        }
        ctx.force_rescan_memory = false;
    }
    
    if (ctx.memory_addresses_dirty_counter)
    {
        if (get_time() > ctx.next_process_search_time && !scan_memory_addresses())
        {
            ctx.next_process_search_time = get_time() + 0.1;
        }
    }
    
    {
        b32 memory_scan_failed = false;
        if (!process_scan_memory(ctx.node_position_address, &ctx.node_position, sizeof(V3f)))
        {
            memory_scan_failed = true;
        }
        //  @bug:  address seems to break at some point after doing some code injection to use console commands to teleport
        //         around the world, have not seen this get fired off for awhile so unsure. still needs testing to see what
        //         the actual cause was
        //  @note:  some pointers will get stale after some point so need to do a validation check if it's either out of bounds
        //          other times the value might be near zero for all floats for vec3 (example dos2 screen to world cursor position)
        
        // validation check if position is in bounds, aabb size should be configurable
        if (ctx.node_position.x < -10000.0f || ctx.node_position.x > 10000.0f || 
            ctx.node_position.y < -10000.0f || ctx.node_position.y > 10000.0f ||
            ctx.node_position.z < -10000.0f || ctx.node_position.z > 10000.0f)
        {
            memory_scan_failed = true;
        }
        
        // validation check if position is near zero, all valid values should be something reasonable (0.1f+) and not be extremely tiny
        if (is_near_zero(ctx.node_position.x) && is_near_zero(ctx.node_position.y) && is_near_zero(ctx.node_position.z))
        {
            memory_scan_failed = true;
        }
        
        ctx.level_name.length = (u8)process_scan_memory(ctx.level_name_address, &ctx.level_name.str, sizeof(String::str));
        if (!ctx.level_name.length)
        {
            memory_scan_failed = true;
        }
        else
        {
            // clip end of name of invalid characters
            u8 new_length = ctx.level_name.length;
            for (u8 index = 0; index < ctx.level_name.length; ++index)
            {
                if (ctx.level_name.str[index] < ' ')
                {
                    new_length = index;
                    break;
                }
            }
            
            ctx.level_name.length = new_length;
            ctx.level_name.str[ctx.level_name.length] = 0;
        }
        ctx.game_screen_bounds = process_window_size();
        
        if (memory_scan_failed)
        {
            ctx.memory_addresses_dirty_counter++;
        }
        else
        {
            ctx.memory_addresses_dirty_counter = 0;
        }
    }
    
    {
        if (ctx.global_hotkeys_enabled && (ctx.window_type & WindowType_Node))
        {
            if (global_button_is_pressed(&ctx.buttons[Action_Set_World_Position]))
            {
                ctx.next_action = Action_Set_World_Position;
            }
            if (global_button_is_pressed(&ctx.buttons[Action_Scan_Regions]))
            {
                ctx.next_action = Action_Scan_Regions;
            }
            if (global_button_is_pressed(&ctx.buttons[Action_Set_Inventory_Position]))
            {
                ctx.inventory_screen_position = ctx.game_screen_position;
            }
        }
        if (global_button_is_pressed(&ctx.buttons[Action_Cancel]))
        {
            ctx.next_action = Action_Cancel;
        }
        
        if (ctx.next_action == Action_Set_World_Position)
        {
            if (ctx.selected_record_index < ctx.recorded_screen_positions.count)
            {
                V2i screen_position = ctx.recorded_screen_positions.points[ctx.selected_record_index];
                setup_do_set_world_position(ctx.game_type, screen_position, &ctx.action_co);
            }
        }
        else if (ctx.next_action == Action_Scan_Regions)
        {
            setup_do_scan_regions(ctx.game_type, &ctx.action_co);
        }
        else if (ctx.next_action == Action_Screenshot)
        {
            setup_do_screenshot(&ctx.action_co);
        }
        else if (ctx.next_action == Action_Cancel)
        {
            if (ctx.action_co)
            {
                // most cases we're canceling during a scan so make sure to release the drag action otherwise next scan or fling will keep 
                // failing to drag the item
                push_mouse_button(MouseButton_Left, false);
                mco_destroy(ctx.action_co);
                ctx.action_co = nullptr;
            }
        }
        ctx.next_action = Action_Count;
        
        if (ctx.action_co)
        {
            if (mco_status(ctx.action_co) != MCO_DEAD)
            {
                mco_resume(ctx.action_co);
            }
            else
            {
                mco_destroy(ctx.action_co);
                ctx.action_co = nullptr;
            }
        }
        
        if (ctx.image_queued)
        {
            AppTexture new_texture = renderer_push_image(ctx.image, ctx.game_screen_bounds);
            if (new_texture.id)
            {
                if (ctx.texture.id)
                {
                    renderer_unload_texture(ctx.texture);
                }
                ctx.texture = new_texture;
            }
            ctx.image_queued = false;
        }
    }
    
    previous_node_position_address = ctx.node_position_address;
}

void ui_actions()
{
    if (ctx.ui_next_action == Action_Get_Clipboard_Image)
    {
        ClipboardImage clipboard_image = clipboard_get_image();
        if (clipboard_image.image)
        {
            Aabbi image_bounds = aabbi_make({}, {(s16)clipboard_image.width, (s16)clipboard_image.height});
            AppTexture clipboard_texture = renderer_push_image(clipboard_image.image, image_bounds);
            renderer_set_clipboard_texture(clipboard_texture);
        }
    }
    ctx.ui_next_action = Action_Count;
}

void update_inputs()
{
    if (ctx.previous_rebind_type != ctx.rebind_type)
    {
        ctx.rebind_wait_time = get_time() + 0.1;
    }
    
    ctx.raw_mouse_position = get_cursor_position();
    if (process_is_active())
    {
        ctx.game_screen_position = v2i_sub(ctx.raw_mouse_position, ctx.game_screen_bounds.min);
    }
    else
    {
        ctx.game_screen_position = {};
    }
    
    if (ctx.rebind_type != RebindType_None && get_time() > ctx.rebind_wait_time)
    {
        if (ctx.rebind_type == RebindType_Inventory_Position)
        {
            if (key_is_down(KeyCode_MouseLeft))
            {
                ctx.inventory_screen_position = ctx.game_screen_position;
                ctx.rebind_type = RebindType_None;
            }
        }
        else
        {
            s32 rebound_key_code = KeyCode_Count;
            for (s32 key_code = KeyCode_Mouse5 + 1; key_code < KeyCode_Count; ++key_code)
            {
                if (key_is_down(key_code))
                {
                    rebound_key_code = key_code;
                    break;
                }
            }
            
            if (rebound_key_code != KeyCode_Count)
            {
                b32 rebinding_game_inventory_button = false;
                Action action = Action_Count;
                if (ctx.rebind_type == RebindType_Fling) action = Action_Set_World_Position;
                else if (ctx.rebind_type == RebindType_Scan) action = Action_Scan_Regions;
                else if (ctx.rebind_type == RebindType_Cancel) action = Action_Cancel;
                else if (ctx.rebind_type == RebindType_Set_Inventory_Position) action = Action_Set_Inventory_Position;
                else if (ctx.rebind_type == RebindType_Inventory) rebinding_game_inventory_button = true;
                
                if (action != Action_Count)
                {
                    ctx.buttons[action].key = rebound_key_code;
                }
                if (rebinding_game_inventory_button)
                {
                    ctx.game_inventory_key_code = rebound_key_code;
                }
                
                ctx.rebind_type = RebindType_None;
                
                save_settings();
            }
        }
    }
    else
    {
        for (s32 index = 0; index < ARRAY_SIZE(ctx.buttons); ++index)
        {
            global_button_update(ctx.buttons + index);
        }
    }
    
    for (s32 index = 0; index < ARRAY_SIZE(ctx.auto_map_buttons); ++index)
    {
        global_button_update(ctx.auto_map_buttons + index);
    }
    
    ctx.previous_rebind_type = ctx.rebind_type;
}

void update()
{
    if (!process_is_active() || ctx.memory_addresses_dirty_counter > 10)
    {
        StringCollection process_names = get_game_process_names(ctx.game_type);
        if (get_time() > ctx.next_process_search_time && !process_find(&process_names))
        {
            // search every 100ms
            ctx.next_process_search_time = get_time() + 0.1;
        }
        else
        {
            ctx.memory_addresses_dirty_counter = 1;
        }
    }
    else
    {
        game_actions();
    }
    ui_actions();
}

int main(void)
{
    set_frame_rate(60);
    window_create(1280, 720, "Divinity Fling Visualizer");
    init();
    load_settings();
    
    if (!load_assets(&ctx.arena, &ctx))
    {
        load_default_assets(&ctx.arena, &ctx);
    }
    
    ctx.map_texture = renderer_render_texture_make();
    
    b32 should_quit = false;
    while (!should_quit)
    {
        update_inputs();
        update();
        
        renderer_begin(ctx.background_color);
        should_quit = do_window(&ctx) || window_should_close();
        renderer_end();
    }
    
    save_settings();
    renderer_unload_render_texture(ctx.map_texture);
    window_close();
    return 0;
}