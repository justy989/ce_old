CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=c11 -g
LINK=-lncurses

all: ce ce_config.so

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl
	echo "testing" > one_line_file.txt
	touch empty_file.txt

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -f ce empty_file.txt one_line_file.txt test_file.txt messages.txt ce_config.o ce_config.so ce.o
