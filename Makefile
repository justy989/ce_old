CC=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE $(SCROLL_FLAG)
LINK=-lncurses -lutil -lm

all: LINK += -lpthread
all: ce ce_config.so

release: CFLAGS += -DNDEBUG -O3
release: all

cov: coverage
coverage: CFLAGS += -fprofile-arcs -ftest-coverage
coverage: clean_test test
	llvm-cov gcov ce.test.o
	llvm-cov gcov vim.test.o

test: LINK += -rdynamic
test: clean_test test_ce test_vim

test_ce: test_ce.c ce.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	./$@ 2> test_ce_output.txt || (cat test_ce_output.txt && false)

test_vim: test_vim.c ce.test.o vim.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	./$@ 2> test_vim_output.txt || (cat test_vim_output.txt && false)

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

%.o: %.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

%.test.o: %.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o vim.o terminal.o syntax.o text_history.o auto_complete.o tab_view.o jump.o view.o buffer.o input.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config clean_test
	rm -rf ce *.o valgrind_results.txt *.dSYM

clean_config:
	rm -f ce_config.so

clean_test:
	rm -f test_ce test_vim ce.test.o ce_vim.test.o *.gcda *.gcno *.gcov test_ce_output.txt test_vim_output.txt default.profraw
