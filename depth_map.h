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

#ifndef _DEPTH_MAP_H_
#define _DEPTH_MAP_H_

class DepthMap {
    static int seqno_base_;

    int seqno_;
    float resolution_;
    int width_, height_;

    std::unique_ptr<float[]> val_;
    std::unique_ptr<bool[]> extended_snow_;

    void extend_coastal_snow();
    int map_idx(int i_lon, int i_lat) const;    // idx into array

 public:
    DepthMap(float resolution);     // in fractions of 1° e.g. 0.25
    ~DepthMap() { log_msg("DepthMap destroyed: %d", seqno_); }
    float get(float lon, float lat) const;
    bool is_extended_snow(float lon, float lat) const;
    void load_csv(const char *csv_name);
    int seqno() const { return seqno_; }
};
#endif
