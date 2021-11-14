CC=gcc
CFLAGS=-std=c17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined

sekirofpsunlock: sekirofpsunlock.o
	$(CC) $(CFLAGS) sekirofpsunlock.o -o sekirofpsunlock

sekirofpsunlock.o: main.c
	$(CC) $(CFLAGS) -c main.c -o sekirofpsunlock.o
