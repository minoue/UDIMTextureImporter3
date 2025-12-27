#include "loader.hpp"
#include <filesystem>
#include <fstream>

float EXPORT loaderMain(char* GoZFilePath, double value,
    char* pOptBuffer1, [[maybe_unused]] int optBuffer1Size,
    char* pOptBuffer2, [[maybe_unused]] int optBuffer2Size,
    [[maybe_unused]] char** zData)
{

    std::filesystem::path path = GoZFilePath;

    // Create log file
    path.replace_filename("UDIMTextureImporterLoader.log");
    std::ofstream log(path.string());
    log.clear();

    // Get dll path and load
    char loaderPath[MAX_PATH];
    GetModuleFileNameA(GetModuleHandleA("UDIMTextureImporterLoader.dll"), loaderPath, MAX_PATH);
    std::filesystem::path baseDir = std::filesystem::path(loaderPath).parent_path();
    std::filesystem::path dllPath = baseDir / "UDIMTextureImporter3.dll";

    // Add dir path to the dll search path
    SetDllDirectoryA(baseDir.string().c_str());

    // HMODULE hLib = LoadLibraryA(dllPath.string().c_str());
    HMODULE hLib = LoadLibraryA("UDIMTextureImporter3.dll");

    log << "DLL path: " << dllPath.string() << std::endl;
    if (hLib == NULL) {
        // If dll not found
        log << "No main DLL found" << std::endl;
        SetDllDirectoryA(NULL);
        return 1.0f;
    } else {
        log << "UDIMTextureImporter3.dll has been loaded." << std::endl;
    }

    // Define function pointer, returning float and takes (char*, double, char*, char*) as arguments
    typedef float (*PluginFunc)(char*, double, char*, char*);

    // Search function address from the dll
    PluginFunc importUDIM = (PluginFunc)GetProcAddress(hLib, "importUDIM");

    float result = 0.0f;

    if (importUDIM != NULL) {
        log << "Running the main logic" << std::endl;
        result = importUDIM(GoZFilePath, value, pOptBuffer1, pOptBuffer2);
    } else {
        log << "The main logic was not executed." << std::endl;
        result = 1.0f;
    }

    // Release the dll from memory and unlock the file
    // This allows to rebuild/reload the main dll without restarting ZBrush
    FreeLibrary(hLib);

    // Remove after use
    SetDllDirectoryA(NULL);

    log.close();

    return result;
}
