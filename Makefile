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
CPPFLAGS += -I src -I .
LDFLAGS  += -pthread
LDLIBS   +=

BUILD    := build
APP      := $(BUILD)/vlcflix
TESTBIN  := $(BUILD)/native-tests

LIBSRC   := $(shell find src/core src/library src/platform -name '*.cpp' 2>/dev/null)
APPSRC   := src/main.cpp
TESTSRC  := tests/run_tests.cpp

LIBOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(LIBSRC))
APPOBJ   := $(patsubst %.cpp,$(BUILD)/%.o,$(APPSRC))
TESTOBJ  := $(patsubst %.cpp,$(BUILD)/%.o,$(TESTSRC))

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
