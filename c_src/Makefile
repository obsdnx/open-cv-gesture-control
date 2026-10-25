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
                 -lopencv_dnn      \
                 -lopencv_highgui

# ---------------------------------------------------------------------------
# Hand CNN models (Google MediaPipe, Apache-2.0). `make models` downloads
# the .task bundle and unzips the two .tflite networks into models/.
# The absolute path is baked into the binary so it runs from anywhere.
# ---------------------------------------------------------------------------
MODEL_DIR := $(CURDIR)/models
MODEL_URL := https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task
MODELS    := $(MODEL_DIR)/hand_detector.tflite $(MODEL_DIR)/hand_landmarks_detector.tflite

# ---------------------------------------------------------------------------
# macOS system frameworks
#
#   AVFoundation        — camera capture (camera.mm)
#   CoreMedia/CoreVideo — frame buffers delivered by AVFoundation
#   Foundation          — NSArray, NSString used by camera.mm
#   CoreGraphics        — CGEvent: synthetic mouse + keyboard input
#   ApplicationServices — required for CGEventPost
#   CoreFoundation      — CFRelease
# ---------------------------------------------------------------------------
MAC_FRAMEWORKS := -framework AVFoundation         \
                  -framework CoreMedia            \
                  -framework CoreVideo            \
                  -framework Foundation           \
                  -framework CoreGraphics         \
                  -framework ApplicationServices  \
                  -framework CoreFoundation

# ---------------------------------------------------------------------------
# Include paths: our own headers + OpenCV
# ---------------------------------------------------------------------------
INCLUDES := -I include -I $(OPENCV_INC) -DMODEL_DIR='"$(MODEL_DIR)"'

# ---------------------------------------------------------------------------
# Source files and derived object paths
# ---------------------------------------------------------------------------
SRCS := src/main.cpp        \
        src/hand_model.cpp  \
        src/gesture.cpp     \
        src/calibration.cpp \
        src/controller.cpp

# camera.mm is Objective-C++ (AVFoundation), built with its own rule below
OBJS := $(SRCS:src/%.cpp=build/%.o) build/camera.o

TARGET := gesture_control

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

# Default target: models + binary
all: $(MODELS) $(TARGET)

# Download and unpack the hand models (only if missing)
$(MODELS):
	mkdir -p $(MODEL_DIR)
	curl -L --fail -o $(MODEL_DIR)/hand_landmarker.task $(MODEL_URL)
	cd $(MODEL_DIR) && unzip -o -q hand_landmarker.task && rm hand_landmarker.task

models: $(MODELS)

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

# Objective-C++ files: -fobjc-arc enables automatic reference counting
# so we never call retain/release by hand.
build/%.o: src/%.mm | build
	$(CXX) $(CXXFLAGS) -fobjc-arc $(INCLUDES) -MMD -c $< -o $@

# Create the build directory if it doesn't exist
build:
	mkdir -p build

# Include generated dependency files (if they exist)
-include $(OBJS:.o=.d)

clean:
	rm -rf build $(TARGET)

run: all
	./$(TARGET)

.PHONY: all models clean run
