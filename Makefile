CC=clang
CFLAGS=-Wall -Wextra -O2 -Iinclude
TARGET=server

# Source files
SOURCES=src/main.c src/http.c src/websocket.c src/base64.c src/util.c
OBJECTS=$(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)