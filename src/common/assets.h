#ifndef ASSETS_H
#define ASSETS_H

typedef struct Texture
{
    CF_Texture tex;
    CF_V2 uv0;
    CF_V2 uv1;
} Texture;

typedef struct Assets
{
    // map<game, levels>
    CF_MAP(CF_ARRAY(Config)) regions;
    
    //  @todo:  probably best to change CF_Sprite into CF_Texture, this is piggy on the draw calls
    //          but this is mainly going to run only on PCs and not on any other device
    //          so we can take the draw call hit. this would mean some levels would end up being
    //          50+ but that's okay. this also avoids hitting the sprite atlas cache that causes
    //          invalidations. this would also speed things up on init time ore reload time
    //          since calls to graphics api can be done multithreaded on texture creation/deletion
    //          (maybe?).
    CF_MAP(CF_Sprite) sprites;
    
    Config game_signatures;
    Config game_addresses;
} Assets;

typedef struct Manifest
{
    Config config;
    str8 data;
} Manifest;

typedef struct Manifest_Comparison
{
    Manifest* local;
    Manifest* remote;
    CF_ARRAY(const char*) diff_list;
} Manifest_Comparison;

char* mount_get_directory_path();
void mount_root_read_directory();
void mount_root_write_directory();
void dismount_root_directory();
void mount_data_read_directory();
void mount_data_write_directory();
void dismount_data_directory();
void mount_shaders_read_directory();

void assets_load_configs(void);
void assets_unload_configs(void);

Config_Option* assets_find_config_signatures(str8 file_name, u32 version_major, u32 version_minor, u32 version_build, u32 version_private);
Config_Option* assets_find_config_addresses(str8 file_name, u32 version_major, u32 version_minor, u32 version_build, u32 version_private);

Manifest generate_manifest(void);
void destroy_manifest(Manifest* manifest);
//  @todo:  should pass in some host, port and uri rather than hard coding this
Manifest download_remote_manifest(void);

Manifest_Comparison diff_manifests(Manifest* local, Manifest* remote);
void destroy_manifest_comparison(Manifest_Comparison* manifest_cmp);
void update_assets_from_remote(Manifest_Comparison* manifest_cmp);

#endif //ASSETS_H
