// LemonLime Online — page bootstrap & API client

const MAX_SOURCE_BYTES = 64 * 1024;

async function fetchTasks() {
  const r = await fetch('/api/tasks', { credentials: 'same-origin' });
  if (r.status === 401) { location.href = '/login'; return null; }
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

function fmtTime(iso) {
  if (!iso) return '—';
  const d = new Date(iso);
  if (isNaN(d)) return iso;
  const pad = n => String(n).padStart(2, '0');
  return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}

function fmtDuration(secs) {
  if (secs < 0) secs = 0;
  const pad = n => String(n).padStart(2, '0');
  const h = Math.floor(secs / 3600);
  const m = Math.floor((secs % 3600) / 60);
  const s = Math.floor(secs % 60);
  return pad(h) + ':' + pad(m) + ':' + pad(s);
}

// Computes contest time state with a fixed server-clock offset.
// offset = (server time at response) - (local time at response)
// Compute it ONCE at API response time; reusing on every tick froze the clock.
function contestTimeState(data, offsetMs) {
  if (!data || !data.windowEnabled) return { state: 'disabled' };
  const now = Date.now() + (offsetMs || 0);
  const start = data.startTime ? new Date(data.startTime).getTime() : null;
  const end = data.endTime ? new Date(data.endTime).getTime() : null;
  if (start && now < start) return { state: 'pre', secs: Math.floor((start - now) / 1000) };
  if (end && now > end) return { state: 'ended', secs: 0 };
  if (end) return { state: 'running', secs: Math.floor((end - now) / 1000) };
  return { state: 'running', secs: 0 };
}

function renderCountdown(el, data, onTick) {
  if (!el) return;
  if (!data.windowEnabled) { el.hidden = true; return; }
  el.hidden = false;
  // freeze offset at first call
  const serverNow = data.serverNow ? new Date(data.serverNow).getTime() : Date.now();
  const offsetMs = serverNow - Date.now();
  function tick() {
    const s = contestTimeState(data, offsetMs);
    el.classList.remove('is-pre', 'is-warning', 'is-danger', 'is-ended');
    if (s.state === 'pre') {
      el.classList.add('is-pre');
      el.textContent = '距离开始 ' + fmtDuration(s.secs);
    } else if (s.state === 'ended') {
      el.classList.add('is-ended');
      el.textContent = '比赛已结束';
    } else {
      if (s.secs <= 5 * 60) el.classList.add('is-danger');
      else if (s.secs <= 30 * 60) el.classList.add('is-warning');
      el.textContent = '剩余 ' + fmtDuration(s.secs);
    }
    if (onTick) onTick(s);
  }
  tick();
  setInterval(tick, 1000);
}

function showToast(msg, isError) {
  const t = document.getElementById('toast');
  if (!t) { console.log(msg); return; }
  t.textContent = msg;
  t.classList.toggle('is-error', !!isError);
  t.hidden = false;
  setTimeout(() => { t.hidden = true; }, 2500);
}

async function renderIndex() {
  try {
    const data = await fetchTasks();
    if (!data) return;
    document.getElementById('user').textContent = data.displayName || data.user;
    document.getElementById('contestTitle').textContent = data.contestTitle || '(未命名比赛)';
    if (data.hasStatement) {
      const s = document.getElementById('statementLink');
      if (s) s.hidden = false;
    }
    renderCountdown(document.getElementById('countdown'), data);
    const tbody = document.getElementById('taskBody');
    tbody.innerHTML = '';
    if (!data.tasks.length) {
      tbody.innerHTML = '<tr><td colspan="6" class="muted">本场比赛尚无题目</td></tr>';
      return;
    }
    data.tasks.forEach((t, idx) => {
      const tr = document.createElement('tr');
      tr.tabIndex = 0;
      tr.setAttribute('role', 'button');
      tr.setAttribute('aria-label', `进入题目 ${idx + 1} ${t.title}`);
      tr.addEventListener('click', () => { location.href = '/submit/' + t.id; });
      tr.addEventListener('keydown', e => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); tr.click(); }
      });
      const submitted = !!t.submittedAt;
      tr.innerHTML = `
        <td class="num-col">${idx + 1}</td>
        <td>${escapeHtml(t.title)}</td>
        <td class="num-col">${t.totalScore}</td>
        <td><span class="status-pill ${submitted ? 'is-submitted' : ''}">${submitted ? '已提交' : '未提交'}</span></td>
        <td class="muted">${submitted ? fmtTime(t.submittedAt) : '—'}</td>
        <td class="action-col" aria-hidden="true">→</td>
      `;
      tbody.appendChild(tr);
    });
  } catch (e) {
    showToast('加载失败：' + e.message, true);
  }
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c =>
    ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

async function renderSubmit() {
  // task id from URL
  const m = location.pathname.match(/\/submit\/(\d+)/);
  if (!m) { location.href = '/'; return; }
  const taskId = parseInt(m[1], 10);

  const data = await fetchTasks();
  if (!data) return;
  document.getElementById('user').textContent = data.displayName || data.user;
  if (data.hasStatement) {
    const s = document.getElementById('statementLink');
    if (s) s.hidden = false;
  }
  // window state: gates the submit button if outside contest window
  let outsideWindow = false;
  renderCountdown(document.getElementById('countdown'), data, s => {
    outsideWindow = (s.state === 'pre' || s.state === 'ended');
    const btn = document.getElementById('submitBtn');
    if (outsideWindow) {
      btn.disabled = true;
      btn.textContent = s.state === 'pre' ? '比赛尚未开始' : '比赛已结束';
    }
  });
  const task = data.tasks.find(t => t.id === taskId);
  if (!task) { location.href = '/'; return; }
  document.getElementById('taskNo').textContent = `第 ${taskId + 1} 题 ·`;
  document.getElementById('taskTitle').textContent = task.title;
  document.getElementById('taskScore').textContent = task.totalScore;
  document.getElementById('taskTime').textContent =
    task.timeLimitMs ? (task.timeLimitMs / 1000).toFixed(1) + 's' : '—';
  document.getElementById('taskSource').textContent = task.sourceFileName || '—';
  if (task.submittedAt)
    document.getElementById('lastSubmitted').textContent = '上次提交于 ' + fmtTime(task.submittedAt);

  const charMax = MAX_SOURCE_BYTES;
  document.getElementById('charMax').textContent = charMax;

  const ta = document.getElementById('code');
  const ln = document.getElementById('ln');
  const editor = LemonEditor.mount(ta, ln);

  // restore draft
  const draftKey = `lemon.draft.${data.user}.${taskId}`;
  const saved = localStorage.getItem(draftKey);
  if (saved) editor.set(saved);

  function refreshCount() {
    const v = editor.get();
    const bytes = new TextEncoder().encode(v).length;
    document.getElementById('charCount').textContent = bytes;
    const btn = document.getElementById('submitBtn');
    if (outsideWindow) return; // countdown hook keeps it locked
    btn.disabled = bytes === 0 || bytes > charMax;
  }
  editor.onChange(v => {
    localStorage.setItem(draftKey, v);
    refreshCount();
  });
  refreshCount();

  document.getElementById('fontSize').addEventListener('change', e => {
    editor.setFontSize(parseInt(e.target.value, 10));
  });

  let submitCount = 0;
  if (task.submittedAt) submitCount = 1;

  async function doSubmit() {
    const btn = document.getElementById('submitBtn');
    if (btn.disabled) return;
    if (submitCount >= 1) {
      const ok = confirm(
        '这是第 ' + (submitCount + 1) + ' 次提交此题，' +
        '提交后将覆盖上一次代码（评测以最后一次为准）。确定继续？'
      );
      if (!ok) return;
    }
    btn.disabled = true;
    const orig = btn.textContent;
    btn.textContent = '提交中…';
    try {
      const r = await fetch('/api/submit/' + taskId, {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          source: editor.get(),
          language: document.getElementById('lang').value,
        }),
      });
      if (r.status === 401) { location.href = '/login'; return; }
      const j = await r.json();
      if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
      submitCount++;
      document.getElementById('lastSubmitted').textContent =
        '上次提交于 ' + fmtTime(j.submittedAt);
      showToast('提交成功 · ' + fmtTime(j.submittedAt));
    } catch (e) {
      showToast('提交失败：' + e.message, true);
    } finally {
      btn.disabled = false;
      btn.textContent = orig;
      refreshCount();
    }
  }

  document.getElementById('submitBtn').addEventListener('click', doSubmit);
  document.addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      doSubmit();
    }
  });
}
