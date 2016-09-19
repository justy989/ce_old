CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3
LINK=-lncurses

ce: main.c ce.o ce_config.so
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config
	rm -f ce messages.txt ce.o valgrind_results.txt

clean_config:
	rm -f ce_config.o ce_config.so ce.o
