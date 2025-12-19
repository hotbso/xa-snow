//
//    X Airline Snow: show accumulated snow in X-Plane's world
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

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <algorithm>

#include "xa-snow.h"
#include "depth_map.h"
#include "coast_map.h"

#include "XPLMMap.h"
#include "XPLMGraphics.h"

#if APL == 1
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define RGBA(R,G,B) \
    ((150 << 24) | (((B)&0xff) << 16) | (((G)&0xff) << 8) | ((R)&0xff))

// if you define DEBUG_COLORS in the environment you get a coloring of coast and extended snow
static bool debug_colors;

static XPLMMapLayerID map_layer;

class MapTexture
{
    using Pixel = uint32_t;

    bool valid_{false};
    GLuint tex_id_;
    int snod_seqno_;

    float left_lon_, right_lon_, bottom_lat_, top_lat_;
    float left_x_, right_x_, bottom_y_, top_y_;

  public:
    MapTexture();
    ~MapTexture();
    void set_bounds(const float *ltrb, XPLMMapProjectionID projection);
    bool check_image();

    void draw(const float *ltrb);
};

static std::unique_ptr<MapTexture> map_tex;

MapTexture::MapTexture()
{
    XPLMGenerateTextureNumbers((int *)&tex_id_, 1);
    LogMsg("MapTexture created, tex_id_: %d", tex_id_);
}

MapTexture::~MapTexture()
{
    XPLMBindTexture2d(tex_id_, 0);
    glDeleteTextures(1, &tex_id_);
    LogMsg("MapTexture destroyed, tex_id_: %d", tex_id_);
}

void
MapTexture::set_bounds(const float *ltrb, XPLMMapProjectionID projection)
{
    valid_ = false;

    double lt_lat, lt_lon, rb_lat, rb_lon;
    XPLMMapUnproject(projection, ltrb[0], ltrb[1], &lt_lat, &lt_lon);
    XPLMMapUnproject(projection, ltrb[2], ltrb[3], &rb_lat, &rb_lon);

    left_x_ = ltrb[0];
    top_y_ = ltrb[1];
    right_x_ = ltrb[2];
    bottom_y_ = ltrb[3];

    left_lon_ = lt_lon;
    top_lat_ = lt_lat;
    right_lon_ = rb_lon;
    bottom_lat_ = rb_lat;

    LogMsg("map_bounds: lon: (%0.3f, %0.3f), lat: (%0.3f, %0.3f)",
            left_lon_, right_lon_, bottom_lat_, top_lat_);
    LogMsg("map_bounds: x: (%0.2f, %0.2f), y: (%0.2f, %0.2f)",
            left_x_, right_x_, bottom_y_, top_y_);
}

bool
MapTexture::check_image()
{
    if (snod_map == nullptr)
        return false;

    if (snod_seqno_ != snod_map->seqno())   // have map of stale revision
        valid_ = false;

    if (valid_)
        return true;

    int width, height;

    static constexpr float dll = 0.005f;    // lon, lat step size

    if (left_lon_ < right_lon_) {
        width = (right_lon_ - left_lon_) / dll;
    } else {
        // dateline
        LogMsg("crossing dateline NYI");
        return false;
    }

    height = (top_lat_ - bottom_lat_) / dll;

    std::unique_ptr<Pixel[]> data = std::make_unique<Pixel[]>(width * height);

    int pix_idx = 0;
    float lat = bottom_lat_;
    for (int j = 0; j < height; j++) {
        float lon = left_lon_;
        for (int i = 0; i < width; i++) {
            float sd = snod_map->get(lon, lat);
            //LogMsg("(%d, %d), sd: %0.3f", i, j, sd);

            bool is_coast = false;
            if (debug_colors) {
                int dir_x, dir_y, dir_angle;
                std::tie(is_coast, dir_x, dir_y, dir_angle) = coast_map.is_coast(lon, lat);
                if (is_coast)
                    data[pix_idx] = RGBA(0, 255, 0);
            }

            if (sd > 0.015f) {
                static constexpr float sd_max = 0.25f;
                if (sd > sd_max)
                    sd = sd_max;

                sd = sd / sd_max;   // scale to [0,1]

                static constexpr int ofs = 50;
                uint8_t a = ofs + sd * (255 - ofs);
                Pixel pixel;
                if (debug_colors && snod_map->is_extended_snow(lon, lat))
                    pixel = RGBA(a, 0, a);
                else
                    pixel = RGBA(0, a, a);

                if (is_coast)
                    pixel = RGBA(0, 255, 0);
                //LogMsg("pix_idx: %d, pixel: %08x", pix_idx, pixel);
                data[pix_idx] = pixel;
            }
            lon += dll;
            pix_idx++;
        }
        lat += dll;
    }

    // SaveImagePng(data.get(), width, height, "map.png");

    XPLMBindTexture2d(tex_id_, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.get());

    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR) {
        LogMsg("Gl error %d", err);
    }

    valid_ = true;
    snod_seqno_ = snod_map->seqno();
    LogMsg("texture created, width: %d, height: %d", width, height);
    return true;
}

void
MapTexture::draw(const float *ltrb)
{
    if (! check_image())
        return;

    float left_s = std::clamp((ltrb[0] - left_x_) / (right_x_ - left_x_), 0.0f, 1.0f);
    float top_t = std::clamp((ltrb[1] - bottom_y_) / (top_y_ - bottom_y_), 0.0f, 1.0f);

    float right_s = std::clamp((ltrb[2] - left_x_) / (right_x_ - left_x_), 0.0f, 1.0f);
    float bottom_t = std::clamp((ltrb[3] - bottom_y_) / (top_y_ - bottom_y_), 0.0f, 1.0f);

	XPLMSetGraphicsState(
			0, // no fog
			1, // 1 texture units
			0, // no lighting
			0, // no alpha testing
			1, // do alpha blend
			0, // no depth testing
			0  // no depth writing
	);

    XPLMBindTexture2d(tex_id_, 0);

    glBegin(GL_QUADS);
        glTexCoord2f(left_s, bottom_t);
        glVertex2f(ltrb[0], ltrb[3]);

        glTexCoord2f(left_s, top_t);
        glVertex2f(ltrb[0], ltrb[1]);

        glTexCoord2f(right_s, top_t);
        glVertex2f(ltrb[2], ltrb[1]);

        glTexCoord2f(right_s, bottom_t);
        glVertex2f(ltrb[2], ltrb[3]);
    glEnd();

    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR) {
        LogMsg("Gl error %d", err);
    }
}

static void
SaveBounds([[maybe_unused]] XPLMMapLayerID layer, const float *ltrb,
           XPLMMapProjectionID projection, [[maybe_unused]] void *inRefcon)
{
    if (map_tex)
        map_tex->set_bounds(ltrb, projection);
}

void
DrawSnow([[maybe_unused]] XPLMMapLayerID layer, const float *ltrb, [[maybe_unused]] float zoomRatio,
         [[maybe_unused]] float mapUnitsPerUserInterfaceUnit,
         [[maybe_unused]] XPLMMapStyle mapStyle, [[maybe_unused]] XPLMMapProjectionID projection,
         [[maybe_unused]] void *inRefcon)
{
    if (map_tex)
        map_tex->draw(ltrb);
}


void
DeleteNotify(XPLMMapLayerID layer, [[maybe_unused]] void *inRefcon)
{
	if (layer == map_layer)
		map_layer = NULL;
}

static void
CreateMapLayer(const char *mapIdentifier, [[maybe_unused]] void *refcon)
{
	if((NULL == map_layer) && (0 == strcmp(mapIdentifier, XPLM_MAP_USER_INTERFACE))) {
        LogMsg("creating map layer");
		XPLMCreateMapLayer_t params;
		params.structSize = sizeof(XPLMCreateMapLayer_t);
		params.mapToCreateLayerIn = XPLM_MAP_USER_INTERFACE;
		params.willBeDeletedCallback = &DeleteNotify;
		params.prepCacheCallback = &SaveBounds;
		params.showUiToggle = 1;
		params.refcon = NULL;
		params.layerType = xplm_MapLayer_Fill;
		params.drawCallback = &DrawSnow;
		params.iconCallback = NULL;
		params.labelCallback = NULL;
		params.layerName = "Snow";

		map_layer = XPLMCreateMapLayer(&params);
	}
}

//--- plugin API hooks -----------------------------------------------------------------
void
MapLayerStartHook(void)
{
    debug_colors = (std::getenv("DEBUG_COLORS") != nullptr);
}

void
MapLayerEnableHook(void)
{
    map_tex = std::make_unique<MapTexture>();

	if (XPLMMapExists(XPLM_MAP_USER_INTERFACE)) {
		CreateMapLayer(XPLM_MAP_USER_INTERFACE, NULL);
	}

	XPLMRegisterMapCreationHook(&CreateMapLayer, NULL);
}

void
MapLayerDisableHook(void)
{
	if (map_layer) {
		XPLMDestroyMapLayer(map_layer);
        map_layer = NULL;
	}

    map_tex = nullptr;
}

void
MapLayerStopHook(void)
{
	if (map_layer) {
		XPLMDestroyMapLayer(map_layer);
        map_layer = NULL;
	}
}
