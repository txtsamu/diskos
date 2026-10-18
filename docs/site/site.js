'use strict';
/* Each theme owns its own preview. No autoplay, tracking or external libraries. */
document.querySelectorAll('.theme').forEach(theme => {
  const main = theme.querySelector('.main-shot');
  const image = main.querySelector('img');
  const caption = theme.querySelector('.screen-caption');
  theme.querySelectorAll('[data-screen]').forEach(button => {
    button.addEventListener('click', () => {
      const previous = {src: image.getAttribute('src'), alt: image.alt, caption: caption.textContent};
      image.src = button.dataset.screen;
      image.alt = button.querySelector('img').alt;
      caption.textContent = button.dataset.caption;
      main.href = button.dataset.screen;
      main.setAttribute('aria-label', `Enlarge ${theme.querySelector('h2').textContent} ${caption.textContent}`);
      button.dataset.screen = previous.src;
      button.dataset.caption = previous.caption;
      button.querySelector('img').src = previous.src;
      button.querySelector('img').alt = previous.alt;
      button.querySelector('span').textContent = previous.caption;
      button.setAttribute('aria-label', `Preview ${theme.querySelector('h2').textContent} ${previous.caption}`);
    });
  });
});
const dialog = document.querySelector('.screen-dialog');
if (dialog && typeof dialog.showModal === 'function') {
  document.querySelectorAll('.main-shot').forEach(link => {
    link.addEventListener('click', event => {
      event.preventDefault();
      dialog.querySelector('img').src = link.href;
      dialog.querySelector('img').alt = link.querySelector('img').alt;
      dialog.querySelector('p').textContent = `${link.closest('.theme').querySelector('h2').textContent} · ${link.closest('.theme').querySelector('.screen-caption').textContent}`;
      dialog.showModal();
    });
  });
  dialog.querySelector('button').addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
}

/* Native sticky chapters: document scrolling stays under the reader's control.
 * A tall panel scrolls completely into view before pinning. Mobile, short screens
 * and reduced motion use normal flow. No wheel/touch interception or scroll hijack. */
const chapters = [...document.querySelectorAll('main > section')].map(panel => {
  const chapter = document.createElement('div');
  chapter.className = 'chapter';
  panel.before(chapter); chapter.append(panel); panel.classList.add('chapter-panel');
  return {chapter, panel};
});
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
const rim = document.querySelector('.cd-progress');
const face = document.querySelector('.cd-face');
const progress = document.querySelector('.cd-rim');
const percentage = document.querySelector('.scroll-percent');
let scrollFrame = 0, layoutFrame = 0;
function paintProgress() {
  scrollFrame = 0;
  const range = Math.max(0, document.documentElement.scrollHeight - window.innerHeight);
  const amount = range ? Math.max(0, Math.min(1, window.scrollY / range)) : 0;
  const value = Math.round(amount * 100);
  if (rim) rim.style.strokeDashoffset = String(100 - amount * 100);
  if (face) face.style.transform = reducedMotion.matches ? 'none' : `rotate(${amount * 1080}deg)`;
  if (progress) progress.setAttribute('aria-valuenow', String(value));
  if (percentage) percentage.textContent = `${value}%`;
}
function requestProgress() { if (!scrollFrame) scrollFrame = requestAnimationFrame(paintProgress); }
function measureChapters() {
  layoutFrame = 0;
  const pin = window.innerWidth > 900 && window.innerHeight > 680 && !reducedMotion.matches;
  chapters.forEach(({chapter, panel}) => {
    const height = Math.ceil(panel.getBoundingClientRect().height);
    chapter.style.setProperty('--chapter-height', pin ? `${height + Math.round(window.innerHeight * .2)}px` : 'auto');
    panel.style.setProperty('--pin-top', `${Math.min(0, window.innerHeight - height)}px`);
  });
  requestProgress();
}
function requestLayout() { if (!layoutFrame) layoutFrame = requestAnimationFrame(measureChapters); }
window.addEventListener('scroll', requestProgress, {passive: true});
window.addEventListener('resize', requestLayout, {passive: true});
window.addEventListener('load', requestLayout, {once: true});
reducedMotion.addEventListener('change', requestLayout);
if (typeof ResizeObserver === 'function') {
  const observer = new ResizeObserver(requestLayout);
  chapters.forEach(({panel}) => observer.observe(panel));
}
if (document.fonts) document.fonts.ready.then(requestLayout);
requestLayout();
