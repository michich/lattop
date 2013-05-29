all: lattop

%.o: %.c
	gcc -g -O2 -Wall -D_GNU_SOURCE=1 -c -o $@ $<

lattop: lattop.o rbtree.o back_trace.o process_accountant.o process.o sym_translator.o stap_reader.o timespan.o lat_translator.o timer_reader.o signal_reader.o
	gcc -g -Wall -o $@ $^

.PHONY: clean
clean:
	rm -f *.o lattop
