/*
 * Copyright 2017 Józef Kucia for CodeWeavers
 * Copyright 2020 Henri Verbeet for CodeWeavers
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

#define _GNU_SOURCE
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
    OPTION_PRINT_TARGET_TYPES,
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

static const struct
{
    enum vkd3d_shader_target_type type;
    const char *name;
    const char *description;
}
target_type_info[] =
{
    {VKD3D_SHADER_TARGET_SPIRV_BINARY,
        "spirv-binary", "A SPIR-V shader in binary form.\n"
        "                This is the format used for Vulkan shaders.\n"},
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

static bool write_shader(const struct vkd3d_shader_code *shader, FILE *f)
{
    return fwrite(shader->code, 1, shader->size, f) == shader->size;
}

static void print_usage(const char *program_name)
{
    static const char usage[] =
        "[options...] file\n"
        "Options:\n"
        "  -h, --help            Display this information and exit.\n"
        "  -b <type>             Specify the target type. Currently the only valid value\n"
        "                        is 'spirv-binary'.\n"
        "  --buffer-uav=<type>   Specify the buffer type to use for buffer UAV bindings.\n"
        "                        Valid values are 'buffer-texture' (default) and\n"
        "                        'storage-buffer'.\n"
        "  -o, --output=<file>   Write the output to <file>. If <file> is '-' or no\n"
        "                        output file is specified, output will be written to\n"
        "                        standard output.\n"
        "  --print-source-types  Display the supported source types and exit.\n"
        "  --print-target-types  Display the supported target types for the specified\n"
        "                        source type and exit.\n"
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
    enum vkd3d_shader_target_type target_type;
    bool print_version;
    bool print_source_types;
    bool print_target_types;

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

static enum vkd3d_shader_target_type parse_target_type(const char *target)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(target_type_info); ++i)
    {
        if (!strcmp(target, target_type_info[i].name))
            return target_type_info[i].type;
    }

    return VKD3D_SHADER_TARGET_NONE;
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
        {"print-target-types", no_argument,       NULL, OPTION_PRINT_TARGET_TYPES},
        {"strip-debug",        no_argument,       NULL, OPTION_STRIP_DEBUG},
        {"version",            no_argument,       NULL, OPTION_VERSION},
        {NULL,                 0,                 NULL, 0},
    };

    memset(options, 0, sizeof(*options));
    options->source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
    options->target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;

    for (;;)
    {
        if ((option = getopt_long(argc, argv, "b:ho:Vx:", long_options, NULL)) == -1)
            break;

        switch (option)
        {
            case 'b':
                if ((options->target_type = parse_target_type(optarg)) == VKD3D_SHADER_TARGET_NONE)
                {
                    fprintf(stderr, "Invalid target type '%s' specified.\n", optarg);
                    return false;
                }
                break;

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

            case OPTION_PRINT_TARGET_TYPES:
                options->print_target_types = true;
                break;

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

    if (options->print_target_types)
        return true;

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

static void print_target_types(enum vkd3d_shader_source_type source_type)
{
    const enum vkd3d_shader_target_type *target_types;
    const char *source_type_name;
    unsigned int count, i, j;

    for (i = 0; i < ARRAY_SIZE(source_type_info); ++i)
    {
        if (source_type == source_type_info[i].type)
        {
            source_type_name = source_type_info[i].name;
            break;
        }
    }

    target_types = vkd3d_shader_get_supported_target_types(source_type, &count);
    fprintf(stdout, "Supported target types for source type '%s':\n", source_type_name);
    for (i = 0; i < count; ++i)
    {
        for (j = 0; j < ARRAY_SIZE(target_type_info); ++j)
        {
            if (target_types[i] == target_type_info[j].type)
            {
                fprintf(stdout, "  %s  %s", target_type_info[j].name, target_type_info[j].description);
                break;
            }
        }
    }
}

static FILE *open_output(const char *filename, bool *close)
{
    FILE *f;

    *close = false;

    if (!filename || !strcmp(filename, "-"))
        return stdout;

    if (!(f = fopen(filename, "wb")))
    {
        fprintf(stderr, "Unable to open '%s' for writing.\n", filename);
        return NULL;
    }

    *close = true;
    return f;
}

int main(int argc, char **argv)
{
    struct vkd3d_shader_compile_info info;
    struct vkd3d_shader_code spirv;
    bool close_output = false;
    struct options options;
    char *messages;
    FILE *output;
    int fail = 1;
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

    if (options.print_target_types)
    {
        print_target_types(options.source_type);
        return 0;
    }

    if (!(output = open_output(options.output_filename, &close_output)))
        goto done;

    if (!options.output_filename && isatty(fileno(output)))
    {
        fprintf(stderr, "Output is a tty and output format is binary, exiting.\n"
                "If this is really what you intended, specify the output explicitly.\n");
        goto done;
    }

    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = NULL;
    info.source_type = options.source_type;
    info.target_type = options.target_type;
    info.options = options.compile_options;
    info.option_count = options.compile_option_count;
    info.log_level = VKD3D_SHADER_LOG_INFO;
    info.source_name = options.filename;

    if (!read_shader(&info.source, options.filename))
    {
        fprintf(stderr, "Failed to read input shader.\n");
        goto done;
    }

    ret = vkd3d_shader_compile(&info, &spirv, &messages);
    if (messages)
        fputs(messages, stderr);
    vkd3d_shader_free_messages(messages);
    vkd3d_shader_free_shader_code(&info.source);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to compile shader, ret %d.\n", ret);
        goto done;
    }

    if (!write_shader(&spirv, output))
    {
        fprintf(stderr, "Failed to write output shader.\n");
        vkd3d_shader_free_shader_code(&spirv);
        goto done;
    }

    fail = 0;
    vkd3d_shader_free_shader_code(&spirv);
done:
    if (close_output)
        fclose(output);
    return fail;
}
