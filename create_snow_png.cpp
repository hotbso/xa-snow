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

#include <fstream>
#include <memory>
#include <string>

#include "xa-snow.h"
#include "depth_map.h"

#include <spng.h> // For image processing, include after xa-snow.h


// -> 0 = success
int
SaveImagePng(uint32_t *data, int width, int height, const std::string& png_path)
{
    // create .png
    struct spng_ihdr ihdr = {};

    // Creating an encoder context requires a flag
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);

    // Encode to internal buffer managed by the library
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);

    // Set image properties, this determines the destination image format
    ihdr.width = width;
    ihdr.height = height;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.bit_depth = 8;

    spng_set_ihdr(ctx, &ihdr);

    // SPNG_ENCODE_FINALIZE will finalize the PNG with the end-of-file marker
    int ret = spng_encode_image(ctx, data, width * height * sizeof(uint32_t),
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

    std::ofstream f(png_path, std::ios::binary);
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
    spng_ctx_free(ctx);
    return ret;
}

#define RGBA(R,G,B) \
    ((255 << 24) | (((B)&0xff) << 16) | (((G)&0xff) << 8) | ((R)&0xff))

static constexpr int kWidth = 3600;
static constexpr int kHeight = 1800;

// xlate right to get the common Mercator layout
int xlate(int i) {
    i += kWidth / 2;
    if (i >= kWidth)
        i-= kWidth;
    return i;
}

int
CreateSnowMapPng(DepthMap& snod_map, const std::string& png_path)
{
    auto img = std::make_unique<uint32_t[]>(kWidth * kHeight);

    // land
    uint32_t pixel = RGBA(80, 80, 80);
    for (int i = 0; i < kWidth; i++) {
        for (int j = 0; j < kHeight; j++) {
            if (coast_map.is_land(i, j)) {
               img[(kHeight - j - 1) * kWidth + xlate(i)] = pixel;
            }
        }
    }

    // snow
    for (int i = 0; i < kWidth; i++) {
        for (int j = 0; j < kHeight; j++) {
            float sd = snod_map.get(i, j);
            if (sd <= 0.01f)
                continue;

            static constexpr float sd_max = 0.25f;
            if (sd > sd_max)
                sd = sd_max;

            sd = sd / sd_max;   // scale to [0,1]

            uint32_t pixel;
            static constexpr int ofs = 70;
            uint8_t a = ofs + sd * (255 - ofs);
            if (snod_map.is_extended_snow(i, j)) {
                pixel = RGBA(a, 0, a);
            } else {
                pixel = RGBA(0, a, a);
            }
            img[(kHeight - j - 1) * kWidth + xlate(i)] = pixel;
        }
    }

#if 0
    // incomplete fragment
    // coast line
    for (int i = 0; i < kWidth; i++) {
        for (int j = 0; j < kHeight; j++) {
            auto [yes, _, _, dir] = coast_map.IsCoast(i, j);
            if (yes) {
                float ang = static_cast<float>(dir) * 45.0f;
                ang = 90.0f - ang; // for visualization use true hdg
                float r, g, b;
                // hslToRGBf32 implementation
                png::rgb_pixel cCoast(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
                img[(kHeight - j - 1) * kWidth + i] = cCoast;
            }
        }
    }
#endif

    return SaveImagePng(img.get(), kWidth, kHeight, png_path);
}
