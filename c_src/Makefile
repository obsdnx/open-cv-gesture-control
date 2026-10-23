# =============================================================================
# Makefile — gesture control (C++17 + OpenCV 5 + CoreGraphics)
#
# HOW THE BUILD WORKS
# -------------------
# 1. Each .cpp in src/ is compiled separately into a .o object file.
# 2. All .o files are linked together into the final binary.
# 3. We link against OpenCV (image processing) and CoreGraphics/
#    ApplicationServices (macOS mouse + keyboard events).
#
# USAGE
#   make          — build the binary (./gesture_control)
#   make clean    — remove all build artifacts
#   make run      — build then run
# =============================================================================

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
CXX      := clang++
CXXFLAGS := -std=c++17 -O2 -Wall

# ---------------------------------------------------------------------------
# OpenCV 5 — headers and libraries
#
# Homebrew installs OpenCV under a versioned Cellar path.
# The include subdirectory is called "opencv5" but the headers
# inside still use the classic "opencv2/" prefix (backward compat).
# ---------------------------------------------------------------------------
OPENCV_PREFIX := /opt/homebrew/Cellar/opencv/5.0.0_11
OPENCV_INC    := $(OPENCV_PREFIX)/include/opencv5
OPENCV_LIBS   := -L$(OPENCV_PREFIX)/lib \
                 -lopencv_core     \
                 -lopencv_imgproc  \
                 -lopencv_geometry \
                 -lopencv_videoio  \
                 -lopencv_highgui

# ---------------------------------------------------------------------------
# macOS system frameworks (pure C APIs, no Objective-C needed)
#
#   CoreGraphics        — CGEvent: synthetic mouse + keyboard input
#   ApplicationServices — higher-level wrapper, required for CGEventPost
#   CoreFoundation      — CFRelease, CFStringRef (used by CGEvent)
# ---------------------------------------------------------------------------
MAC_FRAMEWORKS := -framework CoreGraphics        \
                  -framework ApplicationServices  \
                  -framework CoreFoundation

# ---------------------------------------------------------------------------
# Include paths: our own headers + OpenCV
# ---------------------------------------------------------------------------
INCLUDES := -I include -I $(OPENCV_INC)

# ---------------------------------------------------------------------------
# Source files and derived object paths
# ---------------------------------------------------------------------------
SRCS := src/main.cpp       \
        src/camera.cpp     \
        src/gesture.cpp    \
        src/controller.cpp

OBJS := $(SRCS:src/%.cpp=build/%.o)

TARGET := gesture_control

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

# Default target: build the binary
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) \
	    $(OPENCV_LIBS) $(MAC_FRAMEWORKS) \
	    -Wl,-rpath,$(OPENCV_PREFIX)/lib
	@echo "Built: $(TARGET)"

# Compile each source file into build/*.o
# The -MMD flag generates dependency files (.d) so header changes
# automatically trigger recompilation of dependent sources.
build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -c $< -o $@

# Create the build directory if it doesn't exist
build:
	mkdir -p build

# Include generated dependency files (if they exist)
-include $(OBJS:.o=.d)

clean:
	rm -rf build $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: clean run
