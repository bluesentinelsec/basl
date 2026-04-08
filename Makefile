BUILD_DIR ?= build
BASELINE_ROOT ?= ../vigil-main
COMPLEXITY_BUILD_DIR ?= build-complexity

.PHONY: all build test clean format configure-dev configure-release build-dev build-release full perf coverage complexity

all: build

configure-dev:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON -DVIGIL_PLUGIN_TILED=ON

configure-release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DVIGIL_BUILD_TESTS=ON -DVIGIL_PLUGIN_TILED=ON

build: configure-dev
	cmake --build $(BUILD_DIR)

build-dev: configure-dev
	cmake --build $(BUILD_DIR)

build-release: configure-release
	cmake --build $(BUILD_DIR)

full:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DVIGIL_BUILD_TESTS=ON \
		-DVIGIL_PLUGIN_SDL=ON \
		-DVIGIL_PLUGIN_GUI=ON \
		-DVIGIL_PLUGIN_AUDIO=ON \
		-DVIGIL_PLUGIN_SYSQUERY=ON \
		-DVIGIL_PLUGIN_TILED=ON
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

perf: build-release
	python3 scripts/run_benchmarks.py \
		--vigil-bin $(BUILD_DIR)/vigil \
		--manifest benchmarks/manifest.json \
		--output $(BUILD_DIR)/benchmarks.json

coverage:
	cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON -DVIGIL_ENABLE_COVERAGE=ON
	cmake --build build-coverage
	ctest --test-dir build-coverage --output-on-failure
	python3 scripts/check_test_surface.py
	scripts/generate_coverage_reports.sh build-coverage coverage-artifacts
	python3 scripts/check_coverage.py \
		--summary coverage-artifacts/coverage-summary.json \
		--details coverage-artifacts/coverage.json \
		--thresholds coverage/thresholds.json \
		--base-ref origin/main

complexity:
	cmake -S . -B $(COMPLEXITY_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake -S $(BASELINE_ROOT) -B $(BASELINE_ROOT)/build-complexity -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	python3 scripts/check_complexity.py \
		--candidate-root . \
		--baseline-root $(BASELINE_ROOT) \
		--thresholds complexity/thresholds.json \
		--summary complexity-artifacts/lizard-summary.json \
		--candidate-report complexity-artifacts/lizard-pr.csv \
		--baseline-report complexity-artifacts/lizard-main.csv
	CLANG_TIDY_CONFIG_FILE=$(CURDIR)/.clang-tidy scripts/run_clang_tidy.sh . $(COMPLEXITY_BUILD_DIR) complexity-artifacts/clang-tidy-pr.log
	CLANG_TIDY_CONFIG_FILE=$(CURDIR)/.clang-tidy scripts/run_clang_tidy.sh $(BASELINE_ROOT) $(BASELINE_ROOT)/build-complexity complexity-artifacts/clang-tidy-main.log
	python3 scripts/check_clang_tidy.py \
		--baseline-log complexity-artifacts/clang-tidy-main.log \
		--candidate-log complexity-artifacts/clang-tidy-pr.log \
		--summary complexity-artifacts/clang-tidy-summary.json

format:
	scripts/run_clang_format.sh --in-place

clean:
	cmake -E rm -rf $(BUILD_DIR)
