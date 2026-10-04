# Host unit tests for the hardware-free scan logic (flipper/rf_scan.c).
# Usage: make -C flipper/tests        (needs any C11 compiler + libm)
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test clean

test: test_rf_scan
	./test_rf_scan

test_rf_scan: test_rf_scan.c ../rf_scan.c ../rf_scan.h
	$(CC) $(CFLAGS) $(SAN) -I.. ../rf_scan.c test_rf_scan.c -lm -o $@

clean:
	rm -f test_rf_scan
