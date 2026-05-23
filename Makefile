CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LIBS     = -lncursesw

# Scheduler binary (interactive UI)
SCHEDULER_SRC = src/main.cpp
SCHEDULER_BIN = scheduler

# Benchmark binary (headless, no ncurses)
BENCH_SRC = src/benchmark.cpp
BENCH_BIN = benchmark

# Build both by default
all: $(SCHEDULER_BIN) $(BENCH_BIN)

$(SCHEDULER_BIN): $(SCHEDULER_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

$(BENCH_BIN): $(BENCH_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

run: $(SCHEDULER_BIN)
	./$(SCHEDULER_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

clean:
	rm -f $(SCHEDULER_BIN) $(BENCH_BIN)