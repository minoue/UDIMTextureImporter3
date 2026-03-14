#include <windows.h>
#include "loader.hpp"
#include <filesystem>
#include <fstream>

extern "C" __declspec(dllexport) auto loaderMain(char* GoZFilePath, double value,
    char* pOptBuffer1, [[maybe_unused]] int optBuffer1Size,
    char* pOptBuffer2, [[maybe_unused]] int optBuffer2Size,
    [[maybe_unused]] char** zData) -> float
{

    std::filesystem::path path = GoZFilePath;

    // Create log file
    path.replace_filename("UDIMTextureImporterLoader.log");
    std::ofstream log(path.string());
    log.clear();

    // Get dll path and load
    char loaderPath[MAX_PATH];
    GetModuleFileNameA(GetModuleHandleA("UDIMTextureImporterLoader.dll"), loaderPath, MAX_PATH); // NOLINT
    std::filesystem::path baseDir = std::filesystem::path(loaderPath).parent_path();
    std::filesystem::path dllPath = baseDir / "UDIMTextureImporter3.dll";

    // Add dir path to the dll search path
    SetDllDirectoryA(baseDir.string().c_str());

    // Check if the file exists
    log << "DLL path: " << dllPath.string() << '\n';
    if (std::filesystem::exists(dllPath)) {
        log << "Main DLL exists" << '\n';
    } else {
        log << "Main DLL doesn't exists" << '\n';
        SetDllDirectoryA(nullptr);
        return 1.0F;
    }

    HMODULE hLib = LoadLibraryA(dllPath.string().c_str());

    if (hLib == nullptr) {
        // If dll not found
        log << "Failed to load the main DLL." << '\n';
        SetDllDirectoryA(nullptr);
        return 1.0F;
    }

    log << "UDIMTextureImporter3.dll has been loaded." << '\n';

    // Define function pointer, returning float and takes (char*, double, char*, char*) as arguments
    using PluginFunc = float (*)(char*, double, char*, char*);

    // Search function address from the dll
    PluginFunc importUDIM = (PluginFunc)GetProcAddress(hLib, "importUDIM"); // NOLINT

    float result = 0.0F;

    if (importUDIM != nullptr) {
        log << "Running the main logic" << '\n';
        result = importUDIM(GoZFilePath, value, pOptBuffer1, pOptBuffer2);
    } else {
        log << "The main logic was not executed." << '\n';
        result = 1.0F;
    }

    // Release the dll from memory and unlock the file
    // This allows to rebuild/reload the main dll without restarting ZBrush
    FreeLibrary(hLib);

    // Remove after use
    SetDllDirectoryA(nullptr);

    log.close();

    return result;
}
