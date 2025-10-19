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

fmt:
    @echo "Formatting..."
    @find include src -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" -o -name "*.c" \) \
        -print -exec clang-format -i {} \;
    @if [ -f spellcheck.cc ]; then \
        clang-format -i spellcheck.cc; \
    fi

fmt-check:
    @echo "Formatting (dry run)..."
    @find include src -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" -o -name "*.c" \) \
        -print -exec clang-format --dry-run --Werror {} \;
    @if [ -f spellcheck.cc ]; then \
        clang-format --dry-run --Werror spellcheck.cc; \
    fi
