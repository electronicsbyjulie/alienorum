cmake_minimum_required(VERSION 3.20)

project(alienorum VERSION 0.1.0 LANGUAGES CXX)

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
		$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
		$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
		$<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
		OpenGL::GL
		gdi32
		winmm
    )
endif()

# --- Windows installer packaging (see package/build_windows_installer.sh) ---
if(WIN32)
	install(TARGETS alienorum RUNTIME DESTINATION .)

	install(DIRECTORY assets DESTINATION .)

	install(FILES
		catalogs/star_orbits.dat
		catalogs/planets.json
		catalogs/soles_alienorum.dat
		catalogs/mainseq.dat
		catalogs/urls.dat
		DESTINATION catalogs)
	install(FILES catalogs/sat/sources.json DESTINATION catalogs/sat)

	if(EXISTS "${CMAKE_SOURCE_DIR}/package/release-maps")
		install(DIRECTORY package/release-maps/ DESTINATION maps)
	endif()

	set(CPACK_GENERATOR "NSIS")
	set(CPACK_PACKAGE_NAME "Alienorum")
	set(CPACK_PACKAGE_VENDOR "Alienorum")
	set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "Alienorum")
	set(CPACK_PACKAGE_EXECUTABLES "alienorum" "Alienorum")
	set(CPACK_NSIS_DISPLAY_NAME "Alienorum")
	set(CPACK_NSIS_PACKAGE_NAME "Alienorum")
	set(CPACK_NSIS_INSTALLED_ICON_NAME "alienorum.exe")
	set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/icon48.ico")
	set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/icon48.ico")
	set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
	set(CPACK_NSIS_MODIFY_PATH OFF)
	# The exe is installed to the package root (not a bin/ subfolder) specifically so
	# NSIS's default Start Menu shortcut -- whose working directory defaults to the
	# target executable's own folder -- lands on $INSTDIR, where catalogs/assets/maps
	# also live. The app resolves all its data paths relative to its working directory
	# (see look_for_catalogs() in loaders.cpp), so this keeps it finding its data without
	# any source changes.
	include(CPack)
endif()
