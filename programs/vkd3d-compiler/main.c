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

#define MAX_COMPILE_OPTIONS 1

enum
{
    OPTION_HELP = CHAR_MAX + 1,
    OPTION_OUTPUT,
    OPTION_STRIP_DEBUG,
    OPTION_VERSION,
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
        "  -h, --help           Display this information and exit.\n"
        "  -o, --output=<file>  Write the output to <file>.\n"
        "  --strip-debug        Strip debug information from the output.\n"
        "  -V, --version        Display version information and exit.\n"
        "  -x <type>            Specify the type of the source. Valid values are\n"
        "                       'dxbc-tpf' and 'none'.\n"
        "  --                   Stop option processing. Any subsequent argument is\n"
        "                       interpreted as a filename.\n";

    fprintf(stderr, "Usage: %s %s", program_name, usage);
}

struct options
{
    const char *filename;
    const char *output_filename;
    enum vkd3d_shader_source_type source_type;
    bool print_version;

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

static enum vkd3d_shader_source_type parse_source_type(const char *source)
{
    if (!strcmp(source, "dxbc-tpf") || !strcmp(source, "none"))
        return VKD3D_SHADER_SOURCE_DXBC_TPF;

    return VKD3D_SHADER_SOURCE_NONE;
}

static bool parse_command_line(int argc, char **argv, struct options *options)
{
    int option;

    static struct option long_options[] =
    {
        {"help",        no_argument,       NULL, OPTION_HELP},
        {"output",      required_argument, NULL, OPTION_OUTPUT},
        {"strip-debug", no_argument,       NULL, OPTION_STRIP_DEBUG},
        {"version",     no_argument,       NULL, OPTION_VERSION},
        {NULL,          0,                 NULL, 0},
    };

    memset(options, 0, sizeof(*options));
    options->source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;

    for (;;)
    {
        if ((option = getopt_long(argc, argv, "ho:Vx:", long_options, NULL)) == -1)
            break;

        switch (option)
        {
            case OPTION_OUTPUT:
            case 'o':
                options->output_filename = optarg;
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

    if (optind >= argc)
        return false;

    options->filename = argv[argc - 1];

    return true;
}

int main(int argc, char **argv)
{
    struct vkd3d_shader_compile_info info;
    struct vkd3d_shader_code spirv;
    struct options options;
    int ret;

    if (!parse_command_line(argc, argv, &options))
    {
        print_usage(argv[0]);
        return 1;
    }

    if (options.print_version)
    {
        fprintf(stdout, "vkd3d shader compiler version " PACKAGE_VERSION "\n");
        return 0;
    }

    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = NULL;
    info.source_type = options.source_type;
    info.target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;
    info.options = options.compile_options;
    info.option_count = options.compile_option_count;

    if (!read_shader(&info.source, options.filename))
    {
        fprintf(stderr, "Failed to read DXBC shader.\n");
        return 1;
    }

    ret = vkd3d_shader_compile(&info, &spirv);
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
