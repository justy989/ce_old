CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3
LINK=-lncurses
CONFIG_SRC=ce_config.c

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath=.

j: CONFIG_SRC=j_ce_config.c
j: clean_config ce_config.so
b: CONFIG_SRC=b_ce_config.c
b: clean_config ce_config.so

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o:
	$(CC) -c -fpic $(CFLAGS) $(CONFIG_SRC) -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config
	rm -f ce messages.txt ce.o

clean_config:
	rm -f ce_config.o ce_config.so ce.o
