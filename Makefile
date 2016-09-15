CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3
LINK=-lncurses
# export CONFIG_SRC=/path/to/config.c in your environment to build your own ce_config.so
CONFIG_SRC?=ce_config.c

all: ce ce_config.so

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath=.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: $(CONFIG_SRC)
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -f ce messages.txt ce_config.o ce_config.so ce.o
