CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g
CPPFLAGS ?=
PREFIX ?= /usr/local
BUILD_DIR ?= build/make

WARNINGS = -Wall -Wextra -Wpedantic -Wconversion
INCLUDES = -Iinclude
THREADS = -pthread

.PHONY: all test example clean install

all: $(BUILD_DIR)/libopenftds.a $(BUILD_DIR)/libftdsclient.a $(BUILD_DIR)/openftds-report

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/openftds.o: src/openftds.c include/openftds/openftds.h include/openftds/ftds_compat.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(INCLUDES) $(THREADS) -c $< -o $@

$(BUILD_DIR)/libopenftds.a: $(BUILD_DIR)/openftds.o
	$(AR) rcs $@ $<

$(BUILD_DIR)/libftdsclient.a: $(BUILD_DIR)/libopenftds.a
	cp $< $@

$(BUILD_DIR)/openftds-report: tools/openftds-report | $(BUILD_DIR)
	cp $< $@
	chmod +x $@

$(BUILD_DIR)/openftds-test: tests/test_openftds.c $(BUILD_DIR)/libopenftds.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(INCLUDES) $(THREADS) $< $(BUILD_DIR)/libopenftds.a -o $@

$(BUILD_DIR)/openftds-cpp20-test: tests/test_cpp.cpp $(BUILD_DIR)/libopenftds.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNINGS) -std=c++20 $(INCLUDES) $(THREADS) $< $(BUILD_DIR)/libopenftds.a -o $@

$(BUILD_DIR)/openftds-example: examples/basic.c $(BUILD_DIR)/libopenftds.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(INCLUDES) $(THREADS) $< $(BUILD_DIR)/libopenftds.a -o $@

$(BUILD_DIR)/openftds-ftds-compatible: examples/ftds_compatible.c $(BUILD_DIR)/libftdsclient.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(INCLUDES) $(THREADS) $< $(BUILD_DIR)/libftdsclient.a -o $@

test: $(BUILD_DIR)/openftds-test $(BUILD_DIR)/openftds-cpp20-test $(BUILD_DIR)/openftds-report
	$(BUILD_DIR)/openftds-test
	$(BUILD_DIR)/openftds-cpp20-test
	python3 tests/test_report.py $(BUILD_DIR)/openftds-report
	python3 tests/test_yrcache_build.py

example: $(BUILD_DIR)/openftds-example $(BUILD_DIR)/openftds-ftds-compatible
	$(BUILD_DIR)/openftds-example
	OPENFTDS_DIR=./demo-output-compat $(BUILD_DIR)/openftds-ftds-compatible

install: all
	install -d $(DESTDIR)$(PREFIX)/include/openftds $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin
	install -m 0644 include/openftds/*.h include/openftds/*.hpp $(DESTDIR)$(PREFIX)/include/openftds/
	install -m 0644 include/ftds.h $(DESTDIR)$(PREFIX)/include/
	install -m 0644 $(BUILD_DIR)/libopenftds.a $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 $(BUILD_DIR)/libftdsclient.a $(DESTDIR)$(PREFIX)/lib/
	install -m 0755 $(BUILD_DIR)/openftds-report $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -rf $(BUILD_DIR)
