BUILD_TYPE ?= Debug
BUILD_ROOT ?= build
BUILD_DIR ?= $(BUILD_ROOT)/$(BUILD_TYPE)
CMAKE_FLAGS ?=
BUILD_TARGET ?=
TEST_TARGET ?= kubeforward_tests

ifeq ($(strip $(VCPKG_ROOT)),)
$(error VCPKG_ROOT is not set. Export it (e.g. VCPKG_ROOT=/path/to/vcpkg) before running make)
endif

.PHONY: build configure clean test

configure:
	cmake -S . -B "$(BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_TOOLCHAIN_FILE="$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake" \
		$(CMAKE_FLAGS)

build: configure
ifeq ($(strip $(BUILD_TARGET)),)
	cmake --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)"
else
	cmake --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --target "$(BUILD_TARGET)"
endif

test: configure
	cmake --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --target "$(TEST_TARGET)"
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

clean:
	rm -rf "$(BUILD_ROOT)" build-* cmake-build-* out dist
