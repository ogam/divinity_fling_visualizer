s32 _align_of(s32 value, s32 alignment)
{
    return (value + (alignment - 1)) & -alignment;
}

void _config_lexer_term(CF_Arena* arena, Config_Lexer_Token* token, const char* str, s32 length)
{
    s32 capacity = _align_of(length + 1, 32);
    
    str8 s = make_arena_string(arena, capacity);
    CF_MEMCPY(s, str, length);
    s[length] = '\0';
    cf_array_setlen(s, length);
    
#if 0
    char* walker = s + length - 1;
    while (walker > s)
    {
        if (!(*walker == ' ' || *walker == '\r' || *walker == '\n'))
        {
            walker[1] = '\0';
            cf_array_setlen(s, (s32)(walker - s) + 1);
            break;
        }
        walker--;
    }
#endif
    
    token->s = s;
}

Config config_make_empty(void)
{
    Config config = { 0 };
    config.arena = cf_make_arena(16, CF_KB * 512);
    
    return config;
}

Config config_parse(const char* text)
{
    Config config = config_make_empty();
    CF_ARRAY(Config_Lexer_Token) tokens = NULL;
    
    s32 total_open_blocks = 0;
    s32 total_close_blocks = 0;
    s32 max_assignments = 0;
    // pull out all tokens
    {
        Config_Lexer_Token term = { 0 };
        term.type = Config_Lexer_Token_Type_Term;
        s32 line = 1;
        s32 assignments = 0;
        const char* walker = text;
        while (*walker)
        {
            switch (*walker)
            {
                case ' ':
                case '\t':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    break;
                }
                case '{':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    
                    Config_Lexer_Token token = { 0 };
                    token.type = Config_Lexer_Token_Type_Left_Brace;
                    token.line = line;
                    token.offset = (s32)(walker - text);
                    cf_array_push(tokens, token);
                    ++total_open_blocks;
                    max_assignments = cf_max(max_assignments, assignments);
                    assignments = 0;
                    break;
                }
                case '}':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    
                    Config_Lexer_Token token = { 0 };
                    token.type = Config_Lexer_Token_Type_Right_Brace;
                    token.line = line;
                    token.offset = (s32)(walker - text);
                    cf_array_push(tokens, token);
                    ++total_close_blocks;
                    break;
                }
                case '=':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    
                    Config_Lexer_Token token = { 0 };
                    token.type = Config_Lexer_Token_Type_Equal;
                    token.line = line;
                    token.offset = (s32)(walker - text);
                    cf_array_push(tokens, token);
                    ++assignments;
                    break;
                }
                case '\r':
                case '\n':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    
                    Config_Lexer_Token token = { 0 };
                    token.type = Config_Lexer_Token_Type_New_Line;
                    token.line = line;
                    token.offset = (s32)(walker - text);
                    cf_array_push(tokens, token);
                    
                    line++;
                    
                    if (*walker == '\r')
                    {
                        ++walker;
                    }
                    
                    break;
                }
                case ',':
                {
                    if (term.s)
                    {
                        _config_lexer_term(&config.arena, &term, term.s, (s32)(walker - term.s));
                        cf_array_push(tokens, term);
                    }
                    term.s = NULL;
                    
                    Config_Lexer_Token token = { 0 };
                    token.type = Config_Lexer_Token_Type_Comma;
                    token.line = line;
                    token.offset = (s32)(walker - text);
                    cf_array_push(tokens, token);
                    break;
                }
                default:
                {
                    if (!term.s)
                    {
                        term.s = (const char*)walker;
                        term.line = line;
                        term.offset = (s32)(walker - text);
                    }
                    
                    break;
                }
            }
            
            ++walker;
        }
    }
    
    arena_array_fit(&config.arena, config.options, cf_max(total_open_blocks, total_close_blocks));
    // build up token to config option mappings
    {
        CF_ARRAY(Config_Option) option_stack = NULL;
        arena_array_fit(&g_arena, option_stack, 16);
        for (s32 index = 0; index < cf_array_count(tokens); ++index)
        {
            Config_Lexer_Token* token = tokens + index;
            if (token->type == Config_Lexer_Token_Type_Left_Brace)
            {
                Config_Option option = { 0 };
                option.start_option_offset = token->offset;
                cf_array_push(option_stack, option);
            }
            else if (token->type == Config_Lexer_Token_Type_Right_Brace)
            {
                if (cf_array_count(option_stack) > 0)
                {
                    cf_array_last(option_stack).end_option_offset = token->offset;
                    cf_array_push(config.options, cf_array_pop(option_stack));
                }
            }
            else if (token->type == Config_Lexer_Token_Type_Equal)
            {
                Config_Option* option = cf_array_count(option_stack) > 0 ? &cf_array_last(option_stack) : NULL;
                if (option)
                {
                    Config_Lexer_Token* prev = token - 1;
                    Config_Lexer_Token* next = token;
                    s32 assignment_term_count = 0;
                    
                    if (prev->type == Config_Lexer_Token_Type_Term)
                    {
                        while (next->type != Config_Lexer_Token_Type_New_Line)
                        {
                            if (next->type == Config_Lexer_Token_Type_Term)
                            {
                                ++assignment_term_count;
                            }
                            ++next;
                        }
                        
                        CF_ARRAY(const char*) value = NULL;
                        arena_array_fit(&config.arena, value, assignment_term_count);
                        
                        next = token;
                        while (next->type != Config_Lexer_Token_Type_New_Line)
                        {
                            if (next->type == Config_Lexer_Token_Type_Term)
                            {
                                cf_array_push(value, next->s);
                            }
                            ++next;
                        }
                        
                        cf_map_set(option->kv, cf_sintern(prev->s), value);
                    }
                }
            }
        }
    }
    
    config.tokens = tokens;
    
    return config;
}

Config config_load(const char* file)
{
    size_t file_size = 0;
    char* data = (char*)cf_fs_read_entire_file_to_memory(file, &file_size);
    
    Config config = { 0 };
    if (data)
    {
        config = config_parse(data);
        config.file = arena_fmt(&config.arena, file);
        
        cf_free(data);
    }
    
    return config;
}

void destroy_config(Config* config)
{
    cf_array_free(config->tokens);
    for (s32 index = 0; index < cf_array_count(config->options); ++index)
    {
        cf_map_free(config->options[index].kv);
    }
    cf_destroy_arena(&config->arena);
    MEMZERO(config);
}

Optional config_options_get_string(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_String;
            result.str_value = **value_ptr;
        }
    }
    
    return result;
}

Optional config_options_get_int(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Int;
            result.int_value = cf_string_toint(**value_ptr);
        }
    }
    
    return result;
}

Optional config_options_get_float(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Float;
            result.float_value = cf_string_tofloat(**value_ptr);
        }
    }
    
    return result;
}

Optional config_options_get_float3(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Float3;
            result.float3_value.x = cf_string_tofloat(**value_ptr);
            
            if (cf_array_count(*value_ptr) >= 1)
            {
                result.float3_value.y = cf_string_tofloat((*value_ptr)[1]);
            }
            if (cf_array_count(*value_ptr) >= 2)
            {
                result.float3_value.z = cf_string_tofloat((*value_ptr)[2]);
            }
        }
    }
    
    return result;
}

Optional config_options_get_hex_uint(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Uint;
            result.uint_value = strtoul(**value_ptr, NULL, 16);
        }
    }
    
    return result;
}

Optional config_options_get_uint64(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Uint64;
            result.uint64_value = strtoull(**value_ptr, NULL, 10);
        }
    }
    
    return result;
}

Optional config_options_get_color(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Color;
            result.color_value = cf_make_color_hex_string(**value_ptr);
        }
    }
    
    return result;
}

Optional config_options_get_array_string(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Custom;
            result.custom_value = (void*)*value_ptr;
        }
    }
    
    return result;
}

Optional config_options_get_array_int(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Custom;
            CF_ARRAY(s32) arr = NULL;
            arena_array_fit(&g_arena, arr, cf_array_count(*value_ptr));
            for (s32 index = 0; index < cf_array_count(*value_ptr); ++index)
            {
                cf_array_push(arr, cf_string_toint((*value_ptr)[index]));
            }
            result.custom_value = arr;
        }
    }
    
    return result;
}

Optional config_options_get_array_float(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Custom;
            CF_ARRAY(f32) arr = NULL;
            arena_array_fit(&g_arena, arr, cf_array_count(*value_ptr));
            for (s32 index = 0; index < cf_array_count(*value_ptr); ++index)
            {
                cf_array_push(arr, cf_string_tofloat((*value_ptr)[index]));
            }
            result.custom_value = arr;
        }
    }
    
    return result;
}

Optional config_options_get_array_hex_uint(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Custom;
            CF_ARRAY(u32) arr = NULL;
            arena_array_fit(&g_arena, arr, cf_array_count(*value_ptr));
            for (s32 index = 0; index < cf_array_count(*value_ptr); ++index)
            {
                cf_array_push(arr, strtoul((*value_ptr)[index], NULL, 16));
            }
            result.custom_value = arr;
        }
    }
    
    return result;
}

Optional config_options_get_array_hex_uint64(Config_Option* option, const char* name)
{
    Optional result = { 0 };
    
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8)*)cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            result.has_value = true;
            result.type = Optional_Type_Custom;
            CF_ARRAY(u64) arr = NULL;
            arena_array_fit(&g_arena, arr, cf_array_count(*value_ptr));
            for (s32 index = 0; index < cf_array_count(*value_ptr); ++index)
            {
                cf_array_push(arr, strtoul((*value_ptr)[index], NULL, 16));
            }
            result.custom_value = arr;
        }
    }
    
    return result;
}

void config_options_update_kv(Config* config, Config_Option* option, const char* name, const char* value)
{
    if (!option)
    {
        return;
    }
    
    name = cf_sintern(name);
    CF_ARRAY(const char*)* list = cf_map_get_ptr(option->kv, name);
    
    if (list == NULL)
    {
        CF_ARRAY(const char*) new_list = NULL;
        arena_array_fit(&config->arena, new_list, 8);
        cf_map_set(option->kv, name, new_list);
        
        list = cf_map_get_ptr(option->kv, name);
    }
    
    s32 length = (s32)CF_STRLEN(value) + 1;
    // align strings to 32 byte boundary
    s32 capacity = _align_of(length, 32);
    
    if (cf_array_count(*list))
    {
        str8 str = (str8)**list;
        // reuse string if it can fit new value
        if (cf_array_capacity(str) >= length)
        {
            cf_array_setlen(*list, 1);
            cf_string_fmt(str, "%s", value);
        }
        else
        {
            // string isn't big enough, resort to generating a new string
            cf_array_clear(*list);
        }
    }
    
    // empty list, add a new string
    if (cf_array_count(*list) == 0)
    {
        str8 copy_value = make_arena_string(&config->arena, capacity);
        cf_string_fmt(copy_value, "%s", value);
        cf_array_push(*list, copy_value);
    }
}

void config_options_update_kv_list(Config* config, Config_Option* option, const char* name, const char** values, s32 count)
{
    if (!option)
    {
        return;
    }
    
    name = cf_sintern(name);
    CF_ARRAY(const char*)* list = cf_map_get_ptr(option->kv, name);
    
    if (list == NULL)
    {
        CF_ARRAY(const char*) new_list = NULL;
        arena_array_fit(&config->arena, new_list, 8);
        cf_map_set(option->kv, name, new_list);
        
        list = cf_map_get_ptr(option->kv, name);
    }
    
    if (cf_array_capacity(*list) < count)
    {
        arena_array_fit(&config->arena, *list, count);
    }
    
    s32 write_index = 0;
    for (; write_index < count; ++write_index)
    {
        if (write_index >= cf_array_count(*list))
        {
            break;
        }
        
        s32 length = (s32)CF_STRLEN(values[write_index]);
        s32 capacity = length + 1;
        capacity = _align_of(capacity, 32);
        
        str8 str = (str8)(*list)[write_index];
        if (cf_array_capacity(str) >= length)
        {
            cf_string_fmt(str, "%s", values[write_index]);
        }
        else
        {
            str = make_arena_string(&config->arena, capacity);
            cf_string_fmt(str, "%s", values[write_index]);
            (*list)[write_index] = str;
        }
    }
    
    while (cf_array_count(*list) < count)
    {
        s32 length = (s32)CF_STRLEN(values[write_index]);
        s32 capacity = length + 1;
        capacity = _align_of(capacity, 32);
        
        str8 str = make_arena_string(&config->arena, capacity);
        cf_string_fmt(str, "%s", values[write_index]);
        cf_array_push(*list, str);
        
        ++write_index;
    }
    cf_array_setlen(*list, count);
}

Config_Option* config_make_options(Config* config)
{
    if (!config)
    {
        return NULL;
    }
    
    if (cf_array_capacity(config->options) < cf_array_count(config->options) + 1)
    {
        arena_array_fit(&config->arena, config->options, _align_of(cf_array_count(config->options) + 1, 8));
    }
    Config_Option new_options = 
    {
        .start_option_offset = -1,
        .end_option_offset = -1,
    };
    cf_array_push(config->options, new_options);
    return &cf_array_last(config->options);
}

void config_options_write_to_file(Config_Option* options, CF_File* file)
{
    CF_ASSERT(options);
    
    char buffer[1024];
    cf_fs_write(file, "{\n", 2);
    const char** keys = (const char**)cf_map_keys(options->kv);
    for (s32 index = 0; index < cf_map_size(options->kv); ++index)
    {
        s32 length = (s32)CF_SNPRINTF(buffer, sizeof(buffer), "\t%s = ", keys[index]);
        cf_fs_write(file, buffer, length);
        
        CF_ARRAY(const char*) values = options->kv[index];
        for (s32 array_index = 0; array_index < cf_array_count(values); ++array_index)
        {
            cf_fs_write(file, values[array_index], CF_STRLEN(values[array_index]));
            if (array_index + 1 < cf_array_count(values))
            {
                cf_fs_write(file, ", ", 2);
            }
        }
        cf_fs_write(file, "\n", 1);
    }
    cf_fs_write(file, "}\n", 2);
}

b32 config_save_to_file(Config* config)
{
    b32 any_saved = false;
    if (cf_fs_file_exists(config->file))
    {
        cf_fs_remove(config->file);
    }
    for (s32 index = 0; index < cf_array_count(config->options); ++index)
    {
        any_saved = any_saved || config_options_save_to_file(config, config->options + index);
    }
    return any_saved;
}

b32 config_options_save_to_file(Config* config, Config_Option* options)
{
    if (!config || !options)
    {
        return false;
    }
    
    size_t size;
    const char* old_file = (const char*)cf_fs_read_entire_file_to_memory(config->file, &size);
    
    CF_File* file = cf_fs_open_file_for_write(".temp.txt");
    
    if (file)
    {
        // offset mismatch with file, write to end instead
        if (options->start_option_offset > size || options->end_option_offset > size)
        {
            options->start_option_offset = -1; 
            options->end_option_offset = -1;
        }
        
        // write to start
        if (options->start_option_offset == 0)
        {
            config_options_write_to_file(options, file);
            size_t offset = cf_fs_tell(file);
            if (offset)
            {
                options->end_option_offset = (s32)offset - 1;
            }
            
            cf_fs_write(file, old_file, size);
        }
        else if (options->start_option_offset == -1 && options->end_option_offset == -1)
        {
            // write to end
            cf_fs_write(file, old_file, size);
            config_options_write_to_file(options, file);
        }
        else
        {
            const char* walker = old_file;
            
            s32 block_length = options->end_option_offset - options->start_option_offset;
            if (walker[options->end_option_offset - 1] == '\n')
            {
                ++block_length;
            }
            if (walker[options->end_option_offset + 1] == '\n')
            {
                ++block_length;
            }
            
            s32 write_count_0 = options->start_option_offset;
            s32 write_count_1 = (s32)size - write_count_0 - block_length;
            
            // write to start
            cf_fs_write(file, walker, write_count_0);
            walker += write_count_0 + block_length;
            
            // write to mid
            config_options_write_to_file(options, file);
            size_t offset = cf_fs_tell(file);
            if (offset)
            {
                options->end_option_offset = (s32)offset - 1;
            }
            
            // write to end
            cf_fs_write(file, walker, write_count_1);
        }
        cf_fs_close(file);
    }
    
    if (old_file)
    {
        cf_free((void*)old_file);
    }
    
    return move_file(".temp.txt", config->file);
}

u64 config_options_get_hash(Config_Option* option, const char* name)
{
    u64 hash = 0;
    if (option)
    {
        name = cf_sintern(name);
        CF_ARRAY(str8)* value_ptr = (CF_ARRAY(str8*))cf_map_get_ptr(option->kv, name);
        if (value_ptr && *value_ptr)
        {
            CF_ARRAY(str8) values = *value_ptr;
            for (s32 index = 0; index < cf_array_count(values); ++index)
            {
                hash ^= cf_fnv1a(values[index], cf_string_count(values[index]));
            }
        }
    }
    
    return hash;
}