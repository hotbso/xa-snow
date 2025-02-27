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
#include <memory>
#include <cmath>
#include <cassert>
#include <string>
#include <tuple>
#include <array>
#include <algorithm>

#include "xa-snow.h"
#include "coast_map.h"

#include <spng.h> // For image processing, include after xa-snow.h

// we use a "grid direction" = 360°/45° in standard math convention
// 0 -> x, 2 -> y, 4 -> -x, ...
static const int dir_x[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int dir_y[8] = {0, 1, 1, 1, 0, -1, -1, -1};

enum State {
    sWater,
    sLand,
    sCoast
};

static constexpr int kDirShift = 4;
static constexpr int kItemMask = 0xf;

// These arrays can be quite large, use a compact representation
// []wmap_;		    // encoded as (dir << kDirShift)|sXxx
//
// only valid if for wmap_[idx] == sWater
// []nearest_land_;	// encoded as (dir << kDirShift)|dist in steps

CoastMap coast_map;

std::tuple<int, int>
CoastMap::wrap_ij(int i, int j) const
{
    if (i >= width_)
        i -= width_;
    else if (i < 0)
        i += width_;

    if (j >= height_)
        j = height_ - 1;
    else if (j < 0)
        j = 0;

    return std::tuple(i, j);
}

int
CoastMap::ij_2_idx(int i, int j) const
{
    std::tie(i, j) = wrap_ij(i, j);

    int idx = j * width_ + i;
    assert(0 <= idx && idx < width_ * height_);
    return idx;
}

// return nearest neighbor i,j
std::tuple<int, int>
CoastMap::ll_2_ij(float lon, float lat) const
{
    // to the internal world
    if (lon >= 360.0f)
        lon -= 360.0f;
    else if (lon < 0.0f)
        lon += 360.0f;

    lat += 90.0f;
    lat = std::clamp(lat, 5.0f, 175.0f);

    lon /= resolution_;
    lat /= resolution_;

    // must wrap after round
    return wrap_ij(std::lroundf(lon), std::lroundf(lat));
}

// return nearest neighbor idx
int
CoastMap::ll_2_idx(float lon, float lat) const
{
    auto [i, j] = ll_2_ij(lon, lat);
    int idx = j * width_ + i;
    assert(0 <= idx && idx < width_ * height_);
    return idx;
}

bool
CoastMap::is_water(float lon, float lat) const
{
    int idx = ll_2_idx(lon, lat);
    return (wmap_[idx] & kItemMask) == sWater;
}

// -> is_water, have_nl, lon, lat
std::tuple<bool, bool, float, float>
CoastMap::nearest_land(float lon, float lat) const
{
    int idx = ll_2_idx(lon, lat);
    if ((wmap_[idx] & kItemMask) == sLand)
        return std::tuple(false, false, 0, 0);

    uint8_t v = nearest_land_[idx];
    if (v == 0)
        return std::tuple(true, false, 0, 0);

    int steps = (v & kItemMask);
    int d = v >> kDirShift;
    //log_msg("dir: %d, steps: %d", d, steps);
    assert(0 <= d && d < 8);

    lat = std::clamp(lat + steps * dir_y[d] * resolution_, -85.0f, 85.0f);
    lon += steps * dir_x[d] * resolution_;

    // to the external world
    if (lon >= 180.0f)
        lon -= 360.0f;
    else if (lon < -180.0f)
        lon += 360.0f;

    return std::tuple(true, true, lon, lat);
}

bool
CoastMap::is_land(float lon, float lat) const
{
    int idx = ll_2_idx(lon, lat);
    return (wmap_[idx] & kItemMask) == sLand;
}

std::tuple<bool, int, int, int>
CoastMap::is_coast(float lon, float lat) const
{
    int idx = ll_2_idx(lon, lat);
    uint8_t v = wmap_[idx];
    bool yes_no = (v & kItemMask) == sCoast;
    int dir = v >> kDirShift;
    return {yes_no, dir_x[dir], dir_y[dir], dir};
}

bool
CoastMap::load(const std::string& dir)
{
    std::string filename = dir + "/ESACCI-LC-L4-WB-Ocean-Map-150m-P13Y-2000-v4.0.png";
    FILE *fp = fopen(filename.c_str(), "rb");
    if (fp == nullptr) {
        log_msg("Can't open file '%s'", filename.c_str());
        return false;
    }

    spng_ctx* ctx = spng_ctx_new(0);
    if(ctx == nullptr)
        return false;

    // Ignore and don't calculate chunk CRC's
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);

    // Set memory usage limits for storing standard and unknown chunks,
    // this is important when reading untrusted files!
    size_t limit = 1024 * 1024 * 10;
    spng_set_chunk_limits(ctx, limit, limit);

    // Set source file
    spng_set_png_file(ctx, fp);

    struct spng_ihdr ihdr;
    int ret = spng_get_ihdr(ctx, &ihdr);
    if (ret) {
        log_msg("spng_get_ihdr() error: %s\n", spng_strerror(ret));
        fclose(fp);
        spng_ctx_free(ctx);
        return false;
    }

    width_ = ihdr.width;
    height_ = ihdr.height;
    int color_type = ihdr.color_type;
    int bit_depth = ihdr.bit_depth;

    log_msg("w: %d, h: %d, color_type: %d, bit_depth: %d", width_, height_, color_type, bit_depth);

    resolution_ = 360.0f / width_;
    if ((resolution_ != 180.0f / height_) || bit_depth != 8) {
        log_msg("Invalid map");
        fclose(fp);
        spng_ctx_free(ctx);
        return false;
    }

    log_msg("Decoded: '%s', %s", filename.c_str(), "PNG");

    auto img = std::make_unique<uint32_t[]>(height_ * width_);
    ret = spng_decode_image(ctx, img.get(), sizeof(uint32_t) * height_ * width_, SPNG_FMT_RGBA8, 0);
    fclose(fp);
    spng_ctx_free(ctx);

    if (ret) {
        log_msg("spng_decode_image() error: %s\n", spng_strerror(ret));
        return false;
    }

    wmap_ = std::make_unique<uint8_t[]>(height_ * width_);
    nearest_land_ = std::make_unique<uint8_t[]>(height_ * width_);

    // i,j are is 'png' coordinates, lon 0 = center
    for (int i = 0; i < width_; i++) {
        for (int j = 10; j < height_ - 10; j++) { // stay away from the poles
            // i/j_cm are in "coast map" coordinates, lon 0 = left

            int i_cm = i;
            int j_cm = j;

            i_cm -= width_ / 2;
            if (i_cm < 0)
                i_cm += width_;

            auto is_water_pix = [&](int i, int j) {
                j = height_ - j; // as the image (0,0) is top left to flip y values
                int idx = ij_2_idx(i, j);
                uint32_t pixel = img[idx];
                return (pixel & 0x00FFFFFF) == 0;   // not the alpha channel
            };

            int idx = j_cm * width_ + i_cm;
            assert(0 <= idx && idx < width_ * height_);

            if (is_water_pix(i, j)) {
                wmap_[idx] = sWater;
				// we check whether to the opposite side is only water and in direction 'dir' is land
				// if yes we sum up all unity vectors in dir to get the 'average' direction
                float sum_x = 0.0f;
                float sum_y = 0.0f;
                bool is_coast = false;

                for (int dir = 0; dir < 8; dir++) {
                    int di = dir_x[dir];
                    int dj = dir_y[dir];
                    if (is_water_pix(i - 2 * di, j - 2 * dj)
                        && is_water_pix(i - di, j - dj)
                        && (!is_water_pix(i + di, j + dj)               // check 3 steps for ANY land
                            || !is_water_pix(i + 2 * di, j + 2 * dj)    // works better with fjords
                            || !is_water_pix(i + 3 * di, j + 3 * dj))) {

                        float f = 1.0f;
                        if (dir & 1)
                            f = 0.7071f; // diagonal = 1/sqrt(2)

                        sum_x += f * di;
                        sum_y += f * dj;
                        is_coast = true;
                    }
                }

                if (is_coast) {
					// get angle of the average direction. We consider this as normal
					// of the coast line
                    float ang = atan2(sum_y, sum_x) / kD2R;
                    if (ang < 0)
                        ang += 360.0f;

                    int dir_land = (int)(round(ang / 45));
                    if (dir_land == 8) {
                        dir_land = 0;
                    }

                    wmap_[idx] = (uint8_t)((dir_land << kDirShift) | sCoast);
                }

                // steps must fit in 4 bits,  1 step ~ 7 km
                // We are working in the ll-system here but we want the nearest point to land in the
                // metric system. The metric distance of a delta ll in lon direction is smaller by a
                // factor of cos(lat) than in lat direction.
                // We therefore sort the directions by resulting metric distance = (lon, lat, diagonal)
                static const std::array<int, 8> dir_weighted{0, 4, 2, 6, 1, 3, 5, 7};
                for (int steps = 1; steps < 10; steps++) {
                    for (int dir : dir_weighted) {
                       // probe location, steps in direction dir
                        int s = steps;
                        if (!is_water_pix(i + s * dir_x[dir], j + s * dir_y[dir])) {
                            // If the next higher step is still land we take that one.
                            // Gives better results with small islands in front of the shoreline
                            if (!is_water_pix(i + (s + 1) * dir_x[dir], j + (s + 1) * dir_y[dir]))
                                s++;

                            nearest_land_[idx] = (uint8_t)((dir << kDirShift) | s);
                            // double break
                            goto have_nearest_land;
                        }
                    }
               }

              have_nearest_land:
                continue;   // keep the compiler happy with c++20

            } else {
                wmap_[idx] = sLand;
            }
        }
    }

    return true;
}
