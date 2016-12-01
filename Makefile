.PHONY: release clean

release:
	mkdir -p build
	cd build && \
		cmake -DCMAKE_BUILD_TYPE=Release ..
	cmake --build build --config Release --target ServerMonitor

clean:
	rm -rf build
