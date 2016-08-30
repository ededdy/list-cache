EXECS := benchmark enumerate

all: $(EXECS)

benchmark:	benchmark.c
		$(CC) $(CFLAGS) $(LDFLAGS) -ggdb3 -Wall benchmark.c -o benchmark

enumerate:	enumerate.c
		$(CC) $(CFLAGS) $(LDFLAGS) -ggdb3 -Wall enumerate.c -o enumerate
.PHONY:		clean
clean:
	-rm -f $(EXECS)

