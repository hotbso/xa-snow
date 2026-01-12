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

#include "airport.h"
#include "XPLMGraphics.h"

static float constexpr kArptLimit = 18000;    // m, ~10 nm
static float constexpr kMecSlope = 0.087f;    // 5° slope towards MEC

std::tuple<float, bool> LegacyAirportSnowDepth(float lon, float lat, float snow_depth)  // -> adjusted snow depth, in range of a legacy airport
{
    // look whether we are approaching a legacy airport
    LLPos pos = {lon, lat};

    for (auto& arpt : airports) {
        float dist = len(pos - arpt->mec_center);
        float max_snow_depth = std::min(arpt->max_snow_depth, 0.25f);   // max 25cm snow at legacy airports

        if (dist < kArptLimit) {
            if (snow_depth <= max_snow_depth)
                return std::make_tuple(snow_depth, true);

            if (arpt->elevation == Airport::kNoElevation) {
                double x, y, z;
                const LLPos& pos = arpt->runways[0].end1;
                XPLMWorldToLocal(pos.lat, pos.lon, 0, &x, &y, &z);
                if (xplm_ProbeHitTerrain != XPLMProbeTerrainXYZ(probe_ref, x, y, z, &probeinfo)) {
                    LogMsg("terrain probe failed???");
                }

                double dummy, elev;
                XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &dummy, &dummy, &elev);
                arpt->elevation = elev;
                LogMsg("elevation of '%s', %0.1f ft", arpt->name.c_str(), arpt->elevation / kF2M);
            }

            float haa = XPLMGetDataf(plane_elevation_dr) - arpt->elevation;
            float ref_haa = dist * kMecSlope;          // slope from center
            float dh = std::max(0.0f, haa - ref_haa);  // a delta above ref slope
            float ref_dist = dist + 10.0f * dh;        // is weighted higher

            // now interpolate down to max_snow_depth at the MEC
            float a = (ref_dist - arpt->mec_radius) / (kArptLimit - arpt->mec_radius);
            a = std::max(0.0f, std::min(a, 1.0f));
            a = std::pow(a, 1.5f);  // slightly progressive
            float snow_depth_n = max_snow_depth + a * (std::min(snow_depth, 0.25f) - max_snow_depth);

            //LogMsg("haa: %.0f, ref_haa: %0.f, dist to '%s', %.0f m, snow_depth in: %0.2f, out: %0.3f",
            //        haa, ref_haa, arpt->name.c_str(), dist, snow_depth, snow_depth_n);
            return std::make_tuple(snow_depth_n, true);
        }
    }

    return std::make_tuple(snow_depth, false);
}
