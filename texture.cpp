#define TINYEXR_IMPLEMENTATION
#include <string>
#include <cstdint>

#include "tinyexr.h"
#include "texture.hpp"
#include "tiffio.h"


Image::Image() {};

Image::~Image() {};

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
        this->pixels.resize(size_t(size));
        for (int i = 0; i < size; i++) {
            float x = out[i];
            this->pixels[size_t(i)] = x;
        }

        free(out); // release memory of image data
    }
}

void Image::loadTif(const std::string& path) {
    TIFF* tif = TIFFOpen(path.c_str(), "r");

    if (tif) {
        uint32_t width, height;
        tdata_t buf;
        uint16_t bitDepth, nchannels;

        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &nchannels);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitDepth);

        this->nchannels = int(nchannels);
        this->width = int(width);
        this->height = int(height);
        this->pixels.reserve(width * height * nchannels);

        std::string chaStr = "Number of channels: " + std::to_string(nchannels);
        OutputDebugString(chaStr.c_str());

        std::string depthStr = "Bit depth is : " + std::to_string(bitDepth);
        OutputDebugString(depthStr.c_str());

        buf = _TIFFmalloc(TIFFScanlineSize(tif));

        for (unsigned int row = 0; row < height; row++) {
            TIFFReadScanline(tif, buf, row);
            for (unsigned int col=0; col<width; col++) {
                if (bitDepth == 32) {
                    float r = static_cast<float*>(buf)[col * uint16_t(nchannels) + 0];
                    float g = static_cast<float*>(buf)[col * uint16_t(nchannels) + 1];
                    float b = static_cast<float*>(buf)[col * uint16_t(nchannels) + 2];
                    this->pixels.push_back(r);
                    this->pixels.push_back(g);
                    this->pixels.push_back(b);
                } else if (bitDepth == 16) {
                    uint16_t r = static_cast<uint16_t*>(buf)[col * nchannels + 0];
                    uint16_t g = static_cast<uint16_t*>(buf)[col * nchannels + 1];
                    uint16_t b = static_cast<uint16_t*>(buf)[col * nchannels + 2];
                } else {
                    uint16_t r = static_cast<uint8_t*>(buf)[col * nchannels + 0];
                    uint16_t g = static_cast<uint8_t*>(buf)[col * nchannels + 1];
                    uint16_t b = static_cast<uint8_t*>(buf)[col * nchannels + 2];
                }
            }
        }
        _TIFFfree(buf);
        TIFFClose(tif);
    }
}
