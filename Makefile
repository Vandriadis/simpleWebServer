CC=clang
CFLAGS=-Wall -Wextra -O2 -Iinclude
TARGET=server

# Source files
SOURCES=src/main.c src/http.c src/websocket.c src/base64.c src/util.c src/db.c src/auth.c
OBJECTS=$(SOURCES:.c=.o)

# macOS: link sqlite3 and CommonCrypto
LIBS=-lsqlite3 -framework Security -framework CoreFoundation

# For Linux, you may use:
# CFLAGS+=-DOPENSSL
# LIBS=-lsqlite3 -lcrypto

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)