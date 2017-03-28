.PHONY: release clean

release:
	mkdir -p build
	cd build && \
		cmake -DCMAKE_BUILD_TYPE=Release ..
	cmake --build build --config Release --target ServerMonitor

debug:
	mkdir -p build_debug
	cd build_debug && \
		cmake -DCMAKE_BUILD_TYPE=Debug ..
	cmake --build build_debug --config Debug --target ServerMonitor

clean:
	rm -rf build
