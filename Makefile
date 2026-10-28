# Plain-make build. CMake is the primary build (CMakeLists.txt), but this exists
# because installed packages and build directories do not survive a sandbox
# snapshot: after a restore, `make` with only g++ and pkg-config must still work.
#
#   make            build/vlcflix
#   make test       build and run the unit tests (unit, golden image, fault layers)
#   make verify     everything: build, all tests, and the end-to-end journey
#   make asan       the evidence layer: same tests under AddressSanitizer
#   make preflight  static checks a compiler here cannot make (Windows identity, rules)
#   make scan DIR=… run a library scan through the CLI
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter
PKG_CFLAGS := $(shell pkg-config --cflags sdl2 freetype2 libpng libjpeg libvlc 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs sdl2 freetype2 libpng libjpeg libvlc 2>/dev/null)
CPPFLAGS += -I src -I . -MMD -MP $(PKG_CFLAGS)
LDFLAGS  += -pthread
LDLIBS   += $(PKG_LIBS)

BUILD    := build
APP      := $(BUILD)/vlcflix
TESTBIN  := $(BUILD)/native-tests

LIBSRC   := $(shell find src/core src/library src/platform src/playback src/render src/ui -name '*.cpp' 2>/dev/null)
APPSRC   := src/main.cpp
TESTSRC  := tests/run_tests.cpp tests/ui_tests.cpp tests/player_tests.cpp tests/golden_tests.cpp tests/fault_tests.cpp

LIBOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(LIBSRC))
APPOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(APPSRC))
TESTOBJ  := $(patsubst %.cpp,$(BUILD)/%.o,$(TESTSRC))

# Header dependencies. Without these, editing a header relinked objects compiled
# against the previous layout: that produced a crash inside a destructor, and it is
# the kind of bug that looks like a compiler fault.
DEPS     := $(LIBOBJ:.o=.d) $(APPOBJ:.o=.d) $(TESTOBJ:.o=.d)

.PHONY: all test verify asan preflight clean scan doctor

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

# The one gate. A fresh clone with nothing but a compiler, pkg-config, ffmpeg and
# libVLC has to pass this: unit + golden-image + fault tests, then the journey that
# drives the real binary through a library it has never seen.
verify: preflight $(APP) $(TESTBIN)
	@printf '\n=== layers 1-3: unit, golden image, fault injection ===\n'
	./$(TESTBIN)
	@printf '\n=== layer 4: end-to-end journey (real binary, synthetic library) ===\n'
	./tests/journey.sh
	@printf '\n=== verify: all layers passed ===\n'
	@./$(APP) version >/dev/null && ./$(APP) doctor | sed -n '1,12p'

scan: $(APP)
	./$(APP) scan --folders "$(DIR)" --data "$(DATA)"

doctor: $(APP)
	./$(APP) doctor

# AddressSanitizer. This is not a duplicate of `make test`: it is the layer that turns
# a bug which only *sometimes* misbehaves into a deterministic failure with a file name
# and a line, which is how this turn's lost-preview bug was proved and then proved
# fixed. It builds in one command on purpose - separate objects would need a second
# build directory, and a stale half-ASan binary is worse than a slower build.
ASAN_BIN := $(BUILD)/native-tests-asan
ASANFLAGS := -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer

asan: $(LIBSRC) $(TESTSRC)
	@mkdir -p $(BUILD)
	$(CXX) $(ASANFLAGS) $(CPPFLAGS) -o $(ASAN_BIN) $(LIBSRC) $(TESTSRC) $(LDFLAGS) $(LDLIBS)
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 ./$(ASAN_BIN)

preflight:
	tools/preflight.sh

clean:
	rm -rf $(BUILD)

-include $(DEPS)
