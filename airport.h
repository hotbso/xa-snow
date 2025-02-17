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

#ifndef _AIRPORT_H_
#define _AIRPORT_H_

#include <cmath>
#include <string>
#include <memory>
#include <vector>

#include "xa-snow.h"

// return relative angle in (-180, 180]
static inline
float RA(float angle)
{
    angle = fmodf(angle, 360.0f);
    if (angle > 180.0f)
        return angle - 360.0f;

    if (angle <= -180.0f)
        return angle + 360.0f;

    return angle;
}

//
// Contrary to common belief the earth is flat. She has just a weird coordinate system with (lon,lat).
// To overcome this we attach a 2-d vector space at each (lon, lat) point with orthogonal
// basis scaled in meters. So (lon2, lat2) - (lon1, lat1) gives rise to a vector v in the vector space
// attached at (lon1, lat1) and (lon1, lat1) + v again is (lon2, lat2).
// As we do our math in a circle of ~ 20km this works pretty well.
//
// Should you still be tricked in believing that the earth is a ball you can consider this vector space
// a tangent space. But this is for for visualisation only.
//

struct LLPos {
	float lon, lat;
};

struct Vec2 {
	float x,y;
};

static inline
float len(const Vec2& v)
{
	return sqrtf(v.x * v.x + v.y * v.y);
}

// pos - pos
static inline
Vec2 operator-(const LLPos& b, const LLPos& a)
{
	return {RA(b.lon -  a.lon) * kLat2m * cosf(a.lat * kD2R),
		    RA(b.lat -  a.lat) * kLat2m};
}

// pos + vec
static inline
LLPos operator+(const LLPos &p, const Vec2& v)
{
	return {RA(p.lon + v.x / (kLat2m * cosf(p.lat * kD2R))),
			RA(p.lat + v.y / kLat2m)};
}

// vec - vec
static inline
Vec2 operator-(const Vec2& b, const Vec2& a)
{
	return {b.x - a.x, b.y - a.y};
}

// vec + vec
static inline
Vec2 operator+(const Vec2& a, const Vec2& b)
{
	return {a.x + b.x, a.y + b.y};
}

// c * vec
static inline
Vec2 operator*(float c, const Vec2& v)
{
	return {c * v.x, c * v.y};
}

struct Runway {
	std::string name;
	LLPos end1, end2;
	float psi;
};

struct Airport {
	constexpr static float kNoElevation = -999;	// Not yet determined
	std::string name;
	float elevation{kNoElevation};
	std::vector<Runway> runways;

    // MEC around runways
    LLPos mec_center;
    float mec_radius;
};

extern std::vector<std::unique_ptr<Airport>> airports;

struct SceneryPacks {
    std::vector<std::string> sc_paths;
    SceneryPacks(const std::string& xp_dir);
};

extern bool CollectAirports(const std::string& xp_dir);
extern float LegacyAirportSnowDepth(float snow_depth);		// -> adjusted snow depth

#endif
