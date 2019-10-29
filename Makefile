
CFLAGS ?= -Wall -g
LDFLAGS = -g

.PHONY: all clean

all: cpgmsg

cpgmsg: cpgmsg.o cpg_comm.o
	$(LINK.o) $^ ecc256/ripemd160.o -lreadline -lcpg -lgmp -lpthread -o $@

clean:
	rm -f cpgmsg
	rm -f *.o
