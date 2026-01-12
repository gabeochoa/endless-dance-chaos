
RAYLIB_FLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIB := $(shell pkg-config --libs raylib 2>/dev/null)

# MCP support - run with: make MCP=1
ifdef MCP
	MCP_FLAGS = -DAFTER_HOURS_ENABLE_MCP
else
	MCP_FLAGS =
endif

FLAGS = -std=c++23 -Wall -Wextra -Wuninitialized -Wshadow -g $(RAYLIB_FLAGS) $(MCP_FLAGS)
NOFLAGS = -Wno-deprecated-volatile -Wno-missing-field-initializers \
		  -Wno-c99-extensions -Wno-unused-function -Wno-sign-conversion \
		  -Wno-implicit-int-float-conversion

INCLUDES = -Ivendor/ -Isrc/
LIBS = -Lvendor/ $(RAYLIB_LIB)

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

.PHONY: all clean run format

format:
	@find src/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec clang-format -i {} +

all: format $(OUTPUT_EXE)

$(OUTPUT_EXE): $(H_FILES) $(OBJ_FILES)
	$(CXX) $(FLAGS) $(NOFLAGS) $(INCLUDES) $(OBJ_FILES) $(LIBS) -o $(OUTPUT_EXE)

$(OBJ_DIR)/%.o: %.cpp makefile
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(NOFLAGS) $(INCLUDES) -c $< -o $@ -MMD -MF $(@:.o=.d)

clean:
	rm -rf $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/src/engine
	mkdir -p $(OBJ_DIR)/src/log

run: $(OUTPUT_EXE)
	./$(OUTPUT_EXE)

count:
	git ls-files | grep "src" | grep -v "vendor" | grep -v "resources" | xargs wc -l | sort -rn

-include $(OBJ_FILES:.o=.d)

