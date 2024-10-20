#define _SILENCE_ALL_CXX23_DEPRECATION_WARNINGS

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <format>

#ifdef USE_OPENMP
#include <omp.h>
#endif

#include "main.hpp"
#include "sdk/GoZ_Mesh.h"
#include "texture.hpp"

using namespace Eigen;

void initTextures(std::string& pathStringArray, std::vector<Image>& data)
{
    using std::operator""sv;

    std::vector<std::filesystem::path> paths;
    int maxUDIM = 0;

    for (auto sv : pathStringArray | std::views::split("#"sv)) {
        const std::string p = { sv.begin(), sv.end() };

        // You may get an empty string at the end of the loop so skip it
        if (p.empty()) {
            continue;
        }

        std::filesystem::path path = p;
        paths.push_back(path);

        // Get udim in int eg. 1001, 1011
        int udim = Image::getUDIMfromPath(p);

        int udimCount = udim - 1000;
        if (udimCount > maxUDIM) {
            maxUDIM = udimCount;
        }
    }

    data.resize(size_t(maxUDIM));

    int numPaths = (int)paths.size();

    // Create texture data
    int inc = 1;
#pragma omp parallel for
    for (int i = 0; i <numPaths; i++) {
        std::filesystem::path& path = paths[(size_t)i];
        std::string ext = path.extension().string();

        Image img;
        img.isEmpty = false;

        if (ext == ".exr") {
            img.loadExr(path.string());
        } else if (ext == ".tif" || ext == ".tiff") {
            img.loadTif(path.string());
        } else {
            std::cout << "Not suppported file format" << std::endl;
        }

        int udim = Image::getUDIMfromPath(path.string());
        int udimCount = udim - 1000;
        data[size_t(udimCount) - 1] = img;

        auto msg = std::format("{}/{} : " , inc, numPaths);

#pragma omp atomic
        ++inc;

#pragma omp critical
        std::cout << "Loaded " << msg << path.string() << std::endl;
    }
}

/**
 * @brief Compute tangent and bitangent using 3 point triangle
 * @param [in] P0 : current point of triangle
 * @param [in] P1 : previous point of triangle
 * @param [in] P2 : next point of triangle
 * @param [in] uv0 : current UV of triangle
 * @param [in] uv1 : previous UV of triangle
 * @param [in] uv2 : next UV of triangle
 * @param [out] T : Tangent
 * @param [out] B : Bitangent
 * @param [in] N : Normal
 * @return : Tangent matrix
 */
// https://stackoverflow.com/questions/5255806/how-to-calculate-tangent-and-binorma
Matrix3f computeTangentMatrix(const Vector3f& P0, const Vector3f& P1,
    const Vector3f& P2, const Vector3f& uv0,
    const Vector3f& uv1, const Vector3f& uv2, Vector3f& T,
    Vector3f& B, Vector3f& N)
{

    // 2 edges (E1, E2) of the triangle in 3d space
    Vector3f E1 = P1 - P0;
    Vector3f E2 = P2 - P0;

    // 2 edges of the triangle in uv space
    Vector3f E1_uv = uv1 - uv0;
    Vector3f E2_uv = uv2 - uv0;

    // Create Matrix using 3d points
    // |E1.x, E1.y, E1.z|
    // |E2.x, E2.y, E2.z|
    MatrixXf M_3d(2, 3);
    M_3d << E1.x(), E1.y(), E1.z(), E2.x(), E2.y(), E2.z();

    // Same for using uv points
    // |E1.x, E1.y|
    // |E2.x, E2.y|
    Matrix2f M_2d;
    M_2d << E1_uv.x(), E1_uv.y(), E2_uv.x(), E2_uv.y();

    // Get new tangent and bitangent by multiplying inverted M_2d by M_3d
    // |T|       -1
    // |B| = M_2d   * M_3d
    MatrixXf result(2, 3);
    result = M_2d.inverse() * M_3d;

    // Get the first row as the new tangent vector
    Vector3f new_T = result.row(0);

    // ???
    // new_T -= N * new_T.dot(N);
    new_T.normalize();

    // New bitangent
    Vector3f bitangent = N.cross(new_T);

    T = new_T;
    B = bitangent.normalized();

    Matrix3f mat;
    mat << T.x(), T.y(), T.z(), N.x(), N.y(), N.z(), B.x(), B.y(), B.z();
    return mat;
}

Vector3f getPixelValue(const float u, const float v,
    const std::vector<float>& texture, const int width,
    const int height, const int nchannel)
{
    // Get pixel values by bilinear filtering

    float float_width = (float)width;
    float float_height = (float)height;

    int x1 = int(std::round(float_width * u));
    int x2 = x1 + 1;
    int y1 = static_cast<int>(std::round(float_height * (1 - v)));
    int y2 = y1 + 1;

    size_t target_pixel1 = size_t(((width * (y1 - 1) + x1) - 1) * nchannel);
    size_t target_pixel2 = size_t(((width * (y1 - 1) + x2) - 1) * nchannel);
    size_t target_pixel3 = size_t(((width * (y2 - 1) + x1) - 1) * nchannel);
    size_t target_pixel4 = size_t(((width * (y2 - 1) + x2) - 1) * nchannel);

    Vector3f A;
    A << texture[target_pixel1], texture[target_pixel1 + 1],
        texture[target_pixel1 + 2];
    Vector3f B;
    B << texture[target_pixel2], texture[target_pixel2 + 1],
        texture[target_pixel2 + 2];
    Vector3f C;
    C << texture[target_pixel3], texture[target_pixel3 + 1],
        texture[target_pixel3 + 2];
    Vector3f D;
    D << texture[target_pixel4], texture[target_pixel4 + 1],
        texture[target_pixel4 + 2];

    float u1 = (static_cast<float>(x1) - 0.5f) / float_width;
    float u2 = (static_cast<float>(x2) - 0.5f) / float_width;
    float v1 = (static_cast<float>(y1) - 0.5f) / float_height;
    float v2 = (static_cast<float>(y2) - 0.5f) / float_height;

    Vector3f E = ((u2 - u) / (u2 - u1)) * A + ((u - u1) / (u2 - u1)) * B;
    Vector3f F = ((u2 - u) / (u2 - u1)) * C + ((u - u1) / (u2 - u1)) * D;
    Vector3f G = ((v2 - (1 - v)) / (v2 - v1)) * E + (((1 - v) - v1) / (v2 - v1)) * F;

    return G;
}

void initMesh(GoZ_Mesh* mesh, std::vector<Point>& vertices,
    std::vector<Vector3f>& normals, std::vector<Face>& faces)
{
    int numVerts = mesh->m_vertexCount;
    vertices.reserve(size_t(numVerts));
    for (int i = 0; i < numVerts * 3; i += 3) {
        float x = mesh->m_vertices[i];
        float y = mesh->m_vertices[i + 1];
        float z = mesh->m_vertices[i + 2];
        Point b;
        b << x, y, z;
        vertices.push_back(b);
    }

    // Init normal array
    normals.resize(size_t(numVerts));
    Vector3f zeroVec(0, 0, 0);
    std::fill(normals.begin(), normals.end(), zeroVec);

    // Face array
    int numFaces = mesh->m_faceCount;
    faces.reserve(size_t(numFaces));

    // Loop over all faces
    for (int n = 1; n <= numFaces; n++) {
        int faceIndex = n - 1;
        int arrayIndex1 = 4 * n - 4;
        int arrayIndex2 = 4 * n - 3;
        int arrayIndex3 = 4 * n - 2;
        int arrayIndex4 = 4 * n - 1;

        int vertexIndex1 = mesh->m_vertexIndices[arrayIndex1];
        int vertexIndex2 = mesh->m_vertexIndices[arrayIndex2];
        int vertexIndex3 = mesh->m_vertexIndices[arrayIndex3];
        int vertexIndex4 = mesh->m_vertexIndices[arrayIndex4];

        Vector3f& P1 = vertices[size_t(vertexIndex1)];
        Vector3f& P2 = vertices[size_t(vertexIndex2)];
        Vector3f& P3 = vertices[size_t(vertexIndex3)];
        Vector3f& P4 = vertices[size_t(vertexIndex4)];

        // std::vector<float> p4;
        // int numFaceVertex = 4;
        // if (vertexIndex4 != -1) {
        //     // std::vector<float> p4 = vertices[size_t(id4)];
        //     numFaceVertex = 3;
        // } else {
        // }
        int u1_local = 8 * n - 8;
        int v1_local = 8 * n - 7;
        int u2_local = 8 * n - 6;
        int v2_local = 8 * n - 5;
        int u3_local = 8 * n - 4;
        int v3_local = 8 * n - 3;
        int u4_local = 8 * n - 2;
        int v4_local = 8 * n - 1;
        float u1 = mesh->m_uvs[u1_local];
        float v1 = mesh->m_uvs[v1_local];
        float u2 = mesh->m_uvs[u2_local];
        float v2 = mesh->m_uvs[v2_local];
        float u3 = mesh->m_uvs[u3_local];
        float v3 = mesh->m_uvs[v3_local];
        float u4 = mesh->m_uvs[u4_local];
        float v4 = mesh->m_uvs[v4_local];

        // Face &f = faces[size_t(faceIndex)];
        Face f;
        f.faceIndex = size_t(faceIndex);

        FaceVertex FV1;
        FV1.pointPosition = P1;
        FV1.uvw << u1, v1;
        FV1.vertexIndex = size_t(vertexIndex1);
        f.FaceVertices.push_back(FV1);

        FaceVertex FV2;
        FV2.pointPosition = P2;
        FV2.uvw << u2, v2;
        FV2.vertexIndex = size_t(vertexIndex2);
        f.FaceVertices.push_back(FV2);

        FaceVertex FV3;
        FV3.pointPosition = P3;
        FV3.uvw << u3, v3;
        FV3.vertexIndex = size_t(vertexIndex3);
        f.FaceVertices.push_back(FV3);

        // If not triangle
        if (vertexIndex4 != -1) {
            FaceVertex FV4;
            FV4.pointPosition = P4;
            FV4.uvw << u4, v4;
            FV4.vertexIndex = size_t(vertexIndex4);
            f.FaceVertices.push_back(FV4);
        }

        faces.push_back(f);

        // first point
        Vector3f Vec1 = P2 - P1;
        Vector3f Vec2 = P4 - P1;
        Vector3f N1 = Vec1.cross(Vec2);
        normals[size_t(vertexIndex1)] += N1;

        // second point
        Vector3f Vec3 = P3 - P2;
        Vector3f Vec4 = P1 - P2;
        Vector3f N2 = Vec3.cross(Vec4);
        normals[size_t(vertexIndex2)] += N2;

        // third point
        Vector3f Vec5 = P4 - P3;
        Vector3f Vec6 = P2 - P3;
        Vector3f N3 = Vec5.cross(Vec6);
        normals[size_t(vertexIndex3)] += N3;

        // forth point
        if (vertexIndex4 != -1) {
            Vector3f Vec7 = P1 - P4;
            Vector3f Vec8 = P3 - P4;
            Vector3f N4 = Vec7.cross(Vec8);
            normals[size_t(vertexIndex4)] += N4;
        }
    }

    for (Vector3f& N : normals) {
        N.normalize();
    }
}

void applyVectorDisplacement(GoZ_Mesh* mesh, std::vector<Point>& vertices,
    std::vector<Vector3f>& normals,
    std::vector<Face>& faces,
    std::vector<Image>& texture_data)
{
    std::vector<Vector3f> tempVertices;
    tempVertices.resize(vertices.size());

    // Apply displacement
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        size_t numFaceVertices = faceVertices.size();
        size_t lastFaceVertex = numFaceVertices - 1; // 3 if quad, 2 if triangle
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex *previousVertex, *currentVertex, *nextVertex;
            if (i == 0) {
                previousVertex = &faceVertices[lastFaceVertex];
                currentVertex = &faceVertices[i];
                nextVertex = &faceVertices[i + 1];
            } else if (i == lastFaceVertex) {
                previousVertex = &faceVertices[i - 1];
                currentVertex = &faceVertices[i];
                nextVertex = &faceVertices[0];
            } else {
                previousVertex = &faceVertices[i - 1];
                currentVertex = &faceVertices[i];
                nextVertex = &faceVertices[i + 1];
            }

            size_t previousIndex = previousVertex->vertexIndex;
            size_t currentIndex = currentVertex->vertexIndex;
            size_t nextIndex = nextVertex->vertexIndex;

            Point& pp0 = vertices[currentIndex];
            Point& pp1 = vertices[previousIndex];
            Point& pp2 = vertices[nextIndex];

            if (pp0.isDone) {
                continue;
            }

            Vector3f& uv0 = currentVertex->uvw;
            Vector3f& uv1 = previousVertex->uvw;
            Vector3f& uv2 = nextVertex->uvw;

            Vector3f T, B, N;
            N = normals[currentIndex];

            Matrix3f mat;
            mat = computeTangentMatrix(pp0, pp1, pp2, uv0, uv1, uv2, T, B, N);

            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            Vector3f displacement;

            if (udim > texture_data.size()) {
                displacement << 0, 0, 0;
            } else {
                Image& img = texture_data[udim - 1];
                if (img.isEmpty) {
                    displacement << 0, 0, 0;
                } else {
                    int width = img.width;
                    int height = img.height;
                    int channels = img.nchannels;
                    float localizedUV[2];
                    Image::localizeUV(localizedUV, u, v);
                    Vector3f rgb;
                    rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                        height, channels);
                    displacement = rgb.transpose() * mat;
                }
            }
            Vector3f new_pp = pp0 + displacement;
            tempVertices[currentIndex] = new_pp;

            pp0.isDone = true;
        }
    }

    for (size_t i = 0; i < size_t(vertices.size()) * 3; i += 3) {
        mesh->m_vertices[i] = tempVertices[i / 3].x();
        mesh->m_vertices[i + 1] = tempVertices[i / 3].y();
        mesh->m_vertices[i + 2] = tempVertices[i / 3].z();
    }
}

void applyNormalDisplacement(GoZ_Mesh* mesh, std::vector<Point>& vertices,
    std::vector<Vector3f>& normals,
    std::vector<Face>& faces,
    std::vector<Image>& texture_data, char* channel)
{
    std::vector<Vector3f> tempVertices;
    tempVertices.resize(vertices.size());

    // Apply displacement
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        size_t numFaceVertices = faceVertices.size();
        // size_t lastFaceVertex = numFaceVertices - 1; // 3 if quad, 2 if triangle
        for (size_t i = 0; i < numFaceVertices; i++) {

            FaceVertex& currentVertex = faceVertices[i];
            size_t currentIndex = currentVertex.vertexIndex;

            Point& pp0 = vertices[currentIndex];
            if (pp0.isDone) {
                continue;
            }

            Vector3f& uv0 = currentVertex.uvw;

            Vector3f N;
            N = normals[currentIndex];

            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            Vector3f displacement;

            if (udim > texture_data.size()) {
                displacement << 0, 0, 0;
            } else {
                Image& img = texture_data[udim - 1];
                if (img.isEmpty) {
                    displacement << 0, 0, 0;
                } else {
                    int width = img.width;
                    int height = img.height;
                    int channels = img.nchannels;
                    float localizedUV[2];
                    Image::localizeUV(localizedUV, u, v);
                    Vector3f rgb;
                    displacement = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width, height, channels);
                }
            }
            Vector3f new_pp;
            if (*channel == 'R') {
                new_pp = pp0 + (N * displacement.x());
            } else if (*channel == 'G') {
                new_pp = pp0 + (N * displacement.y());
            } else {
                new_pp = pp0 + (N * displacement.z());
            }
            tempVertices[currentIndex] = new_pp;
            pp0.isDone = true;
        }
    }

    for (size_t i = 0; i < size_t(vertices.size()) * 3; i += 3) {
        mesh->m_vertices[i] = tempVertices[i / 3].x();
        mesh->m_vertices[i + 1] = tempVertices[i / 3].y();
        mesh->m_vertices[i + 2] = tempVertices[i / 3].z();
    }
}

void applyColor(GoZ_Mesh* mesh, std::vector<Face>& faces,
    std::vector<Image>& texture_data, float gamma)
{
    std::string gmmaStr = std::to_string(gamma);

    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices[i];
            size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            if (udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data[udim - 1];
            if (!img.isEmpty) {
                int width = img.width;
                int height = img.height;
                int channels = img.nchannels;
                float localizedUV[2];
                Image::localizeUV(localizedUV, u, v);
                Vector3f rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                    height, channels);

                // Convert 4 8-bit int to a single 32 bit value
                // https://stackoverflow.com/questions/65136404/c-how-to-store-four-8-bit-integers-as-a-32-bit-unsigned-integer
                int m = 0;
                int r = int(round(pow(rgb.x(), 1 / gamma) * 255.0));
                int g = int(round(pow(rgb.y(), 1 / gamma) * 255.0));
                int b = int(round(pow(rgb.z(), 1 / gamma) * 255.0));
                uint32_t ui32 = (uint32_t(m & 0xFF) << 24) | (uint32_t(r & 0xFF) << 16) | (uint32_t(g & 0xFF) << 8) | uint32_t(b & 0xFF);

                // replace color
                if (mesh->m_mrgb[currentIndex] != NULL) {
                    mesh->m_mrgb[currentIndex] = ui32;
                }
            }
        }
    }
}

int remapValue(float oldValue, float oldMin, float oldMax, float newMin, float newMax) {
    float oldRange = oldMax - oldMin;
    float newRange = newMax - newMin;
    float newValue = ((oldValue - oldMin) * newRange / oldRange) + newMin;
    int intValue = int(round(newValue));
    return intValue;
}

void debugNormals(GoZ_Mesh* mesh, std::vector<Vector3f>& normals)
{
    size_t numVerts = normals.size();
    for (size_t i=0; i<numVerts; i++) {
        Vector3f& nml = normals[i];
        float rf = nml.x();
        float gf = nml.y();
        float bf = nml.z();
        int r = remapValue(rf, -1, 1, 0, 255);
        int g = remapValue(gf, -1, 1, 0, 255);
        int b = remapValue(bf, -1, 1, 0, 255);
        int m = 0;
        uint32_t ui32 = (uint32_t(m & 0xFF) << 24) | (uint32_t(r & 0xFF) << 16) | (uint32_t(g & 0xFF) << 8) | uint32_t(b & 0xFF);

        // replace color
        if (mesh->m_mrgb[i] != NULL) {
            mesh->m_mrgb[i] = ui32;
        }
    }
}

void applyMask(GoZ_Mesh* mesh, std::vector<Face>& faces, std::vector<Image>& texture_data)
{
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices[i];
            size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            if (udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data[udim - 1];
            if (!img.isEmpty) {
                int width = img.width;
                int height = img.height;
                int channels = img.nchannels;
                float localizedUV[2];
                Image::localizeUV(localizedUV, u, v);
                Vector3f rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                    height, channels);

                uint16_t r = uint16_t(round(rgb.x() * 65535.0));

                // replace color
                if (mesh->m_mask[currentIndex] != NULL) {
                    mesh->m_mask[currentIndex] = r;
                }
            }
        }
    }
}
float EXPORT importUDIM(char* GoZFilePath, double value,
    char* pOptBuffer1, [[maybe_unused]] int optBuffer1Size,
    char* pOptBuffer2, [[maybe_unused]] int optBuffer2Size,
    [[maybe_unused]] char** zData)
{
    // Open console
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONIN$", "r", stdin);

    std::filesystem::path path = GoZFilePath;

    // Create log file
    path.replace_filename("GoUDIM.log");
    std::ofstream log(path.string());
    log.clear();

    // Find import type
    // RGB : Vector displacement
    // R, G or B : Normal Displacement
    // COL : Color
    // MSK : Mask
    char* channel = pOptBuffer2;

    // Convert/Split long path string to path array
    std::string pathStringArray(pOptBuffer1);

    // Textures
    std::cout << "1/5 : Loading textures..." << std::endl;
    std::vector<Image> texture_data;
    initTextures(pathStringArray, texture_data);
    log << "Textures are initialized" << std::endl;

    // Mesh
    std::cout << "2/5 : Loading temp mesh : " << GoZFilePath << std::endl;
    GoZ_Mesh* mesh = new GoZ_Mesh;
    mesh->readMesh(GoZFilePath);
    log << "Mesh obj has been created..." << std::endl;

    // Create data arrays
    std::vector<Point> vertices;
    std::vector<Vector3f> normals;
    std::vector<Face> faces;

    // init vertices, normals, and faces
    std::cout << "3/5 : Generating data..." << std::endl;
    initMesh(mesh, vertices, normals, faces);
    log << "Mesh has been initialized..." << std::endl;

    // Apply displacement
    if (strcmp(channel, "RGB") == 0) {
        std::cout << "4/5 : Applying vector displacement..." << std::endl;
        applyVectorDisplacement(mesh, vertices, normals, faces, texture_data);
        log << "Vector displacement done" << std::endl;
    } else if (strcmp(channel, "COL") == 0) {
        std::cout << "4/5 : Applying color..." << std::endl;
        applyColor(mesh, faces, texture_data, float(value));
        log << "Color applied" << std::endl;
    } else if (strcmp(channel, "MSK") == 0) {
        std::cout << "4/5 : Applying mask..." << std::endl;
        applyMask(mesh, faces, texture_data);
        log << "Mask applied" << std::endl;
    } else {
        std::cout << "4/5 : Applying normal displacement..." << std::endl;
        applyNormalDisplacement(mesh, vertices, normals, faces, texture_data, channel);
        log << "Normal displacement done" << std::endl;
    }

    // write mesh
    std::cout << "5/5 : Writing mesh..." << std::endl;
    path.replace_filename("GoUDIM_out.GoZ");
    mesh->writeMesh(path.string().c_str());
    log << "Mesh has been saved." << std::endl;

    std::cout << "Done. Close the console" << std::endl;

    // Close all
    delete mesh;
    log.close();
    fclose(fp);
    FreeConsole();

    return 0;
}

#ifdef _WIN32
BOOL WINAPI DllMain([[maybe_unused]] HINSTANCE hinstDLL, DWORD fdwReason, [[maybe_unused]] LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // attach to process
        // return FALSE to fail DLL load
        break;

    case DLL_PROCESS_DETACH:
        // detach from process
        break;

    case DLL_THREAD_ATTACH:
        // attach to thread
        break;

    case DLL_THREAD_DETACH:
        // detach from thread
        break;
    }
    return TRUE; // succesful
}
#endif
