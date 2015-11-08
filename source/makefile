# rescode makefile

INCLUDE = -I../include
CFLAGS = $(INCLUDE) -ansi 

all: z802tzx

clean:
	rm -f z802tzx *.o

z802tzx: z802tzx.o stdafx.o
	$(CC) -o z802tzx $(INCLUDE) z802tzx.o stdafx.o
