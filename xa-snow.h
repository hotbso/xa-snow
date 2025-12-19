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

#ifndef _XA_SNOW_H_
#define _XA_SNOW_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <numbers>
#include <memory>

#include "XPLMDataAccess.h"
#include "XPLMScenery.h"

#include "log_msg.h"
#include "http_get.h"

static constexpr float kD2R = std::numbers::pi/180.0;
static constexpr float kLat2m = 111120;                 // 1° lat in m
static constexpr float kF2M = 0.3048;                   // 1 ft [m]

extern XPLMDataRef plane_lat_dr, plane_lon_dr, plane_elevation_dr, plane_y_agl_dr;

extern XPLMProbeInfo_t probeinfo;
extern XPLMProbeRef probe_ref;

extern std::string xp_dir;
extern std::string plugin_dir;
extern std::string output_dir;

// functions
extern "C" bool HttpGet(const char *url, FILE *f, int timeout);
extern int sub_exec(const std::string& command);

void StartAsyncDownload(bool sys_time, int day, int month, int hour);
bool CheckAsyncDownload();

class DepthMap;

extern std::unique_ptr<DepthMap> snod_map, new_snod_map;
extern std::tuple<float, float, float> SnowDepthToXplaneSnowNow(float depth); // snowNow, snowAreaWidth, iceNow

// -> 0 = success
extern int CreateSnowMapPng(DepthMap& snod_map, const std::string& png_path);
extern int SaveImagePng(uint32_t *data, int width, int height, const std::string& png_path);

// map_layer.cpp
extern void MapLayerStartHook(void);
extern void MapLayerEnableHook(void);
extern void MapLayerDisableHook(void);
extern void MapLayerStopHook(void);
#endif
