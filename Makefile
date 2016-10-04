CC=clang
CFLAGS=-Wall -Werror -Wextra -std=gnu11 -ggdb3 -D_GNU_SOURCE
CFLAGS_TEST=-fprofile-arcs -ftest-coverage
LINK=-lncurses

all: ce ce_config.so

coverage: test test.gcno ce.coverage.o ce.coverage.gcno
	llvm-cov gcov ce.coverage.o

test test.gcno: ce.coverage.gcno ce.coverage.o test.c
	$(CC) $(CFLAGS_TEST) $(CFLAGS) $(filter-out $<,$^) -o $@ $(LINK)
	rm -f *.gcda # remove runtime statistics
	./test 2> test_output.txt

ce.coverage.o ce.coverage.gcno: ce.c
	$(CC) -c -fpic $(CFLAGS_TEST) $(CFLAGS) $^ -o $@

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config clean_test
	rm -rf ce messages.txt ce.o valgrind_results.txt *.dSYM

clean_config:
	rm -f ce_config.o ce_config.so ce.o

clean_test:
	rm -f test ce.coverage.o *.gcda *.gcno *.gcov test_output.txt
