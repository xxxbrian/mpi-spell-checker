build:
    @echo "Building..."
    @mkdir -p build && cd build && cmake .. && cmake --build .

clean:
    @echo "Cleaning..."
    @rm -rf build
    @rm -rf .cache

run *args='': build
    @echo "Running..."
    ./build/spellcheck {{args}}

run-local:
    @just run files/dictionary.spec.txt files/words.spec.txt

test: build
    @echo "Testing..."
    ./build/test
