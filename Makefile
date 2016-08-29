CC=gcc
CFLAGS=-Wall -Werror -Wextra -std=c11 -g
LINK=-lncurses

ce: main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LINK)
	echo "testing" > one_line_file.txt
	touch empty_file.txt

clean:
	rm -f ce empty_file.txt one_line_file.txt
