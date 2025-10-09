console.log('Script starting...');
let ws = null;
let currentUser = null;

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
        messages.forEach(function(msg) {
          addToChat('[' + msg.username + '] ' + msg.content);
        });
      }
    }
  } catch (error) {
    console.error('Failed to load message history:', error);
  }
}

function connectWebSocket() {
  console.log('Connecting to WebSocket...');
  if (ws) { ws.close(); }
  
  // load history first before connecting
  loadMessageHistory().then(function() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    
    ws.onopen = function() {
      console.log('WebSocket connected');
      addToChat('[Connected to chat]');
    };
    
    ws.onmessage = function(event) {
      console.log('WebSocket message received:', event.data);
      addToChat(event.data);
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

function sendMessage() {
  const input = document.getElementById('msg-input');
  const message = input.value.trim();
  if (message && ws && ws.readyState === WebSocket.OPEN) {
    ws.send(message);
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
});

