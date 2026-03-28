CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Os
LDFLAGS ?= -lrt -lpthread

all: libimp-debug libimp-nodbg.so

libimp-debug: libimp-debug.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

libimp-nodbg.so: libimp-nodbg.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $<

clean:
	rm -f libimp-debug libimp-nodbg.so

.PHONY: all clean
