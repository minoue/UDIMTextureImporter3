#ifndef __UDIMTextureImporterLoader_h__
#define __UDIMTextureImporterLoader_h__


extern "C" __declspec(dllexport) auto loaderMain(char* textFromZBrush,
    double value,
    char* pOptBuffer1,
    int optBuffer1Size,
    char* pOptBuffer2,
    int optBuffer2Size,
    char** zData) -> float;


#endif /* __UDIMTextureImporterLoader_h__ */
