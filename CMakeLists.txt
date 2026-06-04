cmake_minimum_required(VERSION 3.20)

project(alienorum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)

add_library(IMGUI STATIC
	src/include/imgui/imgui_demo.cpp
	src/include/imgui/imgui_draw.cpp
	src/include/imgui/imgui_tables.cpp
	src/include/imgui/imgui_widgets.cpp
	src/include/imgui/imgui.cpp
	src/include/imgui/backends/imgui_impl_opengl3.cpp
	src/include/imgui/backends/imgui_impl_sdl2.cpp
)

# PUBLIC so we don't have to do the same for all things that rely on imgui
target_include_directories(IMGUI PUBLIC src/include/imgui)

# SDL required for imgui_impl_sld2.cpp
target_link_libraries(IMGUI PUBLIC SDL2::SDL2)

add_library(CLASSES STATIC
	src/classes/cat.cpp
	src/classes/celestial.cpp
	src/classes/color.cpp
	src/classes/galaxy.cpp
	src/classes/misc.cpp
	src/classes/moon.cpp
	src/classes/noise.cpp
	src/classes/planet.cpp
	src/classes/point.cpp
	src/classes/serial.cpp
	src/classes/star.cpp
)

target_include_directories(CLASSES PUBLIC src/classes src/include)

target_link_libraries(CLASSES jpeg PNG::PNG)

add_executable(alienorum src/alienorum.cpp)

target_link_libraries(alienorum IMGUI CLASSES)

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
		SDL2_image::SDL2_image
	)
elseif(WIN32)
	target_link_libraries(alienorum PRIVATE
		SDL2_image
		OpenGL::GL
		gdi32
		mm32
	)
endif()
