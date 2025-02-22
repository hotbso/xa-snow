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

std::string xp_dir;
std::string plugin_dir;
std::string output_dir;

static void
flightloop_emul()
{
    while (CheckAsyncDownload()) {
        log_msg("... waiting for async download");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

//  g++ -std=c++20 -Wall -Iservices -ISDK/CHeaders/XPLM -DIBM=1 -DLOCAL_DEBUGSTRING async_download.cpp services/log_msg.cpp services/sub_exec.cpp -lcurl
int main()
{
    xp_dir = ".";
    plugin_dir = ".";
    output_dir = ".";

    coast_map.load(plugin_dir);

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
