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
#include <fstream>
#include <string>
#include <cmath>
#include <memory>

#include "xa-snow.h"
#include "depth_map.h"


int DepthMap::seqno_base_;

float
DepthMap::get_idx(int i_lon, int i_lat) const
{
    // for lon we wrap around
    if (i_lon >= kNlon) {
        i_lon -= kNlon;
    } else if (i_lon < 0) {
        i_lon += kNlon;
    }

    // for lat we just confine, doesn't make a difference anyway
    if (i_lat >= kNlat) {
        i_lat = kNlat - 1;
    } else if (i_lat < 0) {
        i_lat = 0;
    }

    return val_[i_lon][i_lat];
}


float
DepthMap::get(float lon, float lat) const
{
    // our snow world map is 3600x1801 [0,359.9]x[0,180.0]
    lat += 90.0;

    // longitude is -180 to 180, we need to convert it to 0 to 360
    if (lon < 0) {
        lon += 360;
    }

    lon *= 10;
    lat *= 10;

    // index of tile is lower left corner
    int i_lon = static_cast<int>(lon);
    int i_lat = static_cast<int>(lat);

    // (s, t) coordinates of (lon, lat) within tile, s,t in [0,1]
    float s = lon - static_cast<float>(i_lon);
    float t = lat - static_cast<float>(i_lat);

    //m.Logger.Infof("(%f, %f) -> (%d, %d) (%f, %f)", lon/10, lat/10 - 90, i_lon, i_lat, s, t)
    float v00 = get_idx(i_lon, i_lat);
    float v10 = get_idx(i_lon + 1, i_lat);
    float v01 = get_idx(i_lon, i_lat + 1);
    float v11 = get_idx(i_lon + 1, i_lat + 1);

	// Lagrange polynoms: pij = is 1 on corner ij and 0 elsewhere
    float p00 = (1 - s) * (1 - t);
    float p10 = s * (1 - t);
    float p01 = (1 - s) * t;
    float p11 = s * t;

    float v = v00 * p00 + v10 * p10 + v01 * p01 + v11 * p11;
	//m.Logger.Infof("vij: %f, %f, %f, %f; v: %f", v00, v10, v01, v11, v)
    return v;
}

void
DepthMap::load_csv(const char *csv_name)
{
    std::ifstream file(csv_name);
    if (!file.is_open()) {
        log_msg("Error opening file: %s", csv_name);
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
            log_msg("invalid csv line: '%s'", line.c_str());
            continue;
        }

        if (value < 0.001f)
            value = 0.0f;

        // Convert longitude and latitude to array indices
        // This example assumes the CSV contains all longitudes and latitudes
        int x = static_cast<int>(lon * 10);         // Adjust these calculations based on your data's range and resolution
        int y = static_cast<int>((lat + 90) * 10);  // Adjust for negative latitudes
        if (x < 0 || x >= kNlon || y < 0 || y >= kNlat) {
            log_msg("invalid csv line: '%s'", line.c_str());
            continue;
        }

        val_[x][y] = value;
        counter++;
    }

    log_msg("depth map size: %d",  counter);
    log_msg("Loading CSV file '%s': Done", csv_name);
}

std::unique_ptr<DepthMap>
DepthMap::extend_coastal_snow() const
{
    const float min_sd = 0.02f; // only go higher than this snow depth
    int n_extend = 0;

    std::unique_ptr<DepthMap> new_dm = std::make_unique<DepthMap>();

    for (int i = 0; i < DepthMap::kNlon; i++) {
        for (int j = 0; j < DepthMap::kNlat; j++) {
            float sd = get_idx(i, j);
            float sdn = new_dm->val_[i][j]; // may already be set by inland extension earlier
            if (sd > sdn) { // always maximize
                new_dm->val_[i][j] = sd;
            }

            const int max_step = 3; // to look for inland snow ~ 5 to 10 km / step
            bool is_coast;
            int dir_x, dir_y, dir_angle;
            std::tie(is_coast, dir_x, dir_y, dir_angle) = coast_map.is_coast(i, j);
            if (is_coast && sd <= min_sd) {
                // look for inland snow
                int inland_dist = 0;
                float inland_sd = 0.0f;
                for (int k = 1; k <= max_step; k++) {
                    int ii = i + k * dir_x;
                    int jj = j + k * dir_y;

                    if (k < max_step && coast_map.is_water(ii, jj)) { // if possible skip water
                        continue;
                    }

                    float tmp = get_idx(ii, jj);
                    if (tmp > sd && tmp > min_sd) { // found snow
                        inland_dist = k;
                        inland_sd = tmp;
                        break;
                    }
                }

                const float decay = 0.8f; // snow depth decay per step
                if (inland_dist > 0) {
					//g.Logger.Infof("Inland snow detected for (%d, %d) at dist %d, sd: %0.3f %0.3f",
					//				 i, j, inland_dist, sd, inland_sd)

					// use exponential decay law from inland point to coast line point
                    for (int k = inland_dist - 1; k >= 0; k--) {
                        inland_sd *= decay;
                        if (inland_sd < min_sd) {
                            inland_sd = min_sd;
                        }
                        int x = i + k * dir_x;
                        int y = j + k * dir_y;
                        if (x >= DepthMap::kNlon) {
                            x -= DepthMap::kNlon;
                        }
                        if (x < 0) {
                            x += DepthMap::kNlon;
                        }

                        // the poles are tricky so we just clamp
                        // anyway it does not make a difference
                        if (y >= DepthMap::kNlat) {
                            y = DepthMap::kNlat - 1;
                        }
                        if (y < 0) {
                            y = 0;
                        }
                        new_dm->val_[x][y] = inland_sd;
                        n_extend++;
                    }
                }
            }
        }
    }

    log_msg("Extended coastal snow on %d grid points", n_extend);
    return new_dm;
}
