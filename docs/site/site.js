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
