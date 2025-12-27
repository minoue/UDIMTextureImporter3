#ifndef __UDIMTextureImporter3_h__
#define __UDIMTextureImporter3_h__

#include <vector>

#ifdef _WIN32
#include <windows.h>
#define EXPORT __declspec(dllexport)
#endif

#include "Eigen/Core"
#include "Eigen/Dense"

#ifdef __cplusplus
extern "C" {
#endif

using namespace Eigen;

struct FaceVertex {
    size_t vertexIndex;
    Vector3f pointPosition;
    Vector3f uvw;
    float u;
    float v;
};

struct Point : Vector3f {
    bool isDone = false;
};

struct Face {
    std::vector<FaceVertex> FaceVertices;
    size_t faceIndex;
};

float EXPORT importUDIM(char* textFromZBrush,
    double value,
    char* pOptBuffer1,
    char* pOptBuffer2);

#ifdef __cplusplus
}
#endif

#endif /* __UDIMTextureImporter3_h__ */
