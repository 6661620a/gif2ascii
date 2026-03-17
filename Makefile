CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
TARGET  = gif2ascii
SRCS    = main.c gif.c ascii.c render.c util.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
