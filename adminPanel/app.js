const API = 'http://raspberrypi.local:5000';

function showPage(name, el) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
  document.getElementById('page-' + name).classList.add('active');
  el.classList.add('active');
  if (name === 'dashboard') loadDashboard();
  if (name === 'logs') loadLogs();
  if (name === 'videos') loadVideos();
  if (name === 'users') loadUsers();
}

function fmt(ts) {
  if (!ts) return '—';
  return new Date(ts).toLocaleString('pl-PL');
}

function fmtDay(ts) {
  return new Date(ts).toLocaleDateString('pl-PL', {month:'short', day:'numeric'});
}

async function loadDashboard() {
  const s = await fetch(API + '/api/stats').then(r => r.json());
  document.getElementById('s-granted').textContent = s.granted;
  document.getElementById('s-denied').textContent = s.denied;
  document.getElementById('s-users').textContent = s.active_users;
  document.getElementById('s-videos').textContent = s.total_videos;

  const chart = document.getElementById('chart');
  if (!s.daily.length) { chart.innerHTML = '<div class="empty">no data</div>'; }
  else {
    const max = Math.max(...s.daily.map(d => d.count), 1);
    chart.innerHTML = s.daily.map(d => `
      <div class="bar-col">
        <div class="bar" style="height:${Math.round((d.count/max)*100)}px" title="${d.count} entries"></div>
        <div class="bar-label">${fmtDay(d.day)}</div>
      </div>`).join('');
  }

  const logs = await fetch(API + '/api/logs').then(r => r.json());
  renderLogs('recent-body', logs.slice(0, 8));
}

async function loadLogs() {
  document.getElementById('logs-body').innerHTML = '<div class="empty">loading...</div>';
  const logs = await fetch(API + '/api/logs').then(r => r.json());
  renderLogs('logs-body', logs);
}

function renderLogs(id, logs) {
  if (!logs.length) { document.getElementById(id).innerHTML = '<div class="empty">No records found</div>'; return; }
  document.getElementById(id).innerHTML = `
    <table>
      <thead><tr><th>#</th><th>Timestamp</th><th>UID</th><th>Name</th><th>Result</th></tr></thead>
      <tbody>${logs.map(l => `<tr>
        <td class="muted">${l.id}</td>
        <td class="muted">${fmt(l.timestamp)}</td>
        <td class="uid">${l.uid}</td>
        <td>${l.name || '<span class="muted">unknown</span>'}</td>
        <td><span class="badge ${l.result.toLowerCase()}">${l.result}</span></td>
      </tr>`).join('')}</tbody>
    </table>`;
}

async function loadVideos() {
  document.getElementById('videos-body').innerHTML = '<div class="empty">loading...</div>';
  const videos = await fetch(API + '/api/videos').then(r => r.json());
  if (!videos.length) { document.getElementById('videos-body').innerHTML = '<div class="empty">No recordings</div>'; return; }
  document.getElementById('videos-body').innerHTML = `
    <table>
      <thead><tr><th>#</th><th>Recorded</th><th>UID</th><th>Name</th><th>Result</th><th>Faces</th><th>Duration</th><th></th></tr></thead>
      <tbody>${videos.map(v => `<tr>
        <td class="muted">${v.id}</td>
        <td class="muted">${fmt(v.recorded_at)}</td>
        <td class="uid">${v.uid}</td>
        <td>${v.name || '<span class="muted">unknown</span>'}</td>
        <td><span class="badge ${v.result.toLowerCase()}">${v.result}</span></td>
        <td>${v.faces_count ?? '—'}</td>
        <td class="muted">${v.duration_seconds}s</td>
        <td><button class="btn btn-outline" onclick="playVideo(${v.id})">▶ Play</button></td>
      </tr>`).join('')}</tbody>
    </table>`;
}

function playVideo(id) {
  document.getElementById('modal-video').src = API + '/api/videos/' + id + '/stream';
  document.getElementById('modal').classList.add('open');
}

function closeModal() {
  const v = document.getElementById('modal-video');
  v.pause(); v.src = '';
  document.getElementById('modal').classList.remove('open');
}

async function loadUsers() {
  document.getElementById('users-body').innerHTML = '<div class="empty">loading...</div>';
  const users = await fetch(API + '/api/users').then(r => r.json());
  if (!users.length) { document.getElementById('users-body').innerHTML = '<div class="empty">No users registered</div>'; return; }
  document.getElementById('users-body').innerHTML = `
    <table>
      <thead><tr><th>#</th><th>UID</th><th>Name</th><th>Status</th><th>Actions</th></tr></thead>
      <tbody>${users.map(u => `<tr>
        <td class="muted">${u.id}</td>
        <td class="uid">${u.uid}</td>
        <td>${u.name}</td>
        <td><span class="badge ${u.active ? 'active' : 'inactive'}">${u.active ? 'Active' : 'Inactive'}</span></td>
        <td>
          <button class="btn btn-warn" onclick="toggleUser(${u.id})" style="margin-right:6px">${u.active ? 'Disable' : 'Enable'}</button>
          <button class="btn btn-danger" onclick="deleteUser(${u.id})">Delete</button>
        </td>
      </tr>`).join('')}</tbody>
    </table>`;
}

async function addUser() {
  const uid = document.getElementById('new-uid').value.trim();
  const name = document.getElementById('new-name').value.trim();
  if (!uid || !name) { alert('Fill in both fields'); return; }
  await fetch(API + '/api/users', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({uid, name}) });
  document.getElementById('new-uid').value = '';
  document.getElementById('new-name').value = '';
  loadUsers();
}

async function deleteUser(id) {
  if (!confirm('Delete this user?')) return;
  await fetch(API + '/api/users/' + id, {method: 'DELETE'});
  loadUsers();
}

async function toggleUser(id) {
  await fetch(API + '/api/users/' + id + '/toggle', {method: 'POST'});
  loadUsers();
}

loadDashboard();