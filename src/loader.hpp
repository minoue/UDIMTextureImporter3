#ifndef __UDIMTextureImporterLoader_h__
#define __UDIMTextureImporterLoader_h__

#include <windows.h>
#define EXPORT __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif

float EXPORT loaderMain(char* textFromZBrush,
    double value,
    char* pOptBuffer1,
    int optBuffer1Size,
    char* pOptBuffer2,
    int optBuffer2Size,
    char** zData);

#ifdef __cplusplus
}
#endif

#endif /* __UDIMTextureImporterLoader_h__ */
