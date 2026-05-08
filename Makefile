BEATRICE_DEV_VERSION ?= ON
CMAKE_GENERATOR ?= Visual Studio 17 2022

all: release

# GitHub Actions の windows-latest 環境でビルドされたものであり、
# そこで使われているのと同じバージョンの Microsoft STL に依存している
lib/beatricelib/beatrice.lib:
	curl -fLo lib/beatricelib/beatrice.lib https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.0/rc.0/beatrice.lib

configure: CMakeLists.txt lib/beatricelib/beatrice.lib
	cmake . -G "$(CMAKE_GENERATOR)" -A x64 -B build/vst -DSMTG_USE_STATIC_CRT=ON -DBEATRICE_DEV_VERSION=$(BEATRICE_DEV_VERSION)

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
