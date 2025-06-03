CC := cc
SOURCES := $(wildcard *.c)
WARN := -Wall -Wextra -Wpedantic
CFLAGS := -O3

build:
	$(CC) -o wtf $(SOURCES) $(WARN) $(CFLAGS)

install: build
	install -m 0755 wtf /usr/local/bin/wtf

clean:
	rm wtf

.PHONY: build install clean
