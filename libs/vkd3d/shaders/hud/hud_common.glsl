#pragma vulkan_memory_model

#extension GL_KHR_memory_scope_semantics : enable
#extension GL_EXT_buffer_reference2 : enable
#extension GL_EXT_null_initializer : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable

#define HUD_MAX_BUFFERED_FRAMES             (8u)
#define HUD_MAX_FRAMETIME_RECORDS           (420u)

#define HUD_ITEM_VERSION                    (1u << 0)
#define HUD_ITEM_DRIVER                     (1u << 1)
#define HUD_ITEM_FPS                        (1u << 2)
#define HUD_ITEM_FRAMETIMES                 (1u << 3)
#define HUD_ITEM_SUBMISSIONS                (1u << 4)

/* Font metadata and glyph look-up table */
#define HUD_FONT_MAX_GLYPHS                 (127u)

struct hud_glyph_t
{
    u16vec2 texture_location;
    uint16_t packed_texture_size;
    uint16_t packed_origin;
};

struct hud_font_info_t
{
    u16vec2 texture_extent;
    uint16_t font_size;
    uint16_t packed_sdf_falloff_mono_advance;
    hud_glyph_t glyphs[HUD_FONT_MAX_GLYPHS];
};


/* Text draws and characters */
#define HUD_MAX_TEXT_DRAWS                  (512u)
#define HUD_MAX_TEXT_CHARS                  (1023u * 16u)

struct vk_draw_indirect_command_t
{
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

struct hud_text_draw_info_t
{
    i16vec2 location;
    uint16_t size;
    uint16_t text_length;
    uint32_t text_offset;
    uint32_t color;
};

struct hud_text_buffer_in_t
{
    /* Easier to work with from rendering code */
    uint32_t text_as_dwords[HUD_MAX_TEXT_CHARS / 4u];

    uint32_t char_count;
    uint32_t draw_count;
    u32vec2 reserved;

    vk_draw_indirect_command_t draw_commands[HUD_MAX_TEXT_DRAWS];
    hud_text_draw_info_t draw_infos[HUD_MAX_TEXT_DRAWS];
};

struct hud_text_buffer_out_t
{
    /* Text is allocated with at least 16 bytes per string */
    u32vec4 text_as_dquads[HUD_MAX_TEXT_CHARS / 16u];

    uint32_t char_count;
    uint32_t draw_count;
    u32vec2 reserved;

    vk_draw_indirect_command_t draw_commands[HUD_MAX_TEXT_DRAWS];
    hud_text_draw_info_t draw_infos[HUD_MAX_TEXT_DRAWS];
};

/* Per-queue submission info */
struct hud_queue_submission_stats_t
{
    uint16_t queue_tid;
    int16_t draw_index;
    uint32_t submission_count;
    uint32_t cmd_buffer_count;
    uint32_t reserved;
};

struct hud_queue_submission_record_t
{
    uint32_t queue_tid;
    uint32_t max_submission_count;
    uint32_t max_cmd_buffer_count;
    uint32_t display_submission_count;
    uint32_t display_cmd_buffer_count;
};

/* Basic frame statistics */
struct hud_stat_buffer_t
{
    uint64_t update_timestamps[HUD_MAX_BUFFERED_FRAMES];

    uint64_t refresh_timestamp;
    uint32_t refresh_update_count;

    float    display_fps;

    float    avg_frametime_ms;
    float    max_frametime_ms;
    float    frame_intervals_ms[HUD_MAX_FRAMETIME_RECORDS];
};

/* For reading */
layout(buffer_reference, buffer_reference_align = 16, scalar)
readonly buffer hud_buffer_in
{
    hud_font_info_t font;
    hud_text_buffer_in_t text;
    hud_stat_buffer_t stats;
};
