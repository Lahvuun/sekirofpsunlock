CFLAGS=-O2 -std=c17 -Wall -Wextra -Wpedantic -Iinclude

sekiro-set-fps: src/fps.c src/common.o
	$(CC) $(CFLAGS) src/fps.c src/common.o -o sekiro-set-fps

src/common.o: src/common.c include/common.h
	$(CC) $(CFLAGS) -c src/common.c -o src/common.o

.PHONY: clean
clean:
	$(RM) sekiro-set-fps
	$(RM) src/common.o
