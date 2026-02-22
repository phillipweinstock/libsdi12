# libsdi12 test suite — platform-agnostic Makefile
#
# Build and run with any C compiler:
#   make            # compile + run
#   make test       # same
#   make CC=clang   # use clang
#   make clean
#
# Works on Linux, macOS, Windows (MinGW/MSYS2), WSL, and CI.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O1
CFLAGS  += -I..

# Source files
TEST_SRCS = test_main.c test_crc.c test_address.c test_sensor.c \
            test_master.c test_metamorphic.c
LIB_SRCS  = ../sdi12_crc.c ../sdi12_sensor.c ../sdi12_master.c

# Output binary
ifeq ($(OS),Windows_NT)
  BIN = test_sdi12.exe
else
  BIN = test_sdi12
endif

all: test

$(BIN): $(TEST_SRCS) $(LIB_SRCS) sdi12_test.h ../sdi12.h ../sdi12_sensor.h ../sdi12_master.h
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) -lm

test: $(BIN)
	./$(BIN)

clean:
	$(RM) $(BIN)

.PHONY: all test clean
