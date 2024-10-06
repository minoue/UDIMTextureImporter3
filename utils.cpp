#include "utils.hpp"
#include <cstddef>
#include <sstream>

Utils::Utils() {};

Utils::~Utils() {};

size_t Utils::get_udim(const float u, const float v)
{
    size_t U = static_cast<size_t>(ceil(u));
    size_t V = static_cast<size_t>(floor(v)) * 10;
    return U + V;
}

Vector2f Utils::localize_uv(const float& u, const float& v)
{
    float u_local = u - floor(u);
    float v_local = v - floor(v);
    Vector2f uv(u_local, v_local);
    return uv;
}

std::vector<std::string> Utils::split(const std::string longText, const char delimiter) {
    std::vector<std::string> stringArray;
    
    if (longText.empty()) {
        return stringArray;
    }
    
    std::stringstream stream{longText};
    std::string buff;
    while (getline(stream, buff, delimiter)) {
        stringArray.push_back(buff);
    }
    return stringArray;
}


// https://marycore.jp/prog/cpp/vector-join/
std::string Utils::join(const std::vector<std::string>& v, const char* delim = 0)
{
    std::string s;
    if (!v.empty()) {
        s += v[0];
        for (decltype(v.size()) i = 1, c = v.size(); i < c; ++i) {
        if (delim) s += delim;
        s += v[i];
    }
  }
  return s;
}

// https://www.oreilly.com/library/view/c-cookbook/0596007612/ch03s06.html
float Utils::sciToFloat(const std::string& str)
{
    std::stringstream ss(str);
    float d = 0;
    ss >> d;

    if (ss.fail()) {
        std::string s = "Unable to format ";
        s += str;
        s += " as a number!";
        throw(s);
    }

    return (d);
}
