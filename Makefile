CFLAGS=-Wall -Wextra -pedantic -ansi -g
DOT=dot

all: spill

%.png : %.dot
	${DOT} -Tpng -o $@ $<

clean:
	rm -f flowchart.png spill

.PHONY: all clean
