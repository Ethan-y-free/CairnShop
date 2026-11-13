.PHONY: build test migrate seed run clean

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build -j

test: build
	ctest --test-dir build --output-on-failure

migrate:
	./build/backend/cairnshop-backend migrate

seed:
	./build/backend/cairnshop-backend seed

run:
	./build/backend/cairnshop-backend serve

clean:
	rm -rf build
