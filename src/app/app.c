#include "app/app.h"

#define PLATFORM_EVENT_DELAY 16

App* g_app;

void push_camera(Camera* camera);
void pop_camera(void);

void app_update_screen_scan(void);
void app_update_world_map(void);

void app_draw_grid(s32 grid_x, s32 grid_y, f32 thickness);

void app_draw_filter_shapes(void);

void app_draw_screen_scan(void);
void app_draw_world_map(void);

CF_Sprite draw_image(CF_Sprite sprite, CF_Aabb3 aabb, CF_Color chroma);
void build_draw_list(Canvas* canvas, Config* region);

void cleanup_clipboard_file(void);

void co_bind_inventory_location(CF_Coroutine co);
void co_bind_inventory(CF_Coroutine co);
void co_scan_region(CF_Coroutine co);

void scan_record_location(s32 x, s32 y, CF_V3 p);

typedef enum Coroutine_Resume_Result
{
    Coroutine_Resume_Result_Resumed,
    Coroutine_Resume_Result_Dead,
    Coroutine_Resume_Result_Escaped,
} Coroutine_Resume_Result;

Coroutine_Resume_Result coroutine_escapeable_resume(CF_Coroutine* co)
{
    Coroutine_Resume_Result result = Coroutine_Resume_Result_Resumed;
    if (coroutine_is_alive(*co))
    {
        if (async_get_key_down(CF_KEY_ESCAPE))
        {
            cf_destroy_coroutine(*co);
            *co = (CF_Coroutine){ 0 };
            result = Coroutine_Resume_Result_Escaped;
        }
        else
        {
            cf_coroutine_resume(*co);
        }
    }
    else
    {
        result = Coroutine_Resume_Result_Dead;
    }
    return result;
}

CF_ARRAY(str8) address_offsets_to_str8_array(u64 address, CF_ARRAY(s32) offsets)
{
    CF_ARRAY(str8) arr = NULL;
    arena_array_fit(&g_arena, arr, cf_array_count(offsets) + 1);
    
    str8 str = make_arena_string(&g_arena, 1024);
    cf_array_push(arr, arena_fmt(&g_arena, "0x%X", address));
    for (s32 index = 0; index < cf_array_count(offsets); ++index)
    {
        cf_array_push(arr, arena_fmt(&g_arena, "0x%X", offsets[index]));
    }
    
    return arr;
}

void game_signature_scan(Config_Option* game_signature)
{
    CF_ARRAY(str8) signature_names = NULL;
    arena_array_fit(&g_arena, signature_names, cf_map_size(game_signature->kv));
    
    const char* key_search = "_signature";
    s32 key_length = (s32)CF_STRLEN(key_search);
    
    // all signature_names are prefixes, any of the suffixes below will be trimmed off
    // _signature
    // _search_start
    // _search_end
    // _address_offsets
    
    const char** keys = (const char**)cf_map_keys(game_signature->kv);
    for (s32 index = 0; index < cf_map_size(game_signature->kv); ++index)
    {
        if (cf_string_suffix(keys[index], key_search))
        {
            str8 signature_name = arena_fmt(&g_arena, keys[index]);
            cf_string_pop_n(signature_name, key_length);
            cf_array_push(signature_names, signature_name);
        }
    }
    
    // build up new address config options
    {
        Config_Option new_config = 
        { 
            .start_option_offset = -1, 
            .end_option_offset = -1,
        };
        cf_map_set(new_config.kv, cf_sintern("process"), cf_map_get(g_app->game_signature->kv, cf_sintern("process")));
        cf_map_set(new_config.kv, cf_sintern("game"), cf_map_get(g_app->game_signature->kv, cf_sintern("game")));
        cf_map_set(new_config.kv, cf_sintern("platform"), cf_map_get(g_app->game_signature->kv, cf_sintern("platform")));
        
        CF_ARRAY(const char*) version_arr = NULL;
        arena_array_fit(&g_arena, version_arr, 1);
        str8 version_str = arena_fmt(&g_arena, "%d.%d.%d.%d", g_app->process.version_major, g_app->process.version_minor, g_app->process.version_build, g_app->process.version_private);
        cf_array_push(version_arr, version_str);
        
        cf_map_set(new_config.kv, cf_sintern("version"), version_arr);
        
        s32 old_count = cf_map_size(new_config.kv);
        
        // scan for base pointer and offsets
        Signature_Scanner scanner = signature_scan_begin(g_app->process, g_app->process.modules[0]);
        str8 buffer = make_arena_string(&g_arena, 256);
        for (s32 index = 0; index < cf_array_count(signature_names); ++index)
        {
            cf_string_fmt(buffer, "%s_signature", signature_names[index]);
            Optional opt_signature = config_options_get_array_hex_uint(game_signature, buffer);
            
            cf_string_fmt(buffer, "%s_search_start", signature_names[index]);
            Optional opt_search_start = config_options_get_int(game_signature, buffer);
            
            cf_string_fmt(buffer, "%s_search_length", signature_names[index]);
            Optional opt_search_length = config_options_get_int(game_signature, buffer);
            
            cf_string_fmt(buffer, "%s_address_offsets", signature_names[index]);
            Optional opt_address_offsets = config_options_get_array_hex_uint(game_signature, buffer);
            
            if (opt_signature.has_value && 
                opt_search_start.has_value && 
                opt_search_length.has_value && 
                opt_address_offsets.has_value)
            {
                Signature_Query query = {
                    .signature = array_int_to_u8((s32*)opt_signature.custom_value),
                    .signature_length = cf_array_count((s32*)opt_signature.custom_value),
                    .search_start = opt_search_start.int_value,
                    .search_end = opt_search_start.int_value + opt_search_length.int_value,
                };
                
                u64 address = signature_scan(g_app->process, scanner, query);
                if (address)
                {
                    CF_ARRAY(str8) offsets_str_arr = address_offsets_to_str8_array(address, (s32*)opt_address_offsets.custom_value);
                    cf_string_fmt(buffer, "%s_offsets", signature_names[index]);
                    CF_ARRAY(const char*) value = NULL;
                    arena_array_fit(&g_app->assets.game_addresses.arena, value, cf_array_count(offsets_str_arr));
                    for (s32 offset_index = 0; offset_index < cf_array_count(offsets_str_arr); ++offset_index)
                    {
                        str8 offset_str = arena_fmt(&g_app->assets.game_addresses.arena, offsets_str_arr[offset_index]);
                        cf_array_push(value, offset_str);
                    }
                    cf_map_set(new_config.kv, cf_sintern(buffer), value);
                }
            }
        }
        signature_scan_end(&scanner);
        
        // update address offsets if any have been found
        if (old_count != cf_map_size(new_config.kv))
        {
            b32 has_updated = false;
            // add new one address since 
            if (g_app->game_address == NULL)
            {
                // new_config.kv will live for entirety of application so don't free new_config.kv here
                arena_array_fit(&g_app->assets.game_addresses.arena, g_app->assets.game_addresses.options, cf_array_count(g_app->assets.game_addresses.options) + 1);
                cf_array_push(g_app->assets.game_addresses.options, new_config);
                g_app->game_address = &cf_array_last(g_app->assets.game_addresses.options);
                has_updated = true;
            }
            else
            {
                // checking each kv pair to see if it matches, if not update it
                const char** keys = (const char**)cf_map_keys(new_config.kv);
                for (s32 index = 0; index < cf_map_size(new_config.kv); ++index)
                {
                    const char* key = keys[index];
                    u64 new_kvp_hash = config_options_get_hash(&new_config, key);
                    u64 kvp_hash = config_options_get_hash(g_app->game_address, key);
                    
                    if (new_kvp_hash != 0 &&
                        kvp_hash != new_kvp_hash)
                    {
                        CF_ARRAY(const char*) values = new_config.kv[index];
                        config_options_update_kv_list(&g_app->assets.game_addresses, g_app->game_address, key, values, cf_array_count(values));
                        has_updated = true;
                    }
                }
            }
            
            // update file on disk of any changes
            if (has_updated && g_app->game_address)
            {
                mount_data_read_directory();
                mount_data_write_directory();
                config_options_save_to_file(&g_app->assets.game_addresses, g_app->game_address);
                dismount_root_directory();
            }
        }
        else
        {
            // only free if none of the addresses has changed
            cf_map_free(new_config.kv);
        }
    }
}

void game_read_state(void)
{
    str8 buffer = make_arena_string(&g_arena, 256);
    s32 suffix_length = (s32)CF_STRLEN("_offsets");
    
    const char** keys = (const char**)cf_map_keys(g_app->game_address->kv);
    for (s32 index = 0; index < cf_map_size(g_app->game_address->kv); ++index)
    {
        if (!cf_string_suffix(keys[index], "offsets"))
        {
            continue;
        }
        
        cf_string_fmt(buffer, "%s", keys[index]);
        cf_string_pop_n(buffer, suffix_length);
        
        Optional opt_offsets = config_options_get_array_hex_uint64(g_app->game_address, keys[index]);
        CF_ARRAY(u64) offsets = (CF_ARRAY(u64))opt_offsets.custom_value;
        
        Optional value = { 0 };
        if (cf_string_contains(keys[index], "location"))
        {
            value.type = Optional_Type_Float3;
            if (process_deref(g_app->process, offsets, &value.float3_value, sizeof(value.float3_value)))
            {
                value.has_value = true;
                cf_map_set(g_app->process_ptrs, cf_sintern(buffer), value);
            }
        }
        else
        {
            value.type = Optional_Type_String;
            char read_buffer[256] = { 0 };
            s32 length = 0;
            
            cf_array_last(offsets) -= 8;
            process_deref(g_app->process, offsets, &length, sizeof(length));
            
            cf_array_last(offsets) += 8;
            
            if (process_deref(g_app->process, offsets, &read_buffer, sizeof(read_buffer)))
            {
                read_buffer[255] = '\0';
                value.has_value = true;
                value.str_value = arena_fmt(&g_arena, read_buffer);
                cf_map_set(g_app->process_ptrs, cf_sintern(buffer), value);
            }
        }
    }
}

void app_reload_process_names(void)
{
    Assets* assets = &g_app->assets;
    for (s32 index = 0; index < cf_array_count(assets->game_signatures.options); ++index)
    {
        Config_Option* options = assets->game_signatures.options + index;
        Optional process_name = config_options_get_string(options, "process");
        if (process_name.has_value)
        {
            const char* name = cf_sintern(process_name.str_value);
            cf_map_set(g_app->process_names, name, name);
        }
    }
}

void app_build_canvases(void)
{
    for (s32 index = 0; index < cf_map_size(g_app->draw.canvases); ++index)
    {
        cf_destroy_canvas(g_app->draw.canvases[index].canvas);
        cf_destroy_draw_list(g_app->draw.canvases[index].draw_list);
    }
    cf_map_clear(g_app->draw.canvases);
    
    Assets* assets = &g_app->assets;
    
    // setup canvases and draw lists
    CF_CanvasParams params = cf_canvas_defaults(CANVAS_WIDTH, CANVAS_HEIGHT);
    const char** keys = (const char**)cf_map_keys(assets->regions);
    for (s32 index = 0; index < cf_map_size(assets->regions); ++index)
    {
        CF_ARRAY(Config) regions = assets->regions[index];
        for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
        {
            Config* region = regions + region_index;
            
            Canvas canvas = 
            {
                .game = keys[index],
                .region_name = cf_sintern(region->tag),
                .canvas = cf_make_canvas(params),
                .draw_list = cf_make_draw_list(),
                .order = cf_map_size(g_app->draw.canvases),
                .is_visible = false,
                .is_draw_list_dirty = true,
            };
            
            cf_map_set(g_app->draw.canvases, canvas.region_name, canvas);
        }
    }
    
#if 0
    // bake sprites into each draw list per region
    for (s32 index = 0; index < cf_map_size(assets->regions); ++index)
    {
        CF_ARRAY(Config) regions = assets->regions[index];
        
        for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
        {
            Config* region = regions + region_index;
            Canvas* canvas = cf_map_get_ptr(g_app->draw.canvases, cf_sintern(region->tag));
            
            build_draw_list(canvas, region);
        }
    }
#endif
}

void update_camera(Camera* camera)
{
    CF_V2 mouse = camera_mouse(camera);
    CF_V2 motion = g_app->input.mouse_motion;
    
    MEMZERO(&camera->motion);
    
    // camera update
    if (!ui_is_hovering_any_windows())
    {
        f32 wheel = g_app->input.mouse_wheel;
        
        // anchored camera zoom
        if (cf_abs(wheel) > 1e-7f)
        {
            CF_V2 old_position = camera_mouse(camera);
            
            camera->zoom = cf_clamp(camera->zoom + wheel * 0.1f, ZOOM_MIN, ZOOM_MAX);
            
            CF_V2 position = camera_mouse(camera);
            CF_V2 dp = cf_sub(old_position, position);
            camera->position = cf_add(camera->position, cf_neg(dp));
        }
        
        // panning
        if (cf_mouse_down(CF_MOUSE_BUTTON_RIGHT))
        {
            camera->position = cf_add(camera->position, cf_div(motion, camera->zoom));
            camera->motion = motion;
        }
    }
}

void push_camera(Camera* camera)
{
    cf_draw_push();
    cf_draw_scale(camera->zoom, camera->zoom);
    cf_draw_translate_v2(camera->position);
}

void pop_camera(void)
{
    cf_draw_pop();
}

void app_save_config(void)
{
    Config config = config_make_empty();
    Config_Option* options = config_make_options(&config);
    
    config_options_update_kv(&config, options, "opacity", arena_fmt(&g_arena, "%.2f", g_app->world_map.opacity));
    config_options_update_kv(&config, options, "character_location_color", color_to_str8(g_app->world_map.character_location_color));
    config_options_update_kv(&config, options, "node_location_color", color_to_str8(g_app->world_map.node_location_color));
    config_options_update_kv(&config, options, "fling_direction_color", color_to_str8(g_app->world_map.fling_direction_color));
    config_options_update_kv(&config, options, "fling_arrow_thickness", arena_fmt(&g_arena, "%.2f", g_app->world_map.fling_arrow_thickness));
    config_options_update_kv(&config, options, "fling_arrow_width", arena_fmt(&g_arena, "%.2f", g_app->world_map.fling_arrow_width));
    config_options_update_kv(&config, options, "world_map_grid_x", arena_fmt(&g_arena, "%d", g_app->world_map.grid_x));
    config_options_update_kv(&config, options, "world_map_grid_y", arena_fmt(&g_arena, "%d", g_app->world_map.grid_y));
    config_options_update_kv(&config, options, "world_map_grid_color", color_to_str8(g_app->world_map.grid_color));
    
    config_options_update_kv(&config, options, "screen_scan_grid_x", arena_fmt(&g_arena, "%d", g_app->screen_scan.grid_x));
    config_options_update_kv(&config, options, "screen_scan_grid_y", arena_fmt(&g_arena, "%d", g_app->screen_scan.grid_y));
    config_options_update_kv(&config, options, "screen_scan_grid_color", color_to_str8(g_app->screen_scan.grid_color));
    
    config_options_update_kv(&config, options, "inventory_key", cf_key_button_to_string(g_app->screen_scan.inventory_key));
    config_options_update_kv(&config, options, "step_rate", arena_fmt(&g_arena, "%d", g_app->screen_scan.step_rate));
    
    config.file = "settings.txt";
    
    mount_root_read_directory();
    mount_root_write_directory();
    
    config_save_to_file(&config);
    
    destroy_config(&config);
    dismount_root_directory();
}

b32 app_read_config_param(void* data, Config_Option* options, Optional_Type type, const char* name)
{
    b32 has_updated = false;
    switch (type)
    {
        case Optional_Type_Float:
        {
            Optional opt = config_options_get_float(options, name);
            if (opt.has_value)
            {
                *(f32*)data = opt.float_value;
                has_updated = true;
            }
            break;
        }
        case Optional_Type_Int:
        {
            Optional opt = config_options_get_int(options, name);
            if (opt.has_value)
            {
                *(s32*)data = opt.int_value;
                has_updated = true;
            }
            break;
        }
        case Optional_Type_Color:
        {
            Optional opt = config_options_get_color(options, name);
            if (opt.has_value)
            {
                *(CF_Color*)data = opt.color_value;
                has_updated = true;
            }
            break;
        }
    }
    
    return has_updated;
}

void app_load_config(void)
{
    mount_root_read_directory();
    Config config = config_load("settings.txt");
    Config_Option* options = config.options;
    
    app_read_config_param(&g_app->world_map.opacity, options, Optional_Type_Float, "opacity");
    app_read_config_param(&g_app->world_map.character_location_color, options, Optional_Type_Color, "character_location_color");
    app_read_config_param(&g_app->world_map.node_location_color, options, Optional_Type_Color, "node_location_color");
    app_read_config_param(&g_app->world_map.fling_direction_color, options, Optional_Type_Color, "fling_direction_color");
    app_read_config_param(&g_app->world_map.fling_arrow_thickness, options, Optional_Type_Float, "fling_arrow_thickness");
    app_read_config_param(&g_app->world_map.fling_arrow_width, options, Optional_Type_Float, "fling_arrow_width");
    app_read_config_param(&g_app->world_map.grid_x, options, Optional_Type_Int, "world_map_grid_x");
    app_read_config_param(&g_app->world_map.grid_y, options, Optional_Type_Int, "world_map_grid_y");
    app_read_config_param(&g_app->world_map.grid_color, options, Optional_Type_Color, "world_map_grid_color");
    app_read_config_param(&g_app->screen_scan.grid_x, options, Optional_Type_Int, "screen_scan_grid_x");
    app_read_config_param(&g_app->screen_scan.grid_y, options, Optional_Type_Int, "screen_scan_grid_y");
    app_read_config_param(&g_app->screen_scan.grid_color, options, Optional_Type_Color, "screen_scan_grid_color");
    app_read_config_param(&g_app->screen_scan.step_rate, options, Optional_Type_Int, "step_rate");
    
    Optional opt_inventory_key = config_options_get_string(options, "inventory_key");
    if (opt_inventory_key.has_value)
    {
        for (s32 index = 0; index < CF_KEY_COUNT; ++index)
        {
            if (cf_string_equ(cf_key_button_to_string(index), opt_inventory_key.str_value))
            {
                g_app->screen_scan.inventory_key = (CF_KeyButton)index;
                break;
            }
        }
    }
    
    destroy_config(&config);
    dismount_root_directory();
}

App* make_app(void)
{
    printf("Initializing App\n");
    
    CF_Stopwatch stopwatch = cf_make_stopwatch();
    
    cf_draw_set_atlas_dimensions(1024*8, 1024*8);
    
    App* app = (App*)cf_alloc(sizeof(App));
    MEMZERO(app);
    app->process_arena = cf_make_arena(16, CF_KB * 512);
    
    g_app = app;
    
    app->threadpool = cf_make_threadpool(4);
    
    app->draw.transparency_shader = cf_make_draw_shader_from_source("vec4 shader(vec4 color, ShaderParams params) {"
                                                                    "return color * params.attributes.a;"
                                                                    "}");
    
    // chroma key is in RGB channels and A channel is used to determine threshold
    
    // sprite
    app->draw.chroma_shader = 
        cf_make_draw_shader_from_source(
                                        "vec4 shader(vec4 color, ShaderParams params) {"
                                        "if (params.attributes.a > 0 &&"
                                        "    abs(params.attributes.r - color.r) <= params.attributes.a &&"
                                        "    abs(params.attributes.g - color.g) <= params.attributes.a &&"
                                        "    abs(params.attributes.b - color.b) <= params.attributes.a)"
                                        "    {"
                                        "        return vec4(0);"
                                        "    }"
                                        "return color;"
                                        "}");
    
    assets_load_configs();
    app_reload_process_names();
    app_build_canvases();
    
    app->state = App_State_World_Map;
    app->world_map.opacity = 0.25f;
    app->world_map.camera = camera_defaults();
    app->world_map.character_location_color = cf_color_white();
    app->world_map.node_location_color = cf_color_white();
    app->world_map.fling_direction_color = cf_color_white();
    app->world_map.fling_arrow_thickness = 1.0f;
    app->world_map.fling_arrow_width = 2.0f;
    app->world_map.grid_x = 256;
    app->world_map.grid_y = 256;
    app->world_map.grid_line_thickness = 0.5;
    app->world_map.grid_color = cf_make_color_rgba_f(1.0f, 1.0f, 1.0f, 0.25f);
    
    app->screen_scan.camera = camera_defaults();
    app->screen_scan.grid_x = 256;
    app->screen_scan.grid_y = 256;
    app->screen_scan.grid_line_thickness = 0.5;
    app->screen_scan.grid_color = cf_make_color_rgba_f(1.0f, 1.0f, 1.0f, 0.25f);
    app->screen_scan.inventory_key = CF_KEY_I;
    app->screen_scan.step_rate = 5;
    
    app->auto_game_filter = true;
    
    {
        Assets* assets = &g_app->assets;
        const char** game_names = (const char**)cf_map_keys(assets->regions);
        for (s32 index = 0; index < cf_map_size(assets->regions); ++index)
        {
            cf_array_push(app->game_filter, game_names[index]);
        }
    }
    
    cf_array_fit(g_app->scan_points.points, 2048);
    cf_array_fit(g_app->scan_points.filter_points, 2048);
    cf_array_fit(g_app->scan_points.filter_shapes, 512);
    
    cleanup_clipboard_file();
    
    printf("Startup time: %.5f seconds\n", cf_stopwatch_seconds(stopwatch));
    
    app_load_config();
    
    return app;
}

void destroy_app(void)
{
    app_save_config();
    
    assets_unload_configs();
    
    cf_destroy_threadpool(g_app->threadpool);
    cf_map_free(g_app->draw.canvases);
    
    cf_map_free(g_app->process_names);
    cf_map_free(g_app->process_ptrs);
    cf_array_free(g_app->game_filter);
    
    cf_array_free(g_app->scan_points.points);
    cf_array_free(g_app->scan_points.filter_points);
    cf_array_free(g_app->scan_points.filter_shapes);
    
    cf_destroy_arena(&g_app->process_arena);
    
    cf_free(g_app);
    g_app = NULL;
    
    cleanup_clipboard_file();
}

void app_update(void* udata)
{
    cf_arena_reset(&g_arena);
    
    {
        g_app->input.mouse_motion = cf_v2(cf_mouse_motion_x(), cf_mouse_motion_y());
        g_app->input.mouse_wheel = cf_mouse_wheel_motion();
        cf_app_get_size(&g_app->draw.width, &g_app->draw.height);
        g_app->draw.screen_size = cf_v2((f32)g_app->draw.width, (f32)g_app->draw.height);
    }
    
    b32 has_loaded_new_signatures = false;
    if (!is_process_alive(g_app->process))
    {
        g_app->game_signature = NULL;
        g_app->game_address = NULL;
        if (cf_on_interval(5.0f, 0.0f))
        {
            for (s32 index = 0; index < cf_map_size(g_app->process_names); ++index)
            {
                cf_arena_reset(&g_app->process_arena);
                g_app->process = find_process(g_app->process_names[index], &g_app->process_arena);
                if (g_app->process.handle.id != 0)
                {
                    g_app->game_signature = assets_find_config_signatures(g_app->process.file_name, 
                                                                          g_app->process.version_major, g_app->process.version_minor,
                                                                          g_app->process.version_build,
                                                                          g_app->process.version_private);
                    g_app->game_address = assets_find_config_addresses(g_app->process.file_name, 
                                                                       g_app->process.version_major, g_app->process.version_minor,
                                                                       g_app->process.version_build,
                                                                       g_app->process.version_private);
                    
                    destroy_coroutine(&g_app->screen_scan.bind_inventory_location_co);
                    destroy_coroutine(&g_app->screen_scan.bind_inventory_key_co);
                    destroy_coroutine(&g_app->screen_scan.scan_region_co);
                    
                    has_loaded_new_signatures = g_app->game_signature != NULL;
                    break;
                }
            }
        }
    }
    
    // clear this every frame to avoid any stale data from game
    cf_map_clear(g_app->process_ptrs);
    
    if (is_process_alive(g_app->process))
    {
        // either game addresses has not been found / cache previously on disk
        // or this is a recently loaded game signatures and game address is going
        // to get a santity check
        if (!g_app->game_address && cf_on_interval(1.0f, 0.0f) || 
            has_loaded_new_signatures)
        {
            Config_Option* game_signature = g_app->game_signature;
            game_signature_scan(g_app->game_signature);
        }
        
        if (g_app->game_address)
        {
            game_read_state();
            
            if (g_app->auto_game_filter)
            {
                Assets* assets = &g_app->assets;
                Optional opt_game_name = config_options_get_string(g_app->game_address, "game");
                if (opt_game_name.has_value && cf_map_has(assets->regions, cf_sintern(opt_game_name.str_value)))
                {
                    cf_array_clear(g_app->game_filter);
                    cf_array_push(g_app->game_filter, cf_sintern(opt_game_name.str_value));
                }
            }
        }
        
        coroutine_escapeable_resume(&g_app->screen_scan.bind_inventory_location_co);
        coroutine_escapeable_resume(&g_app->screen_scan.bind_inventory_key_co);
        coroutine_escapeable_resume(&g_app->scan_points.draw_filter_shape_co);
        if (coroutine_escapeable_resume(&g_app->screen_scan.scan_region_co) == Coroutine_Resume_Result_Escaped)
        {
            stop_scan_region();
        }
    }
    
    // actual update
    {
        g_app->world_map.mouse_hover_region = NULL;
        
        switch (g_app->state)
        {
            case App_State_Screen_Scan:
            {
                app_update_screen_scan();
                break;
            }
            case App_State_World_Map:
            {
                app_update_world_map();
                break;
            }
        }
    }
}

void app_draw(void)
{
    cf_clear_color(0, 0, 0, 0);
    
    switch (g_app->state)
    {
        case App_State_Screen_Scan:
        {
            app_draw_screen_scan();
            break;
        }
        case App_State_World_Map:
        {
            app_draw_world_map();
            break;
        }
    }
    
    app_draw_filter_shapes();
    
    g_app->world_map.ui_hover_region = NULL;
    ui_draw();
}

void app_update_screen_scan(void)
{
    update_camera(&g_app->screen_scan.camera);
}

void app_update_world_map(void)
{
    Assets* assets = &g_app->assets;
    Camera* camera = &g_app->world_map.camera;
    
    update_camera(camera);
    
    // rebuild any dirty canvas
    for (s32 index = 0; index < cf_map_size(g_app->draw.canvases); ++index)
    {
        Canvas* canvas = g_app->draw.canvases + index;
        if (canvas->is_visible && canvas->is_draw_list_dirty)
        {
            Config* region = get_region_config(canvas->game, canvas->region_name);
            if (region)
            {
                build_draw_list(canvas, region);
            }
            
            canvas->is_draw_list_dirty = false;
        }
    }
    
    CF_V2 mouse = world_map_mouse();
    
    // check on hover rooms in world space
    if (!ui_is_hovering_any_windows())
    {
        CF_ARRAY(Canvas*) canvases = get_sorted_canvases(g_app->game_filter, cf_array_count(g_app->game_filter));
        for (s32 canvas_index = 0; canvas_index < cf_array_count(canvases); ++canvas_index)
        {
            Canvas* canvas = canvases[canvas_index];
            if (!canvas->is_visible)
            {
                continue;
            }
            
            CF_ARRAY(Config) regions = cf_map_get(assets->regions, canvas->game);
            for (s32 region_index = 0; region_index < cf_array_count(regions); ++region_index)
            {
                Config* region = regions + region_index;
                if (cf_sintern(region->tag) == canvas->region_name)
                {
                    // check per room
                    Config_Option* rooms = region->options;
                    for (s32 room_index = 0; room_index < cf_array_count(rooms); ++room_index)
                    {
                        Config_Option* room = rooms + room_index;
                        
                        Optional opt_min = config_options_get_array_float(room, "min");
                        Optional opt_max = config_options_get_array_float(room, "max");
                        
                        CF_V3 min = *(CF_V3*)opt_min.custom_value;
                        CF_V3 max = *(CF_V3*)opt_max.custom_value;
                        
                        CF_V2 min2 = cf_v2(min.x, min.z);
                        CF_V2 max2 = cf_v2(max.x, max.z);
                        
                        CF_Aabb aabb = cf_make_aabb(min2, max2);
                        
                        // only display tooltip to first one overlapping
                        if (cf_contains_point(aabb, mouse))
                        {
                            g_app->world_map.mouse_hover_region = room;
                            goto EXIT_HOVER_REGION;
                        }
                    }
                }
            }
        }
        EXIT_HOVER_REGION:
    }
    
}

void app_draw_grid(s32 grid_x, s32 grid_y, f32 thickness)
{
    CF_Aabb bounds = cf_screen_bounds_to_world();
    
    if (grid_x > 0)
    {
        f32 min_x = (f32)((s32)bounds.min.x - (s32)bounds.min.x % grid_x);
        f32 max_x = (f32)((s32)bounds.max.x + (s32)bounds.min.x % grid_x) + grid_x;
        
        for (f32 x = min_x; x < max_x; x += grid_x)
        {
            CF_V2 p0 = cf_v2(x, bounds.min.y);
            CF_V2 p1 = cf_v2(x, bounds.max.y);
            cf_draw_line(p0, p1, thickness);
        }
    }
    
    if (grid_y > 0)
    {
        f32 min_y = (f32)((s32)bounds.min.y - (s32)bounds.min.y % grid_y);
        f32 max_y = (f32)((s32)bounds.max.y + (s32)bounds.min.y % grid_y) + grid_y;
        
        for (f32 y = min_y; y < max_y; y += grid_y)
        {
            CF_V2 p0 = cf_v2(bounds.min.x, y);
            CF_V2 p1 = cf_v2(bounds.max.x, y);
            cf_draw_line(p0, p1, thickness);
        }
    }
}

void app_draw_filter_shapes(void)
{
    Camera* camera = &g_app->world_map.camera;
    if (g_app->state == App_State_Screen_Scan)
    {
        camera = &g_app->screen_scan.camera;
    }
    
    CF_ARRAY(Filter_Shape) shapes = g_app->scan_points.filter_shapes;
    CF_Color color = cf_color_white();
    CF_Color highlight_color = get_pulsing_color(color);
    
    push_camera(camera);
    
    cf_draw_push_color(color);
    // only draw game screen space filters
    if (g_app->state == App_State_Screen_Scan)
    {
        for (s32 index = 0; index < cf_array_count(shapes); ++index)
        {
            Filter_Shape shape = shapes[index];
            
            if (index == g_app->scan_points.highlight_shape_index)
            {
                cf_draw_push_color(highlight_color);
            }
            
            if (shape.is_game_screen_space)
            {
                if (shape.is_circle)
                {
                    cf_draw_circle(shape.c, 1.0f);
                }
                else
                {
                    CF_Aabb aabb = cf_make_aabb(cf_min(shape.bb.p0, shape.bb.p1), cf_max(shape.bb.p0, shape.bb.p1));
                    cf_draw_box(aabb, 1.0f, 0.0f);
                }
            }
            
            if (index == g_app->scan_points.highlight_shape_index)
            {
                cf_draw_pop_color();
            }
        }
    }
    else
    {
        // only draw world space filters
        for (s32 index = 0; index < cf_array_count(shapes); ++index)
        {
            Filter_Shape shape = shapes[index];
            
            if (index == g_app->scan_points.highlight_shape_index)
            {
                cf_draw_push_color(highlight_color);
            }
            
            if (!shape.is_game_screen_space)
            {
                if (shape.is_circle)
                {
                    cf_draw_circle(shape.c, 1.0f);
                }
                else
                {
                    CF_Aabb aabb = cf_make_aabb(cf_min(shape.bb.p0, shape.bb.p1), cf_max(shape.bb.p0, shape.bb.p1));
                    cf_draw_box(aabb, 1.0f, 0.0f);
                }
            }
            
            if (index == g_app->scan_points.highlight_shape_index)
            {
                cf_draw_pop_color();
            }
        }
    }
    
    cf_draw_pop_color();
    
    pop_camera();
}

void app_draw_screen_scan(void)
{
    Camera* camera = &g_app->screen_scan.camera;
    CF_V2 mouse = camera_mouse(camera);
    
    push_camera(camera);
    
    if (g_app->screen_scan.show_grid)
    {
        cf_draw_push_color(g_app->screen_scan.grid_color);
        app_draw_grid(g_app->screen_scan.grid_x, g_app->screen_scan.grid_y, g_app->screen_scan.grid_line_thickness);
        cf_draw_pop_color();
    }
    
    if (g_app->draw.game_screen_sprite.id)
    {
        CF_Sprite sprite = g_app->draw.game_screen_sprite;
        cf_draw_sprite(&sprite);
        CF_Aabb aabb = cf_make_aabb_center_half_extents(sprite.transform.p, cf_v2(sprite.w * 0.5f, sprite.h * 0.5f));
        cf_draw_box(aabb, 1.0f, 0.0f);
    }
    
    {
        Scan_Shape* scan_shape = &g_app->screen_scan.shape;
        if (scan_shape->is_circle)
        {
            cf_draw_circle2(scan_shape->c.p, scan_shape->c.r, 1.0f);
        }
        else
        {
            CF_Aabb aabb = cf_make_aabb(cf_min(scan_shape->bb.p0, scan_shape->bb.p1),
                                        cf_max(scan_shape->bb.p0, scan_shape->bb.p1));
            cf_draw_box(aabb, 1.0f, 0.0f);
        }
    }
    
    // draw position of current inventory item
    if (g_app->screen_scan.show_inventory_item_location)
    {
        CF_V2 p = process_position_to_screen_scan_position(g_app->screen_scan.inventory_x, g_app->screen_scan.inventory_y);
        
        cf_draw_push_color(cf_color_white());
        cf_draw_circle_fill2(p, 2.5f);
        cf_draw_text("Item", p, -1);
        cf_draw_pop_color();
    }
    
    // draw points to screen scan
    if (g_app->screen_scan.show_points)
    {
        CF_ARRAY(Scan_Point) points = get_filtered_scan_points();
        f32 r = 2.0f;
        
        CF_Color color = get_pulsing_color2(cf_color_white(), cf_color_black());
        cf_draw_push_color(color);
        for (s32 index = 0; index < cf_array_count(points); ++index)
        {
            Scan_Point point = points[index];
            CF_V2 p = process_position_to_screen_scan_position(point.x, point.y);
            cf_draw_circle2(p, r, 1.0f);
            if (cf_len_sq(cf_sub(p, mouse)) < 16.0f)
            {
                ui_push_tooltip_scan_point(point.p);
            }
        }
        cf_draw_pop_color();
    }
    
    // draw relative position from desktop space to game space
    {
        s32 x = 0;
        s32 y = 0;
        desktop_mouse_position(&x, &y);
        
        CF_V2 p = process_position_to_screen_scan_position(x, y);
        
        f32 radius = cf_sin_f((f32)CF_SECONDS * 10.0f);
        radius = cf_remap(radius, -1.0f, 1.0f, 1.0f, 5.0f);
        
        cf_draw_circle2(p, radius, 1.0f);
    }
    
    pop_camera();
    cf_render_to(cf_app_get_canvas(), true);
}

void app_draw_world_map(void)
{
    Assets* assets = &g_app->assets;
    Camera* camera = &g_app->world_map.camera;
    CF_V2 mouse = world_map_mouse();
    
    CF_ARRAY(Canvas*) canvas_order = get_sorted_canvases(NULL, 0);
    
    CF_Aabb region_size = { 0 };
    CF_Aabb main_region_size = { 0 };
    
    if (!ui_is_hovering_any_windows())
    {
        ui_push_tooltip_world_hover_point(cf_v3(mouse.x, 0, mouse.y));
    }
    
    push_camera(camera);
    
    if (g_app->world_map.show_grid)
    {
        cf_draw_push_color(g_app->world_map.grid_color);
        app_draw_grid(g_app->world_map.grid_x, g_app->world_map.grid_y, g_app->world_map.grid_line_thickness);
        cf_draw_pop_color();
    }
    
    // push static draw list to respective canvases
    {
        for (s32 order_index = 0; order_index < cf_array_count(canvas_order); ++order_index)
        {
            Canvas* canvas = canvas_order[order_index];
            
            if (canvas->is_visible)
            {
                cf_draw_list(canvas->draw_list);
                cf_render_to(canvas->canvas, true);
                
                if (cf_area_aabb(region_size) <= 1e-7f)
                {
                    region_size = canvas->aabb;
                }
                else
                {
                    region_size = cf_combine(region_size, canvas->aabb);
                }
            }
        }
    }
    pop_camera();
    
    // draw each layer
    {
        CF_V2 position = cf_v2(0, 0);
        CF_V2 scale = g_app->draw.screen_size;
        
        s32 draw_canvas_count = 0;
        f32 opacity = g_app->world_map.opacity;
        cf_draw_push_shader(g_app->draw.transparency_shader);
        for (s32 order_index = 0; order_index < cf_array_count(canvas_order); ++order_index)
        {
            Canvas* canvas = canvas_order[order_index];
            if (canvas->is_visible)
            {
                cf_draw_push_vertex_attributes(0, 0, 0, draw_canvas_count == 0 ? 1.0f : opacity);
                cf_draw_canvas(canvas->canvas, position, scale);
                cf_draw_pop_vertex_attributes();
                
                ++draw_canvas_count;
                
                if (cf_area_aabb(main_region_size) <= 1e-7f)
                {
                    main_region_size = canvas->aabb;
                }
            }
        }
        cf_draw_pop_shader();
    }
    
    push_camera(camera);
    
    // draw points to screen scan
    if (g_app->world_map.show_points)
    {
        CF_ARRAY(Scan_Point) points = get_filtered_scan_points();
        f32 r = 0.1f;
        f32 r2 = r * r;
        str8 label = make_arena_string(&g_arena, 256);
        
        CF_Color color = get_pulsing_color2(cf_color_white(), cf_color_black());
        cf_draw_push_color(color);
        for (s32 index = 0; index < cf_array_count(points); ++index)
        {
            Scan_Point point = points[index];
            CF_V2 p = cf_v2(point.p.x, point.p.z);
            cf_draw_circle2(p, r, 1.0f);
        }
        cf_draw_pop_color();
    }
    
    // level outlines
    {
        if (g_app->world_map.show_region_size)
        {
            cf_draw_box(main_region_size, 1.0f, 0.0f);
            
            CF_Color color = cf_color_white();
            color.a = g_app->world_map.opacity;
            
            cf_draw_push_color(color);
            cf_draw_box(region_size, 1.0f, 0.0f);
            cf_draw_pop_color();
        }
    }
    
    // room outline
    {
        if (g_app->world_map.mouse_hover_region)
        {
            CF_Aabb aabb = config_option_room_to_aabb(g_app->world_map.mouse_hover_region);
            cf_draw_box(aabb, 1.0f, 0.0f);
        }
        
        if (g_app->world_map.ui_hover_region)
        {
            CF_Aabb aabb = config_option_room_to_aabb(g_app->world_map.ui_hover_region);
            cf_draw_box(aabb, 1.0f, 0.0f);
        }
    }
    
    // if editing room draw that in the world map
    {
        Edit_Room room = { 0 };
        if (ui_get_edit_room(&room))
        {
            CF_V3 extents = cf_extents_aabb3(room.aabb);
            f32 volume = extents.x * extents.y * extents.z;
            
            if (volume > 1e-7f && CF_STRLEN(room.image))
            {
                CF_Aabb aabb = cf_make_aabb(cf_v2(room.aabb.min.x, room.aabb.min.z), 
                                            cf_v2(room.aabb.max.x, room.aabb.max.z));
                
                CF_Sprite* sprite_ptr = cf_map_get_ptr(assets->sprites, cf_sintern(room.image));
                if (cf_string_equ(room.image, CLIPBOARD_FILE))
                {
                    sprite_ptr = &g_app->draw.clipboard_sprite;
                }
                
                if (sprite_ptr)
                {
                    cf_draw_push_shader(g_app->draw.chroma_shader);
                    
                    CF_Sprite sprite = draw_image(*sprite_ptr, room.aabb, room.chroma);
                    cf_draw_pop_shader();
                    
                    CF_Color outline_color = get_pulsing_color(cf_color_white());
                    
                    cf_draw_push_color(outline_color);
                    cf_draw_box(aabb, 1.0f, 0.0f);
                    cf_draw_pop_color();
                }
            }
        }
    }
    
    // draw node position and fling direction
    {
        Optional opt_node_location = cf_map_get(g_app->process_ptrs, cf_sintern("node_location"));
        Optional opt_character_location = cf_map_get(g_app->process_ptrs, cf_sintern("character_location"));
        
        if (opt_character_location.has_value)
        {
            CF_V2 p = cf_v2(opt_character_location.float3_value.x, opt_character_location.float3_value.z);
            cf_draw_push_color(g_app->world_map.character_location_color);
            cf_draw_circle2(p, 3.0f, 0.5f);
            cf_draw_pop_color();
        }
        
        if (opt_node_location.has_value)
        {
            CF_V2 p = cf_v2(opt_node_location.float3_value.x, opt_node_location.float3_value.z);
            f32 r = 3.0f;
            CF_Color color = g_app->world_map.node_location_color;
            
            // draw cross hair to show where the node lines up at
            {
                cf_draw_push_color(cf_make_color_rgba_f(color.r, color.g, color.b, color.a * 0.5f));
                
                cf_draw_line(cf_v2(p.x - r, p.y), cf_v2(p.x + r, p.y), 0);
                cf_draw_line(cf_v2(p.x, p.y - r), cf_v2(p.x, p.y + r), 0);
                
                cf_draw_pop_color();
            }
            
            // draw outline circle
            cf_draw_push_color(color);
            cf_draw_circle2(p, r, 0.5f);
            cf_draw_pop_color();
        }
        
        if (opt_node_location.has_value && opt_character_location.has_value)
        {
            CF_V2 p0 = cf_v2(opt_character_location.float3_value.x, opt_character_location.float3_value.z);
            CF_V2 p1 = cf_v2(opt_node_location.float3_value.x, opt_node_location.float3_value.z);
            
            cf_draw_push_color(g_app->world_map.fling_direction_color);
            cf_draw_arrow(p0, p1, g_app->world_map.fling_arrow_thickness, g_app->world_map.fling_arrow_width);
            cf_draw_pop_color();
        }
    }
    
    pop_camera();
    
    cf_render_to(cf_app_get_canvas(), true);
}

Camera camera_defaults(void)
{
    return (Camera){ .zoom = 1.0f };
}

CF_ARRAY(Canvas*) get_sorted_canvases(const char** game_filters, s32 filter_count)
{
    CF_ARRAY(Canvas*) canvas_order = NULL;
    arena_array_fit(&g_arena, canvas_order, cf_map_size(g_app->draw.canvases));
    
    // setup layer sort order
    {
        for (s32 index = 0; index < cf_map_size(g_app->draw.canvases); ++index)
        {
            Canvas* canvas = g_app->draw.canvases + index;
            
            b32 can_add = filter_count == 0;
            for (s32 filter_index = 0; filter_index < filter_count; ++filter_index)
            {
                if (cf_string_prefix(canvas->game, game_filters[filter_index]))
                {
                    can_add = true;
                    break;
                }
            }
            
            if (can_add)
            {
                cf_array_push(canvas_order, canvas);
            }
        }
        
        SDL_qsort(canvas_order, cf_array_count(canvas_order), sizeof(*canvas_order), sort_compare_canvas);
    }
    
    return canvas_order;
}

Config* get_region_config(const char* game, const char* region_name)
{
    Config* found = NULL;
    Assets* assets = &g_app->assets;
    
    region_name = cf_sintern(region_name);
    
    CF_ARRAY(Config) regions = cf_map_get(assets->regions, cf_sintern(game));
    for (s32 index = 0; index < cf_array_count(regions); ++index)
    {
        Config* region = regions + index;
        if (region->tag == region_name)
        {
            found = region;
            break;
        }
    }
    
    return found;
}

CF_Sprite draw_image(CF_Sprite sprite, CF_Aabb3 aabb, CF_Color chroma)
{
    CF_V3 center3 = cf_center_aabb3(aabb);
    CF_V3 size3 = cf_extents_aabb3(aabb);
    
    sprite.transform.p = cf_v2(center3.x, center3.z);
    sprite.scale.x = size3.x / (f32)sprite.w;
    sprite.scale.y = size3.z / (f32)sprite.h;
    
    cf_draw_push_vertex_attributes(chroma.r, chroma.g, chroma.b, chroma.a);
    cf_draw_sprite(&sprite);
    cf_draw_pop_vertex_attributes();
    
    return sprite;
}

void build_draw_list(Canvas* canvas, Config* region)
{
    Assets* assets = &g_app->assets;
    CF_ARRAY(Config_Option) options = region->options;
    
    cf_draw_push_shader(g_app->draw.chroma_shader);
    
    cf_draw_list_begin(canvas->draw_list);
    
    CF_Aabb aabb = { 0 };
    
    for (s32 option_index = 0; option_index < cf_array_count(options); ++option_index)
    {
        Config_Option* room = options + option_index;
        
        Optional opt_image = config_options_get_string(room, "image");
        Optional opt_chroma = config_options_get_color(room, "chroma");
        Optional opt_min = config_options_get_array_float(room, "min");
        Optional opt_max = config_options_get_array_float(room, "max");
        
        if (!opt_image.has_value || opt_image.type != Optional_Type_String)
        {
            continue;
        }
        
        if (!opt_min.has_value || !opt_max.has_value)
        {
            continue;
        }
        
        CF_Sprite* sprite_ptr = cf_map_get_ptr(assets->sprites, cf_sintern(opt_image.str_value));
        if (sprite_ptr)
        {
            CF_Color chroma = cf_color_clear();
            
            if (opt_chroma.has_value && opt_chroma.type == Optional_Type_Color)
            {
                chroma.r = opt_chroma.color_value.r;
                chroma.g = opt_chroma.color_value.g;
                chroma.b = opt_chroma.color_value.b;
                chroma.a = opt_chroma.color_value.a;
            }
            
            CF_V3 min3 = *(CF_V3*)opt_min.custom_value;
            CF_V3 max3 = *(CF_V3*)opt_max.custom_value;
            CF_V2 min2 = cf_v2(min3.x, min3.z);
            CF_V2 max2 = cf_v2(max3.x, max3.z);
            
            CF_Aabb3 aabb3 = cf_make_aabb3(min3, max3);
            CF_Aabb aabb2 = cf_make_aabb(min2, max2);
            
            draw_image(*sprite_ptr, aabb3, chroma);
            
            if (cf_area_aabb(aabb2) < 1e-7f)
            {
                aabb = aabb2;
            }
            else
            {
                aabb = cf_combine(aabb, aabb2);
            }
        }
    }
    
    cf_draw_list_end();
    
    cf_draw_pop_shader();
    
    canvas->aabb = aabb;
}

CF_V2 camera_mouse(Camera* camera)
{
    push_camera(camera);
    
    CF_V2 mouse = cf_v2(cf_mouse_x(), cf_mouse_y());
    mouse = cf_screen_to_world(mouse);
    
    pop_camera();
    return mouse;
}

CF_V2 world_map_mouse(void)
{
    Camera* camera = &g_app->world_map.camera;
    
    push_camera(camera);
    
    CF_V2 mouse = cf_v2(cf_mouse_x(), cf_mouse_y());
    mouse = cf_screen_to_world(mouse);
    
    pop_camera();
    
    return mouse;
}

CF_Aabb config_option_room_to_aabb(Config_Option* room)
{
    CF_Aabb3 aabb3 = config_option_room_to_aabb3(room);
    
    CF_V2 min2 = cf_v2(aabb3.min.x, aabb3.min.z);
    CF_V2 max2 = cf_v2(aabb3.max.x, aabb3.max.z);
    
    return cf_make_aabb(min2, max2);
}

CF_Aabb3 config_option_room_to_aabb3(Config_Option* room)
{
    Optional opt_min = config_options_get_array_float(room, "min");
    Optional opt_max = config_options_get_array_float(room, "max");
    
    CF_V3 min = *(CF_V3*)opt_min.custom_value;
    CF_V3 max = *(CF_V3*)opt_max.custom_value;
    
    return cf_make_aabb3(min, max);
}

void update_sprite_data(CF_Sprite* sprite, CF_Pixel* pixels, s32 w, s32 h)
{
    if (pixels == NULL || w <= 0 || h <= 0)
    {
        return;
    }
    
    if (sprite->w != w || sprite->h != h)
    {
        // destroy old sprite since size doesn't match
        if (sprite->id)
        {
            cf_easy_sprite_unload(sprite);
        }
        // make a new sprite with pixels
        *sprite = cf_make_easy_sprite_from_pixels(pixels, w, h);
    }
    else
    {
        cf_easy_sprite_update_pixels(sprite, pixels);
    }
}

void cleanup_clipboard_file(void)
{
    mount_data_write_directory();
    mount_data_read_directory();
    if (cf_fs_file_exists(CLIPBOARD_FILE))
    {
        cf_fs_remove(CLIPBOARD_FILE);
    }
    dismount_data_directory();
}

void save_clipboard_image(void)
{
    enum
    {
        Image_Type_PNG,
        Image_Type_JPG,
        Image_Type_BMP,
    };
    
    s32 image_type = Image_Type_PNG;
    s32 size = 0;
    void* data = cf_clipboard_get_data("image/png", &size);
    
    if (!data)
    {
        data = cf_clipboard_get_data("image/jpeg", &size);
        image_type = Image_Type_JPG;
    }
    
    if (!data)
    {
        data = cf_clipboard_get_data("image/bmp", &size);
        image_type = Image_Type_BMP;
    }
    
    if (data)
    {
        CF_Image image = { 0 };
        if (image_type == Image_Type_PNG)
        {
            CF_Result image_result = cf_image_load_png_from_memory(data, size, &image);
            if (image_result.code == CF_RESULT_SUCCESS)
            {
                update_sprite_data(&g_app->draw.clipboard_sprite, image.pix, image.w, image.h);
                
                mount_data_write_directory();
                cf_image_save_png(CLIPBOARD_FILE, &image);
                dismount_data_directory();
                
                cf_image_free(&image);
            }
        }
        else if (image_type == Image_Type_JPG)
        {
            CF_Result image_result = cf_image_load_jpg_from_memory(data, size, &image);
            if (image_result.code == CF_RESULT_SUCCESS)
            {
                update_sprite_data(&g_app->draw.clipboard_sprite, image.pix, image.w, image.h);
                
                mount_data_write_directory();
                cf_image_save_png(CLIPBOARD_FILE, &image);
                dismount_data_directory();
                
                cf_image_free(&image);
            }
        }
        else if (image_type == Image_Type_BMP)
        {
            // header
            s32 offset = *(s32*)((u8*)data + 0x0A);
            // dib header
            s32 w = *(s32*)((u8*)data + 0x12);
            s32 h = *(s32*)((u8*)data + 0x16);
            u16 bpp = *(u16*)((u8*)data + 0x1C);
            
            CF_Pixel* pixels = (CF_Pixel*)((u8*)data + offset);
            // only handle 32bit bitmaps for now
            if (w > 0 && h > 0 && bpp == 32)
            {
                s32 bytes = w * h;
                // bgra -> rgba
                for (s32 index = 0; index < bytes; ++index)
                {
                    SWAP(pixels[index].r, pixels[index].b);
                }
                
                // flip y
                s32 byte_stride = (s32)(sizeof(CF_Pixel) * w);
                CF_Pixel* row = (CF_Pixel*)cf_alloc(byte_stride);
                for (s32 y = 0; y < h / 2; ++y)
                {
                    s32 y0 = y;
                    s32 y1 = h - y - 1;
                    
                    s32 i0 = y0 * w;
                    s32 i1 = y1 * w;
                    
                    CF_MEMCPY(row, pixels + i0, byte_stride);
                    CF_MEMCPY(pixels + i0, pixels + i1, byte_stride);
                    CF_MEMCPY(pixels + i1, row, byte_stride);
                }
                
                update_sprite_data(&g_app->draw.clipboard_sprite, pixels, w, h);
                
                CF_Image img = 
                {
                    .pix = pixels,
                    .w = w, 
                    .h = h,
                };
                
                mount_data_write_directory();
                cf_image_save_png(CLIPBOARD_FILE, &img);
                dismount_data_directory();
                
                cf_free(row);
            }
        }
        
        cf_free(data);
    }
}

b32 move_clipboard_file(const char* new_path)
{
    mount_data_write_directory();
    mount_data_read_directory();
    
    b32 has_file_moved = move_file(CLIPBOARD_FILE, new_path);
    
    dismount_data_directory();
    
    return has_file_moved;
}

u64 hash_edit_room(Edit_Room* room)
{
    u64 hash = cf_fnv1a(room->game, (s32)CF_STRLEN(room->game));
    hash ^= cf_fnv1a(room->region, (s32)CF_STRLEN(room->region));
    hash ^= cf_fnv1a(room->name, (s32)CF_STRLEN(room->name));
    hash ^= cf_fnv1a(room->image, (s32)CF_STRLEN(room->image));
    hash ^= cf_fnv1a(&room->aabb, sizeof(CF_Aabb3));
    hash ^= cf_fnv1a(&room->chroma, sizeof(CF_Color));
    return hash;
}

void game_screenshot(void)
{
    Process_Screenshot screenshot = process_screenshot(g_app->process, true);
    
    if (screenshot.pixels)
    {
        // update screen_scan texture
        update_sprite_data(&g_app->draw.game_screen_sprite, (CF_Pixel*)screenshot.pixels, screenshot.w, screenshot.h);
    }
    
    destroy_process_screenshot(&screenshot);
}

void game_press_inventory(void)
{
    process_focus(g_app->process);
    send_keyboard_down(CF_KEY_I);
    cf_sleep(PLATFORM_EVENT_DELAY);
    send_keyboard_up(CF_KEY_I);
}

void screen_scan_position_to_process_position(CF_V2 position, s32* out_x, s32 *out_y)
{
    CF_Sprite sprite = g_app->draw.game_screen_sprite;
    position.x += sprite.w * 0.5f;
    position.y += sprite.h * 0.5f;
    // flip position vertically here to ensure that positive y goes down
    position.y = sprite.h - position.y;
    
    CF_Rect rect = process_window_rect(g_app->process);
    if (out_x)
    {
        *out_x = rect.x + (s32)position.x;
    }
    if (out_y)
    {
        *out_y = rect.y + (s32)position.y;
    }
}

CF_V2 process_position_to_screen_scan_position(s32 x, s32 y)
{
    CF_Rect rect = process_window_rect(g_app->process);
    CF_V2 p = cf_v2(0);
    
    // transfrom from game space to screen scan tool space
    p.x = x - rect.x - rect.w * 0.5f;
    // rect.h - (y - rect.y) - rect.h * 0.5f
    // ^ flip                  ^ back to scan tool space
    p.y = rect.h * 0.5f - (y - rect.y);
    
    return p;
}

s32 screen_scan_radius_to_process_radius(f32 radius)
{
    s32 c_x = 0;
    s32 r = 0;
    screen_scan_position_to_process_position(cf_v2(0), &c_x, NULL);
    screen_scan_position_to_process_position(cf_v2(radius, 0), &r, NULL);
    
    r = r - c_x;
    
    return r;
}

f32 process_radius_to_screen_scan_radius(s32 radius)
{
    CF_V2 p0 = process_position_to_screen_scan_position(0, 0);
    CF_V2 p1 = process_position_to_screen_scan_position(radius, 0);
    
    return cf_distance(p0, p1);
}

void co_bind_inventory_location(CF_Coroutine co)
{
    while (async_get_mouse_down(CF_MOUSE_BUTTON_LEFT))
    {
        if (async_get_key_down(CF_KEY_ESCAPE))
        {
            return;
        }
        cf_coroutine_yield(co);
    }
    
    while (!async_get_mouse_down(CF_MOUSE_BUTTON_LEFT))
    {
        if (async_get_key_down(CF_KEY_ESCAPE))
        {
            return;
        }
        cf_coroutine_yield(co);
    }
    
    desktop_mouse_position(&g_app->screen_scan.inventory_x, &g_app->screen_scan.inventory_y);
}

void co_bind_inventory_key(CF_Coroutine co)
{
    CF_KeyButton button = CF_KEY_UNKNOWN;
    
    while (button == CF_KEY_UNKNOWN)
    {
        for (s32 index = 0; index < CF_KEY_COUNT; ++index)
        {
            if (index == CF_KEY_ESCAPE)
            {
                continue;
            }
            
            if (cf_key_just_pressed((CF_KeyButton)index))
            {
                button = (CF_KeyButton)index;
                break;
            }
        }
        
        cf_coroutine_yield(co);
    }
    
    if (button != CF_KEY_UNKNOWN)
    {
        g_app->screen_scan.inventory_key = button;
    }
}

void wiggle_mouse(CF_Coroutine co)
{
    s32 cur_x = 0;
    s32 cur_y = 0;
    desktop_mouse_position(&cur_x, &cur_y);
    
    s32 begin_drag_count = 5;
    while (begin_drag_count--)
    {
        send_mouse_position_absolute(cur_x + 32, cur_y);
        
        cf_sleep(PLATFORM_EVENT_DELAY);
        cf_coroutine_yield(co);
        
        send_mouse_position_absolute(cur_x, cur_y);
        cf_sleep(PLATFORM_EVENT_DELAY);
        cf_coroutine_yield(co);
    }
}

void co_scan_region(CF_Coroutine co)
{
    Scan_Shape* shape = &g_app->screen_scan.shape;
    
    process_focus(g_app->process);
    
    game_press_inventory();
    
    send_mouse_position_absolute(g_app->screen_scan.inventory_x, g_app->screen_scan.inventory_y);
    cf_coroutine_yield(co);
    
    // wiggle mouse after move to signal game that we're highlighting an item in inventory
    wiggle_mouse(co);
    send_mouse_button_down(CF_MOUSE_BUTTON_LEFT);
    
    // wiggle mouse after button down to signal we're starting a drag motion
    wiggle_mouse(co);
    
    // close inventory
    game_press_inventory();
    
    // walk from top left to bottom right for specified shape in the game
    // and record any node locations have changed
    s32 step_rate = cf_max(g_app->screen_scan.step_rate, 1);
    if (shape->is_circle)
    {
        CF_Aabb aabb = cf_make_aabb_center_half_extents(shape->c.p, cf_v2(shape->c.r, shape->c.r));
        s32 min_x = 0;
        s32 min_y = 0;
        s32 max_x = 0;
        s32 max_y = 0;
        
        screen_scan_position_to_process_position(aabb.max, &max_x, &min_y);
        screen_scan_position_to_process_position(aabb.min, &min_x, &max_y);
        
        s32 c_x = 0;
        s32 c_y = 0;
        
        screen_scan_position_to_process_position(shape->c.p, &c_x, &c_y);
        s32 r = screen_scan_radius_to_process_radius(shape->c.r);
        s32 r2 = r * r;
        
        const char* node_location_str = cf_sintern("node_location");
        
        for (s32 y = min_y; y <= max_y; y += step_rate)
        {
            for (s32 x = min_x; x <= max_x; x += step_rate)
            {
                s32 x2 = x - c_x;
                s32 y2 = y - c_y;
                x2 *= x2;
                y2 *= y2;
                s32 d2 = x2 + y2;
                
                if (d2 <= r2)
                {
                    Optional opt_old_node_location = cf_map_get(g_app->process_ptrs, node_location_str);
                    
                    send_mouse_position_absolute(x, y);
                    
                    cf_sleep(PLATFORM_EVENT_DELAY);
                    cf_coroutine_yield(co);
                    
                    Optional opt_node_location = cf_map_get(g_app->process_ptrs, node_location_str);
                    if (opt_node_location.has_value)
                    {
                        b32 can_add = true;
                        if (opt_old_node_location.has_value)
                        {
                            CF_V3 dp = cf_sub(opt_node_location.float3_value, opt_old_node_location.float3_value);
                            if (cf_len_sq_v3(dp) < 1e-7f)
                            {
                                can_add = false;
                            }
                        }
                        if (can_add)
                        {
                            scan_record_location(x, y, opt_node_location.float3_value);
                        }
                    }
                }
            }
        }
    }
    else
    {
        CF_Aabb aabb = cf_make_aabb(cf_min(shape->bb.p0, shape->bb.p1), cf_max(shape->bb.p0, shape->bb.p1));
        s32 min_x = 0;
        s32 min_y = 0;
        s32 max_x = 0;
        s32 max_y = 0;
        
        screen_scan_position_to_process_position(aabb.max, &max_x, &min_y);
        screen_scan_position_to_process_position(aabb.min, &min_x, &max_y);
        
        const char* node_location_str = cf_sintern("node_location");
        
        for (s32 y = min_y; y <= max_y; y += step_rate)
        {
            for (s32 x = min_x; x <= max_x; x += step_rate)
            {
                Optional opt_old_node_location = cf_map_get(g_app->process_ptrs, node_location_str);
                
                send_mouse_position_absolute(x, y);
                
                cf_sleep(PLATFORM_EVENT_DELAY);
                cf_coroutine_yield(co);
                
                Optional opt_node_location = cf_map_get(g_app->process_ptrs, node_location_str);
                if (opt_node_location.has_value)
                {
                    b32 can_add = true;
                    if (opt_old_node_location.has_value)
                    {
                        CF_V3 dp = cf_sub(opt_node_location.float3_value, opt_old_node_location.float3_value);
                        if (cf_len_sq_v3(dp) < 1e-7f)
                        {
                            can_add = false;
                        }
                    }
                    if (can_add)
                    {
                        scan_record_location(x, y, opt_node_location.float3_value);
                    }
                }
            }
        }
    }
    cf_coroutine_yield(co);
    
    send_mouse_button_up(CF_MOUSE_BUTTON_LEFT);
}

void co_draw_filter_shape(CF_Coroutine co)
{
    b32 is_circle = false;
    cf_coroutine_pop(co, &is_circle, sizeof(is_circle));
    
    App_State state = g_app->state;
    Camera* camera = &g_app->world_map.camera;
    if (state == App_State_Screen_Scan)
    {
        camera = &g_app->screen_scan.camera;
    }
    
    CF_V2 p0 = cf_v2(0);
    CF_V2 p1 = cf_v2(0);
    
    while (!cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
    {
        // state changed, stop drawing
        if (state != g_app->state)
        {
            return;
        }
        cf_coroutine_yield(co);
    }
    
    p0 = camera_mouse(camera);
    
    while (!cf_mouse_just_released(CF_MOUSE_BUTTON_LEFT))
    {
        // state changed, stop drawing
        if (state != g_app->state)
        {
            return;
        }
        
        CF_V2 p = camera_mouse(camera);
        push_camera(camera);
        cf_draw_push_layer(1);
        {
            if (is_circle)
            {
                cf_draw_circle2(p0, cf_distance(p0, p), 1.0f);
            }
            else
            {
                CF_Aabb aabb = cf_make_aabb(cf_min(p0, p), cf_max(p0, p));
                cf_draw_box(aabb, 1.0f, 0.0f);
            }
        }
        cf_draw_pop_layer();
        pop_camera();
        
        cf_coroutine_yield(co);
    }
    
    p1 = camera_mouse(camera);
    
    Filter_Shape shape = 
    {
        .is_game_screen_space = state == App_State_Screen_Scan,
        .is_circle = is_circle,
    };
    
    if (is_circle)
    {
        shape.c = cf_make_circle(p0, cf_distance(p0, p1));
    }
    else
    {
        shape.bb.p0 = p0;
        shape.bb.p1 = p1;
    }
    
    add_filter_shape(shape);
}

void start_bind_inventory_location(void)
{
    destroy_coroutine(&g_app->screen_scan.bind_inventory_location_co);
    g_app->screen_scan.bind_inventory_location_co = cf_make_coroutine(co_bind_inventory_location, 0, NULL);
}

void start_bind_inventory_key(void)
{
    destroy_coroutine(&g_app->screen_scan.bind_inventory_key_co);
    g_app->screen_scan.bind_inventory_key_co = cf_make_coroutine(co_bind_inventory_key, 0, NULL);
}

void start_scan_region(void)
{
    destroy_coroutine(&g_app->screen_scan.scan_region_co);
    g_app->screen_scan.scan_region_co = cf_make_coroutine(co_scan_region, 0, NULL);
    cf_array_clear(g_app->scan_points.points);
}

void stop_scan_region(void)
{
    destroy_coroutine(&g_app->screen_scan.scan_region_co);
    send_mouse_button_up(CF_MOUSE_BUTTON_LEFT);
}

void start_draw_filter_shape(b32 is_circle)
{
    destroy_coroutine(&g_app->scan_points.draw_filter_shape_co);
    g_app->scan_points.draw_filter_shape_co = cf_make_coroutine(co_draw_filter_shape, 0, NULL);
    cf_coroutine_push(g_app->scan_points.draw_filter_shape_co, &is_circle, sizeof(is_circle));
}

void stop_draw_filter_shape(void)
{
    destroy_coroutine(&g_app->scan_points.draw_filter_shape_co);
}

void scan_record_location(s32 x, s32 y, CF_V3 p)
{
    Scan_Point point =
    {
        .x = x,
        .y = y,
        .p = p,
    };
    cf_array_push(g_app->scan_points.points, point);
    mark_filter_scan_points_dirty();
}

void add_filter_shape(Filter_Shape shape)
{
    cf_array_push(g_app->scan_points.filter_shapes, shape);
    mark_filter_scan_points_dirty();
}

void clear_filter_shapes(void)
{
    cf_array_clear(g_app->scan_points.filter_shapes);
    mark_filter_scan_points_dirty();
}

// essentially this is using scene union of everything, might be neat to have it later on
// but for now it's probably more confusing than needed
//  @todo:  come back to this later if it's a requested feature, below is untested
//          all shapes and points needs to be brought down to NDC [-1, 1] space 
//          otherwise we can't compare the isosurface
#if 0
// IQ SDF
// https://iquilezles.org/articles/distfunctions2d/
f32 sd_box(CF_Aabb bb, CF_V2 p)
{
    CF_V2 half_extents = cf_half_extents(bb);
    CF_V2 center = cf_center(bb);
    CF_V2 dp = cf_sub(p, center);
    dp = cf_sub(cf_abs(dp), half_extents);
    
    return cf_max(dp.x, dp.y);
}

f32 sd_circle(CF_Circle c, CF_V2 p)
{
    CF_V2 dp = cf_sub(p, c.p);
    f32 dr = cf_len(dp) - c.r;
    
    return dr;
}

f32 sd_scene(CF_ARRAY(Filter_Shape) shapes, Scan_Point point)
{
    // world point
    CF_V2 wp = cf_v2(point.p.x, point.p.z);
    // screen point
    CF_V2 sp = process_position_to_screen_scan_position(point.x, point.y);
    
    // sdf min shape
    f32 dist = F32_MAX;
    for (s32 shape_index = 0; shape_index < cf_array_count(shapes); ++shape_index)
    {
        Filter_Shape shape = shapes[shape_index];
        if (shape.is_circle)
        {
            f32 d = shape.is_game_screen_space ? sd_circle(shape.c, wp) : sd_circle(shape.c, sp);
            dist = cf_min(d, dist);
        }
        else
        {
            CF_Aabb aabb = cf_make_aabb(cf_min(shape.bb.p0, shape.bb.p1), cf_max(shape.bb.p0, shape.bb.p1));
            f32 d = shape.is_game_screen_space ? sd_box(aabb, wp) : sd_box(aabb, sp);
            dist = cf_min(d, dist);
        }
    }
    
    return dist;
}

CF_ARRAY(Scan_Point) get_filtered_scan_points(void)
{
    if (cf_array_count(g_app->scan_points.filter_points) == 0 &&
        cf_array_count(g_app->scan_points.filter_shapes) > 0)
    {
        CF_ARRAY(Scan_Point) points = g_app->scan_points.points;
        CF_ARRAY(Filter_Shape) shapes = g_app->scan_points.filter_shapes;
        
        for (s32 point_index = 0; point_index < cf_array_count(points); ++point_index)
        {
            Scan_Point point = points[point_index];
            f32 dist = sd_scene(shapes, point);
            if (dist < 1.0f)
            {
                cf_array_push(g_app->scan_points.filter_points, point);
            }
        }
    }
    
    return g_app->scan_points.filter_points;
}
#endif

CF_ARRAY(Scan_Point) get_filtered_scan_points(void)
{
    // check if need to rebuild filter points
    if (cf_array_count(g_app->scan_points.filter_points) == 0)
    {
        if (cf_array_count(g_app->scan_points.filter_shapes) > 0)
        {
            // only checks if points are inside any of the shapes
            CF_ARRAY(Scan_Point) points = g_app->scan_points.points;
            CF_ARRAY(Filter_Shape) shapes = g_app->scan_points.filter_shapes;
            
            for (s32 point_index = 0; point_index < cf_array_count(points); ++point_index)
            {
                Scan_Point point = points[point_index];
                // world point
                CF_V2 wp = cf_v2(point.p.x, point.p.z);
                // screen point
                CF_V2 sp = process_position_to_screen_scan_position(point.x, point.y);
                
                b32 add_point = false;
                for (s32 shape_index = 0; shape_index < cf_array_count(shapes); ++shape_index)
                {
                    Filter_Shape shape = shapes[shape_index];
                    if (shape.is_circle)
                    {
                        f32 dist = shape.is_game_screen_space ? cf_distance(shape.c.p, sp) : cf_distance(shape.c.p, wp);
                        if (dist < shape.c.r)
                        {
                            add_point = true;
                            break;
                        }
                    }
                    else
                    {
                        CF_Aabb aabb = cf_make_aabb(cf_min(shape.bb.p0, shape.bb.p1), cf_max(shape.bb.p0, shape.bb.p1));
                        if (shape.is_game_screen_space)
                        {
                            if (cf_contains_point(aabb, sp))
                            {
                                add_point = true;
                                break;
                            }
                        }
                        else if (cf_contains_point(aabb, wp))
                        {
                            add_point = true;
                            break;
                        }
                    }
                }
                
                if (add_point)
                {
                    cf_array_push(g_app->scan_points.filter_points, point);
                }
            }
        }
        else
        {
            // no shapes so add in the entire point list instead
            cf_array_set(g_app->scan_points.filter_points, g_app->scan_points.points);
        }
    }
    
    return g_app->scan_points.filter_points;
}

void mark_filter_scan_points_dirty(void)
{
    cf_array_clear(g_app->scan_points.filter_points);
}