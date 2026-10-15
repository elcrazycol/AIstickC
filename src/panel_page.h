// Страница веб-панели: один файл, без внешних зависимостей.
//
// Стик отдаёт её целиком, поэтому ни CDN, ни шрифтов извне — только HTML, CSS
// и немного JS. Код доступа страница берёт из своего же адреса (`?k=...`), так
// что подставлять его в текст не нужно.
//
// Обновление — опросом раз в секунду: на глаз это мгновенно, а в отличие от
// постоянного соединения не держит память и не мешает стику отвечать голосом.
// Когда вкладка уходит в фон, опрос останавливается: иначе забытая вкладка
// держала бы стик бодрым и жгла батарею.
#pragma once

namespace panel {

constexpr const char* kPanelPage = R"HTML(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<title>AIstick</title>
<style>
:root{--bg:#0b0f0c;--panel:#151b16;--line:#26302a;--text:#e6efe8;--dim:#8a9a8f;--acc:#3ddc84}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
     font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
     -webkit-text-size-adjust:100%}
header{position:sticky;top:0;z-index:2;display:flex;align-items:center;gap:8px;
       padding:12px 16px;background:rgba(11,15,12,.94);
       border-bottom:1px solid var(--line)}
header b{color:var(--acc);font-weight:600}
header span{color:var(--dim);font-size:13px;margin-left:auto}
main{padding:16px;max-width:820px;margin:0 auto}
.row{display:flex;margin:0 0 14px}
.row.u{justify-content:flex-end}
.bub{max-width:84%;padding:10px 14px;border-radius:16px;overflow-wrap:anywhere}
.u .bub{background:var(--acc);color:#06210f;border-bottom-right-radius:4px;white-space:pre-wrap}
.a .bub{background:var(--panel);border:1px solid var(--line);border-bottom-left-radius:4px}
.meta{display:flex;gap:10px;margin-top:8px}
button.copy{background:none;border:1px solid var(--line);color:var(--dim);
            border-radius:8px;padding:3px 10px;font-size:12px;cursor:pointer}
button.copy:active{color:var(--acc);border-color:var(--acc)}
code{background:#000;border:1px solid var(--line);border-radius:6px;padding:1px 5px;
     font:14px/1.4 ui-monospace,SFMono-Regular,Menlo,monospace}
pre{background:#000;border:1px solid var(--line);border-left:3px solid var(--acc);
    border-radius:8px;padding:10px 12px;overflow-x:auto;margin:8px 0}
pre code{border:0;padding:0;background:none}
.empty{color:var(--dim);text-align:center;padding:48px 0}
.asleep{position:fixed;left:0;right:0;bottom:0;padding:10px 16px;background:#3a2a10;
        color:#ffd479;font-size:13px;text-align:center}
[hidden]{display:none!important}
</style>
</head>
<body>
<header><b>AIstick</b><span id="st">подключаюсь…</span></header>
<main id="chat"><div class="empty">Пока пусто. Спроси стик — ответ появится здесь.</div></main>
<div class="asleep" id="asleep" hidden>Стик спит — нажми кнопку на нём, и панель оживёт</div>
<script>
var K = new URLSearchParams(location.search).get('k') || '';
var chat = document.getElementById('chat');
var st = document.getElementById('st');
var asleep = document.getElementById('asleep');
var rev = -1, fails = 0, timer = null;

function esc(s) {
  return s.replace(/[&<>"]/g, function (c) {
    return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];
  });
}

// Разметка простая: блоки кода, код в строке, жирный. Полный markdown тут
// незачем — ответы короткие, а весить страница должна мало.
function md(t) {
  var h = esc(t);
  h = h.replace(/```([\s\S]*?)```/g, function (m, p) {
    // Первая строка после ``` — метка языка («go», «python»), в код её не тащим.
    p = p.replace(/^[^\n]*\n/, '');
    return '<pre><code>' + p.replace(/^\n+|\n+$/g, '') + '</code></pre>';
  });
  h = h.replace(/`([^`]+)`/g, '<code>$1</code>');
  h = h.replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>');
  return h;
}

function render(msgs) {
  if (!msgs.length) {
    chat.innerHTML = '<div class="empty">Пока пусто. Спроси стик — ответ появится здесь.</div>';
    return;
  }
  var html = '';
  for (var i = 0; i < msgs.length; i++) {
    var m = msgs[i];
    var copy = m.u ? '' : '<div class="meta"><button class="copy" data-i="' + i + '">скопировать</button></div>';
    html += '<div class="row ' + (m.u ? 'u' : 'a') + '"><div class="bub">' + md(m.t) + copy + '</div></div>';
  }
  chat.innerHTML = html;
  var btns = chat.querySelectorAll('button.copy');
  for (var b = 0; b < btns.length; b++) {
    btns[b].onclick = function () {
      var self = this;
      var text = msgs[+self.dataset.i].t;
      navigator.clipboard.writeText(text).then(function () {
        self.textContent = 'скопировано';
        setTimeout(function () { self.textContent = 'скопировать'; }, 1200);
      });
    };
  }
}

// Русское склонение: 1 сообщение, 2 сообщения, 5 сообщений.
function plural(n) {
  var m10 = n % 10, m100 = n % 100;
  if (m10 === 1 && m100 !== 11) return n + ' сообщение';
  if (m10 >= 2 && m10 <= 4 && (m100 < 10 || m100 >= 20)) return n + ' сообщения';
  return n + ' сообщений';
}

function atBottom() {
  return window.innerHeight + window.scrollY > document.body.scrollHeight - 120;
}

function tick() {
  timer = null;
  fetch('/api/chat?k=' + encodeURIComponent(K), {cache: 'no-store'}).then(function (r) {
    if (r.status === 403) { st.textContent = 'код неверный'; return null; }
    return r.json();
  }).then(function (d) {
    if (!d) return;
    fails = 0;
    asleep.hidden = true;
    st.textContent = d.msgs.length ? plural(d.msgs.length) : 'пусто';
    if (d.rev !== rev) {
      var stick = atBottom();
      rev = d.rev;
      render(d.msgs);
      if (stick) window.scrollTo(0, document.body.scrollHeight);
    }
  }).catch(function () {
    // Стик ушёл в сон: Wi-Fi выключен, и запросы просто не проходят.
    if (++fails >= 3) { st.textContent = 'нет связи'; asleep.hidden = false; }
  }).then(function () {
    schedule();
  });
}

function schedule() {
  if (timer || document.hidden) return;
  timer = setTimeout(tick, 1000);
}

// В фоне не опрашиваем: иначе забытая вкладка держала бы стик бодрым.
document.addEventListener('visibilitychange', function () {
  if (!document.hidden) { fails = 0; tick(); }
});
tick();
</script>
</body>
</html>
)HTML";

}  // namespace panel
