// Local layout interactions only; no MQTT or configuration persistence.
const stage = document.querySelector('.data-stage');
const separator = document.querySelector('.resizer');
let opener;
function closePanels() {
  document.querySelectorAll('.overlay').forEach(panel => { panel.hidden = true; });
  (opener?.closest('.overlay') ? document.querySelector('[data-open=connections]') : opener)?.focus();
}
document.addEventListener('click', event => {
  const trigger = event.target.closest('button[data-open],button[data-close],button[data-focus]');
  if (!trigger) return;
  if (trigger.hasAttribute('data-close')) return closePanels();
  if (trigger.dataset.open) {
    closePanels();
    opener = trigger;
    const panel = document.getElementById(trigger.dataset.open);
    panel.hidden = false;
    panel.querySelector('[data-close]').focus();
  }
  if (trigger.dataset.focus) {
    const expand = document.body.dataset.focus !== trigger.dataset.focus;
    delete document.body.dataset.focus;
    document.querySelectorAll('[data-focus]').forEach(button => {
      button.textContent = '⛶ 展开' + (button.dataset.focus === 'receive' ? '接收区' : '发送区');
    });
    if (expand) {
      document.body.dataset.focus = trigger.dataset.focus;
      trigger.textContent = '⛶ 恢复上下布局';
    }
  }
});
document.addEventListener('keydown', event => {
  const panel = document.querySelector('.overlay:not([hidden])');
  if (!panel) return;
  if (event.key === 'Escape') closePanels();
  if (event.key === 'Tab') {
    const controls = [...panel.querySelectorAll('button:not([disabled]),input,a[href],textarea,select')];
    const first = controls[0], last = controls.at(-1);
    if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
    if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
  }
});
function resize(value) {
  const percent = Math.round(Math.max(30, Math.min(70, value)));
  stage.style.setProperty('--receive-size', percent + '%');
  separator.setAttribute('aria-valuenow', percent);
}
separator.addEventListener('pointerdown', event => { separator.setPointerCapture(event.pointerId); });
separator.addEventListener('pointermove', event => {
  if (!separator.hasPointerCapture(event.pointerId)) return;
  const bounds = stage.getBoundingClientRect();
  resize((event.clientY - bounds.top) / bounds.height * 100);
});
separator.addEventListener('pointerup', event => { separator.releasePointerCapture(event.pointerId); });
separator.addEventListener('dblclick', () => resize(56));
separator.addEventListener('keydown', event => {
  if (!['ArrowUp', 'ArrowDown', 'Home'].includes(event.key)) return;
  event.preventDefault();
  resize(event.key === 'Home' ? 56 : Number(separator.getAttribute('aria-valuenow')) + (event.key === 'ArrowUp' ? -3 : 3));
});
