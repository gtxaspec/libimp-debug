CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Os
LDFLAGS ?= -lrt -lpthread

libimp-debug: libimp-debug.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f libimp-debug

.PHONY: clean
