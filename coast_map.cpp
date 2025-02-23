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

CoastMap coast_map;

std::tuple<int, int>
CoastMap::wrap_ij(int i, int j) const
{
    if (i >= n_wm) {
        i -= n_wm;
    } else if (i < 0) {
        i += n_wm;
    }

    if (j >= m_wm) {
        j = m_wm - 1;
    } else if (j < 0) {
        j = 0;
    }

    return std::make_tuple(i, j);
}

// return nearest neighbor i,j
std::tuple<int, int>
CoastMap::ll_2_ij(float lon, float lat) const
{
    float lon_in = lon;
    if (lon >= 360.0f)
        lon -= 360.0f;
    else if (lon < 0.0f)
        lon += 360.0f;

    lat = std::clamp(lat, -89.0f, 89.0f);

    // must wrap after round
    auto [i, j] = wrap_ij((int)(lon * 10.0f + 0.5f), (int)((lat + 90.0f) * 10.0f + 0.5f));
    if (!(0 <= i && i < n_wm))
        log_msg("%0.3f, %0.3f, %d, %d, %0.4f", lon, lat, i, j, lon_in);
    assert(0 <= i && i < n_wm);
    assert(0 <= j && j < m_wm);
    return std::make_tuple(i, j);
}

bool
CoastMap::is_water(float lon, float lat)
{
    auto [i, j] = ll_2_ij(lon, lat);
    return (wmap[i][j] & 0x3) == sWater;
}

bool
CoastMap::is_land(float lon, float lat)
{
    auto [i, j] = ll_2_ij(lon, lat);
    return (wmap[i][j] & 0x3) == sLand;
}

std::tuple<bool, int, int, int>
CoastMap::is_coast(float lon, float lat)
{
    auto [i, j] = ll_2_ij(lon, lat);
    uint8_t v = wmap[i][j];
    bool yes_no = (v & 0x3) == sCoast;
    int dir = v >> 2;
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

    int width = ihdr.width;
    int height = ihdr.height;
    int color_type = ihdr.color_type;
    int bit_depth = ihdr.bit_depth;

    log_msg("w: %d, h: %d, color_type: %d, bit_depth: %d", width, height, color_type, bit_depth);

    if (width != n_wm || height != m_wm || bit_depth != 8) {
        log_msg("Invalid map");
        fclose(fp);
        spng_ctx_free(ctx);
        return false;
    }

    log_msg("Decoded: '%s', %s", filename.c_str(), "PNG");

    // ~ 20 MB, so no stack allocation RGBA = uint32_t
    auto img = std::make_unique<uint32_t[]>(m_wm * n_wm);

    ret = spng_decode_image(ctx, img.get(), sizeof(uint32_t) * m_wm * n_wm, SPNG_FMT_RGBA8, 0);
    fclose(fp);
    spng_ctx_free(ctx);

    if (ret) {
        log_msg("spng_decode_image() error: %s\n", spng_strerror(ret));
        return false;
    }

    for (int i = 0; i < n_wm; i++) {
        for (int j = 10; j < m_wm - 10; j++) { // stay away from the poles
			// determined by visual adjustment
			// could be one system is at point, the other at center of grid
            int i_cs = i - 3;
            int j_cs = j - 3;

            i_cs -= n_wm / 2;
            if (i_cs < 0) {
                i_cs += n_wm;
            }

            auto is_water_pix = [&](int i, int j) {
                j = m_wm - j; // as the image (0,0) is top left to flip y values
                auto [ii, jj] = wrap_ij(i, j);
                uint32_t pixel = img[jj * n_wm + ii];
                return (pixel & 0x00FFFFFF) == 0;   // not the alpha channel
            };

            auto is_land = [&](int i, int j) {
                return !is_water_pix(i, j);
            };

            if (is_water_pix(i, j)) {
                wmap[i_cs][j_cs] = sWater;
				// we check whether to the opposite side is only water and in direction 'dir' is land
				// if yes we sum up all unity vectors in dir to get the 'average' direction
                float sum_x = 0.0f;
                float sum_y = 0.0f;
                bool is_coast = false;

                for (int dir = 0; dir < 8; dir++) {
                    int di = dir_x[dir];
                    int dj = dir_y[dir];
                    if (is_water_pix(i - 2 * di, j - 2 * dj) && is_water_pix(i - di, j - dj) && is_land(i + di, j + dj)) {
                        float f = 1.0f;
                        if (dir & 1) {
                            f = 0.7071f; // diagonal = 1/sqrt(2)
                        }
                        sum_x += f * di;
                        sum_y += f * dj;
                        is_coast = true;
                    }
                }

                if (is_coast) {
					// get angle of the average direction. We consider this as normal
					// of the coast line
                    float ang = atan2(sum_y, sum_x) / kD2R;
                    if (ang < 0) {
                        ang += 360.0f;
                    }

                    int dir_land = (int)(round(ang / 45));
                    if (dir_land == 8) {
                        dir_land = 0;
                    }

                    wmap[i_cs][j_cs] = (uint8_t)((dir_land << 2) | sCoast);
                }
            } else {
                wmap[i_cs][j_cs] = sLand;
            }
        }
    }

    return true;
}
