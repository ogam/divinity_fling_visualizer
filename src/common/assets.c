#include "common/assets.h"

char* mount_get_directory_path()
{
    //  mounting vfs
    char* path = cf_path_normalize(cf_fs_get_base_directory());
    char* dir = cf_path_directory_of(path);
    s32 directory_depth = 0;
    //  running from debugger
    if (cf_string_iequ(dir, "/build"))
    {
        directory_depth = 2;
    }
    //  running from debug build
    else if (cf_string_equ(dir, "/Debug") || cf_string_equ(dir, "/Release"))
    {
        directory_depth = 2;
    }
    cf_string_free(dir);
    path = cf_path_pop_n(path, directory_depth);
    
    return path;
}

void mount_root_read_directory()
{
    char* path = mount_get_directory_path();
    cf_fs_mount(path, "/", false);
    cf_string_free(path);
}

void mount_root_write_directory()
{
    char* path = mount_get_directory_path();
    cf_fs_set_write_directory(path);
    cf_string_free(path);
}

void dismount_root_directory()
{
    char* path = mount_get_directory_path();
    cf_fs_dismount(path);
    cf_string_free(path);
}

void mount_data_read_directory()
{
    char* path = mount_get_directory_path();
    cf_string_append(path, "/data");
    CF_Result result = cf_fs_mount(path, "/", false);
    if (result.code != CF_RESULT_SUCCESS)
    {
        printf("failed to mount: %s\n", result.details);
    }
    cf_string_free(path);
}

void mount_data_write_directory()
{
    char* path = mount_get_directory_path();
    cf_string_append(path, "/data");
    cf_fs_set_write_directory(path);
    cf_string_free(path);
}

void dismount_data_directory()
{
    char* path = mount_get_directory_path();
    cf_string_append(path, "/data");
    cf_fs_dismount(path);
    cf_string_free(path);
}

void mount_shaders_read_directory()
{
    char *path = mount_get_directory_path();
    cf_string_append(path, "/data/shaders");
    cf_fs_mount(path, "/", false);
    cf_string_free(path);
}

void assets_load_png(const char* path, const char* name)
{
    CF_Result result = { 0 };
    CF_Sprite sprite = cf_make_easy_sprite_from_png(path, &result);
    if (result.code == CF_RESULT_SUCCESS)
    {
        sprite.name = name;
        cf_map_set(g_app->assets.sprites, cf_sintern(name), sprite);
    }
}

void assets_load_dds(const char* path, const char* name)
{
    s32 image_width = 0;
    s32 image_height = 0;
    CF_Pixel* image_data = (CF_Pixel*)dds_load(path, 0, 3, &image_width, &image_height);
    if (image_data)
    {
        CF_Sprite sprite = cf_make_easy_sprite_from_pixels(image_data, image_width, image_height);
        dds_free(image_data);
        
        sprite.name = name;
        cf_map_set(g_app->assets.sprites, cf_sintern(name), sprite);
    }
}

typedef struct Asset_Load_Sprite_Data
{
    const char* name;
    const char* file;
    b32 is_dds;
} Asset_Load_Sprite_Data;

void assets_load_sprite_task(void* udata)
{
    Asset_Load_Sprite_Data* data = (Asset_Load_Sprite_Data*)udata;
    if (data->is_dds)
    {
        assets_load_dds(data->file, data->name);
    }
    else
    {
        assets_load_png(data->file, data->name);
    }
}

int assets_sprites_cmp(const void* a, const void* b)
{
    CF_Sprite* a_sprite = (CF_Sprite*)a;
    CF_Sprite* b_sprite = (CF_Sprite*)b;
    return CF_STRCMP(a_sprite->name, b_sprite->name);
}

void assets_load_configs(void)
{
    Assets* assets = &g_app->assets;
    
    mount_data_read_directory();
    
    str8 buffer = make_arena_string(&g_arena, 1024);
    str8 images_directory = mount_get_directory_path();
    cf_string_append(images_directory, "/data/images/");
    
    assets->game_signatures = config_load("game_signatures.txt");
    assets->game_addresses = config_load("game_addresses.txt");
    
    const char** main_directory = cf_fs_enumerate_directory("/");
    const char** main_directory_walker = main_directory;
    
    CF_ARRAY(Asset_Load_Sprite_Data) task_datas = NULL;
    cf_array_fit(task_datas, 128);
    
    // build up map<game, array<region>>
    while (*main_directory_walker)
    {
        CF_Stat path_stat = { 0 };
        cf_fs_stat(*main_directory_walker, &path_stat);
        
        // ignore any non directory and images directory
        if (path_stat.type == CF_FILE_TYPE_DIRECTORY && 
            !cf_string_equ(*main_directory_walker, "images"))
        {
            const char** levels_directory = cf_fs_enumerate_directory(*main_directory_walker);
            const char** levels_directory_walker = levels_directory;
            
            CF_ARRAY(Config) level_regions = NULL;
            cf_array_fit(level_regions, 32);
            
            while (*levels_directory_walker)
            {
                cf_string_fmt(buffer, "%s/%s/regions.txt", *main_directory_walker, *levels_directory_walker);
                Config regions = config_load(buffer);
                regions.tag = cf_sintern(*levels_directory_walker);
                printf("Loaded Region: %s\n", buffer);
                
                // walk through all region rooms to see what images needs to be loaded
                for (s32 option_index = 0; option_index < cf_array_count(regions.options); ++option_index)
                {
                    Config_Option* options = regions.options + option_index;
                    
                    Optional file = config_options_get_string(options, "image");
                    if (file.has_value)
                    {
                        if (cf_path_ext_equ(file.str_value, ".dds"))
                        {
                            Asset_Load_Sprite_Data task_data = 
                            {
                                .name = cf_sintern(file.str_value),
                                .file = arena_fmt(&g_arena, "%s%s", images_directory, file.str_value),
                                .is_dds = true,
                            };
                            cf_array_push(task_datas, task_data);
                        }
                        else if (cf_path_ext_equ(file.str_value, ".png"))
                        {
                            Asset_Load_Sprite_Data task_data = 
                            {
                                .name = cf_sintern(file.str_value),
                                .file = arena_fmt(&g_arena, "images/%s", file.str_value),
                                .is_dds = false,
                            };
                            cf_array_push(task_datas, task_data);
                        }
                    }
                }
                
                // push room to regions
                cf_array_push(level_regions, regions);
                ++levels_directory_walker;
            }
            
            cf_fs_free_enumerated_directory(levels_directory);
            
            // push game regions
            cf_map_set(assets->regions, cf_sintern(*main_directory_walker), level_regions);
        }
        
        ++main_directory_walker;
    }
    
    cf_fs_free_enumerated_directory(main_directory);
    
    cf_string_free(images_directory);
    
    printf("Loading %d sprites\n", cf_array_count(task_datas));
    for (s32 index = 0; index < cf_array_count(task_datas); ++index)
    {
        cf_threadpool_add_task(g_app->threadpool, assets_load_sprite_task, task_datas + index);
    }
    cf_threadpool_kick_and_wait(g_app->threadpool);
    
    dismount_data_directory();
    
    cf_array_free(task_datas);
    
    {
        cf_map_sort(assets->sprites, assets_sprites_cmp);
        const char** keys = (const char**)cf_map_keys(assets->sprites);
        for (s32 index = 0; index < cf_map_size(assets->sprites); ++index)
        {
            CF_Sprite* sprite = assets->sprites + index;
            printf("Loaded sprite %s (%d, %d)\n", keys[index], sprite->w, sprite->h);
        }
    }
}

void assets_unload_configs(void)
{
    Assets* assets = &g_app->assets;
    
    for (s32 game_index = 0; game_index < cf_map_size(assets->regions); ++game_index)
    {
        CF_ARRAY(Config) regions = assets->regions[game_index];
        for (s32 index = 0; index < cf_array_count(regions); ++index)
        {
            destroy_config(regions + index);
        }
        
        cf_array_free(regions);
    }
    
    cf_map_free(assets->regions);
    
    destroy_config(&assets->game_signatures);
    destroy_config(&assets->game_addresses);
    
    for (s32 index = 0; index < cf_map_size(assets->sprites); ++index)
    {
        cf_easy_sprite_unload(&assets->sprites[index]);
    }
}

Config_Option* assets_find_config_signatures(str8 file_name, u32 version_major, u32 version_minor, u32 version_build, u32 version_private)
{
    Assets* assets = &g_app->assets;
    CF_ARRAY(Config_Option) options_list = assets->game_signatures.options;
    
    b32 is_steam = false;
    {
        Process_Info* process = &g_app->process;
        for (s32 index = 0; index < cf_array_count(process->modules); ++index)
        {
            Module_Info* module = process->modules + index;
            if (CF_STRSTR(module->name, "steam"))
            {
                is_steam = true;
                break;
            }
        }
    }
    
    Config_Option* result = NULL;
    
    for (s32 index = 0; index < cf_array_count(options_list); ++index)
    {
        Config_Option* options = options_list + index;
        Optional opt_file_name = config_options_get_string(options, "process");
        Optional opt_platform_name = config_options_get_string(options, "platform");
        Optional opt_version_major = config_options_get_int(options, "version_major");
        Optional opt_version_minor = config_options_get_int(options, "version_minor");
        Optional opt_version_build = config_options_get_int(options, "version_build");
        Optional opt_version_private = config_options_get_int(options, "version_private");
        
        u32 _version_major = opt_version_major.has_value ? opt_version_major.int_value : 0;
        u32 _version_minor = opt_version_minor.has_value ? opt_version_minor.int_value : 0;
        u32 _version_build = opt_version_build.has_value ? opt_version_build.int_value : 0;
        u32 _version_private = opt_version_private.has_value ? opt_version_private.int_value : 0;
        
        if (opt_platform_name.has_value && 
            CF_STRSTR(opt_platform_name.str_value, "steam") && !is_steam)
        {
            continue;
        }
        
        if (!(opt_file_name.has_value && cf_string_equ(file_name, opt_file_name.str_value)))
        {
            continue;
        }
        
        if (_version_major <= version_major && 
            _version_minor <= version_minor && 
            _version_build <= version_build &&
            _version_private <= version_private)
        {
            result = options;
        }
    }
    
    return result;
}

Config_Option* assets_find_config_addresses(str8 file_name, u32 version_major, u32 version_minor, u32 version_build, u32 version_private)
{
    Assets* assets = &g_app->assets;
    CF_ARRAY(Config_Option) options_list = assets->game_addresses.options;
    
    b32 is_steam = false;
    {
        Process_Info* process = &g_app->process;
        for (s32 index = 0; index < cf_array_count(process->modules); ++index)
        {
            Module_Info* module = process->modules + index;
            if (CF_STRSTR(module->name, "steam"))
            {
                is_steam = true;
                break;
            }
        }
    }
    
    Config_Option* result = NULL;
    
    for (s32 index = 0; index < cf_array_count(options_list); ++index)
    {
        Config_Option* options = options_list + index;
        Optional opt_file_name = config_options_get_string(options, "process");
        Optional opt_platform_name = config_options_get_string(options, "platform");
        Optional opt_version = config_options_get_string(options, "version");
        
        u32 _version_major = 0;
        u32 _version_minor = 0;
        u32 _version_build = 0;
        u32 _version_private = 0;
        
        if (opt_platform_name.has_value && 
            CF_STRSTR(opt_platform_name.str_value, "steam") && !is_steam)
        {
            continue;
        }
        
        if (!(opt_file_name.has_value && cf_string_equ(file_name, opt_file_name.str_value)))
        {
            continue;
        }
        
        if (opt_version.has_value)
        {
            sscanf(opt_version.str_value, "%u.%u.%u.%u", &_version_major, &_version_minor, &_version_build, &_version_private);
        }
        
        if (_version_major == version_major && 
            _version_minor == version_minor && 
            _version_build == version_build &&
            _version_private == version_private)
        {
            result = options;
            break;
        }
    }
    
    return result;
}

u64 file_hash(const char* path)
{
    u64 hash = 0;
    size_t size = 0;
    void* data = cf_fs_read_entire_file_to_memory(path, &size);
    if (size)
    {
        hash = cf_fnv1a(data, (s32)size);
        cf_free(data);
    }
    return hash;
}

void generate_manifest_directory_walker(Manifest* manifest, const char* directory)
{
    char buffer[1024];
    char hash_buffer[1024];
    
    const char** sub_directory = cf_fs_enumerate_directory(directory);
    const char** sub_directory_walker = sub_directory;
    
    CF_Stat path_stat = { 0 };
    
    while (*sub_directory_walker)
    {
        CF_SNPRINTF(buffer, sizeof(buffer), "%s/%s", directory, *sub_directory_walker);
        cf_fs_stat(buffer, &path_stat);
        
        if (path_stat.type == CF_FILE_TYPE_DIRECTORY)
        {
            generate_manifest_directory_walker(manifest, buffer);
        }
        else if (path_stat.type == CF_FILE_TYPE_REGULAR)
        {
            u64 hash = file_hash(buffer);
            if (hash)
            {
                CF_SNPRINTF(hash_buffer, sizeof(hash_buffer), "%llu", hash);
                config_options_update_kv(&manifest->config, manifest->config.options, buffer, hash_buffer);
            }
        }
        
        ++sub_directory_walker;
    }
    
    cf_fs_free_enumerated_directory(sub_directory);
}


// usage
#if 0
{
    Manifest manifest = generate_manifest();
    Manifest remote_manifest = download_remote_manifest();
    Manifest_Comparison cmp = diff_manifests(&manifest, &remote_manifest);
    
    // print and select which files to update
    for (s32 index = 0; index < cf_array_count(cmp.diff_list); ++index)
    {
        printf("Remote file - %s\n", cmp.diff_list[index]);
    }
    
    update_assets_from_remote(&cmp);
    
    destroy_manifest_comparison(&cmp);
    destroy_manifest(&manifest);
}
#endif
Manifest generate_manifest(void)
{
    Manifest manifest = { 0 };
    manifest.config = config_make_empty();
    manifest.config.file = arena_fmt(&manifest.config.arena, "manifest.txt");
    config_make_options(&manifest.config);
    
    str8 buffer = make_arena_string(&g_arena, 1024);
    
    mount_data_read_directory();
    
    const char** main_directory = cf_fs_enumerate_directory("/");
    const char** main_directory_walker = main_directory;
    
    // build up manifest file hashes
    while (*main_directory_walker)
    {
        CF_Stat path_stat = { 0 };
        cf_fs_stat(*main_directory_walker, &path_stat);
        
        if (path_stat.type == CF_FILE_TYPE_DIRECTORY)
        {
            generate_manifest_directory_walker(&manifest, *main_directory_walker);
        }
        else if (cf_string_equ(*main_directory_walker, "game_signatures.txt"))
        {
            u64 hash = file_hash(*main_directory_walker);
            cf_string_fmt(buffer, "%llu", hash);
            config_options_update_kv(&manifest.config, manifest.config.options, *main_directory_walker, buffer);
        }
        
        ++main_directory_walker;
    }
    
    cf_fs_free_enumerated_directory(main_directory);
    
    // save manifest file
    if (cf_map_size(manifest.config.options->kv) > 0)
    {
        mount_data_write_directory();
        if (config_save_to_file(&manifest.config))
        {
            printf("Generated Manifest - data/%s\n", manifest.config.file);
        }
        else
        {
            printf("Failed to generated Manifest - data/%s\n", manifest.config.file);
        }
    }
    dismount_data_directory();
    
    return manifest;
}

void destroy_manifest(Manifest* manifest)
{
    if (manifest)
    {
        destroy_config(&manifest->config);
        if (manifest->data)
        {
            cf_string_free(manifest->data);
        }
        
        MEMZERO(manifest);
    }
}

Manifest download_remote_manifest(void)
{
    //  @todo:  move this to a menu bar button to check for updates
    //          do not replace immediately, only after hashes don't match
    //          probably also check per folder region?
    
    Manifest manifest = { 0 };
    {
        const char* host_name = "raw.githubusercontent.com";
        s32 port = 443;
        const char* uri = "/ogam/divinity_visualizer/refs/heads/main/data/manifest.txt";
        
        str8 remote_manifest_text = https_request(host_name, port, uri);
        
        if (remote_manifest_text)
        {
            manifest.config = config_parse(remote_manifest_text);
            manifest.data = remote_manifest_text;
        }
    }
    
    return manifest;
}

Manifest_Comparison diff_manifests(Manifest* local, Manifest* remote)
{
    Config_Option* local_options = local->config.options;
    Config_Option* remote_options = remote->config.options;
    
    Manifest_Comparison cmp =
    {
        .local = local,
        .remote = remote,
    };
    cf_array_fit(cmp.diff_list, cf_map_size(remote_options->kv));
    
    const char* remote_server_host_str = cf_sintern("remote_server_host");
    const char* remote_server_port_str = cf_sintern("remote_server_port");
    const char* remote_server_uri_str = cf_sintern("remote_server_uri");
    
    const char** keys = (const char**)cf_map_keys(remote_options->kv);
    for (s32 index = 0; index < cf_map_size(remote_options->kv); ++index)
    {
        const char* key = keys[index];
        
        if (key == remote_server_host_str || 
            key == remote_server_port_str || 
            key == remote_server_uri_str)
        {
            continue;
        }
        
        Optional opt_local_hash = config_options_get_uint64(local_options, key);
        Optional opt_remote_hash = config_options_get_uint64(remote_options, key);
        
        if (!opt_remote_hash.has_value)
        {
            continue;
        }
        
        if (!opt_local_hash.has_value ||
            opt_local_hash.uint64_value != opt_remote_hash.uint64_value)
        {
            cf_array_push(cmp.diff_list, key);
        }
    }
    
    return cmp;
}

void destroy_manifest_comparison(Manifest_Comparison* manifest_cmp)
{
    if (manifest_cmp)
    {
        cf_array_free(manifest_cmp->diff_list);
        MEMZERO(manifest_cmp);
    }
}

//  @todo:  untested, need to setup a remote server somewhere to grab all these assets
//          github/gitlab etc will be too fat for binary files
void update_assets_from_remote(Manifest_Comparison* manifest_cmp)
{
    Config_Option* remote_options = manifest_cmp->remote->config.options;
    
    const char* remote_server_host_str = cf_sintern("remote_server_host");
    const char* remote_server_port_str = cf_sintern("remote_server_port");
    const char* remote_server_uri_str = cf_sintern("remote_server_uri");
    
    Optional opt_server_host = config_options_get_string(remote_options, remote_server_host_str);
    Optional opt_server_port = config_options_get_int(remote_options, remote_server_port_str);
    Optional opt_server_uri = config_options_get_string(remote_options, remote_server_uri_str);
    
    if (!opt_server_host.has_value || 
        !opt_server_port.has_value ||
        !opt_server_uri.has_value)
    {
        return;
    }
    
    const char* server_host = opt_server_host.str_value;
    s32 server_port = opt_server_host.int_value;
    const char* server_uri = opt_server_uri.str_value;
    
    str8 uri = make_arena_string(&g_arena, 1024);
    
    mount_data_read_directory();
    mount_data_write_directory();
    
    const char** keys = (const char**)cf_map_keys(remote_options->kv);
    for (s32 index = 0; index < cf_map_size(remote_options->kv); ++index)
    {
        const char* key = keys[index];
        
        if (key == remote_server_host_str || 
            key == remote_server_port_str || 
            key == remote_server_uri_str)
        {
            continue;
        }
        
        cf_string_fmt(uri, "%s/%s", key);
        
        str8 data = https_request(server_host, server_port, uri);
        if (data)
        {
            // write to temp file to see if it was successful before overwriting existing file
            CF_Result file_write_result = cf_fs_write_entire_buffer_to_file(".temp", data, cf_string_count(data));
            if (file_write_result.code == CF_RESULT_SUCCESS)
            {
                // move temp file over to existing file
                if (cf_fs_file_exists(key))
                {
                    cf_fs_remove(key);
                }
                move_file(".temp", key);
            }
            
            cf_string_free(data);
        }
    }
    
    dismount_data_directory();
}