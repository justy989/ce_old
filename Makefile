CC?=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE
LINK=-lncurses

all: ce ce_config.so

cov: coverage
coverage: CFLAGS += -fprofile-arcs -ftest-coverage
coverage: clean_test test
	llvm-cov gcov ce.test.o

test: clean_test test.c ce.test.o
	$(CC) $(CFLAGS) $(filter-out $<,$^) -o $@ $(LINK)
	./test 2> test_output.txt || (cat test_output.txt && false)

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

ce%o: ce.c
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
	rm -f test ce.test.o *.gcda *.gcno *.gcov test_output.txt default.profraw
