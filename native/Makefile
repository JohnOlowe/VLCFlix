# Plain-make build. CMake is the primary build (CMakeLists.txt), but this exists
# because installed packages and build directories do not survive a sandbox
# snapshot: after a restore, `make` with only g++ and pkg-config must still work.
#
#   make            build/vlcflix
#   make test       build and run the unit tests
#   make scan DIR=… run a library scan through the CLI
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter
PKG_CFLAGS := $(shell pkg-config --cflags sdl2 freetype2 libpng libjpeg 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs sdl2 freetype2 libpng libjpeg 2>/dev/null)
CPPFLAGS += -I src -I . -MMD -MP $(PKG_CFLAGS)
LDFLAGS  += -pthread
LDLIBS   += $(PKG_LIBS)

BUILD    := build
APP      := $(BUILD)/vlcflix
TESTBIN  := $(BUILD)/native-tests

LIBSRC   := $(shell find src/core src/library src/platform src/render src/ui -name '*.cpp' 2>/dev/null)
APPSRC   := src/main.cpp
TESTSRC  := tests/run_tests.cpp tests/ui_tests.cpp

LIBOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(LIBSRC))
APPOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(APPSRC))
TESTOBJ  := $(patsubst %.cpp,$(BUILD)/%.o,$(TESTSRC))

# Header dependencies. Without these, editing a header relinked objects compiled
# against the previous layout: that produced a crash inside a destructor, and it is
# the kind of bug that looks like a compiler fault.
DEPS     := $(LIBOBJ:.o=.d) $(APPOBJ:.o=.d) $(TESTOBJ:.o=.d)

.PHONY: all test clean scan doctor

all: $(APP)

$(APP): $(LIBOBJ) $(APPOBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TESTBIN): $(LIBOBJ) $(TESTOBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

test: $(TESTBIN)
	./$(TESTBIN)

scan: $(APP)
	./$(APP) scan --folders "$(DIR)" --data "$(DATA)"

doctor: $(APP)
	./$(APP) doctor

clean:
	rm -rf $(BUILD)

-include $(DEPS)
