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

// Accepts a single element or array of elements; updates each on every tick.
function renderCountdown(target, data, onTick) {
  const els = (Array.isArray(target) ? target : [target]).filter(Boolean);
  if (!els.length) return;
  if (!data.windowEnabled) { els.forEach(el => { el.hidden = true; }); return; }
  els.forEach(el => { el.hidden = false; });
  const serverNow = data.serverNow ? new Date(data.serverNow).getTime() : Date.now();
  const offsetMs = serverNow - Date.now();
  function tick() {
    const s = contestTimeState(data, offsetMs);
    let label;
    if (s.state === 'pre')        label = '距离开始 ' + fmtDuration(s.secs);
    else if (s.state === 'ended') label = '比赛已结束';
    else                          label = '剩余 ' + fmtDuration(s.secs);
    els.forEach(el => {
      el.classList.remove('is-pre', 'is-warning', 'is-danger', 'is-ended');
      if (s.state === 'pre')          el.classList.add('is-pre');
      else if (s.state === 'ended')   el.classList.add('is-ended');
      else if (s.secs <= 5 * 60)      el.classList.add('is-danger');
      else if (s.secs <= 30 * 60)     el.classList.add('is-warning');
      // for the bj inline text node, show only digits without prefix
      if (el.id === 'bjCountdownText') {
        el.textContent = (s.state === 'ended') ? '已结束' :
                         (s.state === 'pre')   ? fmtDuration(s.secs) :
                                                 fmtDuration(s.secs);
      } else {
        el.textContent = label;
      }
    });
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

// ---- Beijing theme helpers ----
function bjClockTick() {
  const el = document.getElementById('bjClock');
  if (!el) return;
  const d = new Date();
  const pad = n => String(n).padStart(2, '0');
  el.textContent = pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}
(function bootBjClock() {
  if (document.body && document.body.dataset.theme === 'beijing') {
    bjClockTick();
    setInterval(bjClockTick, 1000);
  } else {
    // body might not be ready when script first runs; defer
    document.addEventListener('DOMContentLoaded', () => {
      if (document.body.dataset.theme === 'beijing') {
        bjClockTick();
        setInterval(bjClockTick, 1000);
      }
    });
  }
})();

function bjPopulateUser(displayName) {
  const u = document.getElementById('bjUser');
  if (u) u.textContent = displayName || '—';
}

function bjUpdateStatementLink(hasStatement) {
  const link = document.getElementById('bjStatementLink');
  if (!link) return;
  if (hasStatement) {
    link.classList.remove('bj-sb-disabled');
    link.removeAttribute('aria-disabled');
  } else {
    link.classList.add('bj-sb-disabled');
    link.setAttribute('aria-disabled', 'true');
    link.addEventListener('click', e => {
      if (link.classList.contains('bj-sb-disabled')) e.preventDefault();
    }, { once: true });
  }
}

// notice modal — wired generically so it works on both index and submit
document.addEventListener('click', e => {
  const trigger = e.target.closest && e.target.closest('[data-bj-action="notice"]');
  if (trigger) {
    e.preventDefault();
    const modal = document.getElementById('noticeModal');
    if (modal) modal.classList.add('is-open');
  }
  const msgTrigger = e.target.closest && e.target.closest('[data-bj-action="messages"]');
  if (msgTrigger) {
    e.preventDefault();
    showToast('暂无未读消息');
  }
});

async function renderIndex() {
  try {
    const data = await fetchTasks();
    if (!data) return;
    document.getElementById('user').textContent = data.displayName || data.user;
    document.getElementById('contestTitle').textContent = data.contestTitle || '(未命名比赛)';
    bjPopulateUser(data.displayName || data.user);
    bjUpdateStatementLink(!!data.hasStatement);
    if (data.hasStatement) {
      const s = document.getElementById('statementLink');
      if (s) s.hidden = false;
    }
    // beijing-theme inline countdown label
    const bjLabel = document.getElementById('bjCountdownLabel');
    if (bjLabel && data.windowEnabled) bjLabel.hidden = false;

    // when contest starts, auto-reload so locked state lifts
    let reloaded = false;
    renderCountdown([
      document.getElementById('countdown'),
      document.getElementById('bjCountdownText'),
    ], data, s => {
      if (s.state === 'running' && data.preContest && !reloaded) {
        reloaded = true; location.reload();
      }
    });
    const tbody = document.getElementById('taskBody');
    tbody.innerHTML = '';
    if (data.preContest) {
      tbody.innerHTML =
        '<tr><td colspan="7" class="locked-cell">' +
        '<div class="locked-title">比赛尚未开始</div>' +
        '<div class="locked-sub">题目列表将在开始时间到达后自动出现，请耐心等待。</div>' +
        '</td></tr>';
      return;
    }
    if (!data.tasks.length) {
      tbody.innerHTML = '<tr><td colspan="7" class="muted">本场比赛尚无题目</td></tr>';
      return;
    }
    data.tasks.forEach((t, idx) => {
      const tr = document.createElement('tr');
      tr.tabIndex = 0;
      tr.setAttribute('role', 'button');
      tr.setAttribute('aria-label', `进入题目 ${idx + 1} ${t.title}`);
      tr.addEventListener('click', e => {
        if (e.target && e.target.closest('[data-view]')) return; // viewing, not navigating
        location.href = '/submit/' + t.id;
      });
      tr.addEventListener('keydown', e => {
        if (e.key === 'Enter' || e.key === ' ') {
          if (e.target && e.target.closest('[data-view]')) return;
          e.preventDefault(); tr.click();
        }
      });
      const submitted = !!t.submittedAt;
      const viewCell = submitted
        ? `<a class="row-action" data-view="${t.id}">查看</a>`
        : '<span class="muted">—</span>';
      tr.innerHTML = `
        <td class="num-col">${idx + 1}</td>
        <td>${escapeHtml(t.title)}</td>
        <td class="num-col">${t.totalScore}</td>
        <td><span class="status-pill ${submitted ? 'is-submitted' : ''}">${submitted ? '已提交' : '未提交'}</span></td>
        <td class="muted">${submitted ? fmtTime(t.submittedAt) : '—'}</td>
        <td class="view-col">${viewCell}</td>
        <td class="action-col" aria-hidden="true">→</td>
      `;
      tbody.appendChild(tr);
    });
    // wire up the view links
    tbody.querySelectorAll('[data-view]').forEach(link => {
      link.addEventListener('click', e => {
        e.preventDefault(); e.stopPropagation();
        openCodeModal(parseInt(link.dataset.view, 10));
      });
    });
  } catch (e) {
    showToast('加载失败：' + e.message, true);
  }
}

async function openCodeModal(taskId) {
  const modal = document.getElementById('codeModal');
  const titleEl = document.getElementById('codeModalTitle');
  const filenameEl = document.getElementById('codeFilename');
  const submittedEl = document.getElementById('codeSubmittedAt');
  const bytesEl = document.getElementById('codeBytes');
  const codeEl = document.getElementById('codeContent');
  if (!modal) return;

  titleEl.textContent = '查看提交代码';
  filenameEl.textContent = '加载中…';
  submittedEl.textContent = '—';
  bytesEl.textContent = '—';
  codeEl.textContent = '';
  modal.classList.add('is-open');
  document.body.style.overflow = 'hidden';

  try {
    const r = await fetch('/api/source/' + taskId, { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    if (!r.ok) {
      const j = await r.json().catch(() => ({}));
      throw new Error(j.error || ('HTTP ' + r.status));
    }
    const j = await r.json();
    filenameEl.textContent = j.filename || '—';
    submittedEl.textContent = fmtTime(j.submittedAt);
    bytesEl.textContent = j.bytes;
    codeEl.textContent = j.content || '';
  } catch (e) {
    codeEl.textContent = '';
    filenameEl.textContent = '加载失败';
    showToast('加载失败：' + e.message, true);
  }
}

function closeAnyModal(target) {
  const modals = target
    ? [target.closest('.modal')].filter(Boolean)
    : Array.from(document.querySelectorAll('.modal.is-open'));
  modals.forEach(m => m.classList.remove('is-open'));
  if (!document.querySelector('.modal.is-open'))
    document.body.style.overflow = '';
}

document.addEventListener('click', e => {
  const closer = e.target.closest && e.target.closest('[data-modal-close]');
  if (closer) closeAnyModal(closer);
});
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') closeAnyModal();
});
document.addEventListener('click', e => {
  if (e.target && e.target.id === 'codeCopyBtn') {
    const text = document.getElementById('codeContent').textContent;
    navigator.clipboard?.writeText(text).then(
      () => showToast('已复制到剪贴板'),
      err => showToast('复制失败：' + err.message, true)
    );
  }
});

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
  if (data.preContest) { location.href = '/'; return; }
  document.getElementById('user').textContent = data.displayName || data.user;
  bjPopulateUser(data.displayName || data.user);
  bjUpdateStatementLink(!!data.hasStatement);
  if (data.hasStatement) {
    const s = document.getElementById('statementLink');
    if (s) s.hidden = false;
  }
  // window state: gates the submit button if outside contest window
  let outsideWindow = false;
  // also unhide bj label if window enabled
  if (data.windowEnabled) {
    const lbl = document.getElementById('bjCountdownLabel');
    if (lbl) lbl.hidden = false;
  }
  renderCountdown([
    document.getElementById('countdown'),
    document.getElementById('bjCountdownText'),
  ], data, s => {
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

  // ----- mode (paste vs file) — server-controlled -----
  const mode = (data.submitMode === 'file') ? 'file' : 'paste';
  let uploadedSource = '';
  let uploadedName = '';
  const extToLang = {
    cpp: 'cpp', cc: 'cpp', cxx: 'cpp', cp: 'cpp',
    c: 'c', h: 'cpp',
    py: 'python',
    pas: 'pascal', pp: 'pascal',
  };

  // Render only the chosen mode
  document.getElementById('pasteMode').hidden = mode !== 'paste';
  document.getElementById('fileMode').hidden = mode !== 'file';
  document.getElementById('fontSizeField').style.display =
      mode === 'paste' ? '' : 'none';

  function getCurrentSource() {
    return mode === 'paste' ? editor.get() : uploadedSource;
  }

  function refreshCount() {
    const v = getCurrentSource();
    const bytes = new TextEncoder().encode(v).length;
    document.getElementById('charCount').textContent = bytes;
    const btn = document.getElementById('submitBtn');
    if (outsideWindow) return;
    btn.disabled = bytes === 0 || bytes > charMax;
  }
  editor.onChange(v => {
    localStorage.setItem(draftKey, v);
    if (mode === 'paste') refreshCount();
  });
  refreshCount();

  function pickLangFromExt(name) {
    const m = (name || '').toLowerCase().match(/\.([^.]+)$/);
    if (!m) return null;
    return extToLang[m[1]] || null;
  }

  async function ingestFile(file) {
    if (!file) return;
    if (file.size === 0) {
      showToast('文件为空', true);
      return;
    }
    if (file.size > charMax) {
      showToast('文件超过 ' + (charMax / 1024) + ' KB', true);
      return;
    }
    let text;
    try { text = await file.text(); }
    catch (e) { showToast('读取文件失败：' + e.message, true); return; }
    uploadedSource = text;
    uploadedName = file.name;
    document.getElementById('fileTitle').textContent = '已选择：' + file.name;
    document.getElementById('fileSub').textContent =
        file.size + ' 字节 · 点击或拖入可替换';
    document.querySelector('#fileDrop').classList.add('has-file');
    const guess = pickLangFromExt(file.name);
    if (guess) document.getElementById('lang').value = guess;
    refreshCount();
  }

  const fileInput = document.getElementById('fileInput');
  document.getElementById('filePickBtn').addEventListener('click', e => {
    e.stopPropagation();
    fileInput.click();
  });
  document.getElementById('fileDrop').addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', e => {
    if (e.target.files && e.target.files[0]) ingestFile(e.target.files[0]);
  });

  const drop = document.getElementById('fileDrop');
  ['dragenter', 'dragover'].forEach(ev =>
    drop.addEventListener(ev, e => {
      e.preventDefault(); e.stopPropagation();
      drop.classList.add('is-drag');
    })
  );
  ['dragleave', 'drop'].forEach(ev =>
    drop.addEventListener(ev, e => {
      e.preventDefault(); e.stopPropagation();
      drop.classList.remove('is-drag');
    })
  );
  drop.addEventListener('drop', e => {
    const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
    if (f) ingestFile(f);
  });

  document.getElementById('fontSize').addEventListener('change', e => {
    editor.setFontSize(parseInt(e.target.value, 10));
  });

  let submitCount = 0;
  if (task.submittedAt) submitCount = 1;

  async function doSubmit() {
    const btn = document.getElementById('submitBtn');
    if (btn.disabled) return;
    const source = getCurrentSource();
    if (!source.trim()) {
      showToast(mode === 'file' ? '请选择文件' : '请输入代码', true);
      return;
    }
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
          source: source,
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
