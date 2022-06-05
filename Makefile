CFLAGS=-std=c17 -Wall -Wextra -Wpedantic -fanalyzer

sekirofpsunlock: sekirofpsunlock.o
	$(CC) $(CFLAGS) sekirofpsunlock.o -o sekirofpsunlock

sekirofpsunlock.o: main.c
	$(CC) $(CFLAGS) -c main.c -o sekirofpsunlock.o

.PHONY: clean
clean:
	$(RM) sekirofpsunlock
	$(RM) sekirofpsunlock.o
