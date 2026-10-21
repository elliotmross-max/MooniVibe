# Makefile -- build and run the MooniVibe contract test on the HOST.
# Needs only a C compiler and libm. No Docker, no Move.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -I src
LDLIBS  ?= -lm

TESTBIN := build/moonivibe_test
SRCS    := src/moonivibe.c src/moonivibe_test.c

.PHONY: all test clean

all: test

$(TESTBIN): $(SRCS) src/moonivibe.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

test: $(TESTBIN)
	@./$(TESTBIN)

clean:
	rm -rf build
