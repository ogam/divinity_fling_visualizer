CF_Arena g_arena;

void init_memory(s32 scratch_size)
{
    g_arena = cf_make_arena(16, scratch_size);
}