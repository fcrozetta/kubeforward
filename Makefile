.PHONY: build

build:
	cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake"
	cmake --build build
