#pragma once

#include <string>
#include <vector>

class Image {
public:
    Image();
    ~Image();
    int width;
    int height;
    int nchannels;
    int udim = 0;
    std::vector<float> pixels;
    bool isEmpty = true;
    void loadExr(const std::string& path);
    void loadTif(const std::string& path);

    static void localizeUV(float* localUV, const float& u, const float& v);
    static int getUDIM(const std::string& path);
    static size_t getUDIMfromUV(float u, float v);

private:
};
