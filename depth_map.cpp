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
#include <cassert>
#include <fstream>
#include <string>
#include <cmath>
#include <memory>

#include "xa-snow.h"
#include "depth_map.h"
#include "coast_map.h"

int DepthMap::seqno_base_;
DepthMap::DepthMap(float resolution)
{
    seqno_ = ++seqno_base_;
    resolution_ = resolution;
    width_ = 360.0f / resolution_;
    height_ = (int)(180.0f / resolution_) + 1;
    val_ = std::make_unique<float[]>(height_ * width_);
    extended_snow_ = std::make_unique<bool[]>(height_ * width_);
    LogMsg("DepthMap created: %d, width %d, height: %d", seqno_, width_, height_);
}

int
DepthMap::map_idx(int i_lon, int i_lat) const
{
    // for lon we wrap around
    if (i_lon >= width_)
        i_lon -= width_;
    else if (i_lon < 0)
        i_lon += width_;

    // for lat we just confine, doesn't make a difference anyway
    if (i_lat >= height_)
        i_lat = height_ - 1;
    else if (i_lat < 0)
        i_lat = 0;

    int idx = i_lat * width_ + i_lon;
    assert(0 <= idx && idx < width_ * height_);
    return idx;
}


float
DepthMap::get(float lon, float lat) const
{
    // our snow world's (lat, lon) is in [0,360) x [0, 180]
    lat += 90.0;

    // longitude is (-180,180], we need to convert it to [0,360)
    if (lon < 0)
        lon += 360;

    lon /= resolution_;
    lat /= resolution_;

    // index of tile is lower left corner
    int i_lon = lon;
    int i_lat = lat;

    // (s, t) coordinates of (lon, lat) within tile, s,t in [0,1]
    float s = lon - i_lon;
    float t = lat - i_lat;

    //LogMsg("(%f, %f) -> (%d, %d) (%f, %f)", lon/10, lat/10 - 90, i_lon, i_lat, s, t)
    float v00 = val_[map_idx(i_lon, i_lat)];
    float v10 = val_[map_idx(i_lon + 1, i_lat)];
    float v01 = val_[map_idx(i_lon, i_lat + 1)];
    float v11 = val_[map_idx(i_lon + 1, i_lat + 1)];

	// Lagrange polynoms: pij = is 1 on corner ij and 0 elsewhere
    float p00 = (1 - s) * (1 - t);
    float p10 = s * (1 - t);
    float p01 = (1 - s) * t;
    float p11 = s * t;

    float v = v00 * p00 + v10 * p10 + v01 * p01 + v11 * p11;
	//LogMsg("vij: %f, %f, %f, %f; v: %f", v00, v10, v01, v11, v)
    return v;
}

// return "some neighbor" has extended snow
bool
DepthMap::is_extended_snow(float lon, float lat) const
{
    // our snow world map is 3600x1801 [0,359.9]x[0,180.0]
    lat += 90.0;

    // longitude is -180 to 180, we need to convert it to 0 to 360
    if (lon < 0) {
        lon += 360;
    }

    lon /= resolution_;
    lat /= resolution_;

    // index of tile is lower left corner
    int i_lon = lon;
    int i_lat = lat;

    //LogMsg("(%f, %f) -> (%d, %d) (%f, %f)", lon/10, lat/10 - 90, i_lon, i_lat, s, t)
    bool es00 = extended_snow_[map_idx(i_lon, i_lat)];
    bool es10 = extended_snow_[map_idx(i_lon + 1, i_lat)];
    bool es01 = extended_snow_[map_idx(i_lon, i_lat + 1)];
    bool es11 = extended_snow_[map_idx(i_lon + 1, i_lat + 1)];
    return (es00 || es01 || es10 || es11);
}

void
DepthMap::load_csv(const char *csv_name)
{
    std::ifstream file(csv_name);
    if (!file.is_open()) {
        LogMsg("Error opening file: %s", csv_name);
        return;
    }

    std::string line;
    int counter = 0;

    // Skip the header
    std::getline(file, line);
    counter++;

    while (std::getline(file, line)) {
        float lat, lon, value;
        if (3 != sscanf(line.c_str(), "%f,%f,%f", &lon, &lat, &value)) {
            LogMsg("invalid csv line: '%s'", line.c_str());
            continue;
        }

        if (value < 0.001f)
            continue;

        // Convert longitude and latitude to array indices (with rounding!)
        int x = std::lroundf(lon / resolution_);
        int y = std::lroundf((lat + 90.0f) / resolution_);  // Adjust for negative latitudes

        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            LogMsg("invalid csv line: '%s'", line.c_str());
            continue;
        }

        val_[y * width_ + x] = value;
        counter++;
    }

    LogMsg("Loaded %d lines from CSV file '%s'", counter, csv_name);

    // use multiple passes for snow extension, e.g. for fjords, islands close to coast, ...
    extend_coastal_snow();
    extend_coastal_snow();
    extend_coastal_snow();
}

void
DepthMap::extend_coastal_snow()
{
    static constexpr float min_sd = 0.02f; // only go higher than this snow depth
    int n_extend = 0;

    for (int i = 0; i < width_; i++) {
        for (int j = 0; j < height_; j++) {
            float sd = val_[map_idx(i, j)];
            static constexpr int max_step = 2; // to look for inland snow ~ 10 to 20 km / step
            float lon = i * resolution_;
            float lat = j * resolution_ - 90.0f;
            auto [is_coast, dir_x, dir_y, dir_angle] = coast_map.is_coast(lon, lat);
            if (is_coast && sd <= min_sd) {
                // look for inland snow
                int inland_dist = 0;
                float inland_sd = 0.0f;
                for (int k = 1; k <= max_step; k++) {
                    int ii = i + k * dir_x;
                    int jj = j + k * dir_y;
                    float lon = ii * resolution_;
                    float lat = jj * resolution_ - 90.0f;

                    if (k < max_step && coast_map.is_water(lon, lat)) { // if possible skip water
                        continue;
                    }

                    float tmp = val_[map_idx(ii, jj)];
                    if (tmp > sd && tmp > min_sd) { // found snow
                        inland_dist = k;
                        inland_sd = tmp;
                        break;
                    }
                }

                static constexpr float decay = 0.8f; // snow depth decay per step
                if (inland_dist > 0) {
					//LogMsg("Inland snow detected for (%d, %d) at dist %d, sd: %0.3f %0.3f",
					//		  i, j, inland_dist, sd, inland_sd)

					// use exponential decay law from inland point to coast line point
                    for (int k = inland_dist - 1; k >= 0; k--) {
                        inland_sd *= decay;
                        if (inland_sd < min_sd) {
                            inland_sd = min_sd;
                        }
                        int x = i + k * dir_x;
                        int y = j + k * dir_y;
                        if (x >= width_)
                            x -= width_;
                        else if (x < 0)
                            x += width_;

                        // the poles are tricky so we just clamp
                        // anyway it does not make a difference
                        if (y >= height_)
                            y = height_ - 1;
                        else if (y < 0)
                            y = 0;

                        val_[y * width_ + x] = std::max(val_[y * width_ + x], inland_sd);
                        extended_snow_[y * width_ + x] = true;
                        n_extend++;
                    }
                }
            }
        }
    }

    LogMsg("Extended coastal snow on %d grid points", n_extend);
}
