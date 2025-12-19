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

#include <cstdio>
#include <ctime>
#include <array>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdio.h>

#include "xa-snow.h"
#include "coast_map.h"

const char *log_msg_prefix = "gt: ";

std::string xp_dir;
std::string plugin_dir;
std::string output_dir;

static void
flightloop_emul()
{
    while (CheckAsyncDownload()) {
        LogMsg("... waiting for async download");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// note: arguments are lat lon here to facilitate cut&paste from google map
static void
probe_nearest_land(float lat, float lon)
{
   auto [is_water, have_nl, nl_lon, nl_lat] = coast_map.nearest_land(lon, lat);
   LogMsg("probe_nl: ll %10.5f,%10.5f, is_water: %d, have_nl: %d, nl_ll: %10.5f,%10.5f",
           lat, lon, is_water, have_nl, nl_lat, nl_lon);

}

int main()
{
    xp_dir = ".";
    plugin_dir = ".";
    output_dir = ".";

    coast_map.load(plugin_dir);

    probe_nearest_land(54.401964, 11.311532);   // Fehmarn
    probe_nearest_land(54.298076, 8.402394);    // West of St.Peter Ording
    probe_nearest_land(55.258987, 12.963942);   // South of Trelleborg
    probe_nearest_land(60.297378, 4.679465);    // Bergen
    probe_nearest_land(55.191715, -27.482858);  // Atlantic
    probe_nearest_land(59.182860, 18.937188);   // Stockholm
    probe_nearest_land(63.378151, -21.262616);  // Iceland
    probe_nearest_land(69.888846, 16.774953);   // Tromso

    StartAsyncDownload(true, 0, 0, 0);
    flightloop_emul();

    std::cout << "-------------------------------------------------\n\n";

#if 0
    StartAsyncDownload(false, 2, 10, 21);
    flightloop_emul();
    std::cout << "-------------------------------------------------\n\n";

    StartAsyncDownload(false, 2, 20, 22);
    flightloop_emul();
    std::cout << "-------------------------------------------------\n\n";

    StartAsyncDownload(false, 1, 20, 10);
    flightloop_emul();
#endif

    return 0;
}
