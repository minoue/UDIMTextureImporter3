# UDIMTextureImporter3
Import UDIM displacement(**sculpt using maps** in Mudbox), color, and mask textures.

NOTE: **Windows only**

![](https://raw.githubusercontent.com/minoue/UDIMTextureImporter/main/img/demo.gif)

**Resculpted mesh vs the orignal**:
If everything(mesh, UVs, Smooth options, map settings, etc) are all correct and consistent, then the result should be pretty close to the original, still you may have some lines around the UV borders.
![](https://raw.githubusercontent.com/minoue/UDIMTextureImporter/main/img/compare.gif)

![](https://raw.githubusercontent.com/minoue/UDIMTextureImporter/main/img/edge.gif)

## Install
### Windows10/11 & ZBrush2022
1. Go to [release page](https://github.com/minoue/UDIMTextureImporter3/releases), download the latest and extract it.
2. Move **tiff.dll**, **zlib.dll**, and **libdeflate.dll** to the same directory as ZBrush.exe. (For example. `C:\Program Files\Pixologic\ZBrush 2022\tiff.dll`)
3. Move `UDIMTextureImporter3_2022.zsc` to `ZPlugs64` folder.
4. Move `UDIMTextureImporter3Data` folder to `ZPlugs64` folder.

So they should be placed like this
```
└── ZBrush 2022/
    ├── ZBrush.exe
    ├── tiff.dll
    ├── zlib.dll
    ├── deflate.dll
    └── ZStartup/
        └── ZPlugs64/
            ├── UDIMTextureImporter3_2022.zsc
            └── UDIMTextureImporter3Data/
                ├── UDIMTextureImporter3.dll
                └── ZFileUtils/
                    └── ZFileUtils64.dll
```

## Usage
Go to `ZPlugin` -> `UDIMTextureImporter3`.

Make sure textures are in **UDIM naming convention (eg. `filename.1001.tif`, `filename.1002.exr`, etc)**,

### Supported image format
#### Vector/Normal Displacement
Images need to be exported from ZBrush/Mudbox.
* 32 bit tif/tiff (compression: None, and Deflate, LZW with zlib and deflate lib)
* 16 bit FP exr
* For tangent space vector displacement, textures need to be exported in the following settings.
    * Mid value 0.0
    * ZBrush: FlipAndSwitch and TangentFlipAndSwitch: 25
    * Mudbox: Absolute tangent

#### Color
Color import doesn't work if layer recording is turned on. Make sure it's turned off.
* 8/16 bit tiff/exr

#### Mask
* 8/16 bit tiff/exr

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

### Build instruction
1. Build zlib and deflate, then build libtiff with the first two libraries.
2. Download third party libraries.
    1. Download [`miniz.c` and `miniz.h`](https://github.com/richgel999/miniz/releases) and move to src directory.
    2. Download tinyexr.h and move to src directory.
    3. Download [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview), and move all files to sdk directory.
    4. Download [Eigen](https://eigen.tuxfamily.org/) and move to the src directory.

3. So the project directory should be like this.
    ```
    └── UDIMTextureImporter3/
        ├── CMakeLists.txt
        ├── build/
        └── src/
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
`UDIMTextureImporter3.dll` file should be generated under the project root.
