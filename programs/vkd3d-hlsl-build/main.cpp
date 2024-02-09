/*
 * Copyright 2024 Philip Rebohle for Valve Software
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

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <queue>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <utility>

#include "dxbc_library.h"
#include "dxil_library.h"

template<typename T>
std::string from_wide_string(const T &str)
{
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); i++)
        result += char(str[i]);

    return result;
}


bool create_output_directory(const std::filesystem::path &path)
{
  return std::filesystem::is_directory(path) ||
          std::filesystem::create_directory(path);
}


/* Structure that stores compiler instances */
struct compiler_instances
{
    dxc_library dxc;
    d3dcompiler_library d3dcompiler;
};


struct shader_profile
{
    /* Full target name for DXC */
    std::wstring wide_name;
    /* Full target name for d3dcompiler */
    std::string name;
    /* Compiler arguments. For DXIL, these are passed directly to
     * dxcapi, for d3dcompiler these match fxc arguments. */
    std::vector<std::wstring> arguments;
    /* Whether or not the shader targets Shader Model 6 or above */
    bool is_dxil = false;

    template<typename string_type>
    static std::optional<shader_profile> parse(const string_type &str)
    {
        using char_type = typename string_type::value_type;

        shader_profile result;
        uint32_t idx = 0;

        /* Validate profile name until delimiter. Only ASCII
         * characters are allowed. */
        while (str[idx] && str[idx] != char_type('_'))
        {
            if (str[idx++] > char_type('\x7f'))
                return std::nullopt;
        }

        if (str[idx++] != char_type('_'))
            return std::nullopt;

        /* Valicate major version and check whether the profile is
         * DXIL (6_x) or DXBC (5_x) */
        if (str[idx] < char_type('0') || str[idx] > char_type('9'))
            return std::nullopt;

        uint8_t hi_ver = uint8_t(str[idx++] - char_type('0'));

        if (str[idx++] != char_type('_'))
            return std::nullopt;

        result.is_dxil = hi_ver >= 6;

        /* Validate minor version number */
        while (str[idx])
        {
            if (str[idx] < char_type('0') || str[idx] > char_type('9'))
                return std::nullopt;
            idx++;
        }

        if (result.is_dxil)
            result.wide_name.reserve(idx);
        else
            result.name.reserve(idx);

        /* Copy profile name to relevant string */
        for (uint32_t i = 0; i < idx; i++)
        {
            if (result.is_dxil)
              result.wide_name.push_back(str[i]);
            else
              result.name.push_back(char(str[i]));
        }

        return std::make_optional(std::move(result));
    }


    template<typename string_type>
    void add_argument(const string_type &str)
    {
        /* Handle spaces as delimiters within compiler arguments */
        using char_type = typename string_type::value_type;

        string_type prefix;
        prefix = char_type('-');

        auto delim = str.find(' ');
        arguments.push_back(prefix + str.substr(0, delim));

        while (delim != string_type::npos)
        {
            auto next = str.find(' ', delim + 1);
            arguments.push_back(str.substr(delim + 1, next - delim - 1));
            delim = next;
        }
    }
};


struct work_item
{
    std::filesystem::path in_file;
    std::filesystem::path out_file;

    /* Support one DXIL profile and one DXBC one */
    std::array<shader_profile, 2> profiles;
    uint32_t profile_count = 0;


    static std::optional<work_item> parse(std::filesystem::path file,
            const std::filesystem::path &dst_dir)
    {
        work_item result;

        /* Get rid of .hlsl extension right away */
        auto file_name = file.filename().stem().native();

        /* File names are formatted as:
         *    frog.vs_6_6.enable-16-bit-types.vs_5_1.hlsl.Gis.hlsl
         *
         * where 'frog' is used as the output file name, vs_x_y are the shader
         * profiles to compile the shader for, and all other parts are interpreted
         * as compiler options for the previously specified profile. */
        auto delim = file_name.find('.');
        result.out_file = dst_dir / file_name.substr(0, delim);
        result.out_file += ".h";

        /* Parse the file name as a sequence of shader targets delimited
         * by dots, e.g. frog.vs_6_6.vs_5_1.hlsl */
        while (delim != std::filesystem::path::string_type::npos)
        {
            auto next = file_name.find('.', delim + 1);
            auto substr = file_name.substr(delim + 1, next - delim - 1);

            auto profile = shader_profile::parse(substr);

            if (profile)
            {
                if (result.profile_count == result.profiles.size())
                {
                    std::wcerr << file << ": Too many profiles specified." << std::endl;
                    return std::nullopt;
                }

                result.profiles[result.profile_count++] = std::move(*profile);
            }
            else
            {
                if (!result.profile_count)
                {
                    std::wcerr << file << ": Found option " << substr << ", but no profile specified." << std::endl;
                    return std::nullopt;
                }

                result.profiles[result.profile_count - 1].add_argument(substr);
            }

            delim = next;
        }

        if (!result.profile_count)
        {
            std::wcerr << file << ": No profiles specified." << std::endl;
            return std::nullopt;
        }

        result.in_file = std::move(file);
        return std::make_optional(std::move(result));
    }


    bool needs_update() const
    {
        /* Check if the output file exists first, then compare access dates */
        if (!std::filesystem::is_regular_file(out_file))
            return true;

        auto in_time = std::filesystem::last_write_time(in_file);
        auto out_time = std::filesystem::last_write_time(out_file);

        return in_time > out_time;
    }


    bool compile(const compiler_instances &compilers) const
    {
        if (!create_output_directory(out_file.parent_path()))
            return false;

        /* Read entire source file into a character array */
        std::ifstream in_stream(in_file, std::ios::binary | std::ios::ate);

        if (!in_stream.is_open())
        {
            std::wcerr << in_file << ": Failed to open file." << std::endl;
            return false;
        }

        std::vector<char> code(in_stream.tellg());
        in_stream.seekg(0, std::ios::beg);

        if (!in_stream.read(code.data(), code.size()))
        {
            std::wcerr << in_file << ": Failed to read file." << std::endl;
            return false;
        }

        /* Source file name, used by both DXC and d3dcompiler for
         * error reporting, but in different character encodings */
        auto source_name_wide = in_file.filename().native();
        auto source_name = from_wide_string(source_name_wide);

        /* Accumulate data in a string stream first, so that we can
         * discard it and not alter the output file's time stamp in
         * case any profile fails to compile. */
        std::stringstream header_stream;

        for (uint32_t i = 0; i < profile_count; i++)
        {
            std::vector<char> binary;
            bool status;

            if (profiles[i].is_dxil)
                status = compilers.dxc.compile(code, in_file.filename().c_str(),
                        profiles[i].wide_name.data(), L"main", profiles[i].arguments, binary);
            else
                status = compilers.d3dcompiler.compile(code, source_name.c_str(),
                        profiles[i].name.data(), "main", profiles[i].arguments, binary);

            if (!status)
                return false;

            emit_binary(header_stream, binary, profiles[i].is_dxil);
        }

        /* Compiling will have succeeded at this point, write the output. */
        std::ofstream out_stream(out_file, std::ios::trunc | std::ios::binary);

        if (!out_stream.is_open())
        {
            std::wcerr << out_file << ": Failed to create file." << std::endl;
            return false;
        }

        if (!(out_stream << header_stream.str()))
        {
            std::wcerr << out_file << ": Failed to write file." << std::endl;
            return false;
        }

        return true;
    }

private:

    void emit_binary(std::ostream &out_stream, const std::vector<char> &binary, bool is_dxil) const
    {
        const char *var_suffix = is_dxil ? "dxil" : "dxbc";
        std::string var_name = from_wide_string(out_file.stem().native());

        out_stream << "static const " << (is_dxil ? "BYTE" : "DWORD")
                   << " " << var_name << "_code_" << var_suffix << "[] =\n{\n";

        /* Stick to the usual formatting */
        size_t unit_size = is_dxil ? 1 : 4;
        size_t row_length = 32;

        for (size_t i = 0; i < binary.size(); i += row_length)
        {
            out_stream << "   ";

            for (size_t j = 0; j < row_length / unit_size; j++)
            {
                size_t offset = i + j * unit_size;

                /* Handle lines that are less than the maximum length */
                if (offset >= binary.size())
                    break;

                uint32_t unit = 0u;
                std::memcpy(&unit, &binary[offset], unit_size);
                out_stream << " 0x" << std::hex << std::setfill('0') << std::setw(2 * unit_size) << unit << ",";
            }

            out_stream << "\n";
        }

        out_stream << "};\n";
        out_stream << "#ifdef __GNUC__\n";
        out_stream << "#define UNUSED_ARRAY_ATTR __attribute__((unused))\n";
        out_stream << "#else\n";
        out_stream << "#define UNUSED_ARRAY_ATTR\n";
        out_stream << "#endif\n";
        out_stream << "UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE " << var_name << "_" << var_suffix
                   << " = { " << var_name << "_code_" << var_suffix << ", "
                   << "sizeof(" << var_name << "_code_" << var_suffix << ") };\n";
        out_stream << "#undef UNUSED_ARRAY_ATTR\n";
    }

};


/* Simple worker thread system that spawns up to one thread
 * per available CPU core on demand in case a large number
 * of files needs updating. */
class thread_manager
{
public:

    thread_manager()
    : m_max_threads(std::thread::hardware_concurrency())
    {
        m_threads.reserve(m_max_threads);
    }

    /* Queues up work item and spawns a thread if necessary */
    void add_item(work_item &&item)
    {
        if (m_threads.size() < m_max_threads)
            m_threads.emplace_back([this] { run(); });

        std::lock_guard lock(m_mutex);
        m_work_items.push(std::move(item));
        m_cond.notify_one();
    }

    /* Waits for all threads to finish and returns false
     * if any queued up compile job has failed. */
    bool stop()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
            m_cond.notify_all();
        }

        for (auto &t : m_threads)
            t.join();

        m_threads.clear();
        return m_status.load();
    }

private:

    compiler_instances        m_compilers;

    std::atomic<bool>         m_status = { true };

    std::mutex                m_mutex;
    std::condition_variable   m_cond;
    std::queue<work_item>     m_work_items;
    std::vector<std::thread>  m_threads;
    bool                      m_stop = false;

    uint32_t                  m_max_threads = 0;

    void run()
    {
        while (true)
        {
            std::unique_lock lock(m_mutex);

            m_cond.wait(lock, [this]
            {
                return m_stop || !m_work_items.empty();
            });

            /* Ensure queue is drained before exiting */
            if (m_work_items.empty())
                return;

            work_item item = std::move(m_work_items.front());
            m_work_items.pop();

            lock.unlock();

            if (!item.compile(m_compilers))
                m_status.store(false);
        }
    }

};


/* Scans shader files in the given source directory and queues up any
 * file for compiling if it has been updated more recently than its
 * corresponding output file. */
bool build_shader_files(const std::filesystem::path &src_dir,
        const std::filesystem::path &dst_dir, bool rebuild_all)
{
    thread_manager threads;
    bool error = false;

    for (const auto &file : std::filesystem::directory_iterator(src_dir))
    {
        if (!file.is_regular_file())
            continue;

        const auto &file_path = file.path();

        if (file_path.extension() != ".hlsl")
            continue;

        auto item = work_item::parse(file_path, dst_dir);

        if (!item)
        {
            error = true;
            continue;
        }

        if (rebuild_all || item->needs_update())
            threads.add_item(std::move(*item));
    }

    return threads.stop() && !error;
}


int main(int argc, char **argv)
{
    std::filesystem::path src_dir = std::filesystem::current_path();
    std::filesystem::path dst_dir;
    bool rebuild_all = false;

    for (int i = 1; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "-a"))
        {
            rebuild_all = true;
        }
        else if (!std::strcmp(argv[i], "-t"))
        {
            if (i + 1 >= argc)
            {
                std::wcerr << "-t: No target directory specified." << std::endl;
                return 1;
            }

            dst_dir = argv[++i];
        }
        else if (!std::strcmp(argv[i], "--help"))
        {
            std::wcout << "Usage: " << argv[0] << " [-a] [-t output_dir] [input_dir]" << std::endl;
            return 0;
        }
        else if (argv[i][0] != '-')
        {
            src_dir = argv[i];
        }
    }

    if (dst_dir.empty())
        dst_dir = src_dir / "headers";

    if (!std::filesystem::is_directory(src_dir))
    {
        std::wcerr << "Input directory '" << src_dir << "' does not exist." << std::endl;
        return 1;
    }

    if (!create_output_directory(dst_dir))
    {
        std::wcerr << "Failed to create target directory '" << dst_dir << "'." << std::endl;
        return 1;
    }

    return build_shader_files(src_dir, dst_dir, rebuild_all) ? 0 : 1;
}
