#pragma once

#include <vector>
#include <string>

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

    static int getUDIM(const std::string& path);
private:
};
