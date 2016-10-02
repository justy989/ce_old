CC=clang
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3 -D_GNU_SOURCE
CFLAGS_TEST=-fprofile-arcs -ftest-coverage
LINK=-lncurses

all: ce ce_config.so

testrun: test
	./test
	llvm-cov gcov ce.gcov.o
	grep "#####" ce.c.gcov

test: test.c ce.gcov.o
	$(CC) $(CFLAGS_TEST) $(CFLAGS) $^ -o $@ $(LINK)

ce.gcov.o: ce.c
	$(CC) -c -fpic $(CFLAGS_TEST) $(CFLAGS) $^ -o $@

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config
	rm -rf ce messages.txt ce.o valgrind_results.txt test test.gcda test.gcno \
	ce.gcov.* ce.c.gcov ce.gcno ce.dSYM test.dSYM

clean_config:
	rm -f ce_config.o ce_config.so ce.o
