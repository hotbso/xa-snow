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
#include <ctime>
#include <array>
#include <string>
#include <mutex>
#include <future>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <stdexcept>

#include "xa-snow.h"
#include "depth_map.h"

// A note on async processing:
// Everything is synchronously fired by the flightloop so we don't need mutexes

// these 2 variables are owned and written by the main (= flightloop) thread
static bool download_active;
std::unique_ptr<DepthMap> snod_map;

// use of this variable is alternate
// If download_active:
//  true:  written by the download thread
//  false: read and written by the main thread
std::unique_ptr<DepthMap> new_snod_map;

// variable under system control
static std::future<bool> download_future;

// in:  user specified time
// out: url, cycle time, cycle num
static std::tuple<std::string, std::tm, int>
GetDownloadUrl(bool sys_time, const std::tm utime_utc)
{
    // Adjusted time considering publish delay
    std::tm ctime_utc = utime_utc;
    ctime_utc.tm_hour -= 4;
    ctime_utc.tm_min -= 25;
    std::mktime(&ctime_utc); // Normalize the time structure

    char buffer[1000];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &ctime_utc);
    LogMsg("adjusted utime_utc: %s", buffer);

    const static std::array<int, 4> cycles = {0, 6, 12, 18};
    int cycle = 0;
    for (int c : cycles) {
        if (ctime_utc.tm_hour >= c) {
            cycle = c;
        }
    }

    int adjs = 0;
    if (ctime_utc.tm_mday != utime_utc.tm_mday) {
        adjs = 24;
    }
    int forecast = (adjs + utime_utc.tm_hour - cycle) / 3 * 3;

    snprintf(buffer, sizeof(buffer), "%d%02d%02d", ctime_utc.tm_year + 1900, ctime_utc.tm_mon + 1, ctime_utc.tm_mday);
    std::string cycleDate(buffer);

    if (sys_time) {
        snprintf(buffer, sizeof(buffer), "gfs.t%02dz.pgrb2.0p25.f0%02d", cycle, forecast);
        std::string filename(buffer);
        LogMsg("NOAA Filename: '%s', %d, %d", filename.c_str(), cycle, forecast);

        snprintf(buffer, sizeof(buffer), "https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl?dir=%%2Fgfs.%s%%2F%02d%%2Fatmos&file=%s&var_SNOD=on&all_lev=on", cycleDate.c_str(), cycle, filename.c_str());
        return {buffer, ctime_utc, cycle};
    } else {
        forecast = 6; // TODO: for now
        snprintf(buffer, sizeof(buffer), "gfs.0p25.%s%02d.f0%02d.grib2", cycleDate.c_str(), cycle, forecast);
        std::string filename(buffer);
        LogMsg("GITHUB Filename: '%s', %d, %d", filename.c_str(), cycle, forecast);

        // gh limits to 1000 assets per release, so we have to split per month
        snprintf(buffer, sizeof(buffer), "https://github.com/zodiac1214/weather-data/releases/download/daily-%02d/%s", ctime_utc.tm_mon+1, filename.c_str());
        return {buffer, ctime_utc, cycle};
    }
}

static std::string
DownloadGribFile(bool sys_time, int month, int day, int hour)
{
    LogMsg("downloadGribFile: Using system time: %d, month: %d, day: %d, hour: %d", sys_time, month, day, hour);

    std::time_t now = std::time(nullptr);
    std::tm now_tm = *std::localtime(&now);
    std::time_t provided_time = now;


    if (! sys_time) {
        std::tm provided_tm = now_tm;
        provided_tm.tm_mon = month - 1; // Adjust month
        provided_tm.tm_mday = day;
        provided_tm.tm_hour = hour;
        provided_tm.tm_min = 0;
        provided_tm.tm_sec = 0;

        provided_time = std::mktime(&provided_tm);
        std::time_t twenty_four_hours_ago = now - 24 * 60 * 60;

        // Check if the provided time is not within the last 24 hours
        if (provided_time <= twenty_four_hours_ago || provided_time > now) {
            // historic mode
            int year = now_tm.tm_year;
            int current_month = now_tm.tm_mon + 1;
            int current_day = now_tm.tm_mday;
            int current_hour = now_tm.tm_hour;

            if ((month > current_month) ||
                (month == current_month && day > current_day) ||
                (month == current_month && day == current_day && hour > current_hour)) {
                // future month/day/hour -> use previous year
                year--;
            }
            provided_tm.tm_year = year;
            provided_time = std::mktime(&provided_tm);
        } else {
            LogMsg("The provided time is within the last 24 hours. Using system time.");
            sys_time = true;
            provided_time = now;
        }
    }

    // now convert provided time to UTC
    auto ptime_utc_tm = *std::gmtime(&provided_time);
    char buffer[500];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H:%M:%S", &ptime_utc_tm);
    LogMsg("provided time (UTC): %s", buffer);

    auto [url, ctime_utc_tm, cycle] = GetDownloadUrl(sys_time, ptime_utc_tm);

    // Get grib file's date date in yyyy-mm-dd format
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &ctime_utc_tm);
    char fn_buffer[500];
    snprintf(fn_buffer, sizeof(fn_buffer), "%s/%s_%d_noaa.grib2", output_dir.c_str(), buffer, cycle);
    std::string grib_file_path = fn_buffer;
    LogMsg("GRIB file path: '%s'", grib_file_path.c_str());

    // if file does not exist, download
    if (!std::filesystem::exists(grib_file_path)) {
        LogMsg("Downloading GRIB file from '%s'", url.c_str());

        std::string data;
        data.reserve(2 * 1024 * 1024);
        if (HttpGet(url, data, 10)) {
            LogMsg("GRIB File downloaded successfully");
            // save to file
            std::ofstream out (grib_file_path, std::ios::binary);
            if (!out) {
                LogMsg("Error creating GRIB file '%s'", grib_file_path.c_str());
                return "";
            }
            out.write(data.data(), data.size());
            out.close();
        } else {
            LogMsg("GRIB File download failed");
            grib_file_path = "";
        }
    }

    return grib_file_path;
}

static void
RemoveOldGribFiles(std::string file_to_keep)
{
    // posixify filenames for textual comparison
#if IBM == 1
    std::replace(file_to_keep.begin(), file_to_keep.end(), '\\', '/');
#endif

    LogMsg("Removing old grib files");
    LogMsg("File to keep: %s", file_to_keep.c_str());
    LogMsg("Grib file folder: %s", output_dir.c_str());

    try {
        for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
            auto path = entry.path().string();
#if IBM == 1
            std::replace(path.begin(), path.end(), '\\', '/');
#endif
            // Check for files with .grib extension
            if (path.find("_noaa.grib2") != std::string::npos && path.find(file_to_keep) == std::string::npos) {
                std::filesystem::remove(path);
                LogMsg("Removed: %s", path.c_str());
            }
        }
    } catch (const std::exception& e) {
        LogMsg("Error removing old grib files: %s", e.what());
    }
}

static const char *wgrib2 =
#if IBM == 1
"/WIN32wgrib2.exe";
#elif LIN == 1
"/linux-wgrib2";
#elif APL == 1
"/OSX11wgrib2";
#endif

// Runs async
static bool
DownloadAndProcessGribFile(bool sys_time, int month, int day, int hour)
{
    const char *snod_csv_name = std::getenv("USE_SNOD_CSV");

    if (NULL == snod_csv_name) {
        std::string grib_file_path = DownloadGribFile(sys_time, month, day, hour);
        if (grib_file_path.size() == 0)
            return false;

        snod_csv_name = "snod.csv";

        // export grib file to csv
        // 0:3600:0.25 means scan longitude from 0, 3600 steps with step 0.25 degree
        // -90:1800:0.25 means scan latitude from -90, 1800 steps with step 0.25 degree
        std::string cmd = "\"" + plugin_dir + "/bin" + wgrib2
            + "\" -s -lola 0:3600:0.25 -90:1800:0.25 \"" + snod_csv_name + "\" spread \"" + grib_file_path + "\" -match_fs SNOD";

        LogMsg("cmd:'%s'", cmd.c_str());
        int ex = sub_exec(cmd);
        if (ex != 0)
            return false;

        RemoveOldGribFiles(grib_file_path);
    } else
        LogMsg("Using existing snod_csv file '%s'", snod_csv_name);

    // create new snow map
    new_snod_map = std::make_unique<DepthMap>(0.25f);
    new_snod_map->load_csv(snod_csv_name);
    CreateSnowMapPng(*new_snod_map, "snow_depth.png");
    return true;
}

static bool
AsyncDownloadAndProcess(bool sys_time, int month, int day, int hour)
{
    for (int i = 0; i < 3; i++) {
        bool res = DownloadAndProcessGribFile(sys_time, month, day, hour);
        if (!res) {
            LogMsg("Download grib file failed, retry: %d", i);
        } else {
            LogMsg("Download and process grib file successfully");
            return true;
        }
    }

    LogMsg("grib download/process: all retry failed");
    return false;
}

// --------------------------------------------------------------------------------
// Logically these routines belong to xa-snow.cpp but that would create a reference
// to XPML_64 for grib_test.cpp so we leave them here

// start download in the background
void
StartAsyncDownload(bool sys_time, int month, int day, int hour)
{
    if (download_active) {
        LogMsg("Download is already in progress, request ignored");
    }

    download_future = std::async(std::launch::async, AsyncDownloadAndProcess, sys_time, month, day, hour);
    download_active = true;
}

//
// Check for download and activate the new map
// return true if download is still in progress
bool
CheckAsyncDownload()
{
    if (download_active) {
        if (std::future_status::ready != download_future.wait_for(std::chrono::seconds::zero()))
            return true;

        download_active = false;
        bool res = download_future.get();
        LogMsg("CheckAsyncDownload(): Download status: %d", res);
        snod_map = std::move(new_snod_map);     // activate the new map
    }

    return download_active;
}
