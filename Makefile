# ============================================================
# Makefile for VoxelPilot
#
# Standalone-only build:
#
#   standalone / app  Single-machine CUDA + GUI workstation
#
# Usage:
#   make standalone       Build VoxelPilot
#   make app              Alias for standalone
#   make all              Build VoxelPilot
#   make clean            Remove all build artifacts
#   make clean-standalone Remove standalone artifacts only
#
# ============================================================


# ============================================================
# Toolchain Configuration
# ============================================================

NVCC          := nvcc
CXX           := g++
OBJ_EXT       := o
CUDART_MODE   :=

# Common values:
#   sm_60  Pascal   (GTX 1080, P100)
#   sm_75  Turing   (RTX 2080)
#   sm_80  Ampere   (A100, RTX 3090)
#   sm_86  Ampere   (RTX 3060/3070)
#   sm_89  Ada      (RTX 4090)
CUDA_ARCH     := sm_75


# ============================================================
# Directory Layout
# ============================================================

COMMON_DIR    := common
RENDERER_DIR  := renderer
IMGUI_DIR     := imgui
IMGUI_BACKEND := $(IMGUI_DIR)/backends
TINY_DIR      := third_party/tinyfiledialogs
GLEW_ARCHIVE  := glew-2.2.0.tgz
GLEW_DIR      := third_party/glew-2.2.0
GLEW_INC      := $(GLEW_DIR)/include
GLEW_SRC      := $(GLEW_DIR)/src/glew.c

BUILD_DIR         := build
BUILD_STANDALONE  := $(BUILD_DIR)/standalone


# ============================================================
# Standalone Build Configuration
# ============================================================

STANDALONE_BIN  := volume_renderer_standalone

STANDALONE_OBJS = \
    $(BUILD_STANDALONE)/renderer.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/raymarch_kernel.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/volume_textures.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/standalone_main.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/glew.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui_draw.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui_tables.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui_widgets.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui_impl_glfw.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/imgui_impl_opengl3.$(OBJ_EXT) \
    $(BUILD_STANDALONE)/tinyfiledialogs.$(OBJ_EXT)

STANDALONE_NVCC_FLAGS := \
    -rdc=true \
    -arch=$(CUDA_ARCH) \
    -I$(COMMON_DIR) \
    -I$(IMGUI_DIR) \
    -I$(IMGUI_BACKEND) \
    -I$(TINY_DIR) \
    -Xcompiler -Wall \
    -O2


# ============================================================
# Platform-specific linker flags
# ============================================================

ifeq ($(OS),Windows_NT)
    SHELL := cmd.exe
    .SHELLFLAGS := /C
    OBJ_EXT        := obj
    CUDART_MODE    := -cudart hybrid
    CUDA_HOME      := C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2
    VS_DEV_CMD     := call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 >nul
    NVCC           := $(VS_DEV_CMD) && "$(CUDA_HOME)/bin/nvcc.exe"
    CXX            := $(VS_DEV_CMD) && cl
    GLFW_LIB       := imgui/examples/libs/glfw/lib-vc2010-64/glfw3.lib
    CXX_FLAGS      := /nologo /TP /EHsc /std:c++17 /O2 /D_CRT_SECURE_NO_WARNINGS /DGLEW_STATIC /DGLFW_HAS_PER_MONITOR_DPI=0 /DGLFW_HAS_GAMEPAD_API=0 /DGLFW_HAS_GETERROR=0 /I$(COMMON_DIR) /I$(IMGUI_DIR) /I$(IMGUI_BACKEND) /I$(TINY_DIR) /I$(GLEW_INC) /Iglfw/include
    STANDALONE_NVCC_FLAGS := -rdc=true $(CUDART_MODE) -arch=$(CUDA_ARCH) -I$(COMMON_DIR) -I$(IMGUI_DIR) -I$(IMGUI_BACKEND) -I$(TINY_DIR) -I$(GLEW_INC) -Iglfw/include -DGLFW_HAS_PER_MONITOR_DPI=0 -DGLFW_HAS_GAMEPAD_API=0 -DGLFW_HAS_GETERROR=0 -Xcompiler="/W3 /EHsc /MD /DGLEW_STATIC /D_CRT_SECURE_NO_WARNINGS" -O2
    STANDALONE_LDFLAGS := -Xcompiler="/MD" -Xlinker /NODEFAULTLIB:LIBCMT -Xlinker /DEFAULTLIB:MSVCRT opengl32.lib gdi32.lib user32.lib shell32.lib comdlg32.lib ole32.lib $(GLFW_LIB)
    STANDALONE_BIN     := volume_renderer_standalone.exe
else ifeq ($(findstring MINGW,$(MSYSTEM)),MINGW)
    STANDALONE_LDFLAGS := -lopengl32 -lglew32 -lglfw3 -lgdi32 -lm
    STANDALONE_BIN     := volume_renderer_standalone.exe
else
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Darwin)
        STANDALONE_LDFLAGS := -framework OpenGL -lGLEW -lglfw -lm -lpthread
    else
        STANDALONE_LDFLAGS := -lGL -lGLEW -lglfw -lm -lpthread
    endif
endif


# ============================================================
# Phony Targets
# ============================================================

.PHONY: all standalone app clean clean-standalone help


# ============================================================
# Default Target
# ============================================================

all: standalone


# ============================================================
# Help
# ============================================================

help:
	@echo ""
	@echo "VoxelPilot Build System"
	@echo "======================="
	@echo ""
	@echo "  make standalone       Build the standalone app"
	@echo "  make app              Alias for standalone"
	@echo "  make all              Build the standalone app"
	@echo "  make clean            Clean all"
	@echo "  make clean-standalone Clean standalone artifacts"
	@echo "  make help             Show this message"
	@echo ""
	@echo "  standalone requires: nvcc, GLFW, GLEW, OpenGL"
	@echo ""
	@echo "Configuration:"
	@echo "  CUDA_ARCH = $(CUDA_ARCH)"
	@echo "  NVCC      = $(NVCC)"
	@echo "  CXX       = $(CXX)"
	@echo "  IMGUI_DIR = $(IMGUI_DIR)"
	@echo ""


# ============================================================
# Directory Creation
# ============================================================

$(BUILD_STANDALONE):
ifeq ($(OS),Windows_NT)
	@if not exist "$(BUILD_STANDALONE)" mkdir "$(BUILD_STANDALONE)"
else
	@mkdir -p $(BUILD_STANDALONE)
endif

$(GLEW_DIR)/include/GL/glew.h:
	@tar -xf $(GLEW_ARCHIVE) -C third_party


# ============================================================
# Standalone Build Rules
# ============================================================

standalone: $(STANDALONE_BIN)

app: standalone

$(STANDALONE_BIN): $(STANDALONE_OBJS)
	$(NVCC) -rdc=true $(CUDART_MODE) -arch=$(CUDA_ARCH) $(STANDALONE_OBJS) -o $@ $(STANDALONE_LDFLAGS)
	@echo ""
	@echo "=== Standalone built: $(STANDALONE_BIN) ==="
	@echo ""

$(BUILD_STANDALONE)/renderer.$(OBJ_EXT): \
    $(RENDERER_DIR)/renderer.cu \
    $(COMMON_DIR)/volume_structs.h \
    $(COMMON_DIR)/math_utils.h | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -c $< -o $@

$(BUILD_STANDALONE)/raymarch_kernel.$(OBJ_EXT): \
    $(RENDERER_DIR)/raymarch_kernel.cu \
    $(COMMON_DIR)/volume_structs.h \
    $(COMMON_DIR)/math_utils.h | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -c $< -o $@

$(BUILD_STANDALONE)/volume_textures.$(OBJ_EXT): \
    $(RENDERER_DIR)/volume_textures.cu \
    $(COMMON_DIR)/volume_structs.h | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -c $< -o $@

$(BUILD_STANDALONE)/standalone_main.$(OBJ_EXT): \
    standalone_main.cu \
    $(COMMON_DIR)/volume_structs.h \
    $(COMMON_DIR)/math_utils.h \
    $(GLEW_DIR)/include/GL/glew.h | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -c $< -o $@

$(BUILD_STANDALONE)/glew.$(OBJ_EXT): \
    $(GLEW_SRC) \
    $(GLEW_DIR)/include/GL/glew.h | $(BUILD_STANDALONE)
ifeq ($(OS),Windows_NT)
	$(CXX) $(CXX_FLAGS) /I$(GLEW_INC) /Fo$@ /c $(GLEW_SRC)
else
	$(CXX) -O2 -DGLEW_STATIC -I$(GLEW_INC) -c $< -o $@
endif

$(BUILD_STANDALONE)/imgui.$(OBJ_EXT): \
    $(IMGUI_DIR)/imgui.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/imgui_draw.$(OBJ_EXT): \
    $(IMGUI_DIR)/imgui_draw.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/imgui_tables.$(OBJ_EXT): \
    $(IMGUI_DIR)/imgui_tables.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/imgui_widgets.$(OBJ_EXT): \
    $(IMGUI_DIR)/imgui_widgets.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/imgui_impl_glfw.$(OBJ_EXT): \
    $(IMGUI_BACKEND)/imgui_impl_glfw.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/imgui_impl_opengl3.$(OBJ_EXT): \
    $(IMGUI_BACKEND)/imgui_impl_opengl3.cpp | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@

$(BUILD_STANDALONE)/tinyfiledialogs.$(OBJ_EXT): \
    $(TINY_DIR)/tinyfiledialogs.c | $(BUILD_STANDALONE)
	$(NVCC) $(STANDALONE_NVCC_FLAGS) -x cu -c $< -o $@


# ============================================================
# Clean Rules
# ============================================================

clean: clean-standalone
ifeq ($(OS),Windows_NT)
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
else
	@rm -rf $(BUILD_DIR)
endif
	@echo "All build artifacts cleaned."

clean-standalone:
ifeq ($(OS),Windows_NT)
	@if exist "$(BUILD_STANDALONE)" rmdir /s /q "$(BUILD_STANDALONE)"
	@if exist "$(STANDALONE_BIN)" del /q "$(STANDALONE_BIN)"
else
	@rm -f $(STANDALONE_OBJS) $(STANDALONE_BIN)
	@rm -rf $(BUILD_STANDALONE)
endif
	@echo "Standalone artifacts cleaned."
