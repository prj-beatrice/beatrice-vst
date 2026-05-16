BEATRICE_DEV_VERSION ?= ON

ifeq ($(OS),Windows_NT)
CMAKE_GENERATOR ?= Visual Studio 17 2022
BEATRICE_LIBRARY := lib/beatricelib/beatrice.lib
CMAKE_CONFIGURE_ARGS := -A x64 -DSMTG_USE_STATIC_CRT=ON
else
CMAKE_GENERATOR ?= Xcode
BEATRICE_LIBRARY := lib/beatricelib/libbeatrice.a
CMAKE_CONFIGURE_ARGS := -DCMAKE_OSX_ARCHITECTURES=arm64 -DSMTG_BUILD_UNIVERSAL_BINARY=OFF
endif

ifneq ($(BEATRICE_PROCESSOR_UID),)
CMAKE_CONFIGURE_ARGS += -DBEATRICE_PROCESSOR_UID="$(BEATRICE_PROCESSOR_UID)"
endif
ifneq ($(BEATRICE_CONTROLLER_UID),)
CMAKE_CONFIGURE_ARGS += -DBEATRICE_CONTROLLER_UID="$(BEATRICE_CONTROLLER_UID)"
endif

all: release

# GitHub Actions の windows-latest 環境でビルドされたものであり、
# そこで使われているのと同じバージョンの Microsoft STL に依存している
lib/beatricelib/beatrice.lib:
	curl -fLo lib/beatricelib/beatrice.lib https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.2/rc.0/beatrice.lib

lib/beatricelib/libbeatrice.a:
	curl -fLo lib/beatricelib/libbeatrice.a https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.2/rc.0/libbeatrice.a

configure: CMakeLists.txt $(BEATRICE_LIBRARY)
	cmake . -G "$(CMAKE_GENERATOR)" -B build/vst -DBEATRICE_DEV_VERSION=$(BEATRICE_DEV_VERSION) $(CMAKE_CONFIGURE_ARGS)

debug: configure $(wildcard src/*/*)
	cmake --build build/vst --config Debug

release: configure $(wildcard src/*/*)
	cmake --build build/vst --config Release

distribution: configure
	cmake --build build/vst --config Release --target distribution

cpplint:
	cpplint --filter=-runtime/references,-build/header_guard,-readability/nolint --recursive src

clean:
	cmake -E rm -rf build

.PHONY: all configure debug release distribution cpplint clean
