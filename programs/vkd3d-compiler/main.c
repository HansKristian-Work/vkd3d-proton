/*
 * Copyright 2017 Józef Kucia for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>

#include "vkd3d_common.h"
#include "vkd3d_shader.h"

#define MAX_COMPILE_OPTIONS 2

enum
{
    OPTION_HELP = CHAR_MAX + 1,
    OPTION_BUFFER_UAV,
    OPTION_OUTPUT,
    OPTION_PRINT_SOURCE_TYPES,
    OPTION_STRIP_DEBUG,
    OPTION_VERSION,
};

static const struct
{
    enum vkd3d_shader_source_type type;
    const char *name;
    const char *description;
}
source_type_info[] =
{
    {VKD3D_SHADER_SOURCE_DXBC_TPF,
        "dxbc-tpf", "A 'Tokenized Program Format' shader embedded in a DXBC container.\n"
        "            This is the format used for Direct3D shader model 4 and 5 shaders."},
};

static bool read_shader(struct vkd3d_shader_code *shader, const char *filename)
{
    struct stat st;
    void *code;
    FILE *fd;

    memset(shader, 0, sizeof(*shader));

    if (stat(filename, &st) == -1)
    {
        fprintf(stderr, "Could not stat file: '%s'.\n", filename);
        return false;
    }
    shader->size = st.st_size;

    if (!(fd = fopen(filename, "rb")))
    {
        fprintf(stderr, "Cannot open file for reading: '%s'.\n", filename);
        return false;
    }

    if (!(code = malloc(shader->size)))
    {
        fprintf(stderr, "Out of memory.\n");
        fclose(fd);
        return false;
    }
    shader->code = code;

    if (fread(code, 1, shader->size, fd) != shader->size)
    {
        fprintf(stderr, "Could not read shader bytecode from file: '%s'.\n", filename);
        free(code);
        fclose(fd);
        return false;
    }

    fclose(fd);
    return true;
}

static bool write_shader(const struct vkd3d_shader_code *shader, const char *filename)
{
    FILE *fd;

    if (!(fd = fopen(filename, "wb")))
    {
        fprintf(stderr, "Cannot open file for writing: '%s'.\n", filename);
        return false;
    }

    if (fwrite(shader->code, 1, shader->size, fd) != shader->size)
        fprintf(stderr, "Could not write shader bytecode to file: '%s'.\n", filename);

    fclose(fd);
    return true;
}

static void print_usage(const char *program_name)
{
    static const char usage[] =
        "[options...] file\n"
        "Options:\n"
        "  -h, --help            Display this information and exit.\n"
        "  --buffer-uav=<type>   Specify the buffer type to use for buffer UAV bindings.\n"
        "                        Valid values are 'buffer-texture' (default) and\n"
        "                        'storage-buffer'.\n"
        "  -o, --output=<file>   Write the output to <file>.\n"
        "  --print-source-types  Display the supported source types and exit.\n"
        "  --strip-debug         Strip debug information from the output.\n"
        "  -V, --version         Display version information and exit.\n"
        "  -x <type>             Specify the type of the source. Valid values are\n"
        "                        'dxbc-tpf' and 'none'.\n"
        "  --                    Stop option processing. Any subsequent argument is\n"
        "                        interpreted as a filename.\n";

    fprintf(stderr, "Usage: %s %s", program_name, usage);
}

struct options
{
    const char *filename;
    const char *output_filename;
    enum vkd3d_shader_source_type source_type;
    bool print_version;
    bool print_source_types;

    struct vkd3d_shader_compile_option compile_options[MAX_COMPILE_OPTIONS];
    unsigned int compile_option_count;
};

static void add_compile_option(struct options *options,
        enum vkd3d_shader_compile_option_name name, unsigned int value)
{
    struct vkd3d_shader_compile_option *o;
    unsigned int i;

    for (i = 0; i < options->compile_option_count; ++i)
    {
        o = &options->compile_options[i];

        if (o->name == name)
        {
            o->value = value;
            return;
        }
    }

    if (options->compile_option_count >= ARRAY_SIZE(options->compile_options))
    {
        fprintf(stderr, "Ignoring option.\n");
        return;
    }

    o = &options->compile_options[options->compile_option_count++];
    o->name = name;
    o->value = value;
}

static bool parse_buffer_uav(enum vkd3d_shader_compile_option_buffer_uav *buffer_uav, const char *arg)
{
    if (!strcmp(arg, "buffer-texture"))
    {
        *buffer_uav = VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV_STORAGE_TEXEL_BUFFER;
        return true;
    }

    if (!strcmp(arg, "storage-buffer"))
    {
        *buffer_uav = VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV_STORAGE_BUFFER;
        return true;
    }

    return false;
}

static enum vkd3d_shader_source_type parse_source_type(const char *source)
{
    unsigned int i;

    if (!strcmp(source, "none"))
        return VKD3D_SHADER_SOURCE_DXBC_TPF;

    for (i = 0; i < ARRAY_SIZE(source_type_info); ++i)
    {
        if (!strcmp(source, source_type_info[i].name))
            return source_type_info[i].type;
    }

    return VKD3D_SHADER_SOURCE_NONE;
}

static bool parse_command_line(int argc, char **argv, struct options *options)
{
    enum vkd3d_shader_compile_option_buffer_uav buffer_uav;
    int option;

    static struct option long_options[] =
    {
        {"help",               no_argument,       NULL, OPTION_HELP},
        {"buffer-uav",         required_argument, NULL, OPTION_BUFFER_UAV},
        {"output",             required_argument, NULL, OPTION_OUTPUT},
        {"print-source-types", no_argument,       NULL, OPTION_PRINT_SOURCE_TYPES},
        {"strip-debug",        no_argument,       NULL, OPTION_STRIP_DEBUG},
        {"version",            no_argument,       NULL, OPTION_VERSION},
        {NULL,                 0,                 NULL, 0},
    };

    memset(options, 0, sizeof(*options));
    options->source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;

    for (;;)
    {
        if ((option = getopt_long(argc, argv, "ho:Vx:", long_options, NULL)) == -1)
            break;

        switch (option)
        {
            case OPTION_BUFFER_UAV:
                if (!parse_buffer_uav(&buffer_uav, optarg))
                {
                    fprintf(stderr, "Invalid buffer UAV type '%s' specified.\n", optarg);
                    return false;
                }
                add_compile_option(options, VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV, buffer_uav);
                break;

            case OPTION_OUTPUT:
            case 'o':
                options->output_filename = optarg;
                break;

            case OPTION_PRINT_SOURCE_TYPES:
                options->print_source_types = true;
                return true;

            case OPTION_STRIP_DEBUG:
                add_compile_option(options, VKD3D_SHADER_COMPILE_OPTION_STRIP_DEBUG, 1);
                break;

            case OPTION_VERSION:
            case 'V':
                options->print_version = true;
                return true;

            case 'x':
                if ((options->source_type = parse_source_type(optarg)) == VKD3D_SHADER_SOURCE_NONE)
                {
                    fprintf(stderr, "Invalid source type '%s' specified.\n", optarg);
                    return false;
                }
                break;

            default:
                return false;
        }
    }

    if (optind >= argc)
        return false;

    options->filename = argv[argc - 1];

    return true;
}

static void print_source_types(void)
{
    const enum vkd3d_shader_source_type *source_types;
    unsigned int count, i, j;

    source_types = vkd3d_shader_get_supported_source_types(&count);
    fputs("Supported source types:\n", stdout);
    for (i = 0; i < count; ++i)
    {
        for (j = 0; j < ARRAY_SIZE(source_type_info); ++j)
        {
            if (source_types[i] == source_type_info[j].type)
            {
                fprintf(stdout, "  %s  %s\n", source_type_info[j].name, source_type_info[j].description);
                break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    struct vkd3d_shader_compile_info info;
    struct vkd3d_shader_code spirv;
    struct options options;
    char *messages;
    int ret;

    if (!parse_command_line(argc, argv, &options))
    {
        print_usage(argv[0]);
        return 1;
    }

    if (options.print_version)
    {
        const char *version = vkd3d_shader_get_version(NULL, NULL);

        fprintf(stdout, "vkd3d shader compiler version " PACKAGE_VERSION " using %s\n", version);
        return 0;
    }

    if (options.print_source_types)
    {
        print_source_types();
        return 0;
    }

    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = NULL;
    info.source_type = options.source_type;
    info.target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;
    info.options = options.compile_options;
    info.option_count = options.compile_option_count;
    info.log_level = VKD3D_SHADER_LOG_INFO;
    info.source_name = options.filename;

    if (!read_shader(&info.source, options.filename))
    {
        fprintf(stderr, "Failed to read DXBC shader.\n");
        return 1;
    }

    ret = vkd3d_shader_compile(&info, &spirv, &messages);
    if (messages)
        fputs(messages, stderr);
    vkd3d_shader_free_messages(messages);
    vkd3d_shader_free_shader_code(&info.source);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to compile DXBC shader, ret %d.\n", ret);
        return 1;
    }

    if (options.output_filename)
        write_shader(&spirv, options.output_filename);

    vkd3d_shader_free_shader_code(&spirv);
    return 0;
}
