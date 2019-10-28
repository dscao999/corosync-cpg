
CFLAGS ?= -Wall -g
LDFLAGS = -g

.PHONY: all clean

all: cpgmsg

cpgmsg: cpgmsg.o cpg_comm.o
	$(LINK.o) $^ -lreadline -lcpg -lpthread -o $@

clean:
	rm -f cpgmsg
	rm -f *.o
