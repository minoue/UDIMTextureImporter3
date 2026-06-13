#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
// #include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>


#include <atomic>
#include <thread>
#include <mutex>

#include "texture.hpp"
#include "../third_party/GoZ/GoZ_Mesh.h"
#include "../third_party/Eigen/Dense"
#include "main.hpp"

using namespace Eigen;

#ifndef DLL_VERSION_STRING
#define DLL_VERSION_STRING "unknown"
#endif

/**
 * @brief Run body(i) for i in [0, count) across worker threads
 * @param [in] count : number of independent work items
 * @param [in] body : work for a single item; must only touch data unique to i
 *                    (or read-only shared data), otherwise add your own locking
 *
 * Mirrors the worker pattern in initTextures: a shared atomic index hands out
 * items, and the first exception thrown in any thread is captured and
 * rethrown on the calling thread after all threads join (so an exception
 * never escapes a worker and calls std::terminate).
 */
template <typename F>
void parallelFor(size_t count, F&& body)
{
    if (count == 0) {
        return;
    }

    // hardware_concurrency() may return 0, so make sure we get at least 1 thread
    const unsigned int numThreads = (std::min)(
        static_cast<unsigned int>(count),
        (std::max)(1U, std::thread::hardware_concurrency()));

    std::atomic<size_t> nextIndex(0);
    std::atomic<bool> hasError(false);
    std::exception_ptr error = nullptr;
    std::mutex errorMutex;

    auto worker = [&]() -> void {
        try {
            while (!hasError.load()) {
                const size_t i = nextIndex.fetch_add(1);
                if (i >= count) {
                    break;
                }
                body(i);
            }
        } catch (...) {
            hasError.store(true);
            std::scoped_lock lock(errorMutex);
            if (error == nullptr) {
                error = std::current_exception();
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (unsigned int t = 0; t < numThreads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    if (error != nullptr) {
        std::rethrow_exception(error);
    }
}

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
        if (udimCount > maxUDIM) { // NOLINT
            maxUDIM = udimCount;
        }
    }

    data.resize(static_cast<size_t>(maxUDIM));

    int numPaths = static_cast<int>(paths.size());

    // Create texture data
    // hardware_concurrency() may return 0, so make sure we get at least 1 thread
    const unsigned int numThreads = (std::min)(
        static_cast<unsigned int>(numPaths),
        (std::max)(1U, std::thread::hardware_concurrency())
        );

    std::atomic<int> nextIndex(0);
    std::mutex coutMutex;
    std::atomic<int> loadedCount(0);

    // An exception escaping a worker thread calls std::terminate, so capture
    // the first one here and rethrow it on the main thread after join
    std::atomic<bool> hasError(false);
    std::exception_ptr workerError = nullptr;
    std::mutex errorMutex;

    auto worker = [&]() -> void {
        try {
        while (!hasError.load()) {
            int index = nextIndex.fetch_add(1);
            if (index >= numPaths) {
                break;
            }
            // std::filesystem::path& path = paths[static_cast<size_t>(index)];
            std::filesystem::path& path = paths.at(static_cast<size_t>(index));
            std::string ext = path.extension().string();
            Image img;
            if (ext == ".exr") {
                img.loadExr(path.string());
            } else if (ext == ".tif" || ext == ".tiff") {
                img.loadTif(path.string());
            } else {
                throw std::runtime_error("Unsupported file format: " + path.string());
            }
            // Mark as loaded only after the loader succeeded, so a failed
            // image is never sampled later
            img.isEmpty = false;
            int udim = Image::getUDIMfromPath(path.string());
            int udimCount = udim - 1000; // Convert 1001 to 1, 1011 to 11, etc.. NOLINT
            data.at(static_cast<size_t>(udimCount) - 1) = std::move(img);
            // auto msg = std::format("{}/{} : ", index, numPaths);

            // Show progress bar
            {
                int loaded = ++loadedCount;
                std::scoped_lock lock(coutMutex);
                // std::lock_guard<std::mutex> lock(coutMutex);
                float percent = static_cast<float>(loaded) / static_cast<float>(numPaths) * 100.0F;
                std::cout << "\r[";
                const int bar = static_cast<int>(percent / 2.5F);
                const int bar_length = 40;
                for (int b = 0; b < bar_length; b++) {
                    std::cout << (b < bar ? "#" : "-");
                }
                std::cout << "] " << loaded << "/" << numPaths << " (" << static_cast<int>(percent) << "%)" << std::flush;
            }
        }
        } catch (...) {
            hasError.store(true);
            std::scoped_lock lock(errorMutex);
            if (workerError == nullptr) {
                workerError = std::current_exception();
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (unsigned int i = 0; i < numThreads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& thread : threads) {
        thread.join();
    }
    std::cout << "\n";

    if (workerError != nullptr) {
        std::rethrow_exception(workerError);
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

    // Near the tile borders the texel coordinates go out of the valid range
    // (x1/y1 can be 0, x2/y2 can be width/height + 1), so clamp them for
    // fetching. The interpolation weights below still use the unclamped
    // coordinates; when a coordinate is clamped the two fetched texels are
    // identical, so the weights are harmless.
    const int fx1 = std::clamp(x1, 1, width);
    const int fx2 = std::clamp(x2, 1, width);
    const int fy1 = std::clamp(y1, 1, height);
    const int fy2 = std::clamp(y2, 1, height);

    // Get the first index of RGB values. (array is like [R, G, B, A, R, G, B, A, R, ....] if 4 channels
    // tp1: top-left, tp2: top-right, tp3: bot-left, tp4: bot-right
    const auto pixelIndex1 = static_cast<size_t>(((width * (fy1 - 1) + fx1) - 1) * nchannel); // NOLINT
    const auto pixelIndex2 = static_cast<size_t>(((width * (fy1 - 1) + fx2) - 1) * nchannel); // NOLINT
    const auto pixelIndex3 = static_cast<size_t>(((width * (fy2 - 1) + fx1) - 1) * nchannel); // NOLINT
    const auto pixelIndex4 = static_cast<size_t>(((width * (fy2 - 1) + fx2) - 1) * nchannel); // NOLINT

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
    std::span<float> verts(mesh->m_vertices, static_cast<size_t>(numVerts*3));
    for (int i = 0; i < numVerts * 3; i += 3) {
        float x = verts[i]; // NOLINT
        float y = verts[i + 1]; // NOLINT
        float z = verts[i + 2]; // NOLINT
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
    size_t numUVs = static_cast<size_t>(mesh->m_faceCount) * 8; // NOLINT
    size_t numFaceVertices = static_cast<size_t>(numFaces) * 4; // NOLINT
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

        // The 4th index is -1 for triangles (GoZ_TAG_FACE4_LIST_FORMAT_1),
        // so P4 must not be looked up then
        const bool isQuad = vertexIndex4 != -1;

        Vector3f& P1 = vertices.at(static_cast<size_t>(vertexIndex1));
        Vector3f& P2 = vertices.at(static_cast<size_t>(vertexIndex2));
        Vector3f& P3 = vertices.at(static_cast<size_t>(vertexIndex3));
        Vector3f& P4 = isQuad ? vertices.at(static_cast<size_t>(vertexIndex4)) : P3;

        // there are 8*n uv values, packed 8 by 8, n being the number of faces.
        // See GoZ_Mesh.h for the specifications.
        int u1_local = (8 * n) - 8; // NOLINT
        int v1_local = (8 * n) - 7; // NOLINT
        int u2_local = (8 * n) - 6; // NOLINT
        int v2_local = (8 * n) - 5; // NOLINT
        int u3_local = (8 * n) - 4; // NOLINT
        int v3_local = (8 * n) - 3; // NOLINT
        int u4_local = (8 * n) - 2; // NOLINT
        int v4_local = (8 * n) - 1; // NOLINT

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
        if (isQuad) {
            FaceVertex FV4;
            FV4.pointPosition = P4;
            FV4.uvw << u4, v4, 0;
            FV4.vertexIndex = static_cast<size_t>(vertexIndex4);
            f.FaceVertices.push_back(FV4);
        }

        faces.push_back(f);

        // Each point's normal is the cross product of the edges to its two
        // neighbours. For triangles the previous neighbour of P1 is P3 (not
        // P4) and the next neighbour of P3 is P1.

        // first point
        Vector3f Vec1 = P2 - P1;
        Vector3f Vec2 = (isQuad ? P4 : P3) - P1;
        Vector3f N1 = Vec1.cross(Vec2);
        normals.at(static_cast<size_t>(vertexIndex1)) += N1;

        // second point
        Vector3f Vec3 = P3 - P2;
        Vector3f Vec4 = P1 - P2;
        Vector3f N2 = Vec3.cross(Vec4);
        normals.at(static_cast<size_t>(vertexIndex2)) += N2;

        // third point
        Vector3f Vec5 = (isQuad ? P4 : P1) - P3;
        Vector3f Vec6 = P2 - P3;
        Vector3f N3 = Vec5.cross(Vec6);
        normals.at(static_cast<size_t>(vertexIndex3)) += N3;

        // forth point
        if (isQuad) {
            Vector3f Vec7 = P1 - P4;
            Vector3f Vec8 = P3 - P4;
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
    // Initialize with the original positions (Point sliced to Vector3f) so
    // vertices not touched by any face keep their place instead of being
    // written back as (0, 0, 0)
    std::vector<Vector3f> tempVertices(vertices.begin(), vertices.end());

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

            UV uv_global(uv0.x(), uv0.y());
            size_t udim = Image::getUDIMfromUV(uv_global);

            Vector3f displacement;

            // udim == 0 means the UV is invalid (zero or negative)
            if (udim == 0 || udim > texture_data.size()) {
                displacement << 0, 0, 0;
            } else {
                Image& img = texture_data.at(udim - 1);
                if (img.isEmpty) {
                    displacement << 0, 0, 0;
                } else {
                    const int width = img.width;
                    const int height = img.height;
                    const int channels = img.nchannels;
                    UV uv_local = Image::localizeUV(uv_global);
                    Vector3f rgb;
                    rgb = getPixelValue(uv_local.u, uv_local.v, img.pixels, width,
                        height, channels);
                    displacement = rgb.transpose() * mat;
                }
            }
            const Vector3f new_pp = pp0 + displacement;
            tempVertices.at(currentIndex) = new_pp;

            pp0.isDone = true;
        }
    }

    const size_t numVerts = vertices.size();
    std::span<float> vertices_span(mesh->m_vertices, static_cast<size_t>(mesh->m_vertexCount)*3);
    for (size_t i = 0; i < numVerts * 3; i += 3) {
        vertices_span[i] = tempVertices.at(i / 3).x(); // NOLINT
        vertices_span[i + 1] = tempVertices.at(i / 3).y(); // NOLINT
        vertices_span[i + 2] = tempVertices.at(i / 3).z(); // NOLINT
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
    // Initialize with the original positions (Point sliced to Vector3f) so
    // vertices not touched by any face keep their place instead of being
    // written back as (0, 0, 0)
    std::vector<Vector3f> tempVertices(vertices.begin(), vertices.end());

    // The channel to read is constant for the whole call, so resolve it once
    const int channelIndex = (*channel == 'R') ? 0 : ((*channel == 'G') ? 1 : 2);

    // Pass 1 (serial): build one work item per vertex, claiming each vertex
    // for the first face that reaches it. This reproduces the previous
    // "first face wins" behavior and makes the work items independent so the
    // heavy per-vertex math can run in parallel without sharing state.
    std::vector<const FaceVertex*> workItems;
    workItems.reserve(vertices.size());
    std::vector<char> claimed(vertices.size(), 0);
    for (auto& f : faces) {
        for (const FaceVertex& fv : f.FaceVertices) {
            const size_t currentIndex = fv.vertexIndex;
            if (claimed.at(currentIndex) != 0) {
                continue;
            }
            claimed.at(currentIndex) = 1;
            workItems.push_back(&fv);
        }
    }

    // Pass 2 (parallel): each work item writes a unique tempVertices entry and
    // only reads shared-immutable data, so no locking is needed. Vertices with
    // an invalid UV (udim == 0) are left at their original position, which
    // tempVertices already holds from the copy initialization above.
    parallelFor(workItems.size(), [&](size_t k) {
        const FaceVertex* fv = workItems.at(k);
        const size_t currentIndex = fv->vertexIndex;
        const Vector3f& uv0 = fv->uvw;

        UV uv_global(uv0.x(), uv0.y());
        const size_t udim = Image::getUDIMfromUV(uv_global);
        if (udim == 0) {
            return;
        }

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
                UV uv_local = Image::localizeUV(uv_global);
                displacement = getPixelValue(uv_local.u, uv_local.v, img.pixels, width, height, channels);
            }
        }

        const Vector3f& pp0 = vertices.at(currentIndex);
        const Vector3f& N = normals.at(currentIndex);
        tempVertices.at(currentIndex) = pp0 + (N * (displacement[channelIndex] - midValue));
    });

    size_t numVerts = vertices.size();
    std::span<float> vertices_span(mesh->m_vertices, static_cast<size_t>(mesh->m_vertexCount)*3);
    for (size_t i = 0; i < numVerts * 3; i += 3) {
        vertices_span[i] = tempVertices.at(i / 3).x(); // NOLINT
        vertices_span[i + 1] = tempVertices.at(i / 3).y(); // NOLINT
        vertices_span[i + 2] = tempVertices.at(i / 3).z(); // NOLINT
    }
}

/**
 * @brief Get RGB values from textures and apply to the goz mesh m_rgb
 * @param [in] mesh : GoZ mesh data
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 * @param [in] gamma : gamma value, default is 1, use 2.2 for loading linear textures
 */
void applyColor(GoZ_Mesh* mesh, std::vector<Face>& faces,
    std::vector<Image>& texture_data, float gamma)
{
    const auto numVerts = static_cast<size_t>(mesh->m_vertexCount);

    // m_mrgb is NULL when the mesh has no polypaint yet, so create the
    // block initialized to white (the ZBrush default). GoZ_Mesh::clear()
    // releases it with delete[].
    if (mesh->m_mrgb == nullptr) {
        mesh->m_mrgb = new unsigned int[numVerts]; // NOLINT
        std::fill_n(mesh->m_mrgb, numVerts, 0xFFFFFFFFU);
    }

    std::span<unsigned int> rgb_span(mesh->m_mrgb, numVerts);
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices.at(i);
            const size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            UV uv_global(uv0.x(), uv0.y());
            size_t udim = Image::getUDIMfromUV(uv_global);

            // udim == 0 means the UV is invalid (zero or negative)
            if (udim == 0 || udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data.at(udim - 1);
            if (!img.isEmpty) {
                const int width = img.width;
                const int height = img.height;
                const int channels = img.nchannels;
                UV uv_local = Image::localizeUV(uv_global);
                rgb = getPixelValue(uv_local.u, uv_local.v, img.pixels, width,
                    height, channels);

                // Convert 4 8-bit int to a single 32 bit value
                // https://stackoverflow.com/questions/65136404/c-how-to-store-four-8-bit-integers-as-a-32-bit-unsigned-integer
                constexpr int m = 0;
                const int r = static_cast<int>(round(pow(rgb.x(), 1 / gamma) * 255.0));
                const int g = static_cast<int>(round(pow(rgb.y(), 1 / gamma) * 255.0));
                const int b = static_cast<int>(round(pow(rgb.z(), 1 / gamma) * 255.0));
                const uint32_t ui32 = (static_cast<uint32_t>(m & 0xFF) << 24) | (static_cast<uint32_t>(r & 0xFF) << 16) | (static_cast<uint32_t>(g & 0xFF) << 8) | static_cast<uint32_t>(b & 0xFF);

                // replace color
                if (rgb_span[currentIndex] != 0) { // NULL NOLINT
                    rgb_span[currentIndex] = ui32; // NOLINT
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
 * @brief For debug. Bake world space normal color to the mesh
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
        if (mesh->m_mrgb[i] != 0) { // NULL NOLINT
            mesh->m_mrgb[i] = ui32; // NOLINT
        }
    }
}

/**
 * @brief Apply mask from B/W textures
 * @param [in] mesh : GoZ mesh data
 * @param [in] faces : Face array
 * @param [in] texture_data : pixel data for each texture
 */
void applyMask(GoZ_Mesh* mesh, std::vector<Face>& faces, std::vector<Image>& texture_data)
{
    const auto numVerts = static_cast<size_t>(mesh->m_vertexCount);

    // m_mask is NULL when the mesh has no mask yet, so create the block
    // initialized to 0xFFFF (unmasked, the ZBrush default). GoZ_Mesh::clear()
    // releases it with delete[].
    if (mesh->m_mask == nullptr) {
        mesh->m_mask = new unsigned short[numVerts]; // NOLINT
        std::fill_n(mesh->m_mask, numVerts, static_cast<unsigned short>(0xFFFFU));
    }

    std::span<unsigned short> mask_span(mesh->m_mask, numVerts);
    for (auto& f : faces) {
        std::vector<FaceVertex>& faceVertices = f.FaceVertices;
        const size_t numFaceVertices = faceVertices.size();
        for (size_t i = 0; i < numFaceVertices; i++) {
            FaceVertex& currentVertex = faceVertices.at(i);
            const size_t currentIndex = currentVertex.vertexIndex;

            Vector3f& uv0 = currentVertex.uvw;
            UV uv_global(uv0.x(), uv0.y());
            size_t udim = Image::getUDIMfromUV(uv_global);

            // udim == 0 means the UV is invalid (zero or negative)
            if (udim == 0 || udim > texture_data.size()) {
                continue;
            }

            Vector3f rgb;
            rgb << 0, 0, 0;

            Image& img = texture_data.at(udim - 1);
            if (!img.isEmpty) {
                const int width = img.width;
                const int height = img.height;
                const int channels = img.nchannels;
                UV uv_local = Image::localizeUV(uv_global);
                rgb = getPixelValue(uv_local.u, uv_local.v, img.pixels, width,
                    height, channels);

                const auto r = static_cast<uint16_t>(round(rgb.x() * 65535.0));

                // replace color
                if (mask_span[currentIndex] != 0) { // NULL NOLINT
                    mask_span[currentIndex] = r; // NOLINT
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
extern "C" __declspec(dllexport) auto importUDIM(
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

    float result = 0.0F;
    GoZ_Mesh* mesh = nullptr; // NOLINT

    // An exception must not cross the DLL boundary back into ZBrush,
    // so catch everything here and return an error code instead
    try {
        // Find import type
        // RGB : Vector displacement
        // R, G or B : Normal Displacement
        // COL : Color
        // MSK : Mask
        char* channel = pOptBuffer2;

        // Convert/Split long path string to path array
        std::string pathStringArray(pOptBuffer1);

        std::cout << "UDIMTextureImporter3 v" << DLL_VERSION_STRING << "\n";
        log << "UDIMTextureImporter3 v" << DLL_VERSION_STRING << '\n';

        std::cout << "[!!!] If this log does not scroll automatically or the process appears to have stopped, try pressing Enter. [!!!]\n";

        // Textures
        std::cout << "1/5 : Loading textures...\n";

        std::vector<Image> texture_data;
        initTextures(pathStringArray, texture_data);
        log << "Textures are initialized\n";

        // Mesh
        std::cout << "2/5 : Loading temp mesh : " << GoZFilePath << "\n";
        mesh = new GoZ_Mesh; // NOLINT
        if (!mesh->readMesh(GoZFilePath)) {
            throw std::runtime_error("Failed to read the GoZ file");
        }
        if (mesh->m_faceType != GoZ_TAG_FACE4_LIST_FORMAT_1) {
            throw std::runtime_error("Unsupported face format. Only GoZ_TAG_FACE4_LIST_FORMAT_1 is supported");
        }
        if (mesh->m_uvs == nullptr) {
            throw std::runtime_error("The mesh has no UVs");
        }
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
        if (!mesh->writeMesh(path.string().c_str())) {
            throw std::runtime_error("Failed to write the output GoZ file");
        }
        log << "Mesh has been saved.\n";

        std::cout << "\nDone.\nYou can close the console now.\n";
    } catch (const std::exception& e) {
        log << "Error: " << e.what() << '\n';
        std::cout << "\nError: " << e.what() << "\nSee UDIMTextureImporter3.log for details.\n";
        result = 1.0F;
    } catch (...) {
        log << "Error: unknown exception\n";
        std::cout << "\nError: unknown exception\n";
        result = 1.0F;
    }

    // Close all
    delete mesh; // NOLINT
    log.close();

    FreeConsole();
    return result;
}
