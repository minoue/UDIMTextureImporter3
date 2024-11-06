# GoUDIM
Import UDIM displacement('sculpt using maps' in Mudbox), color, and mask textures.

**Only avaiable on Windows**


## Install
### Windows10 & ZBrush2022
1. Go to [release page](), download the latest and extract it.
2. Move **tiff.dll**, **zlib.dll**, and **libdeflate.dll** to the same directory as ZBrush.exe. (For example. `C:\Program Files\Pixologic\ZBrush 2022\tiff.dll`)
3. Move `GoUDIM.zsc` to `ZPlugs64` folder.
4. Move `GoUDIM` to `ZPlugs64` folder.

```
ZStartup/
├─ ZPlugs64/
│  ├─ GoUDIM/
│  │  ├─ ZFileUtils/
│  │  │  ├─ ZFileUtils.dll
│  │  ├─ GoUDIM.dll
│  ├─ GoUDIM.zsc
```

## Usage
Go to `ZPlugin` -> `GoUDIM`.

Make sure textures are in UDIM naming convention (eg. `filename.1001.tif`, `filename.1002.exr`, etc),

### Supported image format
#### Vector/Normal Displacement
Images need to be exported from ZBrush/Mudbox.
* 32 bit tif/tiff (compression: None, and Deflate, LZW with zlib and deflate lib)
* 16 bit FP exr

#### Color
* 8/16 bit tiff/exr

#### Mask
* 8/16 bit tiff/exr

## Build

### Requirements
* C++20
* [Eigen](https://eigen.tuxfamily.org/)
* [libtiff](https://gitlab.com/libtiff/libtiff)
    * [zlib](https://zlib.net) optional for libtiff
    * [deflate](https://github.com/ebiggers/libdeflate) optional for libtiff
* [tinyexr](https://github.com/syoyo/tinyexr)
* [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview)
* [ZFileUtils](https://help.maxon.net/zbr/en-us/Content/html/user-guide/customizing-zbrush/zscripting/zfileutils/zfileutils.html)

1. Build zlib and deflate first, then build libtiff with the first two libraries.
2. asfd
3. asdfa

### Build instruction

```sh
mkdir build
cd build
cmake ../
cmake --build . --config Release --target install
```
