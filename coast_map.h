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

#ifndef _COAST_MAP_H_
#define _COAST_MAP_H_

#include <tuple>

struct CoastMap {
    int width_{0}, height_{0};
    float resolution_;

    std::unique_ptr<uint8_t[]> wmap_;		// encoded as (dir << 2)|sXxx

    std::tuple<int, int> wrap_ij(int i, int j) const;
    int ll_2_idx(float lon, float lat) const;   // -> index into map

  public:
    bool load(const std::string& dir);
    bool is_water(float lon, float lat);
    bool is_land(float lon, float lat);
    std::tuple<bool, int, int, int> is_coast(float lon, float lat); // -> yes_no, dir_x, dir_y, grid_angle
};

extern CoastMap coast_map;
#endif