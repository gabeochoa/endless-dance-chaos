
RAYLIB_FLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIB := $(shell pkg-config --libs raylib 2>/dev/null)

# MCP support - run with: make MCP=1
ifdef MCP
	MCP_FLAGS = -DAFTER_HOURS_ENABLE_MCP
else
	MCP_FLAGS =
endif

FLAGS = -std=c++23 -Wall -Wextra -Wuninitialized -Wshadow -g $(RAYLIB_FLAGS) $(MCP_FLAGS) -DAFTER_HOURS_ENABLE_E2E_TESTING
NOFLAGS = -Wno-deprecated-volatile -Wno-missing-field-initializers \
		  -Wno-c99-extensions -Wno-unused-function -Wno-sign-conversion \
		  -Wno-implicit-int-float-conversion

INCLUDES = -Ivendor/ -Isrc/
LIBS = -Lvendor/ $(RAYLIB_LIB) -framework OpenGL

SRC_FILES := $(wildcard src/*.cpp src/**/*.cpp)
H_FILES := $(wildcard src/*.h src/**/*.h)
OBJ_DIR := ./output
OBJ_FILES := $(SRC_FILES:%.cpp=$(OBJ_DIR)/%.o)

OUTPUT_EXE := $(OBJ_DIR)/dance.exe

CXX := clang++ -std=c++23
# Use ccache if available
CCACHE := $(shell command -v ccache 2>/dev/null)
ifdef CCACHE
    CXX := ccache $(CXX)
endif

.PHONY: all clean run format test metal
.DEFAULT_GOAL := all

all: format $(OUTPUT_EXE)

format:
	-@find src/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec clang-format -i {} +

$(OUTPUT_EXE): $(H_FILES) $(OBJ_FILES)
	$(CXX) $(FLAGS) $(NOFLAGS) $(INCLUDES) $(OBJ_FILES) $(LIBS) -o $(OUTPUT_EXE)

$(OBJ_DIR)/%.o: %.cpp makefile
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(NOFLAGS) $(INCLUDES) -c $< -o $@ -MMD -MF $(@:.o=.d)

clean:
	rm -rf $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/src/engine
	mkdir -p $(OBJ_DIR)/src/log
	mkdir -p $(OBJ_DIR)/src/testing

run: $(OUTPUT_EXE)
	./$(OUTPUT_EXE)

test: $(OUTPUT_EXE)
	./$(OUTPUT_EXE) --test-dir tests/e2e

count:
	git ls-files | grep "src" | grep -v "vendor" | grep -v "resources" | xargs wc -l | sort -rn

-include $(OBJ_FILES:.o=.d)

# ── Metal/Sokol backend ──────────────────────────────────────────────────────
METAL_DIR := ./output_metal
METAL_EXE := $(METAL_DIR)/dance.exe
METAL_BACKEND_DEF := -DAFTER_HOURS_USE_METAL
METAL_SOKOL_INCLUDES := -isystem vendor/afterhours/vendor/
METAL_FLAGS = -std=c++23 -Wall -Wextra -Wuninitialized -Wshadow -g \
			  $(METAL_BACKEND_DEF) $(MCP_FLAGS) -DAFTER_HOURS_ENABLE_E2E_TESTING
METAL_FRAMEWORKS = -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore
METAL_LIBS = -Lvendor/ $(METAL_FRAMEWORKS)

METAL_CPP_FILES := $(wildcard src/*.cpp src/**/*.cpp)
METAL_OBJ_FILES := $(METAL_CPP_FILES:%.cpp=$(METAL_DIR)/%.o)
METAL_SOKOL_OBJ := $(METAL_DIR)/src/sokol_impl.o

metal: $(METAL_EXE)

$(METAL_EXE): $(H_FILES) $(METAL_OBJ_FILES) $(METAL_SOKOL_OBJ)
	$(CXX) $(METAL_FLAGS) $(NOFLAGS) $(INCLUDES) $(METAL_SOKOL_INCLUDES) \
		$(METAL_OBJ_FILES) $(METAL_SOKOL_OBJ) $(METAL_LIBS) -o $(METAL_EXE)

$(METAL_DIR)/%.o: %.cpp makefile
	@mkdir -p $(dir $@)
	$(CXX) $(METAL_FLAGS) $(NOFLAGS) $(INCLUDES) $(METAL_SOKOL_INCLUDES) \
		-c $< -o $@ -MMD -MF $(@:.o=.d)

$(METAL_SOKOL_OBJ): src/sokol_impl.mm makefile
	@mkdir -p $(dir $@)
	$(CXX) -ObjC++ $(METAL_FLAGS) $(NOFLAGS) $(INCLUDES) $(METAL_SOKOL_INCLUDES) \
		-c $< -o $@ -MMD -MF $(@:.o=.d)

-include $(METAL_OBJ_FILES:.o=.d)

