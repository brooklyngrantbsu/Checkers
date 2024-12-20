CC              := gcc
CFLAGS          := -Wall -ggdb
CPPFLAGS        := -I./ -I/usr/X11R6/include/Xm -I/usr/X11R6/include -I/usr/include/openmotif
#LDFLAGS         := -L/usr/lib/X11R6 -lXm -lXaw -lXmu -lXt -lX11 -lm
LDFLAGS         := -L/usr/X11R6/lib -L /usr/X11R6/LessTif/Motif1.2/lib -lXm -lXaw -lXmu -lXt -lX11 -lICE -lSM -pthread -L/usr/lib64/openmotif/ -lm

# Uncomment this next line if you'd like to compile the graphical version of the checkers server.
#CFLAGS          += -DGRAPHICS

all: checkers computer smartypants

checkers: checkers.o graphics.o
computer: myprog.o
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ $^

smartypants: ourprog.o
	${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -o $@ $^


.PHONY: clean
clean:	
	@-rm checkers computer smartypants *.o
