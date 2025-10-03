CC=clang
CFLAGS=-Wall -Wextra -O2
TARGET=server

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)