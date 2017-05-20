CC=clang
CFLAGS+=-Wall -Werror -Wextra -std=c11 -ggdb3 -D_GNU_SOURCE
LINK=-lncurses -lutil -lm -lpthread
SRCS=$(filter-out source/main.c,$(wildcard source/*.c))
OBJS=$(subst source,build,$(SRCS:.c=.o))
TEST_OBJS=$(OBJS:.o=.test.o)
BUILD_DIR=build

all: $(BUILD_DIR) $(BUILD_DIR)/ce $(BUILD_DIR)/ce_config.so

$(BUILD_DIR):
	mkdir build

release: CFLAGS+=-DNDEBUG -O3
release: all

cov: coverage
coverage: CFLAGS+=-fprofile-arcs -ftest-coverage
coverage: clean $(BUILD_DIR) test $(TEST_OBJS)
	mv *.gcno $(BUILD_DIR)/.
	mv *.gcda $(BUILD_DIR)/.
	llvm-cov gcov $(TEST_OBJS)
	mv *.gcov $(BUILD_DIR)/.

test: LINK+=-rdynamic
test: CFLAGS+=-Itest -Isource
test: $(BUILd_DIR) $(subst test/,$(BUILD_DIR)/,$(basename $(wildcard test/*.c)))

$(BUILD_DIR)/test_%: test/test_%.c $(BUILD_DIR)/libcetest.a
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	$@ 2> $(BUILD_DIR)/test_output.txt || (cat $(BUILD_DIR)/test_output.txt && false)

$(BUILD_DIR)/libcetest.a: $(TEST_OBJS)
	ar cr $@ $^

$(BUILD_DIR)/ce: source/main.c $(BUILD_DIR)/ce.o
	$(CC) $(CFLAGS) $^ -o $@ $(LINK) -ldl -Wl,-rpath,.

$(BUILD_DIR)/%.o: source/%.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.test.o: source/%.c
	$(CC) -c -fpic $(CFLAGS) $^ -o $@

$(BUILD_DIR)/ce_config.so: $(BUILD_DIR)/ce.o $(OBJS)
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LINK)

clean:
	rm -fr $(BUILD_DIR) default.profraw
