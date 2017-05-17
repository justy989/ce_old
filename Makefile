CC=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE
LINK=-lncurses -lutil -lm -lpthread
SRCS=$(filter-out source/main.c,$(wildcard source/*.c))
OBJS=$(subst source,build,$(SRCS:.c=.o))
TEST_OBJS=$(OBJS:.o=.test.o)

all: build/ce build/ce_config.so

release: CFLAGS+=-DNDEBUG -O3
release: all

cov: coverage
coverage: CFLAGS+=-fprofile-arcs -ftest-coverage
coverage: clean test $(TEST_OBJS)
	mv *.gcno build/.
	mv *.gcda build/.
	llvm-cov gcov $(TEST_OBJS)
	mv *.gcov build/.

test: LINK+=-rdynamic
test: CFLAGS+=-Itest -Isource
test: $(subst test/,build/,$(basename $(wildcard test/*.c)))

build/test_%: test/test_%.c build/libcetest.a
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	$@ 2> build/test_output.txt || (cat build/test_output.txt && false)

build/libcetest.a: $(TEST_OBJS)
	ar cr $@ $^

build/ce: source/main.c build/ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

build/%.o: source/%.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

build/%.test.o: source/%.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

build/ce_config.so: build/ce.o $(OBJS)
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -f build/*
