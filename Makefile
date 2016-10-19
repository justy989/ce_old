CC?=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE $(SCROLL_FLAG)
LINK=-lncurses

all: LINK += -lpthread
all: ce ce_config.so

release: CFLAGS += -DNDEBUG -O3 -Wno-unused
release: all

cov: coverage
coverage: CFLAGS += -fprofile-arcs -ftest-coverage
coverage: clean_test test
	llvm-cov gcov ce.test.o

test: LINK += -rdynamic
test: clean_test test.c ce.test.o
	$(CC) $(CFLAGS) $(filter-out $<,$^) -o $@ $(LINK)
	./test 2> test_output.txt || (cat test_output.txt && false)

ce: main.c ce.o ce_network.o ce_server.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

ce.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce.test.o: ce.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_network.o: ce_network.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_server.o: ce_server.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_client.o: ce_client.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.o: ce_config.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

ce_config.so: ce_config.o ce.o ce_network.o ce_server.o ce_client.o
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean: clean_config clean_test
	rm -rf ce messages buffers shell_output ce.o valgrind_results.txt *.dSYM

clean_config:
	rm -f ce_config.o ce_config.so ce.o ce_network.o ce_server.o ce_client.o

clean_test:
	rm -f test ce.test.o *.gcda *.gcno *.gcov test_output.txt default.profraw
