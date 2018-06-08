CFLAGS=-std=c11 -O0

low-overhead-polling: low-overhead-polling.o
	gcc -o $@ $^

clean:
	rm -f low-overhead-polling low-overhead-polling.o
