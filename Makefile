CC=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE $(SCROLL_FLAG)
LINK=-lncurses -lutil -lm
OBJS=ce_config.o vim.o terminal.o syntax.o text_history.o auto_complete.o tab_view.o jump.o view.o buffer.o input.o \
     destination.o completion.o command.o info.o terminal_helper.o misc.o

TESTS=test_ce test_vim test_auto_complete test_buffer test_command test_completion test_info test_input

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
test: clean_test $(TESTS)

test_ce: test_ce.c ce.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	./$@ 2> test_output.txt || (cat test_output.txt && false)

test_vim: test_vim.c ce.test.o vim.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_auto_complete: test_auto_complete.c auto_complete.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_buffer: test_buffer.c buffer.test.o ce.test.o syntax.test.o vim.test.o terminal.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -lpthread
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_command: test_command.c command.test.o syntax.test.o view.test.o info.test.o ce.test.o buffer.test.o vim.test.o misc.test.o destination.test.o jump.test.o terminal.test.o terminal_helper.test.o input.test.o text_history.test.o ce_config.test.o auto_complete.test.o completion.test.o tab_view.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -lpthread
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_completion: test_completion.c command.test.o syntax.test.o view.test.o info.test.o ce.test.o buffer.test.o vim.test.o misc.test.o destination.test.o jump.test.o terminal.test.o terminal_helper.test.o input.test.o text_history.test.o ce_config.test.o auto_complete.test.o completion.test.o tab_view.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -lpthread
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_info: test_info.c info.test.o vim.test.o ce.test.o misc.test.o input.test.o text_history.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -lpthread
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

test_input: test_input.c input.test.o ce.test.o vim.test.o text_history.test.o buffer.test.o syntax.test.o terminal.test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -lpthread
	./$@ 2>> test_output.txt || (cat test_output.txt && false)

ce: main.c ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

%.o: %.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

%.test.o: %.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce.o $(OBJS)
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config clean_test
	rm -rf ce *.o valgrind_results.txt *.dSYM

clean_config:
	rm -f ce_config.so

clean_test:
	rm -f $(TESTS) *.test.o *.gcda *.gcno *.gcov test_output.txt default.profraw
