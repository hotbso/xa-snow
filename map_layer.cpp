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
#include <cstring>
#include <cstdint>
#include <cassert>
#include <memory>
#include <algorithm>

#include "xa-snow.h"
#include "depth_map.h"

#include <spng.h> // For image processing, include after xa-snow.h
#include <fstream>

#include "XPLMMap.h"
#include "XPLMGraphics.h"

#include <GL/gl.h>

#define RGBA(R,G,B) \
    ((180 << 24) | (((B)&0xff) << 16) | (((G)&0xff) << 8) | ((R)&0xff))

static XPLMMapLayerID map_layer;
static int tex_id;
static int log_cnt;

class Image
{
    bool valid_{false};

    float left_lon_, right_lon_, bottom_lat_, top_lat_;
    float left_x_, right_x_, bottom_y_, top_y_;

    std::unique_ptr<uint32_t[]> data_;
    int width_, height_;

  public:
    void set_bounds(const float *ltrb, XPLMMapProjectionID projection);
    bool check_image();

    // -> left_s, right_s, bottom_t, top_t
    std::tuple<float, float, float, float>get_s_t(const float *ltrb) const;
};


void
Image::set_bounds(const float *ltrb, XPLMMapProjectionID projection)
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

    log_msg("map_bounds: lon: (%0.3f, %0.3f), lat: (%0.3f, %0.3f)",
            left_lon_, right_lon_, bottom_lat_, top_lat_);
    log_msg("map_bounds: x: (%0.2f, %0.2f), y: (%0.2f, %0.2f)",
            left_x_, right_x_, bottom_y_, top_y_);
}

bool
Image::check_image()
{
    if (snod_map == nullptr)
        return false;

    if (valid_)
        return true;

    int left_idx, right_idx, bottom_idx, top_idx, w_step;

    if (left_lon_ < right_lon_) {
        left_idx = (int)(left_lon_ * 10.0f);
        right_idx = (int)(right_lon_ * 10.0f);
        w_step = 1;
        width_ = right_idx - left_idx;
    } else {
        // dateline
        log_msg("crossing dateline NYI");
        return false;
    }

    bottom_idx = (int)(bottom_lat_ * 10.0f) + 900;
    top_idx = (int)(top_lat_ * 10.0f) + 900;
    height_ = top_idx - bottom_idx;

    data_ = std::make_unique<uint32_t[]>(width_ * height_);

    int pix_idx = 0;
    for (int j = bottom_idx; j < top_idx; j++) {
        for (int i = left_idx; i < right_idx; i += w_step) {
            assert(0 <= pix_idx && pix_idx < width_ * height_);
            uint32_t *pix_ptr = &data_[pix_idx++];
            uint32_t pixel;

            float sd = snod_map->get(i, j);
            log_msg("(%d, %d), sd: %0.3f", i, j, sd);
            auto [is_coast, dir_x, dir_y, dir_angle] = coast_map.is_coast(i, j);
            if (is_coast)
                *pix_ptr = RGBA(0, 255, 0);

            if (sd <= 0.01f)
                continue;

            static constexpr float sd_max = 0.25f;
            if (sd > sd_max)
                sd = sd_max;

            sd = sd / sd_max;   // scale to [0,1]

            static constexpr int ofs = 70;
            uint8_t a = ofs + sd * (255 - ofs);
            if (snod_map->is_extended_snow(i, j)) {
                pixel = RGBA(a, 0, a);
            } else {
                pixel = RGBA(0, a, a);
            }

            if (is_coast)
                pixel = RGBA(0, 255, 0);

            log_msg("pix_idx: %d, pixel: %08x", pix_idx, pixel);
            *pix_ptr = pixel;
        }
    }
#if 1
//---------------------
    // create .png
    std::string png_path = "map.png";
    struct spng_ihdr ihdr = {};

    // Creating an encoder context requires a flag
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);

    // Encode to internal buffer managed by the library
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);

    // Set image properties, this determines the destination image format
    ihdr.width = width_;
    ihdr.height = height_;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.bit_depth = 8;

    spng_set_ihdr(ctx, &ihdr);

    // SPNG_ENCODE_FINALIZE will finalize the PNG with the end-of-file marker
    int ret = spng_encode_image(ctx, data_.get(), width_ * height_ * sizeof(uint32_t),
                            SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    if (ret) {
        log_msg("spng_encode_image() error: %s", spng_strerror(ret));
        spng_ctx_free(ctx);
        return ret;
    }

    size_t png_size;
    void *png_buf = spng_get_png_buffer(ctx, &png_size, &ret);
    // User owns the buffer after a successful call

    if (png_buf == NULL) {
        log_msg("spng_get_png_buffer() error: %s", spng_strerror(ret));
        return ret;
    }

    log_msg("PNG size: %d", (int)png_size);

    std::fstream f(png_path, std::ios::binary | std::ios_base::out | std::ios_base::trunc);
    if (f.fail()) {
        log_msg("Can't open '%s'", png_path.c_str());
        return 1;
    }

    f.write((const char*)png_buf, png_size);
    f.close();
    if (f.fail())
        log_msg("write to png failed");
    else
        log_msg("PNG '%s' created", png_path.c_str());

    free(png_buf);
//------------
#endif
    XPLMBindTexture2d(tex_id, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data_.get());

    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR) {
        log_msg("Gl error %d", err);
    }

    valid_ = true;
    log_msg("texture created, width: %d, height: %d", width_, height_);
    return true;
}

std::tuple<float, float, float, float>
Image::get_s_t(const float *ltrb) const
{
    float left_s = std::clamp((ltrb[0] - left_x_) / (right_x_ - left_x_), 0.0f, 1.0f);
    float top_t = std::clamp((ltrb[1] - bottom_y_) / (top_y_ - bottom_y_), 0.0f, 1.0f);

    float right_s = std::clamp((ltrb[2] - left_x_) / (right_x_ - left_x_), 0.0f, 1.0f);
    float bottom_t = std::clamp((ltrb[3] - bottom_y_) / (top_y_ - bottom_y_), 0.0f, 1.0f);
    return std::make_tuple(left_s, right_s, bottom_t, top_t);
}

static Image image;

static void
SaveBounds([[maybe_unused]]XPLMMapLayerID layer, const float *ltrb,
           XPLMMapProjectionID projection, [[maybe_unused]] void *inRefcon)
{
    image.set_bounds(ltrb, projection);
}

void
DrawSnow([[maybe_unused]] XPLMMapLayerID layer, const float *ltrb, [[maybe_unused]] float zoomRatio,
         [[maybe_unused]] float mapUnitsPerUserInterfaceUnit,
         [[maybe_unused]] XPLMMapStyle mapStyle, [[maybe_unused]] XPLMMapProjectionID projection,
         [[maybe_unused]] void *inRefcon)
{
    if (! image.check_image())
        return;

    float left = ltrb[0];
    float top = ltrb[1];
    float right = ltrb[2];
    float bottom = ltrb[3];
    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR) {
        //log_msg("Gl error %d", err);
    }

    if (log_cnt++ >= 30) {
        log_cnt = 0;
        log_msg("draw: x: (%0.3f, %0.3f), y: (%0.3f, %0.3f)",
                left, right, bottom, top);
    }

    auto [left_s, right_s, bottom_t, top_t] = image.get_s_t(ltrb);

	XPLMSetGraphicsState(
			0 /* no fog */,
			1 /* 1 texture units */,
			0 /* no lighting */,
			0 /* no alpha testing */,
			1 /* do alpha blend */,
			0 /* do depth testing */,
			0 /* no depth writing */
	);

    XPLMBindTexture2d(tex_id, 0);

    glBegin(GL_QUADS);
        glTexCoord2f(left_s, bottom_t);
        glVertex2f(left, bottom);

        glTexCoord2f(left_s, top_t);
        glVertex2f(left, top);

        glTexCoord2f(right_s, top_t);
        glVertex2f(right, top);

        glTexCoord2f(right_s, bottom_t);
        glVertex2f(right, bottom);
    glEnd();

    while((err = glGetError()) != GL_NO_ERROR) {
        log_msg("Gl error %d", err);
    }
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
        log_msg("creating map layer");
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
    XPLMGenerateTextureNumbers(&tex_id, 1);
}

void
MapLayerEnableHook(void)
{
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
}

void
MapLayerStopHook(void)
{
	if (map_layer) {
		XPLMDestroyMapLayer(map_layer);
        map_layer = NULL;
	}
}
