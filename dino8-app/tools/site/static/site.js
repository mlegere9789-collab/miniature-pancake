// Filters the plug-in cards by the text in #q. No network, no storage, no dependencies.
(function () {
  var q = document.getElementById('q');
  var count = document.getElementById('count');
  var cards = document.querySelectorAll('#list .card');
  if (!q || !count) return;
  function apply() {
    var needle = q.value.trim().toLowerCase();
    var shown = 0;
    cards.forEach(function (c) {
      var hit = needle === '' || (c.getAttribute('data-search') || '').indexOf(needle) !== -1;
      c.hidden = !hit;
      if (hit) shown++;
    });
    count.textContent = shown + ' of ' + cards.length + ' plug-in(s)';
  }
  q.addEventListener('input', apply);
})();
