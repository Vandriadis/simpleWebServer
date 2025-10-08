# HTTP Server with WebSocket Support & User Authentication

A lightweight, production-ready HTTP server written in C that supports HTTP requests, WebSocket connections, user authentication, and real-time chat functionality. The server features a modern web interface with registration, login, and secure session management.

## 🚀 Features

### Core Server Features
- **HTTP/1.1 Server**: Full HTTP request parsing and response handling
- **WebSocket Support**: RFC 6455 compliant WebSocket implementation
- **Non-blocking I/O**: Efficient `select()`-based event loop for high concurrency
- **Real-time Chat**: Multi-user WebSocket chat with username broadcasting
- **Cross-platform**: Works on macOS and Linux systems

### Authentication & Security
- **User Registration**: Secure user account creation with validation
- **Password Security**: PBKDF2-HMAC-SHA256 password hashing
- **Session Management**: Secure, HttpOnly, SameSite=Lax cookies
- **Session Expiration**: Configurable session timeouts (7 days default)
- **Input Validation**: Client and server-side validation for all inputs

### Database & Persistence
- **SQLite3 Integration**: Persistent user and session storage
- **User Management**: Complete user lifecycle (create, authenticate, manage)
- **Session Tracking**: Secure session ID generation and validation
- **Data Integrity**: Foreign key constraints and proper schema design

### Web Interface
- **Modern UI**: Clean, responsive design with CSS3 styling
- **Form Handling**: Registration and login forms with real-time validation
- **JavaScript Integration**: AJAX form submission and WebSocket communication
- **Error Handling**: User-friendly error messages and success notifications
- **Responsive Design**: Works on desktop and mobile browsers

## 📁 Project Structure

```
httpservc/
├── include/              # Header files
│   ├── auth.h           # Authentication functions
│   ├── base64.h         # Base64 encoding utilities
│   ├── db.h             # Database operations
│   ├── http.h           # HTTP request/response handling
│   ├── util.h           # Utility functions
│   └── websocket.h      # WebSocket protocol implementation
├── src/                 # Source files
│   ├── auth.c           # Password hashing and session management
│   ├── base64.c         # Base64 encoding implementation
│   ├── db.c             # SQLite database operations
│   ├── http.c           # HTTP parsing and HTML serving
│   ├── main.c           # Main server loop and routing
│   ├── util.c           # Utility functions
│   └── websocket.c      # WebSocket frame handling
├── db.sqlite3           # SQLite database (auto-created)
├── Makefile             # Build configuration
├── compile_commands.json # IDE support for clangd/VS Code
└── README.md            # This file
```

## 🛠 Building & Installation

### Requirements
- **Compiler**: Clang or GCC
- **Platform**: macOS or Linux
- **Libraries**: 
  - SQLite3 (`libsqlite3-dev` on Ubuntu/Debian)
  - CommonCrypto (macOS) or OpenSSL (Linux)

### Build Instructions

```bash
# Clone and build
git clone <repository-url>
cd httpservc
make

# Run the server
./server
```

The server will start listening on `http://127.0.0.1:8081`

### Clean Build
```bash
# Clean object files and binary
make clean

# Rebuild from scratch
make clean && make
```

## 🌐 API Endpoints

### HTTP Endpoints

#### `GET /`
**Description**: Serves the main web interface with login/registration forms and chat interface.

**Response**: HTML page with embedded CSS and JavaScript

#### `GET /me`
**Description**: Returns current user information if authenticated.

**Headers Required**: `Cookie: sid=<session-id>`

**Response**:
- **200 OK**: `{"username": "username"}`
- **401 Unauthorized**: Not authenticated

#### `POST /register`
**Description**: Register a new user account.

**Content-Type**: `application/x-www-form-urlencoded`

**Body**: `username=<username>&password=<password>`

**Validation**:
- Username: 3-32 characters, lowercase letters, numbers, underscores only
- Password: Minimum 8 characters

**Response**:
- **200 OK**: Registration successful
- **400 Bad Request**: Invalid input
- **409 Conflict**: Username already exists

#### `POST /login`
**Description**: Authenticate user and create session.

**Content-Type**: `application/x-www-form-urlencoded`

**Body**: `username=<username>&password=<password>`

**Response**:
- **204 No Content**: Login successful (sets session cookie)
- **401 Unauthorized**: Invalid credentials

#### `POST /logout`
**Description**: End user session.

**Headers Required**: `Cookie: sid=<session-id>`

**Response**: **204 No Content** (clears session cookie)

### WebSocket Endpoint

#### `GET /ws`
**Description**: Upgrade HTTP connection to WebSocket for real-time chat.

**Headers Required**:
- `Connection: Upgrade`
- `Upgrade: websocket`
- `Sec-WebSocket-Key: <base64-encoded-key>`
- `Cookie: sid=<session-id>` (for authentication)

**Response**: HTTP 101 Switching Protocols

**WebSocket Features**:
- **Authentication**: Requires valid session cookie
- **Message Broadcasting**: All messages broadcast to all connected users
- **Username Display**: Messages prefixed with `[username]`
- **Connection Management**: Automatic cleanup on disconnect

## 🎯 Usage Examples

### Starting the Server
```bash
# Build and run
make && ./server

# Output:
# Listening on http://127.0.0.1:8081  (Ctrl+C to stop)
```

### Testing with curl

#### Register a new user:
```bash
curl -X POST -H "Content-Type: application/x-www-form-urlencoded" \
     -d "username=testuser&password=password123" \
     http://127.0.0.1:8081/register
```

#### Login:
```bash
curl -c cookies.txt -X POST -H "Content-Type: application/x-www-form-urlencoded" \
     -d "username=testuser&password=password123" \
     http://127.0.0.1:8081/login
```

#### Check authentication:
```bash
curl -b cookies.txt http://127.0.0.1:8081/me
# Response: {"username":"testuser"}
```

#### Logout:
```bash
curl -b cookies.txt -X POST http://127.0.0.1:8081/logout
```

### Web Interface Usage

1. **Open Browser**: Navigate to `http://127.0.0.1:8081/`
2. **Register**: Click "Need an account?" and create a new account
3. **Login**: Use your credentials to log in
4. **Chat**: After login, the chat interface will appear
5. **Send Messages**: Type messages and press Enter or click Send
6. **Real-time**: Messages appear instantly for all connected users

## 🔧 Technical Details

### Architecture
- **Event Loop**: Single-threaded `select()`-based event loop
- **Connection Pool**: Array-based connection management (FD_SETSIZE limit)
- **Protocol Support**: HTTP/1.1 and WebSocket RFC 6455
- **Database**: SQLite3 with WAL mode for performance
- **Security**: PBKDF2 password hashing, secure cookies, input validation

### Database Schema
```sql
-- Users table
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

-- Sessions table
CREATE TABLE sessions (
    id TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
);
```

### Security Features
- **Password Hashing**: PBKDF2-HMAC-SHA256 with configurable iterations
- **Session Security**: Cryptographically secure session IDs
- **Cookie Security**: HttpOnly, SameSite=Lax, secure cookie settings
- **Input Validation**: Server-side validation for all user inputs
- **SQL Injection Prevention**: Parameterized queries for all database operations

### WebSocket Implementation
- **Handshake**: Full RFC 6455 compliant WebSocket handshake
- **Frame Types**: Text frames (opcode 0x1) for chat messages
- **Authentication**: Session-based authentication before WebSocket upgrade
- **Message Broadcasting**: All messages broadcast to all connected users
- **Connection Management**: Automatic cleanup and error handling

## 🐛 Troubleshooting

### Common Issues

1. **Port Already in Use**
   ```bash
   # Check what's using port 8081
   lsof -i :8081
   
   # Kill the process or change port in src/main.c line 93
   ```

2. **Database Errors**
   ```bash
   # Remove database and restart (will recreate)
   rm db.sqlite3*
   ./server
   ```

3. **Compilation Errors**
   ```bash
   # Install SQLite3 development headers
   # Ubuntu/Debian:
   sudo apt-get install libsqlite3-dev
   
   # macOS:
   brew install sqlite3
   ```

4. **WebSocket Connection Fails**
   - Check browser console for JavaScript errors
   - Ensure you're logged in (authentication required)
   - Verify server is running on correct port

5. **Login/Registration Not Working**
   - Check browser developer console for JavaScript errors
   - Verify form validation (username: 3-32 chars, password: 8+ chars)
   - Check server logs for authentication errors

### Debugging

- **Server Logs**: Check console output for connection status and errors
- **Browser DevTools**: Use Network tab to inspect HTTP requests and WebSocket frames
- **Database Inspection**: Use `sqlite3 db.sqlite3` to inspect database contents
- **Connection Testing**: Use `curl` for API endpoint testing

## 🚀 Development

### Adding New Features

1. **New HTTP Endpoints**: Add route handling in `src/main.c`
2. **Database Operations**: Extend `src/db.c` and `include/db.h`
3. **Authentication**: Modify `src/auth.c` for new security features
4. **WebSocket Features**: Extend `src/websocket.c` for new message types
5. **Frontend**: Modify the HTML/CSS/JavaScript in `src/http.c`

### Code Style
- **C99 Standard**: Uses modern C features
- **Error Handling**: Comprehensive error checking and cleanup
- **Memory Management**: Careful buffer management and bounds checking
- **Documentation**: Inline comments for complex operations
- **Security**: Input validation and secure coding practices

### IDE Support
The project includes `compile_commands.json` for enhanced IDE support:
- **clangd**: Automatic IntelliSense and error checking
- **VS Code**: C/C++ extension uses compile commands automatically
- **Other IDEs**: Most modern C/C++ editors support compile_commands.json

## 📋 Future Enhancements

- [ ] **HTTPS/TLS Support**: SSL/TLS encryption for secure connections
- [ ] **Rate Limiting**: API rate limiting and DDoS protection
- [ ] **Message History**: Persistent chat message storage
- [ ] **File Uploads**: Support for file sharing in chat
- [ ] **User Profiles**: Extended user profile management
- [ ] **Admin Panel**: Administrative interface for user management
- [ ] **Configuration**: External configuration file support
- [ ] **Logging**: Comprehensive logging system
- [ ] **Monitoring**: Performance and health monitoring
- [ ] **Docker Support**: Container deployment support

## 📄 License

This project is open source. Feel free to modify and distribute according to your needs.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test thoroughly
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Submit a pull request

## 📞 Support

For issues and questions:
1. Check the troubleshooting section above
2. Review the code comments and documentation
3. Open an issue on the repository
4. Check browser developer console for client-side errors
5. Check server console output for server-side errors

---

**Happy Coding! 🚀**