// Config Parser
// example expected input text should look like below
#if 0
{
    my_value = thing
}
#endif

// all configs params can be requested with config_options_get_*() as Optional
// return values must be checked with `.has_value` since you won't be guarenteed to exist
// all values stored are considered as array internally within Config with single
// values extracted from first element within that array

#ifndef CONFIG_H
#define CONFIG_H

typedef enum
{
    Optional_Type_Int,
    Optional_Type_Uint,
    Optional_Type_Uint64,
    Optional_Type_Float,
    Optional_Type_Float3,
    Optional_Type_Color,
    Optional_Type_String,
    Optional_Type_Custom,
} Optional_Type;

#pragma warning(push)
#pragma warning(disable:4201)

typedef struct Optional
{
    b32 has_value;
    Optional_Type type;
    union
    {
        s32 int_value;
        u32 uint_value;
        u64 uint64_value;
        f32 float_value;
        CF_V3 float3_value;
        CF_Color color_value;
        const char* str_value;
        void* custom_value;
    };
} Optional;

#pragma warning(pop)

typedef enum
{
    Config_Lexer_Token_Type_Left_Brace,
    Config_Lexer_Token_Type_Right_Brace,
    Config_Lexer_Token_Type_Equal,
    Config_Lexer_Token_Type_New_Line,
    Config_Lexer_Token_Type_Comma,
    Config_Lexer_Token_Type_Term,
    Config_Lexer_Token_Type_EOF,
} Config_Lexer_Token_Type;

typedef struct Config_Lexer_Token
{
    const char* s;
    Config_Lexer_Token_Type type;
    s32 line;
    s32 offset;
} Config_Lexer_Token;

// each option is a block of kv pairs either as int, floats, hex uints, strings or arrays
typedef struct Config_Option
{
    // arena allocated
    CF_MAP(CF_ARRAY(const char*)) kv;
    // file_data[start_option_offset] == '{'
    s32 start_option_offset;
    // file_data[end_option_offset] == '}'
    s32 end_option_offset;
} Config_Option;

// represents entire file as a single config
// config_options are not named and are lists of blocks
typedef struct Config
{
    const char* file;
    // optional tag, user has to set this manually
    const char* tag;
    // dynamically allocated
    CF_ARRAY(Config_Option) options;
    // dynamically allocated
    CF_ARRAY(Config_Lexer_Token) tokens;
    CF_Arena arena;
} Config;

// parse from raw text passed in
Config config_parse(const char* text);
// loads config from file
Config config_load(const char* file);
// configs should be destroyed, otherwise there will be a memory leak
void destroy_config(Config* config);

// single value queries, should be checked for `.has_value`
Optional config_options_get_string(Config_Option* option, const char* name);
Optional config_options_get_int(Config_Option* option, const char* name);
Optional config_options_get_float(Config_Option* option, const char* name);
Optional config_options_get_float3(Config_Option* option, const char* name);
// values are extracted as u32 from hex input
Optional config_options_get_hex_uint(Config_Option* option, const char* name);
Optional config_options_get_uint64(Config_Option* option, const char* name);
Optional config_options_get_color(Config_Option* option, const char* name);

// array value queries, should be checked for `.has_value`
// pull an array from `.custom_value`
#if 0
Optional v = config_options_get_array_float(options, "my_floats");
dyna f32* my_floats = NULL;
if (v.has_value)
{
    my_floats = (dyna f32*)v.custom_value;
}
for (s32 index = 0; index < array_count(my_floats); ++index)
{
    printf("%.2f\n", my_floats[index]);
}
#endif

Optional config_options_get_array_string(Config_Option* option, const char* name);
Optional config_options_get_array_int(Config_Option* option, const char* name);
Optional config_options_get_array_float(Config_Option* option, const char* name);
Optional config_options_get_array_hex_uint(Config_Option* option, const char* name);
Optional config_options_get_array_hex_uint64(Config_Option* option, const char* name);

void config_options_update_kv(Config* config, Config_Option* option, const char* name, const char* value);
void config_options_update_kv_list(Config* config, Config_Option* option, const char* name, const char** values, s32 count);

Config_Option* config_make_options(Config* config);

b32 config_save_to_file(Config* config);
b32 config_options_save_to_file(Config* config, Config_Option* options);

u64 config_options_get_hash(Config_Option* option, const char* name);

#endif //CONFIG_H
