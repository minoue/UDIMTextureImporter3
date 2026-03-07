cmake_minimum_required(VERSION 3.13)
project(UDIMTextureImporter3)
set(LOADER_NAME UDIMTextureImporterLoader)

set(DLL_VERSION 0.3.0)

set(SOURCE_FILES
    src/main.cpp
    src/texture.cpp
    third_party/GoZ/GoZ_Mesh.cpp
    third_party/GoZ/GoZ_Utils.cpp
    third_party/tinyexr/miniz.c
)

set(LOADER_FILES
    src/loader.cpp
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT TIFF_INSTALL_DIR)
    find_package(TIFF)
    if (TIFF_FOUND)
        message("Tiff include dir : ${TIFF_INCLUDE_DIR}")
        message("Tiff libraries : ${TIFF_LIBRARIES}")
    else ()
        message(NOTICE "Tiff package not found. Use TIFF_INSTALL_DIR to specifiy custom tiff install dir.")
        message(FATAL_ERROR "TIFF_INSTALL_DIR must be set!")
    endif()
else()
    message(STATUS "Using TIFF_INSTALL_DIR ${TIFF_INSTALL_DIR}")
    set(TIFF_INCLUDE_DIR "${TIFF_INSTALL_DIR}/include")
    set(TIFF_LIB_DIR "${TIFF_INSTALL_DIR}/lib")
    
    message(STATUS "Tiff include dir : ${TIFF_INCLUDE_DIR}")
    message(STATUS "Tiff lib dir : ${TIFF_LIB_DIR}")

    set(TIFF_LIBRARIES
        "${TIFF_LIB_DIR}/tiff.lib"
        "${TIFF_LIB_DIR}/tiffxx.lib"
        )
endif()

# インクルードパスの適用
include_directories(${TIFF_INCLUDE_DIR})

# Create a library for the main dll
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".dll")
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE third_party/tinyexr)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE third_party/GoZ)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE third_party/Eigen)

# Create a library for the intermediate loader dll
add_library(${LOADER_NAME} SHARED ${LOADER_FILES})
set_target_properties(${LOADER_NAME} PROPERTIES PREFIX "" SUFFIX ".dll")

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    # Windows
    message(STATUS "Build environment: Native Windows")
else()
    # Linux environment（MinGW cross compile）
    message(STATUS "Build environment: Linux (mingw for wine)")
    target_link_options(${PROJECT_NAME} PRIVATE "-static" "-static-libgcc" "-static-libstdc++")
    target_link_options(${LOADER_NAME} PRIVATE "-static" "-static-libgcc" "-static-libstdc++")
endif()

# ---- libtiff ----
target_link_libraries(${PROJECT_NAME} PRIVATE ${TIFF_LIBRARIES})

install(TARGETS ${PROJECT_NAME} ${LOADER_NAME} RUNTIME DESTINATION ../ LIBRARY DESTINATION ../)
set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR})
