#define TINYEXR_IMPLEMENTATION
#include <filesystem>
#include <stdexcept>
#include <string>
#include <cmath>
#include <span>

#include "texture.hpp"
#include "tiffio.h"
#include "../third_party/tinyexr/tinyexr.h"

/**
 * @brief Convert global uv values to values of UDIM local. eg. (1.5, 1.5) -> (0.5, 0.5)
 * @param[out] localUV UDIM localizwed UV values
 * @param[in] u global u value
 * @param[in] v global v value
 * @return : UV in UDIM local
 */
auto Image::localizeUV(const UV& uv) -> UV
{
    float u_local = uv.u - std::floor(uv.u);
    float v_local = uv.v - std::floor(uv.v);
    UV localUV(u_local, v_local);
    return localUV;
}

/**
 * @brief Get UDIM from path string
 * @param[in] path file path, eg. C:/path/to/texture.1001.exr
 * @return : UDIM number as int, eg. 1001, 1011
 */
auto Image::getUDIMfromPath(const std::string& path) -> int
{
    std::filesystem::path tempPath = path;
    const std::string stem = tempPath.stem().string();
    size_t strLen = stem.length();
    const std::string udim_str = stem.substr(strLen - 4);
    int udim = std::stoi(udim_str);
    return udim;
}

/**
 * @brief Get UDIM from UV value
 * @param[in] u u value
 * @param[in] v v value
 * @return : UDIM number without 1000, eg. 1 as 1001, 11 as 1011.
 */
auto Image::getUDIMfromUV(const UV& uv) -> size_t
{
    // UVs at exactly 0 or negative are outside any UDIM tile. Return 0 so
    // callers can skip them. This also avoids casting a negative float to
    // size_t, which is undefined behavior.
    if (uv.u <= 0.0F || uv.v <= 0.0F) {
        return 0;
    }
    auto U = static_cast<size_t>(std::ceil(uv.u));
    auto V = static_cast<size_t>(std::floor(uv.v)) * 10; //NOLINT
    return U + V;
}

void Image::loadExr(const std::string& path)
{
    int& width = this->width;
    int& height = this->height;
    int& nchannels = this->nchannels;
    nchannels = 4;

    float* out = nullptr;
    const char* err = nullptr;

    int ret = LoadEXR(&out, &width, &height, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string message = "Failed to load EXR: " + path;
        if (err != nullptr) {
            message += " : ";
            message += err;
            FreeEXRErrorMessage(err); // release memory of error message.
        }
        free(out); // NOLINT
        throw std::runtime_error(message);
    }

    size_t size = static_cast<size_t>(width * height * nchannels); // NOLINT
    std::span<float> out_span(out, size);
    this->pixels.resize(size);
    for (size_t i = 0; i < size; i++) {
        // float x = out[i];
        float x = out_span[i];
        this->pixels[i] = x;
    }

    free(out); // release memory of image data NOLINT
}

void Image::loadTif(const std::string& path)
{
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (tif == nullptr) {
        throw std::runtime_error("Failed to open TIFF: " + path);
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitDepth = 0;
    uint16_t nchannels = 0;
    uint16_t planarConfig = PLANARCONFIG_CONTIG;

    if (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) != 1
        || TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) != 1) {
        TIFFClose(tif);
        throw std::runtime_error("Failed to read the TIFF image size: " + path);
    }

    // These tags may be omitted, in which case libtiff provides the defaults
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &nchannels);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitDepth);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planarConfig);

    std::string error;
    if (TIFFIsTiled(tif) != 0) {
        error = "Tiled TIFF is not supported, save it as scanline/strip: " + path;
    } else if (nchannels != 1 && nchannels != 3 && nchannels != 4) {
        error = "Unsupported number of TIFF channels (" + std::to_string(nchannels)
            + "), only 1 (grayscale), 3 (RGB) and 4 (RGBA) are supported: " + path;
    } else if (bitDepth != 8 && bitDepth != 16 && bitDepth != 32) {
        error = "Unsupported TIFF bit depth (" + std::to_string(bitDepth)
            + "), only 8, 16 and 32 bit are supported: " + path;
    } else if (nchannels != 1 && planarConfig != PLANARCONFIG_CONTIG) {
        error = "Separate-plane TIFF is not supported: " + path;
    }
    if (!error.empty()) {
        TIFFClose(tif);
        throw std::runtime_error(error);
    }

    this->width = static_cast<int>(width);
    this->height = static_cast<int>(height);
    // Grayscale pixels are expanded to RGB below so the sampling code can
    // always read 3 values per pixel
    this->nchannels = (nchannels == 1) ? 3 : static_cast<int>(nchannels);
    this->pixels.reserve(static_cast<size_t>(width) * height * static_cast<size_t>(this->nchannels));

    tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
    if (buf == nullptr) {
        TIFFClose(tif);
        throw std::runtime_error("Failed to allocate the TIFF scanline buffer: " + path);
    }

    auto readSample = [buf, bitDepth, nchannels](uint32_t col, uint16_t channel) -> float {
        const size_t i = (static_cast<size_t>(col) * nchannels) + channel;
        if (bitDepth == 32) {
            return static_cast<float*>(buf)[i]; // NOLINT
        }
        if (bitDepth == 16) {
            return static_cast<float>(static_cast<uint16_t*>(buf)[i]) / 65535.0F; // NOLINT
        }
        // 8-bit
        return static_cast<float>(static_cast<uint8_t*>(buf)[i]) / 255.0F; // NOLINT
    };

    for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row) != 1) {
            _TIFFfree(buf);
            TIFFClose(tif);
            throw std::runtime_error("Failed to read a TIFF scanline (row "
                + std::to_string(row) + "): " + path);
        }
        for (uint32_t col = 0; col < width; col++) {
            if (nchannels == 1) {
                // Grayscale: store the single value as R, G and B
                const float v = readSample(col, 0);
                this->pixels.push_back(v);
                this->pixels.push_back(v);
                this->pixels.push_back(v);
            } else {
                this->pixels.push_back(readSample(col, 0));
                this->pixels.push_back(readSample(col, 1));
                this->pixels.push_back(readSample(col, 2));
                if (nchannels == 4) {
                    this->pixels.push_back(readSample(col, 3));
                }
            }
        }
    }
    _TIFFfree(buf);
    TIFFClose(tif);
}
