CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

BIN = log-analyzer
TEST_BIN = test_log_analyzer

all: $(BIN)

$(BIN): src/main.cpp src/log_analyzer.cpp src/log_analyzer.h
	$(CXX) $(CXXFLAGS) src/main.cpp src/log_analyzer.cpp -o $(BIN)

$(TEST_BIN): tests/test_log_analyzer.cpp src/log_analyzer.cpp src/log_analyzer.h
	$(CXX) $(CXXFLAGS) tests/test_log_analyzer.cpp src/log_analyzer.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(BIN) $(TEST_BIN)

.PHONY: all test clean
