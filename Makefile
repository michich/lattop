all: lattop

%.o: %.c
	gcc -g -O2 -Wall -c -o $@ $<

lattop: lattop.o app.o rbtree.o back_trace.o command_reader.o process_accountant.o process.o sym_translator.o perf_reader.o cpumap.o
	gcc -g -Wall -o $@ $^

.PHONY: clean
clean:
	rm -f *.o lattop
