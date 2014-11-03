GCC = gcc

all: test

run: test
	./test
	python analyze.py
	./spc.gp

test: test.c spc1.h spc1.c Makefile
	$(GCC) -O2 -g -o test -lm test.c spc1.c
