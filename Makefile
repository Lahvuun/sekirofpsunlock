CFLAGS=-O2 -std=c17 -Wall -Wextra -Wpedantic -Iinclude

.PHONY: all
all: sekiro-set-fps

sekiro-set-fps: src/fps.c src/common.o src/pe.o
	$(CC) $(CFLAGS) src/fps.c src/common.o src/pe.o -o sekiro-set-fps


src/common.o: src/common.c include/common.h
	$(CC) $(CFLAGS) -c src/common.c -o src/common.o

src/pe.o: src/pe.c include/pe.h
	$(CC) $(CFLAGS) -c src/pe.c -o src/pe.o

.PHONY: clean
clean:
	$(RM) sekiro-set-fps
	$(RM) src/common.o
	$(RM) src/pe.o
