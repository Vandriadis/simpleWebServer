console.log('Script starting...');
let ws = null;
let currentUser = null;
let encryptionKey = null;

// Crypto functions for end-to-end encryption
async function deriveKey(password) {
  const enc = new TextEncoder();
  const keyMaterial = await window.crypto.subtle.importKey(
    "raw",
    enc.encode(password),
    "PBKDF2",
    false,
    ["deriveBits", "deriveKey"]
  );
  
  // Use a fixed salt for the room (in production, this should be per-room)
  const salt = enc.encode("websocket-chat-room-salt-v1");
  
  return window.crypto.subtle.deriveKey(
    {
      name: "PBKDF2",
      salt: salt,
      iterations: 100000,
      hash: "SHA-256"
    },
    keyMaterial,
    { name: "AES-GCM", length: 256 },
    false,
    ["encrypt", "decrypt"]
  );
}

async function encryptMessage(text, key) {
  const enc = new TextEncoder();
  const iv = window.crypto.getRandomValues(new Uint8Array(12));
  
  const encrypted = await window.crypto.subtle.encrypt(
    { name: "AES-GCM", iv: iv },
    key,
    enc.encode(text)
  );
  
  // Combine IV and encrypted data
  const combined = new Uint8Array(iv.length + encrypted.byteLength);
  combined.set(iv, 0);
  combined.set(new Uint8Array(encrypted), iv.length);
  
  // Convert to base64 for transmission
  return btoa(String.fromCharCode.apply(null, combined));
}

async function decryptMessage(encryptedBase64, key) {
  try {
    // Decode from base64
    const combined = Uint8Array.from(atob(encryptedBase64), c => c.charCodeAt(0));
    
    // Split IV and encrypted data
    const iv = combined.slice(0, 12);
    const encrypted = combined.slice(12);
    
    const decrypted = await window.crypto.subtle.decrypt(
      { name: "AES-GCM", iv: iv },
      key,
      encrypted
    );
    
    const dec = new TextDecoder();
    return dec.decode(decrypted);
  } catch (e) {
    return "[🔒 Encrypted message - wrong key]";
  }
}

function showLogin() {
  console.log('showLogin called');
  document.getElementById('login-form').classList.remove('hidden');
  document.getElementById('register-form').classList.add('hidden');
  clearMessage();
}

function showRegister() {
  console.log('showRegister called');
  document.getElementById('login-form').classList.add('hidden');
  document.getElementById('register-form').classList.remove('hidden');
  clearMessage();
}

function showMessage(text, type) {
  const msgDiv = document.getElementById('message');
  msgDiv.innerHTML = '<div class="' + type + '">' + text + '</div>';
  msgDiv.scrollIntoView();
}

function clearMessage() {
  document.getElementById('message').innerHTML = '';
}

function setupFormHandlers() {
  console.log('Setting up form handlers...');
  const loginForm = document.getElementById('loginForm');
  const registerForm = document.getElementById('registerForm');
  
  if (loginForm) {
    console.log('Login form found');
    loginForm.addEventListener('submit', async function(e) {
      console.log('Login form submitted');
      e.preventDefault();
      const formData = new FormData(this);
      const data = new URLSearchParams(formData);
      
      try {
        const response = await fetch('/login', {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: data
        });
        
        console.log('Login response status:', response.status);
        if (response.ok) {
          showMessage('Login successful!', 'success');
          console.log('Login successful, checking auth status...');
          setTimeout(function() {
            console.log('Calling checkAuthStatus...');
            checkAuthStatus();
          }, 500);
        } else {
          showMessage('Login failed. Check your credentials.', 'error');
        }
      } catch (error) {
        showMessage('Login error: ' + error.message, 'error');
      }
    });
  }
  
  if (registerForm) {
    console.log('Register form found');
    registerForm.addEventListener('submit', async function(e) {
      e.preventDefault();
      const formData = new FormData(this);
      const data = new URLSearchParams(formData);
      
      try {
        const response = await fetch('/register', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: data
        });
        
        if (response.ok) {
          showMessage('Registration successful! You can now login.', 'success');
          setTimeout(showLogin, 1000);
        } else if (response.status === 409) {
          showMessage('Username already taken. Please choose another.', 'error');
        } else {
          showMessage('Registration failed. Please check your input.', 'error');
        }
      } catch (error) {
        showMessage('Registration error: ' + error.message, 'error');
      }
    });
  }
}

async function checkAuthStatus() {
  console.log('checkAuthStatus called');
  try {
    const response = await fetch('/me', { credentials: 'same-origin' });
    console.log('Auth check response status:', response.status);
    if (response.ok) {
      const user = await response.json();
      console.log('User authenticated:', user.username);
      currentUser = user.username;
      document.getElementById('username-display').textContent = 'Logged in as: ' + user.username;
      document.getElementById('auth-section').classList.add('hidden');
      document.getElementById('user-info').classList.remove('hidden');
      document.getElementById('chat-section').classList.remove('hidden');
      console.log('UI updated, connecting WebSocket...');
      connectWebSocket();
    } else {
      console.log('User not authenticated');
      currentUser = null;
      document.getElementById('auth-section').classList.remove('hidden');
      document.getElementById('user-info').classList.add('hidden');
      document.getElementById('chat-section').classList.add('hidden');
      if (ws) { ws.close(); ws = null; }
    }
  } catch (error) {
    console.error('Auth check failed:', error);
  }
}

async function loadMessageHistory() {
  try {
    const response = await fetch('/messages', { credentials: 'same-origin' });
    if (response.ok) {
      const messages = await response.json();
      // messages come back newest first, so reverse them
      messages.reverse();
      const chatLog = document.getElementById('chat-log');
      chatLog.textContent = '';  // clear chat first
      if (messages.length === 0) {
        addToChat('[No message history]');
      } else {
        for (const msg of messages) {
          let content = msg.content;
          
          // Check if message looks encrypted (base64)
          if (content.match(/^[A-Za-z0-9+/]+=*$/)) {
            if (encryptionKey) {
              // Try to decrypt
              try {
                content = await decryptMessage(content, encryptionKey);
              } catch (e) {
                content = '[🔒 Encrypted - wrong key?]';
              }
            } else {
              content = '[🔒 Encrypted message - no key provided]';
            }
          }
          
          addToChat('[' + msg.username + '] ' + content);
        }
      }
    }
  } catch (error) {
    console.error('Failed to load message history:', error);
  }
}

async function promptForEncryptionKey(showInChat) {
  const password = prompt(
    '🔒 SECURE CHAT ROOM PASSWORD\n\n' +
    '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n' +
    'Create or enter a shared password for this chat room.\n\n' +
    '✅ All users MUST use the SAME password\n' +
    '✅ Example: "pizza2024" or "secretclub"\n' +
    '✅ Messages are encrypted on your device\n' +
    '✅ Server/database cannot read messages\n\n' +
    '⚠️  Press Cancel or leave empty for NO encryption\n\n' +
    'Enter password:'
  );
  
  const statusEl = document.getElementById('encryption-status');
  
  if (password) {
    encryptionKey = await deriveKey(password);
    if (showInChat) {
      addToChat('━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');
      addToChat('[🔒 ENCRYPTION ENABLED]');
      addToChat('[✓] Messages are end-to-end encrypted');
      addToChat('[✓] Only users with correct password can read');
      addToChat('[✓] Server cannot see message contents');
      addToChat('━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');
    }
    if (statusEl) {
      statusEl.textContent = '🔒 ENCRYPTED';
      statusEl.style.color = '#00ff00';
    }
    // Reload message history with new key
    await loadMessageHistory();
  } else {
    encryptionKey = null;
    if (showInChat) {
      addToChat('━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');
      addToChat('[⚠️  ENCRYPTION DISABLED]');
      addToChat('[!] Messages sent in PLAINTEXT');
      addToChat('[!] Anyone can read messages');
      addToChat('━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');
    }
    if (statusEl) {
      statusEl.textContent = '⚠️  NOT ENCRYPTED';
      statusEl.style.color = '#ff0000';
    }
    // Reload message history to show encrypted status
    await loadMessageHistory();
  }
}

function connectWebSocket() {
  console.log('Connecting to WebSocket...');
  if (ws) { ws.close(); }
  
  // Ask for encryption password, then load history and connect
  promptForEncryptionKey(true).then(function() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    
    ws.onopen = function() {
      console.log('WebSocket connected');
      addToChat('[Connected to chat]');
    };
    
    ws.onmessage = async function(event) {
      console.log('WebSocket message received:', event.data);
      // Messages from server come as "[username] content"
      // If encryption is enabled, content will be encrypted
      const match = event.data.match(/^\[([^\]]+)\] (.+)$/);
      
      if (match) {
        const username = match[1];
        const content = match[2];
        
        // Try to decrypt if we have a key
        // Check if content looks like base64 (encrypted)
        if (encryptionKey && content.match(/^[A-Za-z0-9+/]+=*$/)) {
          try {
            const decrypted = await decryptMessage(content, encryptionKey);
            addToChat('[' + username + '] ' + decrypted);
          } catch (e) {
            // Decryption failed, show as encrypted
            addToChat('[' + username + '] [🔒 Encrypted - wrong key?]');
          }
        } else {
          // Either no key or plaintext message
          addToChat('[' + username + '] ' + content);
        }
      } else {
        // System message or other format
        addToChat(event.data);
      }
    };
    
    ws.onclose = function() {
      console.log('WebSocket disconnected');
      addToChat('[Disconnected from chat]');
    };
    
    ws.onerror = function(error) {
      console.log('WebSocket error:', error);
      addToChat('[WebSocket error]');
    };
  });
}

function addToChat(message) {
  const chatLog = document.getElementById('chat-log');
  chatLog.textContent += message + '\n';
  chatLog.scrollTop = chatLog.scrollHeight;
}

async function sendMessage() {
  const input = document.getElementById('msg-input');
  const message = input.value.trim();
  if (message && ws && ws.readyState === WebSocket.OPEN) {
    let toSend = message;
    
    // Encrypt if we have a key
    if (encryptionKey) {
      toSend = await encryptMessage(message, encryptionKey);
    }
    
    ws.send(toSend);
    input.value = '';
  }
}

async function logout() {
  try {
    await fetch('/logout', { method: 'POST', credentials: 'same-origin' });
    if (ws) { ws.close(); ws = null; }
    currentUser = null;
    document.getElementById('chat-log').textContent = '';
    checkAuthStatus();
  } catch (error) {
    console.error('Logout failed:', error);
  }
}

document.addEventListener('DOMContentLoaded', function() {
  console.log('DOM loaded, setting up handlers');
  setupFormHandlers();
  checkAuthStatus();
  
  const msgInput = document.getElementById('msg-input');
  if (msgInput) {
    msgInput.addEventListener('keypress', function(e) {
      if (e.key === 'Enter') {
        sendMessage();
      }
    });
  }
  
  const needAccountBtn = document.getElementById('need-account-btn');
  if (needAccountBtn) {
    console.log('Need account button found');
    needAccountBtn.addEventListener('click', function(e) {
      e.preventDefault();
      console.log('Need account button clicked');
      showRegister();
    });
  } else {
    console.log('Need account button NOT found');
  }
  
  const haveAccountBtn = document.getElementById('have-account-btn');
  if (haveAccountBtn) {
    console.log('Have account button found');
    haveAccountBtn.addEventListener('click', function(e) {
      e.preventDefault();
      console.log('Have account button clicked');
      showLogin();
    });
  } else {
    console.log('Have account button NOT found');
  }
  
  const logoutBtn = document.getElementById('logout-btn');
  if (logoutBtn) {
    console.log('Logout button found');
    logoutBtn.addEventListener('click', function(e) {
      e.preventDefault();
      console.log('Logout button clicked');
      logout();
    });
  }
  
  const sendBtn = document.getElementById('send-btn');
  if (sendBtn) {
    console.log('Send button found');
    sendBtn.addEventListener('click', function(e) {
      e.preventDefault();
      console.log('Send button clicked');
      sendMessage();
    });
  }
  
  const changeKeyBtn = document.getElementById('change-key-btn');
  if (changeKeyBtn) {
    console.log('Change key button found');
    changeKeyBtn.addEventListener('click', function(e) {
      e.preventDefault();
      console.log('Change key button clicked');
      promptForEncryptionKey(true);
    });
  }
});

