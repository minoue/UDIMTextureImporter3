cmake_minimum_required(VERSION 3.13)
project(UDIMTextureImporter3)

set(DLL_VERSION 0.2.14)

set(SOURCE_FILES
    src/main.cpp
    src/texture.cpp
    src/sdk/GoZ_Mesh.cpp
    src/sdk/GoZ_Utils.cpp
    src/miniz.c
)

set(LOADER_FILES
    src/loader.cpp
)

set(CMAKE_CXX_STANDARD 20)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("/std:c++latest" _cpp_latest_flag_supported)
if (_cpp_latest_flag_supported)
	message(STATUS "Using c++latest flag.")
    add_compile_options("/std:c++latest")
endif()

# OpenMP
find_package(OpenMP)
if (OPENMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    add_definitions(-DUSE_OPENMP)
endif()

if (TIFF_INSTALL_DIR)
    set(TIFF_INCLUDE_DIR "${TIFF_INSTALL_DIR}/include")
    set(TIFF_LIB_DIR "${TIFF_INSTALL_DIR}/lib")
    message(STATUS "Tiff include dir : ${TIFF_INCLUDE_DIR}")
    message(STATUS "Tiff lib dir : ${TIFF_LIB_DIR}")

    include_directories(${TIFF_INCLUDE_DIR})
    link_directories(${TIFF_LIB_DIR})
else ()
    find_package(TIFF)
    if (TIFF_FOUND)
        message("Tiff include dir : ${TIFF_INCLUDE_DIR}")
        message("Tiff libraries : ${TIFF_LIBRARIES}")
        include_directories(${TIFF_INCLUDE_DIR})
    else ()
        message(NOTICE "Tiff package not found. Using custom libtiff dir")
        if (NOT TIFF_INSTALL_DIR)
            message(FATAL_ERROR "Tiff install dir must be set")
        endif()
    endif ()
endif()

set(INSTALL_DIR "../")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC src)

add_library(UDIMTextureImporterLoader SHARED ${LOADER_FILES})

set_target_properties(${PROJECT_NAME} UDIMTextureImporterLoader PROPERTIES PREFIX "" SUFFIX ".dll")
if (TIFF_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE TIFF::TIFF)
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE tiff)
endif()

install(TARGETS ${PROJECT_NAME} UDIMTextureImporterLoader RUNTIME DESTINATION ../ LIBRARY DESTINATION ../)
set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR})
