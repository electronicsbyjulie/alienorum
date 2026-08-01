cmake_minimum_required(VERSION 3.20)

project(alienorum LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

# pkg_check_modules(CURLPP REQUIRED curlpp)


add_library(IMGUI STATIC
	src/include/imgui/imgui_demo.cpp
	src/include/imgui/imgui_draw.cpp
	src/include/imgui/imgui_tables.cpp
	src/include/imgui/imgui_widgets.cpp
	src/include/imgui/imgui.cpp
	src/include/imgui/backends/imgui_impl_opengl3.cpp
	src/include/imgui/backends/imgui_impl_sdl2.cpp
)

add_library(ImGuiFileDialog
    src/include/igfd/ImGuiFileDialog.cpp
    src/include/igfd/ImGuiFileDialog.h
    src/include/igfd/ImGuiFileDialogConfig.h
)

add_library(cURLpp
    src/include/cURLpp_single/cURLpp.hpp
)
set_target_properties(cURLpp PROPERTIES LINKER_LANGUAGE CXX)

if(UNIX)
    target_compile_options(ImGuiFileDialog PUBLIC -Wno-unknown-pragmas)
endif()

# PUBLIC so we don't have to do the same for all things that rely on imgui
target_include_directories(IMGUI PUBLIC src/include/imgui)
target_include_directories(ImGuiFileDialog PUBLIC src/include/imgui)

# SDL required for imgui_impl_sld2.cpp
target_link_libraries(IMGUI PUBLIC SDL2::SDL2)

add_library(CLASSES STATIC
	src/classes/cat.cpp
	src/classes/celestial.cpp
	src/classes/color.cpp
	src/classes/cons.cpp
	src/classes/galaxy.cpp
	src/classes/misc.cpp
	src/classes/moon.cpp
	src/classes/noise.cpp
	src/classes/patch.cpp
	src/classes/planet.cpp
	src/classes/point.cpp
	src/classes/satellite.cpp
	src/classes/serial.cpp
	src/classes/shore.cpp
	src/classes/star.cpp
)

target_include_directories(CLASSES PUBLIC src/classes src/include
    ${CURL_INCLUDE_DIRS}
    ${CURLPP_INCLUDE_DIRS})

target_link_libraries(CLASSES JPEG::JPEG PNG::PNG
    ${CURL_LIBRARIES}
    cURLpp)

add_executable(alienorum
	src/dialogs.cpp
	src/globals.cpp
	src/gputex.cpp
	src/housekeeping.cpp
	src/inputs.cpp
	src/loaders.cpp
	src/sphere_impostor.cpp
	src/visuals.cpp
	src/alienorum.cpp)

target_link_libraries(alienorum IMGUI CLASSES ImGuiFileDialog)

if(UNIX AND NOT APPLE)
	target_link_libraries(alienorum SDL2_image::SDL2_image OpenGL::GL dl)
elseif(APPLE)
	find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
	find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
	find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
	find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)

	target_link_libraries(alienorum
		${OPENGL_FRAMEWORK}
		${COCOA_FRAMEWORK}
		${IOKIT_FRAMEWORK}
		${COREVIDEO_FRAMEWORK}
		${CURL_LIBRARIES} 
		${CURLPP_LIBRARIES}
		SDL2_image::SDL2_image
	)
elseif(WIN32)
    target_link_libraries(alienorum
		SDL2::SDL2main
		SDL2::SDL2
		SDL2_image
		OpenGL::GL
		gdi32
		winmm
    )
endif()
