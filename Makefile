CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3
LINK=-lncurses

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath=.

j: CONFIG_SRC=j_ce_config.c
j: ce_config.so
b: CONFIG_SRC=b_ce_config.c
b: ce_config.so

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -f ce messages.txt ce_config.o ce_config.so ce.o
