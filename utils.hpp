#ifndef __UTIL_HPP__
#define __UTIL_HPP__

#include <string>
#include <vector>

#include "Eigen/Core"
#include "Eigen/Dense"
// #include "Eigen/LU"

using namespace Eigen;

class Utils {
public:
    Utils();
    ~Utils();
    static Vector2f localize_uv(const float& u, const float& v);
    static size_t get_udim(const float u, const float v);
    static std::vector<std::string> split(const std::string longText, const char delimiter);
    static std::string join(const std::vector<std::string>& v, const char* delim);
    static std::string pathGetUdim(const std::string path);
    static float sciToFloat(const std::string& str);

private:
};

#endif // __UTIL_HPP__
