let sessionToken = null;
let currentUser = null;
let currentPath = '/';
let currentFile = null;
let files = [];

const API_URL = () => `http://${document.getElementById('host').value}:${document.getElementById('port').value}/api`;

// Add global error handler to catch all errors
window.addEventListener('error', (e) => {
  console.error('❌ GLOBAL ERROR:', e.message, e.error);
  console.error('Error stack:', e.error?.stack);
});

window.addEventListener('unhandledrejection', (e) => {
  console.error('❌ UNHANDLED PROMISE REJECTION:', e.reason);
});

// Utility Functions
function showPage(pageId) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.getElementById(pageId).classList.add('active');
}

function showError(elementId, message) {
  const el = document.getElementById(elementId);
  if (!el) return;
  el.textContent = message;
  el.classList.add('show');
  setTimeout(() => el.classList.remove('show'), 3000);
}

function showPanel(panelId) {
  document.querySelectorAll('.content-panel').forEach(p => p.classList.remove('active'));
  document.getElementById(panelId).classList.add('active');
}

async function apiCall(operation, data = {}) {
  try {
    const payload = { operation, request_id: Date.now().toString(), ...data };
    if (sessionToken && !payload.token) payload.token = sessionToken;
    
    const res = await fetch(API_URL(), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    
    const text = await res.text();
    let json;
    try {
      json = JSON.parse(text);
    } catch(e) {
      console.error('JSON parse error:', e);
      return { status: 'error', error: 'Invalid response' };
    }
    
    // Check for invalid session
    if (json.status === 'error' && json.error === 'invalid_session') {
      console.error('Session expired');
      alert('🔴 SESSION EXPIRED - You will be logged out now!');
      sessionToken = null;
      currentUser = null;
      showPage('auth-page');
      return json;
    }
    
    // Update token if server sends a new one
    if (json.token) {
      sessionToken = json.token;
      // Update token display
      const tokenEl = document.getElementById('token-value');
      if (tokenEl) tokenEl.textContent = json.token.substring(0, 12) + '...';
    }
    
    return json;
  } catch (e) {
    console.error('API Error:', e);
    return { status: 'error', error: e.message };
  }
}

// Auth Page Functions
function initAuthPage() {
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      btn.classList.add('active');
      document.getElementById(btn.dataset.tab + '-form').classList.add('active');
    });
  });

  document.getElementById('login-btn').addEventListener('click', async () => {
    const username = document.getElementById('login-username').value;
    const password = document.getElementById('login-password').value;
    
    if (!username || !password) {
      showError('login-error', 'Please enter username and password');
      return;
    }

    const result = await apiCall('user_login', { username, password });
    if (result.status === 'success' && result.token) {
      currentUser = username;
      document.getElementById('current-user').textContent = username;
      // Update token display
      const tokenEl = document.getElementById('token-value');
      if (tokenEl) tokenEl.textContent = result.token.substring(0, 12) + '...';
      showPage('dashboard-page');
      loadFiles();
    } else {
      showError('login-error', 'Login failed. Check your credentials.');
    }
  });

  document.getElementById('signup-btn').addEventListener('click', async () => {
    const username = document.getElementById('signup-username').value;
    const password = document.getElementById('signup-password').value;
    const role = document.getElementById('signup-role').value;
    
    if (!username || !password) {
      showError('signup-error', 'Please enter username and password');
      return;
    }

    const result = await apiCall('user_create', { username, password, role });
    if (result.status === 'success') {
      // Auto-login after signup
      const loginResult = await apiCall('user_login', { username, password });
      if (loginResult.status === 'success' && loginResult.token) {
        currentUser = username;
        document.getElementById('current-user').textContent = username;
        // Update token display
        const tokenEl = document.getElementById('token-value');
        if (tokenEl) tokenEl.textContent = loginResult.token.substring(0, 12) + '...';
        showPage('dashboard-page');
        loadFiles();
      }
    } else {
      showError('signup-error', 'Signup failed. Username may be taken.');
    }
  });

  // Enter key support
  document.getElementById('login-password').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') document.getElementById('login-btn').click();
  });
  document.getElementById('signup-password').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') document.getElementById('signup-btn').click();
  });
}

// Dashboard Functions
function initDashboard() {
  document.getElementById('logout-btn').addEventListener('click', () => {
    sessionToken = null;
    currentUser = null;
    currentPath = '/';
    // Clear token display
    const tokenEl = document.getElementById('token-value');
    if (tokenEl) tokenEl.textContent = 'none';
    showPage('auth-page');
    showPanel('welcome-screen');
  });

  document.getElementById('refresh-files').addEventListener('click', () => loadFiles());
  
  document.getElementById('new-file-btn').addEventListener('click', () => {
    document.getElementById('modal-title').textContent = 'Create New File';
    document.getElementById('modal-label').textContent = 'File Name';
    document.getElementById('modal-input').value = '';
    document.getElementById('modal-input').placeholder = 'example.txt';
    document.getElementById('modal').classList.add('active');
    document.getElementById('modal-confirm').onclick = (e) => {
      e.preventDefault();
      e.stopPropagation();
      e.stopImmediatePropagation();
      createFile();
      return false;
    };
  });

  document.getElementById('new-folder-btn').addEventListener('click', () => {
    document.getElementById('modal-title').textContent = 'Create New Folder';
    document.getElementById('modal-label').textContent = 'Folder Name';
    document.getElementById('modal-input').value = '';
    document.getElementById('modal-input').placeholder = 'my-folder';
    document.getElementById('modal').classList.add('active');
    document.getElementById('modal-confirm').onclick = (e) => {
      e.preventDefault();
      e.stopPropagation();
      e.stopImmediatePropagation();
      createFolder();
      return false;
    };
  });

  // Prevent Enter key from causing any default behavior in modal
  document.getElementById('modal-input').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      e.stopPropagation();
      document.getElementById('modal-confirm').click();
      return false;
    }
  });
  
  // Prevent any form submission on the entire page
  document.addEventListener('submit', (e) => {
    e.preventDefault();
    return false;
  });
  

  document.getElementById('save-file').addEventListener('click', saveFile);
  document.getElementById('delete-file').addEventListener('click', deleteFile);
  document.getElementById('close-editor').addEventListener('click', () => {
    showPanel('welcome-screen');
    currentFile = null;
  });
  document.getElementById('close-dir').addEventListener('click', () => showPanel('welcome-screen'));
}

async function loadFiles() {
  try {
    const result = await apiCall('dir_list', { path: currentPath });
    if (result.status === 'success') {
      files = result.entries || [];
      renderFileTree();
    } else {
      console.error('Failed to load files:', result);
    }
  } catch (error) {
    console.error('loadFiles exception:', error);
  }
}

function renderFileTree() {
  try {
    const tree = document.getElementById('file-tree');
    if (!tree) {
      console.error('file-tree element not found');
      return;
    }
    tree.innerHTML = '';
    
    if (files.length === 0) {
      tree.innerHTML = '<div style="padding:20px;text-align:center;color:var(--text-muted)">No files yet</div>';
      return;
    }

    files.forEach(file => {
      const item = document.createElement('div');
      item.className = file.type === 1 ? 'folder-item' : 'file-item';
      // Server returns full paths like "/test.txt" - display just the filename
      const displayName = file.name.split('/').pop() || file.name;
      item.innerHTML = `${file.type === 1 ? '📁' : '📄'} ${displayName}`;
      item.onclick = () => {
        if (file.type === 1) {
          openDirectory(file.name);
        } else {
          openFile(file.name);
        }
      };
      tree.appendChild(item);
    });
  } catch (error) {
    console.error('renderFileTree exception:', error);
  }
}

async function openFile(filename) {
  // Server returns full paths like "/test.txt", not just "test.txt"
  const path = filename.startsWith('/') ? filename : (currentPath === '/' ? `/${filename}` : `${currentPath}/${filename}`);
  const result = await apiCall('file_read', { path });
  
  if (result.status === 'success') {
    currentFile = path;
    // Extract just the filename for display
    const displayName = path.split('/').pop() || path;
    document.getElementById('editor-filename').textContent = displayName;
    // Server may return content in 'data' or 'content' field
    const content = result.content || result.data || '';
    document.getElementById('file-editor-content').value = content;
    showPanel('file-editor');
  } else {
    alert('Failed to open file: ' + (result.error || 'Unknown error'));
  }
}

async function openDirectory(dirname) {
  const path = currentPath === '/' ? `/${dirname}` : `${currentPath}/${dirname}`;
  const result = await apiCall('dir_list', { path });
  
  if (result.status === 'success') {
    document.getElementById('dir-name').textContent = dirname;
    const grid = document.getElementById('dir-contents');
    grid.innerHTML = '';
    
    const entries = result.entries || [];
    entries.forEach(entry => {
      const item = document.createElement('div');
      item.className = 'dir-item';
      item.innerHTML = `
        <div class="dir-item-icon">${entry.type === 1 ? '📁' : '📄'}</div>
        <div class="dir-item-name">${entry.name}</div>
      `;
      item.onclick = () => {
        if (entry.type === 1) {
          openDirectory(`${dirname}/${entry.name}`);
        } else {
          openFile(`${dirname}/${entry.name}`);
        }
      };
      grid.appendChild(item);
    });
    
    showPanel('directory-view');
  }
}

async function createFile() {
  const name = document.getElementById('modal-input').value.trim();
  if (!name) return;
  
  try {
    const path = currentPath === '/' ? `/${name}` : `${currentPath}/${name}`;
    const result = await apiCall('file_create', { path, data: '' });
    
    if (result.status === 'success') {
      closeModal();
      await loadFiles();
      // Don't auto-open, just refresh the file list
      // await openFile(path);
    } else {
      console.error('createFile failed:', result);
      alert('Failed to create file: ' + (result.error || 'Unknown error'));
    }
  } catch (error) {
    console.error('createFile exception:', error);
    alert('Error creating file: ' + error.message);
  }
}

async function createFolder() {
  const name = document.getElementById('modal-input').value.trim();
  if (!name) return;
  
  const path = currentPath === '/' ? `/${name}` : `${currentPath}/${name}`;
  const result = await apiCall('dir_create', { path });
  
  if (result.status === 'success') {
    closeModal();
    await loadFiles();
  } else {
    alert('Failed to create folder: ' + (result.error || 'Unknown error'));
  }
}

async function saveFile() {
  if (!currentFile) return;
  
  const data = document.getElementById('file-editor-content').value;
  
  // Delete then recreate (simple update approach)
  const delResult = await apiCall('file_delete', { path: currentFile });
  
  const result = await apiCall('file_create', { path: currentFile, data });
  
  if (result.status === 'success') {
    const btn = document.getElementById('save-file');
    const orig = btn.textContent;
    btn.textContent = '✓ Saved';
    setTimeout(() => btn.textContent = orig, 2000);
    await loadFiles();
  } else {
    alert('Failed to save file: ' + (result.error || 'Unknown error'));
  }
}

async function deleteFile() {
  if (!currentFile || !confirm('Delete this file?')) return;
  
  const result = await apiCall('file_delete', { path: currentFile });
  
  if (result.status === 'success') {
    showPanel('welcome-screen');
    currentFile = null;
    await loadFiles();
  } else {
    alert('Failed to delete file: ' + (result.error || 'Unknown error'));
  }
}

function closeModal() {
  document.getElementById('modal').classList.remove('active');
}

// Initialize on load
document.addEventListener('DOMContentLoaded', () => {
  initAuthPage();
  initDashboard();
  
  // Close modal on outside click
  document.getElementById('modal').addEventListener('click', (e) => {
    if (e.target.id === 'modal') closeModal();
  });
});
