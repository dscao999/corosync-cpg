
CFLAGS ?= -Wall -g
LDFLAGS = -g

.PHONY: all clean

all: cpgmsg

cpgmsg: cpgmsg.o squeue.o cpg_comm.o
	$(LINK.o) $^ -L./ -lecc -lreadline -lcpg -lgmp -lpthread -o $@

clean:
	rm -f cpgmsg
	rm -f *.o
