//
//    X Airline Snow: show accumulated snow in X-Plane's world
//
//    Copyright (C) 2025  Zodiac1214
//    Copyright (C) 2025  Holger Teutsch
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Lesser General Public
//    License as published by the Free Software Foundation; either
//    version 2.1 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
//    USA
//

#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include <string>
#include <memory>

#if IBM == 1
#include <windows.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "xa-snow.h"
#include "wgrib2_lib.h"

namespace {

using Wgrib2Fn = int (*)(int argc, char **argv);

#if IBM == 1
using NativeLibHandle = HMODULE;
#else
using NativeLibHandle = void *;
#endif

class SharedLib {
public:
    SharedLib() = default;
    ~SharedLib() { close(); }

    bool open(const std::string& path) {
        close();
#if IBM == 1
        handle_ = LoadLibraryA(path.c_str());
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW);
#endif
        return handle_ != nullptr;
    }

    void *symbol(const char *name) const {
        if (handle_ == nullptr)
            return nullptr;
#if IBM == 1
        return reinterpret_cast<void *>(GetProcAddress(handle_, name));
#else
        return dlsym(handle_, name);
#endif
    }

    void close() {
        if (handle_ == nullptr)
            return;
#if IBM == 1
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

private:
    NativeLibHandle handle_ = nullptr;
};

class StdoutCapture {
public:
    bool start() {
        if (started_)
            return false;

        std::fflush(stdout);
        file_ = std::tmpfile();
        if (file_ == nullptr) {
            LogMsg("Failed to create temporary file for stdout capture");
            return false;
        }

#if IBM == 1
        saved_fd_ = _dup(_fileno(stdout));
        if (saved_fd_ < 0) {
            std::fclose(file_);
            file_ = nullptr;
            LogMsg("Failed to duplicate stdout");
            return false;
        }

        if (_dup2(_fileno(file_), _fileno(stdout)) != 0) {
            _close(saved_fd_);
            saved_fd_ = -1;
            std::fclose(file_);
            file_ = nullptr;
            LogMsg("Failed to redirect stdout");
            return false;
        }
#else
        saved_fd_ = dup(fileno(stdout));
        if (saved_fd_ < 0) {
            std::fclose(file_);
            file_ = nullptr;
            LogMsg("Failed to duplicate stdout");
            return false;
        }

        if (dup2(fileno(file_), fileno(stdout)) < 0) {
            close(saved_fd_);
            saved_fd_ = -1;
            std::fclose(file_);
            file_ = nullptr;
            LogMsg("Failed to redirect stdout");
            return false;
        }
#endif

        started_ = true;
        return true;
    }

    bool stop(std::string& output) {
        output.clear();
        if (!started_)
            return false;

        std::fflush(stdout);

#if IBM == 1
        if (_dup2(saved_fd_, _fileno(stdout)) != 0) {
            LogMsg("Failed to restore stdout");
        }
        _close(saved_fd_);
#else
        if (dup2(saved_fd_, fileno(stdout)) < 0) {
            LogMsg("Failed to restore stdout");
        }
        close(saved_fd_);
#endif
        saved_fd_ = -1;

        std::rewind(file_);
        std::array<char, 8192> buffer{};
        while (true) {
            size_t n = std::fread(buffer.data(), 1, buffer.size(), file_);
            if (n == 0)
                break;
            output.append(buffer.data(), n);
        }

        std::fclose(file_);
        file_ = nullptr;
        started_ = false;
        return true;
    }

    ~StdoutCapture() {
        if (started_) {
            std::string ignored;
            stop(ignored);
        } else if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

private:
    bool started_ = false;
    FILE *file_ = nullptr;
    int saved_fd_ = -1;
};

std::string DefaultLibName() {
#if IBM == 1
    return "wgrib2.dll";
#elif LIN == 1
    return "libwgrib2.so";
#elif APL == 1
    return "libwgrib2.dylib";
#else
    return "libwgrib2.so";
#endif
}

bool LoadWgrib2(SharedLib& lib, Wgrib2Fn& fn_out) {
    fn_out = nullptr;

    const char *env_lib = std::getenv("WGRIB2_LIB");
    std::vector<std::string> candidates;
    if (env_lib != nullptr && *env_lib != '\0')
        candidates.emplace_back(env_lib);

    candidates.emplace_back(plugin_dir + "/bin/" + DefaultLibName());
    candidates.emplace_back(DefaultLibName());

    for (const std::string& path : candidates) {
        if (!lib.open(path))
            continue;

        fn_out = reinterpret_cast<Wgrib2Fn>(lib.symbol("wgrib2"));
        if (fn_out != nullptr) {
            LogMsg("Loaded wgrib2 shared library: '%s'", path.c_str());
            return true;
        }

        lib.close();
    }

    LogMsg("Unable to load wgrib2 shared library. Set WGRIB2_LIB to explicit path.");
    return false;
}

} // namespace

bool
Wgrib2ExportSnodCsv(const std::string& grib_file_path, std::string& csv_output)
{
    csv_output.clear();

    SharedLib lib;
    Wgrib2Fn wgrib2_fn = nullptr;
    if (!LoadWgrib2(lib, wgrib2_fn))
        return false;

    std::vector<std::string> args_storage = {
        "wgrib2",
        "-s",
        "-lola", "0:3600:0.25", "-90:1800:0.25", "-", "spread",
        grib_file_path,
        "-match_fs", "SNOD"
    };

    std::vector<char *> argv;
    argv.reserve(args_storage.size());
    for (std::string& arg : args_storage) {
        argv.push_back(arg.data());
    }

    StdoutCapture capture;
    if (!capture.start())
        return false;

    int rc = wgrib2_fn((int)argv.size(), argv.data());
    capture.stop(csv_output);

    if (rc != 0) {
        LogMsg("wgrib2 library call failed, rc=%d", rc);
        return false;
    }

    if (csv_output.empty()) {
        LogMsg("wgrib2 library call succeeded but no CSV output was captured");
        return false;
    }

    return true;
}
