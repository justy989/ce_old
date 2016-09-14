CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=c11 -ggdb3
LINK=-lncurses

all: ce ce_config.so

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath=.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -f ce empty_file.txt one_line_file.txt test_file.txt messages.txt ce_config.o ce_config.so ce.o
