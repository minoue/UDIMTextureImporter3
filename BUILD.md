# Build

This document explains how to build the plugin from source. The build produces two DLLs: `UDIMTextureImporter3.dll` (the main plugin) and `UDIMTextureImporterLoader.dll` (a thin loader that loads the main DLL at runtime, so the main DLL can be rebuilt and swapped without restarting ZBrush).

## Requirements
* VS 2015 or higher (or `mingw64-gcc-c++` if building on Linux)
* C++20
* [Eigen](https://eigen.tuxfamily.org/) — linear algebra (vectors/matrices used for displacement math)
* [libtiff](https://gitlab.com/libtiff/libtiff) — TIFF reading
* [zlib](https://zlib.net) — required by libtiff (Deflate/zip compression)
* [deflate](https://github.com/ebiggers/libdeflate) — required by libtiff
* [tinyexr](https://github.com/syoyo/tinyexr) — EXR reading
* [miniz](https://github.com/richgel999/miniz) — required by tinyexr
* [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview) — ZBrush mesh (GoZ) interchange
* [ZFileUtils](https://help.maxon.net/zbr/en-us/Content/html/user-guide/customizing-zbrush/zscripting/zfileutils/zfileutils.html) — file dialogs / utilities on the ZScript side

Eigen and tinyexr are header-only, so they only need to be placed in the right folder. libtiff (with zlib and deflate) is the one external library you actually have to compile first.

## Build Instructions
### Preparation
1. Build zlib and deflate, then build libtiff against those two libraries. (libtiff needs them for compressed TIFFs.)
2. Move `zlib.dll`, `deflate.dll`, and `libtiff.dll` into the `UDIMTextureImporter3Data` folder, since those DLLs ship alongside the plugin and must be found at runtime.
3. Download the third-party (header-only) libraries:
    1. Download [`miniz.c` and `miniz.h`](https://github.com/richgel999/miniz/releases) and move them to the `third_party/tinyexr` directory.
    2. Download `tinyexr.h` and move it to the `third_party/tinyexr` directory.
    3. Download the [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview) and move all of its files to the `third_party/GoZ` directory.
    4. Download [Eigen](https://eigen.tuxfamily.org/) and move it to the `third_party` directory.

4. After this, the project directory should look like the following. The `third_party` libraries are not committed to the repository, so this layout is what you reproduce locally before building:
    ```
    └── UDIMTextureImporter3Data/
        ├── CMakeLists.txt
        ├── build/
        ├── third_party/
        │   ├── tinyexr/
        │   │   ├── miniz.c
        │   │   ├── miniz.h
        │   │   └── tinyexr.h
        │   ├── Eigen/
        │   │   ├── Core
        │   │   ├── Dense
        │   │   └── ...
        │   └── GoZ/
        │       ├── GoZ_Mesh.cpp
        │       ├── GoZ_***
        │       └── ...
        └── src/
            ├── loader.cpp
            ├── loader.hpp
            ├── main.cpp
            ├── main.hpp
            ├── texture.cpp
            └── texture.hpp
    ```

### Build

The DLL version number is set in one place — `set(DLL_VERSION x.y.z)` in `CMakeLists.txt`. It is embedded both into the Windows file properties (Details tab) and into the startup log, so you can always tell which build is actually loaded.

#### Windows
```sh
mkdir build
cd build
cmake ../
cmake --build . --config Release --target install
```
If CMake cannot find the libtiff libraries or include directories, specify the libtiff installation directory manually:
```sh
cmake -DTIFF_INSTALL_DIR="C:/opt" ../
```
(This assumes the headers and libraries are installed under `C:/opt/include` and `C:/opt/lib` respectively.)

#### Linux (mingw64 cross-compile)
This builds the Windows DLLs from Linux using the MinGW-w64 toolchain (handy for running under Wine while developing).
```sh
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../mingw-w64.cmake -DTIFF_INSTALL_DIR=/home/XXXX/opt/libtiff -DCMAKE_BUILD_TYPE=Release ../
cmake --build . --config Release --target install
```

Always build the **Release** configuration for distribution and for measuring speed — a Debug build can be several times slower.

`UDIMTextureImporter3.dll` and `UDIMTextureImporterLoader.dll` will be generated under the project root (the `UDIMTextureImporter3Data` folder). Copy them into the plugin's `Data` folder as shown in the [README](README.md) to install your own build.
