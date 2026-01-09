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

#include <cstring>
#include <fstream>

#include "airport.h"

std::vector<std::unique_ptr<Airport>> airports;

// SceneryPacks constructor
SceneryPacks::SceneryPacks(const std::string& xp_dir) {
    std::string scpi_name(xp_dir + "/Custom Scenery/scenery_packs.ini");

    std::ifstream scpi(scpi_name);
    if (scpi.fail()) {
        LogMsg("Can't open '%s'", scpi_name.c_str());
        return;
    }

    sc_paths.reserve(500);
    std::string line;

    while (std::getline(scpi, line)) {
        size_t i;
        if ((i = line.find('\r')) != std::string::npos)
            line.resize(i);

        if (!line.starts_with("SCENERY_PACK ") || line.find("*GLOBAL_AIRPORTS*") != std::string::npos)
            continue;

        // autoortho pretends every file exists but
        // reads give errors
        if (line.find("/z_ao_") != std::string::npos)
            continue;

        line.erase(0, 13);
        std::string sc_path;
        bool is_absolute = (line[0] == '/' || line.find(':') != std::string::npos);
        if (is_absolute)
            sc_path = line;
        else
            sc_path = xp_dir + "/" + line;

        // posixify
        for (unsigned i = 0; i < sc_path.size(); i++)
            if (sc_path[i] == '\\')
                sc_path[i] = '/';

        sc_paths.push_back(sc_path);
    }

    scpi.close();
    sc_paths.shrink_to_fit();
}

static void SplitWords(std::string str, std::vector<std::string>& words) {
    char* pch = strtok((char*)str.c_str(), " ");
    while (pch != NULL) {
        words.push_back(pch);
        pch = strtok(NULL, " ");
    }
}

// go through apt.dat and collect runways from 100 lines
// only type 15 = transparent runway is collected
// only one airport per apt.dat is processed (which is the 99.99% case)
static std::unique_ptr<Airport> ParseAptDat(const std::string& fn) {
    std::ifstream apt(fn);
    if (apt.fail())
        return nullptr;

    auto arpt = std::make_unique<Airport>();

    LogMsg("Processing '%s'", fn.c_str());

    std::string line;
    line.reserve(2000);  // can be quite long
    bool have_airport = false;

    while (std::getline(apt, line)) {
        size_t i = line.find('\r');
        if (i != std::string::npos)
            line.resize(i);

        // 1    681 0 0 ENGM Oslo Gardermoen
        if (line.starts_with("1 ")) {
            // LogMsg("%s", line.c_str());
            if (have_airport)
                return arpt;  // only one airport per apt.dat

            have_airport = true;

            int ofs;
            sscanf(line.c_str(), "%*d %*d %*d %*d %n", &ofs);
            if (ofs < (int)line.size())
                line.erase(0, ofs);
            arpt->name = line;
            continue;
        }

        // 100 45.11 15 0 0.00 1 3 0 01L  60.18499584  011.07373840 0 148 3 1 0 0 19R  60.21615335  011.09170422 0 140 3
        // 2 1 0
        if (line.starts_with("100 ")) {
            std::vector<std::string> words;
            SplitWords(line, words);
            if (words.size() < 20)
                continue;

            int code = std::atoi(words[2].c_str());
            code %= 100;
            if (code != 15)
                continue;

            // only save type 15 = transparent runway, candidate for not being weather aware
            // LogMsg("%s", line.c_str());
            Runway rwy;
            rwy.name = words[8];
            rwy.end1.lat = std::atof(words[9].c_str());
            rwy.end1.lon = std::atof(words[10].c_str());
            rwy.end2.lat = std::atof(words[18].c_str());
            rwy.end2.lon = std::atof(words[19].c_str());
            arpt->runways.push_back(rwy);
        }
    }

    apt.close();
    return arpt;
}

struct Circle {
    Vec2 c;
    float r;
};

// circle from 2 points
static Circle CircleFrom(const Vec2& a, const Vec2& b) {
    return {0.5f * (a + b), 0.5f * len(a - b)};
}

// circle from 3 points
static Circle CircleFrom(const Vec2& v1, const Vec2& v2, const Vec2& v3) {
    // Check whether a circle around 2 points covers the third one.
    // It's cheap and covers the case that all 3 are collinear
    Circle c = CircleFrom(v1, v2);
    if (len(v3 - c.c) <= c.r)
        return c;

    c = CircleFrom(v1, v3);
    if (len(v2 - c.c) <= c.r)
        return c;

    c = CircleFrom(v2, v3);
    if (len(v1 - c.c) <= c.r)
        return c;

    Vec2 v21 = v2 - v1;
    Vec2 v31 = v3 - v1;

    float lv1 = v1.x * v1.x + v1.y * v1.y;
    float lv2 = v2.x * v2.x + v2.y * v2.y;
    float lv3 = v3.x * v3.x + v3.y * v3.y;

    Vec2 d{0.5f * (lv2 - lv1), 0.5f * (lv3 - lv1)};
    float D = v21.x * v31.y - v31.x * v21.y;
    Vec2 cc{(d.x * v31.y - d.y * v21.y) / D, (v21.x * d.y - v31.x * d.x) / D};
    return {cc, len(v1 - cc)};
}

static Circle min_circle_trivial(std::vector<Vec2>& P) {
    if (P.empty())
        return {{0, 0}, 0};

    if (P.size() == 1)
        return {P[0], 0};

    if (P.size() == 2)
        return CircleFrom(P[0], P[1]);

    return CircleFrom(P[0], P[1], P[2]);
}

// Welzl's algorithm for the MEC
//
// P = set of points
// R = set of boundary points
// n = # of points in P
static Circle Welzl(std::vector<Vec2>& P, std::vector<Vec2> R, int n) {
    // Base case when all points processed or |R| = 3
    if (n == 0 || R.size() == 3) {
        return min_circle_trivial(R);
    }

    // Pick a random point
    int idx = rand() % n;
    Vec2 p = P[idx];

    // Put the picked point at the end of P
    // since it's more efficient than
    // deleting from the middle of the vector
    std::swap(P[idx], P[n - 1]);

    Circle d = Welzl(P, R, n - 1);
    if (len(d.c - p) <= d.r)
        return d;

    // Otherwise, must be on the boundary of the MEC
    R.push_back(p);

    // Return the MEC for P - {p} and R U {p}
    return Welzl(P, R, n - 1);
}

bool CollectAirports(const std::string& xp_dir) {
    SceneryPacks scp(xp_dir);
    if (scp.sc_paths.size() == 0) {
        LogMsg("Can't collect scenery_packs.ini");
        return false;
    }

    airports.reserve(50);

    for (auto& path : scp.sc_paths) {
        auto arpt = ParseAptDat(path + "Earth nav data/apt.dat");
        if (arpt == nullptr || arpt->runways.size() == 0 || arpt->runways.size() > 10)
            continue;

        airports.push_back(std::move(arpt));
    }

    airports.shrink_to_fit();

    LogMsg("Collected %d airports with transparent runways", (int)airports.size());

    for (auto& arpt : airports) {
        LogMsg("%s", arpt->name.c_str());
        std::vector<Vec2> rwy_ends;

        LLPos base = arpt->runways[0].end1;  // pick arbitrary base for circle computation
        for (auto& rw : arpt->runways) {
            LogMsg("  rw: %-3s, end1: (%0.4f, %0.4f), end2: (%0.4f, %0.4f)", rw.name.c_str(), rw.end1.lat, rw.end1.lon,
                   rw.end2.lat, rw.end2.lon);
            rwy_ends.push_back(rw.end1 - base);
            rwy_ends.push_back(rw.end2 - base);
        }

        Circle c;
        if (rwy_ends.size() == 2)
            c = CircleFrom(rwy_ends[0], rwy_ends[1]);
        else
            c = Welzl(rwy_ends, {}, rwy_ends.size());

        arpt->mec_center = base + c.c;
        arpt->mec_radius = c.r;
        LogMsg("Center: (%0.4f, %0.4f), r = %0.1f", arpt->mec_center.lat, arpt->mec_center.lon, arpt->mec_radius);
    }

    return true;
}

#ifdef TEST_AIRPORTS
// g++ -std=c++20  -o collect_airports -DTEST_AIRPORTS -DLOCAL_DEBUGSTRING -DIBM -DXPLM200  -I. -I../xplib -I../SDK/CHeaders/XPLM collect_airports.cpp ../xplib/log_msg.cpp

const char* log_msg_prefix = "collect_airports: ";

int main() {
    bool res = CollectAirports("e:/X-Plane-12-test");
    LogMsg("res: %d", res);
    return 0;
}
#endif
