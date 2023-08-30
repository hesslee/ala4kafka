CC = gcc
CFLAGS = -Wall $(shell pkg-config --cflags glib-2.0 rdkafka)
LDLIBS = $(shell pkg-config --libs glib-2.0 rdkafka)

all: producer

producer: producer.c cJSON.o
        $(CC) $(CFLAGS) $@.c -o $@ cJSON.o $(LDLIBS)

cJSON.o : cJSON.c cJSON.h
        $(CC) $(CFLAGS) -c cJSON.c

#.c.o :
#       $(CC) $(CFLAGS) -c $*.c

clean :
        rm -f producer *.o
