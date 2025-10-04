# HTTP Server with WebSocket Support

A lightweight HTTP server written in C that supports both HTTP requests and WebSocket connections. The server provides a simple echo WebSocket implementation and serves a basic HTML page for testing WebSocket functionality.

## Features

- **HTTP Server**: Handles basic HTTP requests with proper response headers
- **WebSocket Support**: Full WebSocket handshake and frame handling
- **Echo Server**: WebSocket echo functionality for testing
- **Non-blocking I/O**: Uses `select()` for efficient connection handling
- **Cross-platform**: Works on macOS and Linux systems
- **Minimal Dependencies**: Only uses standard C libraries and system calls

## Project Structure

```
httpservc/
├── include/           # Header files
│   ├── http.h        # HTTP request/response handling
│   ├── websocket.h   # WebSocket protocol implementation
│   ├── base64.h      # Base64 encoding utilities
│   └── util.h        # Utility functions
├── src/              # Source files
│   ├── main.c        # Main server loop and connection handling
│   ├── http.c        # HTTP request parsing and responses
│   ├── websocket.c   # WebSocket frame handling
│   ├── base64.c      # Base64 encoding implementation
│   └── util.c        # Utility functions
├── Makefile          # Build configuration
└── README.md         # This file
```

## Building

The project uses a simple Makefile for compilation:

```bash
# Build the server
make

# Clean object files and binary
make clean
```

### Requirements

- **Compiler**: Clang or GCC
- **Platform**: macOS or Linux
- **Libraries**: Standard C library, CommonCrypto (macOS) or OpenSSL (Linux)

## Usage

### Starting the Server

```bash
# Build and run
make && ./server
```

The server will start listening on `http://127.0.0.1:8080`

### Available Endpoints

- **`/`** - Serves a simple HTML page with WebSocket test interface
- **`/ws`** - WebSocket endpoint for real-time communication

### WebSocket Testing

1. Open your browser and navigate to `http://127.0.0.1:8080`
2. The page will automatically connect to the WebSocket server
3. Type messages in the input field and click "Send"
4. Messages will be echoed back from the server

## API Reference

### HTTP Endpoints

#### GET /
Returns a simple HTML page with WebSocket test interface.

**Response**: HTML page with embedded JavaScript for WebSocket testing

#### GET /ws
Upgrades HTTP connection to WebSocket.

**Headers Required**:
- `Connection: Upgrade`
- `Upgrade: websocket`
- `Sec-WebSocket-Key: <base64-encoded-key>`

**Response**: HTTP 101 Switching Protocols with `Sec-WebSocket-Accept` header

### WebSocket Protocol

The server implements WebSocket text frames for bidirectional communication:

- **Text Frames**: Supports UTF-8 text messages
- **Echo Functionality**: All received messages are echoed back to the client
- **Connection Management**: Automatic connection cleanup on client disconnect

## Technical Details

### Architecture

- **Event Loop**: Uses `select()` for non-blocking I/O
- **Connection Pool**: Simple array-based connection management
- **Protocol Support**: HTTP/1.1 and WebSocket RFC 6455
- **Security**: Proper WebSocket handshake validation

### WebSocket Implementation

- **Handshake**: Full RFC 6455 compliant handshake
- **Frame Types**: Text frames (opcode 0x1)
- **Masking**: Server frames are unmasked (RFC compliant)
- **Payload Length**: Supports up to 64-bit payload lengths

### HTTP Implementation

- **Request Parsing**: Basic HTTP request line and header parsing
- **Response Generation**: Proper HTTP response headers
- **Error Handling**: 400 Bad Request and 404 Not Found responses

## Development

### Adding New Features

1. **New HTTP Endpoints**: Add route handling in `main.c`
2. **WebSocket Message Types**: Extend `websocket.c` for new frame types
3. **Authentication**: Add security headers and validation
4. **Static Files**: Implement file serving capabilities

### Code Style

- **C99 Standard**: Uses modern C features
- **Error Handling**: Comprehensive error checking and cleanup
- **Memory Management**: Careful buffer management and bounds checking
- **Documentation**: Inline comments for complex operations

## Troubleshooting

### Common Issues

1. **Port Already in Use**: Change the port in `main.c` (line 36)
2. **Compilation Errors**: Ensure you have the required development tools
3. **WebSocket Connection Fails**: Check browser console for errors
4. **Permission Denied**: Run with appropriate permissions for port binding

### Debugging

- **Server Logs**: Check console output for connection status
- **Browser DevTools**: Use Network tab to inspect WebSocket frames
- **Connection Testing**: Use `curl` or `wget` for HTTP endpoint testing

## License

This project is open source. Feel free to modify and distribute according to your needs.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Future Enhancements

- [ ] HTTPS/TLS support
- [ ] Static file serving
- [ ] REST API endpoints
- [ ] Authentication system
- [ ] Configuration file support
- [ ] Logging system
- [ ] Performance monitoring
