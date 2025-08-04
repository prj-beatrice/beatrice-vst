all: release

# GitHub Actions の windows-latest 環境でビルドされたものであり、
# そこで使われているのと同じバージョンの Microsoft STL に依存している
lib/beatricelib/beatrice.lib:
	curl -fLo lib/beatricelib/beatrice.lib https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.0/rc.0/beatrice.lib

build/vst: CMakeLists.txt
	mkdir build
	cd build \
	&& cmake .. -G "Visual Studio 17 2022" -A x64 -B vst -DSMTG_USE_STATIC_CRT=ON

debug: build/vst lib/beatricelib/beatrice.lib $(wildcard src/*/*)
	cd build \
	&& cmake --build vst

release: build/vst lib/beatricelib/beatrice.lib $(wildcard src/*/*)
	cd build \
	&& cmake --build vst --config Release

distribution: build/vst lib/beatricelib/beatrice.lib
	cd build \
	&& cmake --build vst --config Release --target distribution

cpplint:
	cpplint --filter=-runtime/references,-build/header_guard,-readability/nolint --recursive src

clean:
	rm -rf build

.PHONY: all debug release distribution cpplint clean
