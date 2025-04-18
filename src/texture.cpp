#define TINYEXR_IMPLEMENTATION
#include <cstdint>
#include <filesystem>
#include <string>

#include "texture.hpp"
#include "tiffio.h"
#include "tinyexr.h"

Image::Image() {};

Image::~Image() {};

/**
 * @brief Convert uv values to values of UDIM local. eg. (1.5, 1.5) -> (0.5, 0.5)
 * @param[out] localUV UDIM localizwed UV values
 * @param[in] u global u value
 * @param[in] v global v value
 */
void Image::localizeUV(float* localUV, const float& u, const float& v)
{
    float u_local = u - floor(u);
    float v_local = v - floor(v);
    localUV[0] = u_local;
    localUV[1] = v_local;
}

/**
 * @brief Get UDIM from path string
 * @param[in] path file path, eg. C:/path/to/texture.1001.exr
 * @return : UDIM number as int, eg. 1001, 1011
 */
int Image::getUDIMfromPath(const std::string& path)
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
 * @return : UDIM number without 1001, eg. 1001 is 1, 1011 is 11
 */
size_t Image::getUDIMfromUV(float u, float v)
{
    size_t U = static_cast<size_t>(ceil(u));
    size_t V = static_cast<size_t>(floor(v)) * 10;
    return U + V;
}

void Image::loadExr(const std::string& path)
{
    int& width = this->width;
    int& height = this->height;
    int& nchannels = this->nchannels;
    nchannels = 4;

    float* out;
    const char* err = nullptr;

    int ret = LoadEXR(&out, &width, &height, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        if (err) {
            fprintf(stderr, "ERR : %s\n", err);
            FreeEXRErrorMessage(err); // release memory of error message.
            exit(0);
        }
    } else {
        int size = width * height * nchannels;
        this->pixels.resize(static_cast<size_t>(size));
        for (int i = 0; i < size; i++) {
            float x = out[i];
            this->pixels[static_cast<size_t>(i)] = x;
        }

        free(out); // release memory of image data
    }
}

void Image::loadTif(const std::string& path)
{
    TIFF* tif = TIFFOpen(path.c_str(), "r");

    if (tif) {
        uint32_t width, height;
        tdata_t buf;
        uint16_t bitDepth, nchannels;

        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &nchannels);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitDepth);

        this->nchannels = static_cast<int>(nchannels);
        this->width = static_cast<int>(width);
        this->height = static_cast<int>(height);
        this->pixels.reserve(width * height * nchannels);

        std::string chaStr = "Number of channels: " + std::to_string(nchannels);
        OutputDebugString(chaStr.c_str());

        std::string depthStr = "Bit depth is : " + std::to_string(bitDepth);
        OutputDebugString(depthStr.c_str());

        buf = _TIFFmalloc(TIFFScanlineSize(tif));

        for (unsigned int row = 0; row < height; row++) {
            TIFFReadScanline(tif, buf, row);
            for (unsigned int col = 0; col < width; col++) {
                if (bitDepth == 32) {
                    float r = static_cast<float*>(buf)[col * uint16_t(nchannels) + 0];
                    float g = static_cast<float*>(buf)[col * uint16_t(nchannels) + 1];
                    float b = static_cast<float*>(buf)[col * uint16_t(nchannels) + 2];
                    this->pixels.push_back(r);
                    this->pixels.push_back(g);
                    this->pixels.push_back(b);
                    if (nchannels == 4) {
                        float a = static_cast<float*>(buf)[col * uint16_t(nchannels) + 3];
                        this->pixels.push_back(a);
                    }
                } else if (bitDepth == 16) {
                    uint16_t r = static_cast<uint16_t*>(buf)[col * nchannels + 0];
                    uint16_t g = static_cast<uint16_t*>(buf)[col * nchannels + 1];
                    uint16_t b = static_cast<uint16_t*>(buf)[col * nchannels + 2];
                    this->pixels.push_back(float(r / 65535.0));
                    this->pixels.push_back(float(g / 65535.0));
                    this->pixels.push_back(float(b / 65535.0));
                    if (nchannels == 4) {
                        uint16_t a = static_cast<uint16_t*>(buf)[col * nchannels + 3];
                        this->pixels.push_back(float(a / 65535.0));
                    }
                } else {
                    // 8-bit
                    uint16_t r = static_cast<uint8_t*>(buf)[col * nchannels + 0];
                    uint16_t g = static_cast<uint8_t*>(buf)[col * nchannels + 1];
                    uint16_t b = static_cast<uint8_t*>(buf)[col * nchannels + 2];
                    this->pixels.push_back(float(r / 255.0));
                    this->pixels.push_back(float(g / 255.0));
                    this->pixels.push_back(float(b / 255.0));
                    if (nchannels == 4) {
                        uint16_t a = static_cast<uint8_t*>(buf)[col * nchannels + 3];
                        this->pixels.push_back(float(a / 255.0));
                    }
                }
            }
        }
        _TIFFfree(buf);
        TIFFClose(tif);
    }
}
