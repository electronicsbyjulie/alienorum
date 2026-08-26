#
# Makefile for Linux, Windows, Mac OS. Make sure to install SDL2 (http://www.libsdl.org)
#
# Linux:
#   apt-get install -y libsdl2-dev libsdl2-image-dev libjpeg-dev libpng-dev build-essential libcurl4-openssl-dev libarchive-dev libgtest-dev
#
# Mac OS:
#   brew install sdl2 sdl2_image jpeg png curl archive
#
# MSYS2 (Run in MINGW64 environment):
#   pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-libpng mingw-w64-x86_64-curl mingw-w64-x86_64-libarchive
#
# Claude Code deletes irreplaceable files and is not PTSD friendly.
#

CPP = g++
CPPFLAGS = -std=c++17 -O2 -MMD -MP -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -Wall -Wformat

# Uncomment for debug mode
# CPPFLAGS += -g -DDEBUG -O0

# Uncomment to track down memory errors
# CPPFLAGS += -g -O0 -fsanitize=address -fno-omit-frame-pointer

# For gprof
# example command line:
# gprof bin/alienorum gmon.out > alienorum.output
# CPPFLAGS += -g -pg -DDEBUG

IMGUI_DIR = src/include/imgui
IGFD_DIR = src/include/igfd
CLASSES_DIR = src/classes
TESTS_DIR = src/tests
IMGUI_SRC = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
            $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IGFD_SRC = $(IGFD_DIR)/ImGuiFileDialog.cpp
CLASSES_SRC = $(CLASSES_DIR)/point.cpp $(CLASSES_DIR)/cat.cpp $(CLASSES_DIR)/star.cpp $(CLASSES_DIR)/celestial.cpp $(CLASSES_DIR)/color.cpp \
            $(CLASSES_DIR)/misc.cpp $(CLASSES_DIR)/planet.cpp $(CLASSES_DIR)/moon.cpp $(CLASSES_DIR)/galaxy.cpp $(CLASSES_DIR)/comet.cpp \
			$(CLASSES_DIR)/serial.cpp $(CLASSES_DIR)/noise.cpp $(CLASSES_DIR)/satellite.cpp $(CLASSES_DIR)/shore.cpp $(CLASSES_DIR)/patch.cpp \
			$(CLASSES_DIR)/cons.cpp $(CLASSES_DIR)/sscimport.cpp
TESTS_SRC = $(TESTS_DIR)/point_test.cpp $(TESTS_DIR)/color_test.cpp \
			$(TESTS_DIR)/celestial_test.cpp $(TESTS_DIR)/galaxy_test.cpp $(TESTS_DIR)/star_test.cpp \
			$(TESTS_DIR)/planet_test.cpp $(TESTS_DIR)/moon_test.cpp $(TESTS_DIR)/comet_test.cpp \
			$(TESTS_DIR)/satellite_test.cpp $(TESTS_DIR)/cons_test.cpp \
			$(TESTS_DIR)/serial_test.cpp $(TESTS_DIR)/misc_test.cpp $(TESTS_DIR)/cat_test.cpp \
			$(TESTS_DIR)/housekeeping_test.cpp 

BIN = bin
OBJ = obj
UNAME_S := $(shell uname -s)
LIBS = -lSDL2_image -ljpeg -lpng -lcurl -larchive
LIBS_GTEST = -lgtest -lgtest_main -pthread
LINUX_GL_LIBS = -lGL

# Dynamically track all object files cleanly
IMGUI_OBJS = $(addsuffix .o, $(addprefix $(OBJ)/, $(basename $(notdir $(IMGUI_SRC)))))
OBJS = $(IMGUI_OBJS)
OBJS += $(addsuffix .o, $(addprefix $(OBJ)/, $(basename $(notdir $(IGFD_SRC)))))
OBJS += $(addsuffix .o, $(addprefix $(OBJ)/, $(basename $(notdir $(CLASSES_SRC)))))
OBJS += $(OBJ)/globals.o $(OBJ)/loaders.o $(OBJ)/housekeeping.o $(OBJ)/inputs.o $(OBJ)/dialogs.o $(OBJ)/visuals.o $(OBJ)/gputex.o $(OBJ)/sphere_impostor.o

TESTS = $(addprefix $(BIN)/, $(basename $(notdir $(TESTS_SRC))))

INCLUDES = -I./src/include -I./$(IMGUI_DIR) -I./$(CLASSES_DIR)
CPPFLAGS += $(INCLUDES)

# Empty in an ordinary build; the tests-asan target below re-enters make with it set. Kept as its
# own variable rather than passing CPPFLAGS on the command line, which would override every
# accumulated -I and sdl2-config flag above rather than adding to them.
SANFLAGS =
CPPFLAGS += $(SANFLAGS)

# Platform-specific configurations
ifeq ($(UNAME_S), Linux)
    ECHO_MESSAGE = "Building for Linux..."
    LIBS += $(LINUX_GL_LIBS) -ldl `sdl2-config --libs`
    CPPFLAGS += `sdl2-config --cflags`
    CFLAGS = $(CPPFLAGS)
endif

ifeq ($(UNAME_S), Darwin)
    ECHO_MESSAGE = "Building for Mac OS..."
    LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs`
    # Support both Intel (/usr/local) and Apple Silicon (/opt/homebrew)
    LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib
    CPPFLAGS += `sdl2-config --cflags` -I/usr/local/include -I/opt/local/include -I/opt/homebrew/include
    CFLAGS = $(CPPFLAGS)
endif

ifeq ($(OS), Windows_NT)
    ECHO_MESSAGE = "Building for Windows in MinGW..."
    LIBS += -lgdi32 -lopengl32 -limm32 `pkg-config --static --libs sdl2`
    CPPFLAGS += `pkg-config --cflags sdl2`
    CFLAGS = $(CPPFLAGS)
endif

all: $(BIN) $(OBJ) objs apps
	@echo $(ECHO_MESSAGE)

# Robust cross-platform directory generation
$(BIN):
	mkdir -p $(BIN)

$(OBJ):
	mkdir -p $(OBJ)

clean:
	rm -Rf $(OBJ)/*.o
	rm -Rf $(OBJ)/*.d
	rm -Rf $(BIN)/*

apps: alienorum

objs: $(OBJS)

# Runs every test binary and does not stop at the first one that fails: a single failing suite used
# to hide the state of every suite after it, which is exactly when you most want to see them. The
# loop walks $(TESTS), so `make OBJ=... BIN=... tests` runs the binaries it just built rather than
# whatever is in bin/. The exit status still reports failure, so a script or a hook can act on it.
tests: $(TESTS)
	@rc=0; for t in $(TESTS); do \
		echo "=== $$t ==="; \
		$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "=== all test suites passed ==="; \
	else echo "=== SOME TEST SUITES FAILED -- see above ==="; fi; \
	exit $$rc

# The same test binaries under AddressSanitizer, built into their own obj/ and bin/ so they never
# get mixed up with the ordinary ones. Use this whenever a test crashes, or a result depends on
# which order the tests ran in: a double free, a read past the end of an array, or a write through
# a pointer whose object is gone gets reported where it happens instead of somewhere later. Two of
# the bugs in these tests were found by their crash and nothing else, which is a bad way to find
# out. Slower to build and to run, so it is not what `make tests` does.
tests-asan:
	mkdir -p obj-asan bin-asan
	$(MAKE) OBJ=obj-asan BIN=bin-asan SANFLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer" tests

alienorum: $(BIN)/alienorum


%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CPP) $(CPPFLAGS) -c -o $(OBJ)/$@ $<

$(OBJ)/imgui_demo.o:$(IMGUI_DIR)/imgui_demo.cpp
	$(CPP) $(IMGUI_DIR)/imgui_demo.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_demo.o

$(OBJ)/imgui_draw.o:$(IMGUI_DIR)/imgui_draw.cpp
	$(CPP) $(IMGUI_DIR)/imgui_draw.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_draw.o

$(OBJ)/imgui_tables.o:$(IMGUI_DIR)/imgui_tables.cpp
	$(CPP) $(IMGUI_DIR)/imgui_tables.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_tables.o

$(OBJ)/imgui_widgets.o:$(IMGUI_DIR)/imgui_widgets.cpp
	$(CPP) $(IMGUI_DIR)/imgui_widgets.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_widgets.o

$(OBJ)/imgui.o:$(IMGUI_DIR)/imgui.cpp
	$(CPP) $(IMGUI_DIR)/imgui.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui.o

$(OBJ)/imgui_impl_opengl3.o:$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
	$(CPP) $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_impl_opengl3.o

$(OBJ)/ImGuiFileDialog.o:$(IGFD_DIR)/ImGuiFileDialog.cpp
	$(CPP) $(IGFD_DIR)/ImGuiFileDialog.cpp $(CPPFLAGS) -c -o $(OBJ)/ImGuiFileDialog.o

$(OBJ)/imgui_impl_sdl2.o:$(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp
	$(CPP) $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_impl_sdl2.o

$(OBJ)/misc.o: $(CLASSES_DIR)/misc.cpp
	$(CPP) $(CLASSES_DIR)/misc.cpp $(CPPFLAGS) -c -o $(OBJ)/misc.o

$(OBJ)/noise.o: $(CLASSES_DIR)/noise.cpp
	$(CPP) $(CLASSES_DIR)/noise.cpp $(CPPFLAGS) -c -o $(OBJ)/noise.o

$(OBJ)/patch.o: $(CLASSES_DIR)/patch.cpp
	$(CPP) $(CLASSES_DIR)/patch.cpp $(CPPFLAGS) -c -o $(OBJ)/patch.o

$(OBJ)/color.o: $(CLASSES_DIR)/color.cpp
	$(CPP) $(CLASSES_DIR)/color.cpp $(CPPFLAGS) -c -o $(OBJ)/color.o

$(OBJ)/point.o: $(CLASSES_DIR)/point.cpp
	$(CPP) $(CLASSES_DIR)/point.cpp $(CPPFLAGS) -c -o $(OBJ)/point.o

$(OBJ)/celestial.o: $(CLASSES_DIR)/celestial.cpp
	$(CPP) $(CLASSES_DIR)/celestial.cpp $(CPPFLAGS) -c -o $(OBJ)/celestial.o

$(OBJ)/galaxy.o: $(CLASSES_DIR)/galaxy.cpp
	$(CPP) $(CLASSES_DIR)/galaxy.cpp $(CPPFLAGS) -c -o $(OBJ)/galaxy.o

$(OBJ)/comet.o: $(CLASSES_DIR)/comet.cpp
	$(CPP) $(CLASSES_DIR)/comet.cpp $(CPPFLAGS) -c -o $(OBJ)/comet.o

$(OBJ)/star.o: $(CLASSES_DIR)/star.cpp
	$(CPP) $(CLASSES_DIR)/star.cpp $(CPPFLAGS) -c -o $(OBJ)/star.o

$(OBJ)/cons.o: $(CLASSES_DIR)/cons.cpp
	$(CPP) $(CLASSES_DIR)/cons.cpp $(CPPFLAGS) -c -o $(OBJ)/cons.o

$(OBJ)/sscimport.o: $(CLASSES_DIR)/sscimport.cpp
	$(CPP) $(CLASSES_DIR)/sscimport.cpp $(CPPFLAGS) -c -o $(OBJ)/sscimport.o

$(OBJ)/planet.o: $(CLASSES_DIR)/planet.cpp
	$(CPP) $(CLASSES_DIR)/planet.cpp $(CPPFLAGS) -c -o $(OBJ)/planet.o

$(OBJ)/moon.o: $(CLASSES_DIR)/moon.cpp
	$(CPP) $(CLASSES_DIR)/moon.cpp $(CPPFLAGS) -c -o $(OBJ)/moon.o

$(OBJ)/satellite.o: $(CLASSES_DIR)/satellite.cpp
	$(CPP) $(CLASSES_DIR)/satellite.cpp $(CPPFLAGS) -c -o $(OBJ)/satellite.o

$(OBJ)/shore.o: $(CLASSES_DIR)/shore.cpp
	$(CPP) $(CLASSES_DIR)/shore.cpp $(CPPFLAGS) -c -o $(OBJ)/shore.o

$(OBJ)/cat.o: $(CLASSES_DIR)/cat.cpp
	$(CPP) $(CLASSES_DIR)/cat.cpp $(CPPFLAGS) -c -o $(OBJ)/cat.o

$(OBJ)/serial.o: $(CLASSES_DIR)/serial.cpp
	$(CPP) $(CLASSES_DIR)/serial.cpp $(CPPFLAGS) -c -o $(OBJ)/serial.o

$(OBJ)/globals.o: src/globals.cpp
	$(CPP) src/globals.cpp $(CPPFLAGS) -c -o $(OBJ)/globals.o

$(OBJ)/loaders.o: src/loaders.cpp
	$(CPP) src/loaders.cpp $(CPPFLAGS) -c -o $(OBJ)/loaders.o

$(OBJ)/housekeeping.o: src/housekeeping.cpp
	$(CPP) src/housekeeping.cpp $(CPPFLAGS) -c -o $(OBJ)/housekeeping.o

$(OBJ)/inputs.o: src/inputs.cpp
	$(CPP) src/inputs.cpp $(CPPFLAGS) -c -o $(OBJ)/inputs.o

$(OBJ)/dialogs.o: src/dialogs.cpp
	$(CPP) src/dialogs.cpp $(CPPFLAGS) -c -o $(OBJ)/dialogs.o

$(OBJ)/visuals.o: src/visuals.cpp
	$(CPP) src/visuals.cpp $(CPPFLAGS) -c -o $(OBJ)/visuals.o

$(OBJ)/gputex.o: src/gputex.cpp
	$(CPP) src/gputex.cpp $(CPPFLAGS) -c -o $(OBJ)/gputex.o

$(OBJ)/sphere_impostor.o: src/sphere_impostor.cpp
	$(CPP) src/sphere_impostor.cpp $(CPPFLAGS) -c -o $(OBJ)/sphere_impostor.o


# Every test binary links the whole object set, so $(OBJS) has to be a prerequisite of each of
# them as well: without it, editing a class only rebuilt that class's .o, and the test kept the
# copy that was linked into it before the edit -- passing or failing on code that is no longer
# there. The .cpp and .h that the test is actually about stay listed too, for clarity.
$(BIN)/point_test: $(OBJS) $(TESTS_DIR)/point_test.cpp $(CLASSES_DIR)/point.h $(CLASSES_DIR)/point.cpp
	$(CPP) $(TESTS_DIR)/point_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/point_test

$(BIN)/color_test: $(OBJS) $(TESTS_DIR)/color_test.cpp $(CLASSES_DIR)/color.h $(CLASSES_DIR)/color.cpp
	$(CPP) $(TESTS_DIR)/color_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/color_test

$(BIN)/celestial_test: $(OBJS) $(TESTS_DIR)/celestial_test.cpp $(CLASSES_DIR)/celestial.h $(CLASSES_DIR)/celestial.cpp
	$(CPP) $(TESTS_DIR)/celestial_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/celestial_test

$(BIN)/galaxy_test: $(OBJS) $(TESTS_DIR)/galaxy_test.cpp $(CLASSES_DIR)/galaxy.h $(CLASSES_DIR)/galaxy.cpp
	$(CPP) $(TESTS_DIR)/galaxy_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/galaxy_test

$(BIN)/star_test: $(OBJS) $(TESTS_DIR)/star_test.cpp $(CLASSES_DIR)/star.h $(CLASSES_DIR)/star.cpp
	$(CPP) $(TESTS_DIR)/star_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/star_test

$(BIN)/cons_test: $(OBJS) $(TESTS_DIR)/cons_test.cpp $(CLASSES_DIR)/cons.h $(CLASSES_DIR)/cons.cpp
	$(CPP) $(TESTS_DIR)/cons_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/cons_test

$(BIN)/planet_test: $(OBJS) $(TESTS_DIR)/planet_test.cpp $(CLASSES_DIR)/planet.h $(CLASSES_DIR)/planet.cpp
	$(CPP) $(TESTS_DIR)/planet_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/planet_test

$(BIN)/moon_test: $(OBJS) $(TESTS_DIR)/moon_test.cpp $(CLASSES_DIR)/moon.h $(CLASSES_DIR)/moon.cpp
	$(CPP) $(TESTS_DIR)/moon_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/moon_test

$(BIN)/comet_test: $(OBJS) $(TESTS_DIR)/comet_test.cpp $(CLASSES_DIR)/comet.h $(CLASSES_DIR)/comet.cpp
	$(CPP) $(TESTS_DIR)/comet_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/comet_test

$(BIN)/satellite_test: $(OBJS) $(TESTS_DIR)/satellite_test.cpp $(CLASSES_DIR)/satellite.h $(CLASSES_DIR)/satellite.cpp
	$(CPP) $(TESTS_DIR)/satellite_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/satellite_test

$(BIN)/serial_test: $(OBJS) $(TESTS_DIR)/serial_test.cpp $(TESTS_DIR)/universe_fixture.h $(CLASSES_DIR)/serial.h $(CLASSES_DIR)/serial.cpp
	$(CPP) $(TESTS_DIR)/serial_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/serial_test

$(BIN)/misc_test: $(OBJS) $(TESTS_DIR)/misc_test.cpp $(CLASSES_DIR)/misc.h $(CLASSES_DIR)/misc.cpp
	$(CPP) $(TESTS_DIR)/misc_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/misc_test

$(BIN)/cat_test: $(OBJS) $(TESTS_DIR)/cat_test.cpp $(TESTS_DIR)/universe_fixture.h $(CLASSES_DIR)/cat.h $(CLASSES_DIR)/cat.cpp
	$(CPP) $(TESTS_DIR)/cat_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/cat_test

$(BIN)/housekeeping_test: $(OBJS) $(TESTS_DIR)/housekeeping_test.cpp $(TESTS_DIR)/universe_fixture.h src/housekeeping.h src/housekeeping.cpp
	$(CPP) $(TESTS_DIR)/housekeeping_test.cpp $(OBJS) $(CPPFLAGS) $(LIBS) $(LIBS_GTEST) -o $(BIN)/housekeeping_test

# gprof requires compiling and linking main code file in one unified command; do not split out.
$(BIN)/alienorum: $(OBJS) src/alienorum.cpp
	$(CPP) src/alienorum.cpp $(OBJS) $(CPPFLAGS) -o $(BIN)/alienorum $(LIBS) $(GPROF)

# Auto-generated per-object header dependencies (see -MMD -MP above). The leading '-' means
# make won't complain on a clean checkout, before any .d files exist yet.
-include $(OBJS:.o=.d)

