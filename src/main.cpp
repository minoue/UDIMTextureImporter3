#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef USE_OPENMP
#include <omp.h>
#endif

#include "main.hpp"
#include "sdk/GoZ_Mesh.h"
#include "texture.hpp"

using namespace Eigen;

/**
 * @brief Split texture path array
 * @param [in] pathStringArray : Long string containing paths eg. "C:/path/image.1001.exr C:/path/image.1002.exr C:..."
 * @param [in] data : texture data
 */
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

        int udimCount = udim - 1000; // NOLINT
        if (udimCount > maxUDIM) {
            maxUDIM = udimCount;
        }
    }

    data.resize(static_cast<size_t>(maxUDIM));

    int numPaths = static_cast<int>(paths.size());

    // Create texture data
    int inc = 1;
#pragma omp parallel for
    for (int i = 0; i < numPaths; i++) {
        std::filesystem::path& path = paths[static_cast<size_t>(i)];
        std::string ext = path.extension().string();

        Image img;
        img.isEmpty = false;

        if (ext == ".exr") {
            img.loadExr(path.string());
        } else if (ext == ".tif" || ext == ".tiff") {
            img.loadTif(path.string());
        } else {
            std::cout << "Not supported file format\n";
        }

        int udim = Image::getUDIMfromPath(path.string());
        int udimCount = udim - 1000; // Convert 1001 to 1, 1011 to 11, etc.. NOLINT
        data[static_cast<size_t>(udimCount) - 1] = std::move(img);

        auto msg = std::format("{}/{} : ", inc, numPaths);

#pragma omp atomic
        ++inc;

#pragma omp critical
        std::cout << "Loaded " << msg << path.string() << "\n";
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
auto computeTangentMatrix(const Vector3f& P0, const Vector3f& P1, const Vector3f& P2,
                          const Vector3f& uv0, const Vector3f& uv1, const Vector3f& uv2,
                          Vector3f& T, Vector3f& B, Vector3f& N) -> Matrix3f
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

/**
 * @brief Get RGB values from textures by u and v value, by bilinear filtering
 * @param [in] u : u value
 * @param [in] v : v value
 * @param [in] texture : 1d array for texture pixels
 * @param [in] width : image width
 * @param [in] height : image height
 * @param [in] nchannel : Number of channels, usually 3 or 4
 * Ref: https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/bilinear-texture-filtering
 */
auto getPixelValue(const float u, const float v,
    const std::vector<float>& texture, const int width,
    const int height, const int nchannel) -> Vector3f
{
    const auto float_width = static_cast<float>(width);
    const auto float_height = static_cast<float>(height);

    // Get 4 texels closest to the sampling point (uv)
    // top-left (x1, y1)
    // top-right (x2, y1)
    // bot-left: (x1, y2)
    // bot-right: (x2, y2)
    const int x1 = static_cast<int>(std::round(float_width * u));
    const int x2 = x1 + 1;
    const int y1 = static_cast<int>(std::round(float_height * (1 - v)));
    const int y2 = y1 + 1;

    // Get the first index of RGB values. (array is like [R, G, B, A, R, G, B, A, R, ....] if 4 channels
    // tp1: top-left, tp2: top-right, tp3: bot-left, tp4: bot-right
    const auto pixelIndex1 = static_cast<size_t>(((width * (y1 - 1) + x1) - 1) * nchannel);
    const auto pixelIndex2 = static_cast<size_t>(((width * (y1 - 1) + x2) - 1) * nchannel);
    const auto pixelIndex3 = static_cast<size_t>(((width * (y2 - 1) + x1) - 1) * nchannel);
    const auto pixelIndex4 = static_cast<size_t>(((width * (y2 - 1) + x2) - 1) * nchannel);

    Vector3f A;
    A << texture.at(pixelIndex1),
         texture.at(pixelIndex1 + 1),
         texture.at(pixelIndex1 + 2);
    Vector3f B;
    B << texture.at(pixelIndex2),
         texture.at(pixelIndex2 + 1),
         texture.at(pixelIndex2 + 2);
    Vector3f C;
    C << texture.at(pixelIndex3),
         texture.at(pixelIndex3 + 1),
         texture.at(pixelIndex3 + 2);
    Vector3f D;
    D << texture.at(pixelIndex4),
         texture.at(pixelIndex4 + 1),
         texture.at(pixelIndex4 + 2);

    // Get the center of the 4 texels
    // A: (u1, v1), B: (u2, v1), C: (u1, v2), D: (u2, v2)
    constexpr float centerOffset = 0.5;
    const float u1 = (static_cast<float>(x1) - centerOffset) / float_width;
    const float u2 = (static_cast<float>(x2) - centerOffset) / float_width;
    const float v1 = (static_cast<float>(y1) - centerOffset) / float_height;
    const float v2 = (static_cast<float>(y2) - centerOffset) / float_height;

    // https://en.wikipedia.org/wiki/Bilinear_interpolation#Application_in_image_processing
    // Linear interpolate pixel values
    const Vector3f E = ((u2 - u) / (u2 - u1)) * A + ((u - u1) / (u2 - u1)) * B;
    const Vector3f F = ((u2 - u) / (u2 - u1)) * C + ((u - u1) / (u2 - u1)) * D;
    Vector3f G = ((v2 - (1 - v)) / (v2 - v1)) * E + (((1 - v) - v1) / (v2 - v1)) * F;

    return G;
}

/**
 * @brief Create necessary data from the mesh and store to containers
 * @param [in] mesh : GoZ mesh object
 * @param [out] vertices : container for vertex positions
 * @param [out] normals : container for vertex normals
 * @param [out] faces : container for face objs
 */
void initMesh(GoZ_Mesh* mesh, std::vector<Point>& vertices,
    std::vector<Vector3f>& normals, std::vector<Face>& faces)
{
    int numVerts = mesh->m_vertexCount;
    vertices.reserve(static_cast<size_t>(numVerts));
    for (int i = 0; i < numVerts * 3; i += 3) {
        float x = mesh->m_vertices[i];
        float y = mesh->m_vertices[i + 1];
        float z = mesh->m_vertices[i + 2];
        Point b;
        b << x, y, z;
        vertices.push_back(b);
    }
    // Init normal array
    normals.resize(static_cast<size_t>(numVerts));
    Vector3f zeroVec(0, 0, 0);
    std::ranges::fill(normals, zeroVec);

    // Face array
    int numFaces = mesh->m_faceCount;
    size_t numUVs = static_cast<size_t>(mesh->m_faceCount) * 8;
    size_t numFaceVertices = static_cast<size_t>(numFaces) * 4;
    faces.reserve(static_cast<size_t>(numFaces));

    std::span<float> uvs(mesh->m_uvs, numUVs);
    std::span<int> vertexIndices(mesh->m_vertexIndices, numFaceVertices);

    // Loop over all faces
    for (int n = 1; n <= numFaces; n++) {
        int faceIndex = n - 1;
        int arrayIndex1 = (4 * n) - 4;
        int arrayIndex2 = (4 * n) - 3;
        int arrayIndex3 = (4 * n) - 2;
        int arrayIndex4 = (4 * n) - 1;

        int vertexIndex1 = vertexIndices[arrayIndex1]; // NOLINT
        int vertexIndex2 = vertexIndices[arrayIndex2]; // NOLINT
        int vertexIndex3 = vertexIndices[arrayIndex3]; // NOLINT
        int vertexIndex4 = vertexIndices[arrayIndex4]; // NOLINT

        Vector3f& P1 = vertices.at(static_cast<size_t>(vertexIndex1));
        Vector3f& P2 = vertices.at(static_cast<size_t>(vertexIndex2));
        Vector3f& P3 = vertices.at(static_cast<size_t>(vertexIndex3));
        Vector3f& P4 = vertices.at(static_cast<size_t>(vertexIndex4));

        int u1_local = (8 * n) - 8;
        int v1_local = (8 * n) - 7;
        int u2_local = (8 * n) - 6;
        int v2_local = (8 * n) - 5;
        int u3_local = (8 * n) - 4;
        int v3_local = (8 * n) - 3;
        int u4_local = (8 * n) - 2;
        int v4_local = (8 * n) - 1;

        float u1 = uvs[u1_local]; // NOLINT
        float v1 = uvs[v1_local]; // NOLINT
        float u2 = uvs[u2_local]; // NOLINT
        float v2 = uvs[v2_local]; // NOLINT
        float u3 = uvs[u3_local]; // NOLINT
        float v3 = uvs[v3_local]; // NOLINT
        float u4 = uvs[u4_local]; // NOLINT
        float v4 = uvs[v4_local]; // NOLINT

        Face f;
        f.faceIndex = static_cast<size_t>(faceIndex);

        FaceVertex FV1;
        FV1.pointPosition = P1;
        FV1.uvw << u1, v1, 0;
        FV1.vertexIndex = static_cast<size_t>(vertexIndex1);
        f.FaceVertices.push_back(FV1);

        FaceVertex FV2;
        FV2.pointPosition = P2;
        FV2.uvw << u2, v2, 0;
        FV2.vertexIndex = static_cast<size_t>(vertexIndex2);
        f.FaceVertices.push_back(FV2);

        FaceVertex FV3;
        FV3.pointPosition = P3;
        FV3.uvw << u3, v3, 0;
        FV3.vertexIndex = static_cast<size_t>(vertexIndex3);
        f.FaceVertices.push_back(FV3);

        // If not triangle
        if (vertexIndex4 != -1) {
            FaceVertex FV4;
            FV4.pointPosition = P4;
            FV4.uvw << u4, v4, 0;
            FV4.vertexIndex = static_cast<size_t>(vertexIndex4);
            f.FaceVertices.push_back(FV4);
        }

        faces.push_back(f);

        // first point
        Vector3f Vec1 = P2 - P1;
        Vector3f Vec2 = P4 - P1;
        Vector3f N1 = Vec1.cross(Vec2);
        normals.at(static_cast<size_t>(vertexIndex1)) += N1;

        // second point
        Vector3f Vec3 = P3 - P2;
        Vector3f Vec4 = P1 - P2;
        Vector3f N2 = Vec3.cross(Vec4);
        normals.at(static_cast<size_t>(vertexIndex2)) += N2;

        // third point
        Vector3f Vec5 = P4 - P3;
        Vector3f Vec6 = P2 - P3;
        Vector3f N3 = Vec5.cross(Vec6);
        normals.at(static_cast<size_t>(vertexIndex3)) += N3;

        // forth point
        if (vertexIndex4 != -1) {
            Vector3f Vec7 = P1 - P4;
            Vector3f Vec8 = P2 - P4;
            Vector3f N4 = Vec7.cross(Vec8);
            normals.at(static_cast<size_t>(vertexIndex4)) += N4;
        }
    }

    for (Vector3f& N : normals) {
        N.normalize();
    }
}

/**
 * @brief Get tangent normals from textures and apply to the goz mesh vertex positions
 * @param [in] mesh : GoZ mesh data
 * @param [in] vertices : vertex array extracted from the goz mesh
 * @param [in] normals : normal array calculated by the goz mesh
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 */
void applyVectorDisplacement(const GoZ_Mesh* mesh, std::vector<Point>& vertices,
    const std::vector<Vector3f>& normals,
    std::vector<Face>& faces,
    std::vector<Image>& texture_data)
{
    std::vector<Vector3f> tempVertices;
    tempVertices.resize(vertices.size());

    // Apply displacement
    for (auto& face : faces) {
        std::vector<FaceVertex>& faceVertices = face.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        const size_t lastFaceVertex = numFaceVertices - 1; // 3 if quad, 2 if triangle
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex *previousVertex = nullptr;
            FaceVertex *currentVertex = nullptr;
            FaceVertex *nextVertex = nullptr;
            if (i == 0) {
                previousVertex = &faceVertices.at(lastFaceVertex);
                currentVertex = &faceVertices.at(i);
                nextVertex = &faceVertices.at(i + 1);
            } else if (i == lastFaceVertex) {
                previousVertex = &faceVertices.at(i - 1);
                currentVertex = &faceVertices.at(i);
                nextVertex = &faceVertices.at(0);
            } else {
                previousVertex = &faceVertices.at(i - 1);
                currentVertex = &faceVertices.at(i);
                nextVertex = &faceVertices.at(i + 1);
            }

            const size_t previousIndex = previousVertex->vertexIndex;
            const size_t currentIndex = currentVertex->vertexIndex;
            const size_t nextIndex = nextVertex->vertexIndex;

            Point& pp0 = vertices.at(currentIndex);
            Point& pp1 = vertices.at(previousIndex);
            Point& pp2 = vertices.at(nextIndex);

            if (pp0.isDone) {
                continue;
            }

            Vector3f& uv0 = currentVertex->uvw;
            Vector3f& uv1 = previousVertex->uvw;
            Vector3f& uv2 = nextVertex->uvw;

            Vector3f T;
            Vector3f B;
            Vector3f N;
            N = normals.at(currentIndex);

            Matrix3f mat;
            mat = computeTangentMatrix(pp0, pp1, pp2, uv0, uv1, uv2, T, B, N);

            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            Vector3f displacement;

            if (udim > texture_data.size()) {
                displacement << 0, 0, 0;
            } else {
                Image& img = texture_data.at(udim - 1);
                if (img.isEmpty) {
                    displacement << 0, 0, 0;
                } else {
                    const int width = img.width;
                    const int height = img.height;
                    const int channels = img.nchannels;
                    float localizedUV[2];
                    Image::localizeUV(localizedUV, u, v);
                    Vector3f rgb;
                    rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                        height, channels);
                    displacement = rgb.transpose() * mat;
                }
            }
            const Vector3f new_pp = pp0 + displacement;
            tempVertices.at(currentIndex) = new_pp;

            pp0.isDone = true;
        }
    }

    size_t numVerts = vertices.size();
    for (size_t i = 0; i < numVerts * 3; i += 3) {
        mesh->m_vertices[i] = tempVertices.at(i / 3).x();
        mesh->m_vertices[i + 1] = tempVertices.at(i / 3).y();
        mesh->m_vertices[i + 2] = tempVertices.at(i / 3).z();
    }
}

/**
 * @brief Get float displacement values from textures and apply to the goz mesh vertex positions
 * @param [in] mesh : GoZ mesh data
 * @param [in] vertices : vertex array extracted from the goz mesh
 * @param [in] normals : normal array calculated by the goz mesh
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 * @param [in] channel : ID to choose channels, "R", "G", or "B"
 * @param [in] midValue : mid value, default is 0
 */
void applyNormalDisplacement(const GoZ_Mesh* mesh, std::vector<Point>& vertices,
    const std::vector<Vector3f>& normals,
    std::vector<Face>& faces,
    std::vector<Image>& texture_data, const char* channel, float midValue)
{
    std::vector<Vector3f> tempVertices;
    tempVertices.resize(vertices.size());

    // Apply displacement
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        // size_t lastFaceVertex = numFaceVertices - 1; // 3 if quad, 2 if triangle
        for (size_t i = 0; i < numFaceVertices; i++) {

            FaceVertex& currentVertex = faceVertices.at(i);
            const size_t currentIndex = currentVertex.vertexIndex;

            Point& pp0 = vertices.at(currentIndex);
            if (pp0.isDone) {
                continue;
            }

            Vector3f& uv0 = currentVertex.uvw;

            Vector3f N;
            N = normals.at(currentIndex);

            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            Vector3f displacement;

            if (udim > texture_data.size()) {
                displacement << 0, 0, 0;
            } else {
                Image& img = texture_data.at(udim - 1);
                if (img.isEmpty) {
                    displacement << 0, 0, 0;
                } else {
                    const int width = img.width;
                    const int height = img.height;
                    const int channels = img.nchannels;
                    float localizedUV[2];
                    Image::localizeUV(localizedUV, u, v);
                    displacement = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width, height, channels);
                }
            }
            Vector3f new_pp;
            if (*channel == 'R') {
                new_pp = pp0 + (N * (displacement.x() - midValue));
            } else if (*channel == 'G') {
                new_pp = pp0 + (N * (displacement.y() - midValue));
            } else {
                new_pp = pp0 + (N * (displacement.z() - midValue));
            }
            tempVertices.at(currentIndex) = new_pp;
            pp0.isDone = true;
        }
    }

    size_t numVerts = vertices.size();
    for (size_t i = 0; i < numVerts * 3; i += 3) {
        mesh->m_vertices[i] = tempVertices.at(i / 3).x();
        mesh->m_vertices[i + 1] = tempVertices.at(i / 3).y();
        mesh->m_vertices[i + 2] = tempVertices.at(i / 3).z();
    }
}

/**
 * @brief Get RGB values from textures and apply to the goz mesh m_rgb
 * @param [in] mesh : GoZ mesh data
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 * @param [in] gamma : gamma value, default is 1, use 2.2 for loading linear textures
 */
void applyColor(const GoZ_Mesh* mesh, std::vector<Face>& faces,
    std::vector<Image>& texture_data, float gamma)
{
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices.at(i);
            const size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            if (udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data.at(udim - 1);
            if (!img.isEmpty) {
                const int width = img.width;
                const int height = img.height;
                const int channels = img.nchannels;
                float localizedUV[2];
                Image::localizeUV(localizedUV, u, v);
                rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                    height, channels);

                // Convert 4 8-bit int to a single 32 bit value
                // https://stackoverflow.com/questions/65136404/c-how-to-store-four-8-bit-integers-as-a-32-bit-unsigned-integer
                constexpr int m = 0;
                const int r = static_cast<int>(round(pow(rgb.x(), 1 / gamma) * 255.0));
                const int g = static_cast<int>(round(pow(rgb.y(), 1 / gamma) * 255.0));
                const int b = static_cast<int>(round(pow(rgb.z(), 1 / gamma) * 255.0));
                const uint32_t ui32 = (static_cast<uint32_t>(m & 0xFF) << 24) | (static_cast<uint32_t>(r & 0xFF) << 16) | (static_cast<uint32_t>(g & 0xFF) << 8) | static_cast<uint32_t>(b & 0xFF);

                // replace color
                if (mesh->m_mrgb[currentIndex] != 0) { // NULL
                    mesh->m_mrgb[currentIndex] = ui32;
                }
            }
        }
    }
}

auto remapValue(const float oldValue, const float oldMin, const float oldMax, const float newMin, const float newMax) -> int
{
    const float oldRange = oldMax - oldMin;
    const float newRange = newMax - newMin;
    const float newValue = ((oldValue - oldMin) * newRange / oldRange) + newMin;
    const int intValue = static_cast<int>(std::round(newValue));
    return intValue;
}

/**
 * @brief For dubug. Bake world space normal color to the mesh
 * @param [in] mesh : GoZ mesh data
 * @param [in] normals : normal vector array
 */
void debugNormals(const GoZ_Mesh* mesh, std::vector<Vector3f>& normals)
{
    const size_t numVerts = normals.size();
    for (size_t i = 0; i < numVerts; i++) {
        Vector3f& nml = normals.at(i);
        const float rf = nml.x();
        const float gf = nml.y();
        const float bf = nml.z();
        const int r = remapValue(rf, -1, 1, 0, 255);
        const int g = remapValue(gf, -1, 1, 0, 255);
        const int b = remapValue(bf, -1, 1, 0, 255);
        constexpr int m = 0;
        const uint32_t ui32 = (static_cast<uint32_t>(m & 0xFF) << 24) | (static_cast<uint32_t>(r & 0xFF) << 16) | (static_cast<uint32_t>(g & 0xFF) << 8) | static_cast<uint32_t>(b & 0xFF);

        // replace color
        if (mesh->m_mrgb[i] != 0) { // NULL
            mesh->m_mrgb[i] = ui32;
        }
    }
}

/**
 * @brief Apply mask from B/W textures
 * @param [in] mesh : GoZ mesh data
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 */
void applyMask(const GoZ_Mesh* mesh, std::vector<Face>& faces, std::vector<Image>& texture_data)
{
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices.at(i);
            const size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            float u = uv0.x();
            float v = uv0.y();
            size_t udim = Image::getUDIMfromUV(u, v);

            if (udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data.at(udim - 1);
            if (!img.isEmpty) {
                const int width = img.width;
                const int height = img.height;
                const int channels = img.nchannels;
                float localizedUV[2];
                Image::localizeUV(localizedUV, u, v);
                rgb = getPixelValue(localizedUV[0], localizedUV[1], img.pixels, width,
                    height, channels);

                const auto r = static_cast<uint16_t>(round(rgb.x() * 65535.0));

                // replace color
                if (mesh->m_mask[currentIndex] != 0) { // NULL
                    mesh->m_mask[currentIndex] = r;
                }
            }
        }
    }
}

/**
 * @brief main function
 * @param [in] GoZFilePath : File path to the temp file
 * @param [in] value : value for gamma or mid-point
 * @param [in] pOptBuffer1 : next point of triangle
 * @param [in] pOptBuffer2 : channel ID
 */
auto EXPORT importUDIM(
    char* GoZFilePath,
    double value,
    char* pOptBuffer1,
    char* pOptBuffer2) -> float
{
    // Open console
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    std::filesystem::path path = GoZFilePath;

    // Create log file
    path.replace_filename("UDIMTextureImporter3.log");
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
     
    std::cout << "[!!!] If this log does not scroll automatically or the process appears to have stopped, try pressing Enter. [!!!]\n";

    // Textures
    std::cout << "1/5 : Loading textures...\n";

    std::vector<Image> texture_data;
    initTextures(pathStringArray, texture_data);
    log << "Textures are initialized\n";

    // Mesh
    std::cout << "2/5 : Loading temp mesh : " << GoZFilePath << "\n";
    GoZ_Mesh* mesh = new GoZ_Mesh; // NOLINT
    mesh->readMesh(GoZFilePath);
    log << "Mesh obj has been created...\n";

    // Create data arrays
    std::vector<Point> vertices;
    std::vector<Vector3f> normals;
    std::vector<Face> faces;

    // init vertices, normals, and faces
    std::cout << "3/5 : Generating data...\n";
    initMesh(mesh, vertices, normals, faces);
    log << "Mesh has been initialized...\n";

    // Apply displacement
    if (strcmp(channel, "RGB") == 0) {
        std::cout << "4/5 : Applying vector displacement...\n";
        applyVectorDisplacement(mesh, vertices, normals, faces, texture_data);
        log << "Vector displacement done\n";
    } else if (strcmp(channel, "COL") == 0) {
        std::cout << "4/5 : Applying color...\n";
        applyColor(mesh, faces, texture_data, static_cast<float>(value));
        log << "Color applied\n";
    } else if (strcmp(channel, "MSK") == 0) {
        std::cout << "4/5 : Applying mask...\n";
        applyMask(mesh, faces, texture_data);
        log << "Mask applied\n";
    } else {
        std::cout << "4/5 : Applying normal displacement...\n";
        applyNormalDisplacement(mesh, vertices, normals, faces, texture_data, channel, static_cast<float>(value));
        log << "Normal displacement done\n";
    }

    // write mesh
    std::cout << "5/5 : Writing mesh...\n";
    path.replace_filename("UDIMTextureImporter3_out.GoZ");
    mesh->writeMesh(path.string().c_str());
    log << "Mesh has been saved.\n";

    // Close all
    delete mesh; // NOLINT
    log.close();

    std::cout << "\nDone.\nYou can close the console now.\n";

    // fclose(fp);
    FreeConsole();

    return 0.0f;
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
    return TRUE; // successful
}
#endif
