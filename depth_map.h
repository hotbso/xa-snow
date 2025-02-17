//
//    A contribution to https://github.com/xairline/xa-snow by zodiac1214
//
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
    // depth map of the world in 0.1° resolution
    static constexpr int kNlon = 3600;  // [0, 360)
    static constexpr int kNlat = 1801;  // [-90, -90]

    static int seqno_base_;
    int seqno_;

    float val_[kNlon][kNlat] = {};

public:
    DepthMap() : seqno_(++seqno_base_) { log_msg("DepthMap created: %d", seqno_); }
    ~DepthMap() { log_msg("DepthMap destroyed: %d", seqno_); }
    float get(float lon, float lat) const;
    float get_idx(int i_lon, int i_lat) const;
    void load_csv(const char *csv_name);
    std::unique_ptr<DepthMap> extend_coastal_snow() const;
};
#endif
