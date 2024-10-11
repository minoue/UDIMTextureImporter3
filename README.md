# GoUDIM
Import UDIM displacement('sculpt using maps' in Mudbox), color, and mask textures.

## Requirements
* C++20
* [Eigen](https://eigen.tuxfamily.org/)
* [libtiff](https://gitlab.com/libtiff/libtiff)
* [tinyexr](https://github.com/syoyo/tinyexr)
* [GoZ_SDK](https://developers.maxon.net/forum/topic/15246/zbrush-sdk-overview)
* ZFileUtils

## Supported image format
* tif/tiff
* exr

## Build

```sh
mkdir build
cd build
cmake ../
cmake --build . --config Release --target install
```
