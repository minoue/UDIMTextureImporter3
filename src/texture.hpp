#pragma once

#include <string>
#include <vector>

class Image {
public:
    Image() = default;
    int width{};
    int height{};
    int nchannels{};
    int udim = 0;
    std::vector<float> pixels;
    bool isEmpty = true;
    void loadExr(const std::string& path);
    void loadTif(const std::string& path);

    static void localizeUV(float* localUV, const float& u, const float& v);
    static auto getUDIMfromPath(const std::string& path) -> int;
    static auto getUDIMfromUV(float u, float v) -> size_t;

private:
};
