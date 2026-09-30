// Lightweight code editor: <textarea> + line numbers + Tab handling.
// Designed for offline classroom use. Future drop-in: replace with CodeMirror 6
// by exposing the same interface (mountEditor returning {get,set,onChange}).

(function (global) {
  function mountEditor(textarea, lineEl) {
    function refreshLines() {
      const lineCount = textarea.value.split('\n').length;
      let buf = '';
      for (let i = 1; i <= lineCount; i++) buf += i + '\n';
      lineEl.textContent = buf.slice(0, -1);
    }
    function syncScroll() { lineEl.scrollTop = textarea.scrollTop; }

    textarea.addEventListener('input', () => {
      refreshLines();
      onChangeListeners.forEach(fn => fn(textarea.value));
    });
    textarea.addEventListener('scroll', syncScroll);
    textarea.addEventListener('keydown', e => {
      if (e.key === 'Tab') {
        e.preventDefault();
        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;
        // simple insert; multi-line indent left as TODO
        textarea.value =
          textarea.value.slice(0, start) + '\t' + textarea.value.slice(end);
        textarea.selectionStart = textarea.selectionEnd = start + 1;
        textarea.dispatchEvent(new Event('input'));
      }
    });

    const onChangeListeners = [];
    refreshLines();

    return {
      get: () => textarea.value,
      set: v => { textarea.value = v; refreshLines(); },
      focus: () => textarea.focus(),
      onChange: fn => { onChangeListeners.push(fn); },
      setFontSize: px => { textarea.style.fontSize = px + 'px'; lineEl.style.fontSize = px + 'px'; },
    };
  }

  global.LemonEditor = { mount: mountEditor };
})(window);
