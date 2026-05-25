#
# Makefile for Linux, Windows, Mac OS. Make sure to install SDL2 (http://www.libsdl.org)
#
# Linux:
#   apt-get install -y libsdl2-dev libsdl2-image-dev
#
# Mac OS:
#   brew install sdl2 sdl2_image
#
# MSYS2 (Run in MINGW64 environment):
#   pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image
#

CPP = g++
CPPFLAGS = -std=c++17 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -Wall -Wformat

# Uncomment for debug mode
# CPPFLAGS += -g -DDEBUG

# For gprof
# example command line:
# gprof bin/alienorum gmon.out > alienorum.output
# CPPFLAGS += -g -pg -DDEBUG

IMGUI_DIR = src/imgui
CLASSES_DIR = src/classes
IMGUI_SRC = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
            $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
CLASSES_SRC = $(CLASSES_DIR)/point.cpp $(CLASSES_DIR)/cat.cpp $(CLASSES_DIR)/star.cpp $(CLASSES_DIR)/celestial.cpp $(CLASSES_DIR)/color.cpp \
            $(CLASSES_DIR)/misc.cpp $(CLASSES_DIR)/planet.cpp $(CLASSES_DIR)/moon.cpp $(CLASSES_DIR)/galaxy.cpp $(CLASSES_DIR)/serial.cpp

BIN = bin
OBJ = obj
UNAME_S := $(shell uname -s)
LIBS =
LINUX_GL_LIBS = -lGL

# Dynamically track all object files cleanly
OBJS = $(addsuffix .o, $(addprefix $(OBJ)/, $(basename $(notdir $(IMGUI_SRC)))))
OBJS += $(addsuffix .o, $(addprefix $(OBJ)/, $(basename $(notdir $(CLASSES_SRC)))))

INCLUDES = -I./src/include -I./src/imgui -I./src/classes
CPPFLAGS += $(INCLUDES)

# Platform-specific configurations
ifeq ($(UNAME_S), Linux)
    ECHO_MESSAGE = "Building for Linux..."
    LIBS += $(LINUX_GL_LIBS) -ldl `sdl2-config --libs` -lSDL2_image
    CPPFLAGS += `sdl2-config --cflags`
    CFLAGS = $(CPPFLAGS)
endif

ifeq ($(UNAME_S), Darwin)
    ECHO_MESSAGE = "Building for Mac OS..."
    LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs` -lSDL2_image
    
    # Support both Intel (/usr/local) and Apple Silicon (/opt/homebrew)
    LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib
    CPPFLAGS += `sdl2-config --cflags` -I/usr/local/include -I/opt/local/include -I/opt/homebrew/include
    CFLAGS = $(CPPFLAGS)
endif

ifeq ($(OS), Windows_NT)
    ECHO_MESSAGE = "Building for Windows in MinGW..."
    LIBS += -lgdi32 -lopengl32 -limm32 `pkg-config --static --libs sdl2` -lSDL2_image
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
	rm -Rf $(BIN)/*

apps: alienorum

objs: $(OBJS)

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

$(OBJ)/imgui_impl_sdl2.o:$(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp
	$(CPP) $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(CPPFLAGS) -c -o $(OBJ)/imgui_impl_sdl2.o

$(OBJ)/misc.o: $(CLASSES_DIR)/misc.cpp $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/misc.cpp $(CPPFLAGS) -c -o $(OBJ)/misc.o

$(OBJ)/color.o: $(CLASSES_DIR)/color.cpp $(CLASSES_DIR)/color.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/color.cpp $(CPPFLAGS) -c -o $(OBJ)/color.o

$(OBJ)/point.o: $(CLASSES_DIR)/point.cpp $(CLASSES_DIR)/point.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/point.cpp $(CPPFLAGS) -c -o $(OBJ)/point.o

$(OBJ)/celestial.o: $(CLASSES_DIR)/celestial.cpp $(CLASSES_DIR)/celestial.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/celestial.cpp $(CPPFLAGS) -c -o $(OBJ)/celestial.o

$(OBJ)/galaxy.o: $(CLASSES_DIR)/galaxy.cpp $(CLASSES_DIR)/galaxy.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/galaxy.cpp $(CPPFLAGS) -c -o $(OBJ)/galaxy.o

$(OBJ)/star.o: $(CLASSES_DIR)/star.cpp $(CLASSES_DIR)/star.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/star.cpp $(CPPFLAGS) -c -o $(OBJ)/star.o

$(OBJ)/planet.o: $(CLASSES_DIR)/planet.cpp $(CLASSES_DIR)/planet.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/planet.cpp $(CPPFLAGS) -c -o $(OBJ)/planet.o

$(OBJ)/moon.o: $(CLASSES_DIR)/moon.cpp $(CLASSES_DIR)/moon.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/moon.cpp $(CPPFLAGS) -c -o $(OBJ)/moon.o

$(OBJ)/cat.o: $(CLASSES_DIR)/cat.cpp $(CLASSES_DIR)/cat.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/cat.cpp $(CPPFLAGS) -c -o $(OBJ)/cat.o

$(OBJ)/serial.o: $(CLASSES_DIR)/serial.cpp $(CLASSES_DIR)/serial.h $(CLASSES_DIR)/misc.h makefile
	$(CPP) $(CLASSES_DIR)/serial.cpp $(CPPFLAGS) -c -o $(OBJ)/serial.o

# gprof requires compiling and linking main code file in one unified command; do not split out.
$(BIN)/alienorum: $(OBJS) src/alienorum.cpp makefile
	$(CPP) src/alienorum.cpp $(OBJS) $(CPPFLAGS) -o $(BIN)/alienorum $(LIBS) $(GPROF)

