# Convert from Justfile by genAI.
SHELL := /bin/bash

# --- Config ---------------------------------------------------------
BUILD_DIR ?= build
BINARY    ?= $(BUILD_DIR)/spellcheck
TEST_BIN  ?= $(BUILD_DIR)/test

CMAKE_ARGS ?=

ARGS ?=

DICT ?= files/dictionary.spec.txt
WORDS ?= files/words.spec.txt

SRC_DIRS := include src

# --- Phony targets --------------------------------------------------
.PHONY: all build clean run run-local test fmt fmt-check

all: build

build:
	@echo "Building..."
	@mkdir -p "$(BUILD_DIR)"
	@cd "$(BUILD_DIR)" && cmake $(CMAKE_ARGS) .. && cmake --build .

clean:
	@echo "Cleaning..."
	@rm -rf "$(BUILD_DIR)"
	@rm -rf .cache

run: build
	@echo "Running..."
	@$(BINARY) $(ARGS)

run-local: build
	@$(MAKE) run ARGS="$(DICT) $(WORDS)"

test: build
	@echo "Testing..."
	@$(TEST_BIN)

fmt:
	@echo "Formatting..."
	@find $(SRC_DIRS) -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" -o -name "*.c" \) -exec clang-format -i {} +
	@if [ -f spellcheck.cc ]; then clang-format -i spellcheck.cc; fi

fmt-check:
	@echo "Formatting (dry run)..."
	@find $(SRC_DIRS) -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" -o -name "*.c" \) -exec clang-format --dry-run --Werror {} +
	@if [ -f spellcheck.cc ]; then clang-format --dry-run --Werror spellcheck.cc; fi
