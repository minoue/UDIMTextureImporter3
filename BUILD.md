## Build

### Requirements
* VS 2015 or higher
* C++20
* [Eigen](https://eigen.tuxfamily.org/)
* [libtiff](https://gitlab.com/libtiff/libtiff)
* [zlib](https://zlib.net) required for libtiff
* [deflate](https://github.com/ebiggers/libdeflate) required for libtiff
* [tinyexr](https://github.com/syoyo/tinyexr)
* [miniz](https://github.com/richgel999/miniz) required for tinyexr
* [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview)
* [ZFileUtils](https://help.maxon.net/zbr/en-us/Content/html/user-guide/customizing-zbrush/zscripting/zfileutils/zfileutils.html)

### Build Instruction
1. Build zlib and deflate, then build libtiff with the first two libraries.
2. Move `zlib.dll`, `deflate.dll`, and `libtiff.dll` to `UDIMTextureImporter3Data` folder
2. Download third party libraries.
    1. Download [`miniz.c` and `miniz.h`](https://github.com/richgel999/miniz/releases) and move to src directory.
    2. Download `tinyexr.h` and move to src directory.
    3. Download [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview), and move all files to sdk directory.
    4. Download [Eigen](https://eigen.tuxfamily.org/) and move to the src directory.

3. So the project directory should be like this.
    ```
    └── UDIMTextureImporter3Data/
        ├── CMakeLists.txt
        ├── build/
        └── src/
            ├── loader.cpp
            ├── loader.hpp
            ├── main.cpp
            ├── main.hpp
            ├── texture.cpp
            ├── texture.hpp
            ├── miniz.c
            ├── miniz.h
            ├── tinyexr.h
            ├── sdk/
            │   ├── GoZ_Mesh.cpp
            │   ├── GoZ_***
            │   └── ...
            └── Eigen/
                ├── Core
                ├── Dense
                └── ...
    ```

4. build
```sh
mkdir build
cd build
cmake ../
cmake --build . --config Release --target install
```

`UDIMTextureImporter3.dll` and `UDIMTextureImporterLoader.dll` will be generated under the project root(`UDIMTextureImporter3Data` folder).


