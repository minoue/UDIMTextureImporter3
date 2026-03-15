#ifndef __UDIMTextureImporter3_h__
#define __UDIMTextureImporter3_h__

#include <windows.h>
#include <vector>
#include "../third_party/Eigen/Core"

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

extern "C" __declspec(dllexport) auto importUDIM(char* textFromZBrush,
    double value,
    char* pOptBuffer1,
    char* pOptBuffer2) -> float;

#endif /* __UDIMTextureImporter3_h__ */
