#pragma once

#include <string>
#include <vector>


struct UV {
    float u, v;
    UV(const float& a, const float& b) : u(a), v(b) {} // NOLINT
};


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
    
    static auto localizeUV(const UV& uv) -> UV;
    static auto getUDIMfromPath(const std::string& path) -> int;
    static auto getUDIMfromUV(const UV& uv) -> size_t;

private:
};
