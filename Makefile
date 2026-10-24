# Student Management System - Makefile
# Usage: make            (build ./build/sms)
#        make test       (build and run the unit tests)
#        make debug      (build with sanitizers: ASan + UBSan)
#        make clean
CXX      ?= g++
CXXFLAGS ?= -O2
STD       = -std=c++17
WARN      = -Wall -Wextra -Wpedantic -Werror
INC       = -Iinclude
BUILD     = build

LIB_SRC := $(shell find src -name '*.cpp' ! -name main.cpp | sort)
LIB_OBJ := $(patsubst src/%.cpp,$(BUILD)/obj/%.o,$(LIB_SRC))
MAIN_OBJ := $(BUILD)/obj/main.o
TEST_SRC := tests/test_main.cpp
TEST_OBJ := $(BUILD)/obj/tests/test_main.o

.PHONY: all test debug clean
all: $(BUILD)/sms

$(BUILD)/sms: $(LIB_OBJ) $(MAIN_OBJ)
	$(CXX) $(STD) $(CXXFLAGS) $^ -o $@

$(BUILD)/sms_tests: $(LIB_OBJ) $(TEST_OBJ)
	$(CXX) $(STD) $(CXXFLAGS) $^ -o $@

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(CXXFLAGS) $(WARN) $(INC) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(CXXFLAGS) $(WARN) $(INC) -MMD -MP -c $< -o $@

test: $(BUILD)/sms_tests
	./$(BUILD)/sms_tests

debug:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" all test

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
