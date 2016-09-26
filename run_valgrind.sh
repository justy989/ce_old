#!/bin/bash

set -x

RESULT_FILE=valgrind_results.txt

valgrind --leak-check=full ./ce *.c *.h 2> $RESULT_FILE
cat $RESULT_FILE
