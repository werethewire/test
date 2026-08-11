/* app.js — 落下・堆積／消滅・色分け・トリガー */
(function () {
  'use strict';

  // 失敗を画面に出す。無言で止まると原因が追えないため最初に仕掛ける。
  function showFatal(msg) {
    const s = document.getElementById('status');
    if (s) { s.textContent = msg; s.className = 'status err'; }
    let b = document.getElementById('fatal');
    if (!b) { b = document.createElement('div'); b.id = 'fatal'; document.body.appendChild(b); }
    b.textContent = msg;
  }
  window.addEventListener('error', e => {
    showFatal('エラー: ' + (e.message || e.error) + ' @ ' + (e.filename || '?').split('/').pop() + ':' + e.lineno);
  });
  window.addEventListener('unhandledrejection', e => {
    showFatal('エラー(非同期): ' + (e.reason && e.reason.message ? e.reason.message : e.reason));
  });

  const cv = document.getElementById('stage');
  const ctx = cv && cv.getContext ? cv.getContext('2d') : null;
  if (!ctx) {
    showFatal('canvas 2d コンテキストを取得できません。Brave のシールドでフィンガープリント対策が「強力」になっていると canvas が制限されることがあります。');
    return;
  }

  const COLORS = {
    positive: { fill: '#4ee6a8', glow: 'rgba(78,230,168,.55)' },
    negative: { fill: '#ff4d6d', glow: 'rgba(255,77,109,.55)' },
    neutral:  { fill: '#8a93a6', glow: 'rgba(138,147,166,.35)' }
  };

  const DEFAULT_FAMILY = '"Noto Sans JP","Yu Gothic UI","Hiragino Kaku Gothic ProN","Meiryo",system-ui,sans-serif';
  const COL_W = 4;               // 堆積ハイトマップの分解能(px)
  const FADE_SEC = 0.4;          // 消える語のフェード時間

  const cfg = {
    speed: 140,
    spawnMin: 200,
    spawnMax: 1100,
    sizeMin: 18,
    sizeMax: 64,
    threshold: 20,
    showNeutral: true,
    autoReset: true,
    maxLanded: 800,
    fontFamily: DEFAULT_FAMILY,
    fontWeight: 600,
    // 画面下に着いたときの振る舞い:
    //   'pile'   = 積み上がる（先に積まれた語の上に乗る）
    //   'bottom' = 画面下の一行に重なって残る
    //   'vanish' = 消える
    behavior: { negative: 'pile', positive: 'vanish', neutral: 'vanish' }
  };

  const sim = {
    running: false,
    paused: false,
    falling: [],
    landed: [],
    fading: [],
    counts: { positive: 0, negative: 0, neutral: 0 },
    reachedNegative: 0,          // 画面下に到達したネガティブ語（トリガーの対象）
    nextSpawn: 0,
    pileFull: false,
    fired: false,
    lastT: 0,
    skip: null,
    lastSpawnAt: 0,
    warned: false
  };

  let W = 0, H = 0, DPR = 1;
  let heightmap = null;

  // ---- キャンバス ---------------------------------------------------------
  function resize() {
    DPR = Math.min(window.devicePixelRatio || 1, 2);
    const newW = cv.clientWidth, newH = cv.clientHeight;
    const dy = H ? (newH - H) : 0;
    W = newW; H = newH;
    cv.width = Math.max(1, Math.round(W * DPR));
    cv.height = Math.max(1, Math.round(H * DPR));
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);

    for (const w of sim.landed) {
      if (w.mode === 'pile') w.y += dy;         // 堆積は下端基準で平行移動
      else w.y = H - w.h;                       // 下段の語は下端に貼り付く
      if (w.x + w.w > W) w.x = Math.max(0, W - w.w);
    }
    rebuildHeightmap();
  }

  // ---- 堆積のハイトマップ -------------------------------------------------
  function rebuildHeightmap() {
    const cols = Math.max(1, Math.ceil(W / COL_W));
    heightmap = new Float32Array(cols).fill(H);
    for (const w of sim.landed) if (w.mode === 'pile') stamp(w);
  }

  function colRange(w) {
    const c0 = Math.max(0, Math.floor(w.x / COL_W));
    const c1 = Math.min(heightmap.length - 1, Math.ceil((w.x + w.w) / COL_W) - 1);
    return [c0, Math.max(c0, c1)];
  }

  function surfaceY(w) {
    const [c0, c1] = colRange(w);
    let y = H;
    for (let c = c0; c <= c1; c++) if (heightmap[c] < y) y = heightmap[c];
    return y;
  }

  function stamp(w) {
    const [c0, c1] = colRange(w);
    for (let c = c0; c <= c1; c++) if (w.y < heightmap[c]) heightmap[c] = w.y;
  }

  // ---- 単語生成 -----------------------------------------------------------
  function fontFor(size) {
    return cfg.fontWeight + ' ' + size.toFixed(1) + 'px ' + cfg.fontFamily;
  }

  function measure(w) {
    ctx.font = fontFor(w.size);
    w.pad = w.size * 0.18;
    w.w = ctx.measureText(w.text).width + w.pad * 2;
    w.h = w.size * 1.18;
  }

  // フォントを変えると既存の語の幅が変わるので測り直す
  function remeasureAll() {
    for (const w of sim.falling) { measure(w); if (w.x + w.w > W) w.x = Math.max(0, W - w.w); }
    for (const w of sim.landed) {
      measure(w);
      if (w.x + w.w > W) w.x = Math.max(0, W - w.w);
      if (w.mode !== 'pile') w.y = H - w.h;
    }
    rebuildHeightmap();
  }

  function spawn() {
    const text = window.WordSource.take();
    if (!text) {
      const mode = window.WordSource.state.mode;
      sim.skip = mode === 'x'
        ? 'X から語を取得できていません（x_proxy.py の起動とトークンを確認、または「接続を診断」）'
        : mode === 'mastodon'
          ? 'Mastodon から語を取得できていません（インスタンス名を確認してください）'
          : '単語ソースが空です（テキストを取り込むか、ソースを「内蔵」に戻してください）';
      return;
    }

    const label = window.Sentiment.classify(text);
    if (label === 'neutral' && !cfg.showNeutral) { sim.skip = '中立語のみ生成されています（「ニュートラル語も表示」を入れてください）'; return; }

    const size = cfg.sizeMin + Math.random() * Math.max(0, cfg.sizeMax - cfg.sizeMin);
    const w = { text, label, size, vk: 0.65 + Math.random() * 0.8, x: 0, y: 0, w: 0, h: 0, pad: 0, mode: null };
    measure(w);
    if (w.w > W) { sim.skip = '語が画面幅より広く捨てられています（文字サイズ最大を下げてください）'; return; }

    w.x = Math.random() * (W - w.w);
    w.y = -w.h;

    sim.skip = null;
    sim.lastSpawnAt = performance.now();
    sim.falling.push(w);
    sim.counts[label]++;
  }

  function scheduleSpawn(now) {
    sim.nextSpawn = now + cfg.spawnMin + Math.random() * Math.max(0, cfg.spawnMax - cfg.spawnMin);
  }

  // ---- 更新 ---------------------------------------------------------------
  function update(dt, now) {
    if (!sim.pileFull && now >= sim.nextSpawn) {
      spawn();
      scheduleSpawn(now);
    }

    // 3 秒以上 1 語も出ていなければ理由を出す（無言で止まらないように）
    if (!sim.pileFull && now - sim.lastSpawnAt > 3000 && sim.falling.length === 0) {
      if (!sim.warned) { setStatus(sim.skip || '単語が生成されていません', 'warn'); sim.warned = true; }
    } else {
      sim.warned = false;
    }

    for (let i = sim.falling.length - 1; i >= 0; i--) {
      const w = sim.falling[i];
      w.y += cfg.speed * w.vk * dt;

      const mode = cfg.behavior[w.label];
      // 積み上がる語は堆積の表面で止まる。それ以外は画面下端まで落ちる。
      const stopAt = mode === 'pile' ? surfaceY(w) : H;
      if (w.y + w.h < stopAt) continue;

      w.y = stopAt - w.h;
      w.mode = mode;
      sim.falling.splice(i, 1);

      if (w.label === 'negative') {
        sim.reachedNegative++;
        checkTrigger();
      }

      if (mode === 'vanish') {
        w.life = FADE_SEC;
        sim.fading.push(w);
        continue;
      }

      sim.landed.push(w);
      if (mode === 'pile') {
        stamp(w);
        if (w.y <= 0) {
          sim.pileFull = true;
          setStatus('堆積が画面上端に達しました。「堆積をクリア」で再開できます', 'warn');
        }
      } else if (sim.landed.length > cfg.maxLanded) {
        sim.landed.shift();
      }
    }

    for (let i = sim.fading.length - 1; i >= 0; i--) {
      const w = sim.fading[i];
      w.life -= dt;
      w.y += cfg.speed * w.vk * dt * 0.35;      // 沈みながら薄くなる
      if (w.life <= 0) sim.fading.splice(i, 1);
    }
  }

  // ---- 描画 ---------------------------------------------------------------
  function drawWord(w, alpha) {
    ctx.globalAlpha = alpha;
    ctx.font = fontFor(w.size);
    ctx.fillStyle = COLORS[w.label].fill;
    ctx.fillText(w.text, w.x + w.pad, w.y + w.h * 0.06);
  }

  function render() {
    ctx.clearRect(0, 0, W, H);
    ctx.textBaseline = 'top';
    ctx.shadowBlur = 0;

    for (const w of sim.landed) drawWord(w, 0.82);
    for (const w of sim.fading) drawWord(w, Math.max(0, w.life / FADE_SEC) * 0.9);

    ctx.globalAlpha = 1;
    for (const w of sim.falling) {
      const c = COLORS[w.label];
      ctx.font = fontFor(w.size);
      ctx.shadowColor = c.glow;
      ctx.shadowBlur = Math.min(28, w.size * 0.5);
      ctx.fillStyle = c.fill;
      ctx.fillText(w.text, w.x + w.pad, w.y + w.h * 0.06);
    }
    ctx.shadowBlur = 0;
    ctx.globalAlpha = 1;
  }

  // ---- ループ -------------------------------------------------------------
  function frame(t) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.05, (t - sim.lastT) / 1000 || 0);
    sim.lastT = t;
    if (sim.running && !sim.paused) update(dt, t);
    render();
    updateHUD();
  }

  // ---- HUD ----------------------------------------------------------------
  const el = id => document.getElementById(id);

  function updateHUD() {
    el('cNeg').textContent = sim.reachedNegative;
    el('cNegTotal').textContent = sim.counts.negative;
    el('cPos').textContent = sim.counts.positive;
    el('cNeu').textContent = sim.counts.neutral;
    el('cAir').textContent = sim.falling.length;
    el('cPile').textContent = sim.landed.length;
    const p = Math.min(1, sim.reachedNegative / Math.max(1, cfg.threshold));
    el('gaugeFill').style.width = (p * 100).toFixed(1) + '%';
    el('gaugeLabel').textContent = sim.reachedNegative + ' / ' + cfg.threshold;
  }

  function setStatus(msg, kind) {
    const s = el('status');
    s.textContent = msg;
    s.className = 'status ' + (kind || 'info');
  }

  // ---- トリガー -----------------------------------------------------------
  const media = { videoURL: null, audioURL: null };
  let playBlocked = false;

  // ブラウザの自動再生制限対策。ユーザー操作の最中に一度 play/pause して解錠しておく。
  function primeMedia() {
    const v = el('trigVideo'), a = el('trigAudio');
    if (media.videoURL && v.src !== media.videoURL) {
      v.src = media.videoURL;
      v.muted = true;
      v.play().then(() => { v.pause(); v.currentTime = 0; v.muted = false; }).catch(() => { v.muted = false; });
    }
    if (media.audioURL && a.src !== media.audioURL) {
      a.src = media.audioURL;
      a.play().then(() => { a.pause(); a.currentTime = 0; }).catch(() => {});
    }
    try {
      actx = actx || new (window.AudioContext || window.webkitAudioContext)();
      if (actx.state === 'suspended') actx.resume();
    } catch (e) { /* 音が出せない環境なら無視 */ }
  }

  function checkTrigger() {
    if (sim.fired) return;
    if (sim.reachedNegative < cfg.threshold) return;
    sim.fired = true;
    fire();
  }

  function fire() {
    sim.paused = true;
    const overlay = el('overlay'), video = el('trigVideo'), audio = el('trigAudio');
    overlay.hidden = false;
    overlay.classList.add('on');

    let pending = 0;
    playBlocked = false;
    const done = () => { if (--pending <= 0) endTrigger(); };
    const blocked = () => {
      playBlocked = true;
      el('overlayHint').textContent = 'ブラウザが自動再生を止めました — クリック / タップで再生';
    };

    if (media.videoURL) {
      video.hidden = false;
      if (video.src !== media.videoURL) video.src = media.videoURL;
      video.currentTime = 0;
      pending++;
      video.onended = done;
      video.play().catch(blocked);
    } else {
      video.hidden = true;
    }

    if (media.audioURL) {
      if (audio.src !== media.audioURL) audio.src = media.audioURL;
      audio.currentTime = 0;
      pending++;
      audio.onended = done;
      audio.play().catch(blocked);
    }

    if (!pending) {
      overlay.classList.add('flash');
      beep();
      setTimeout(endTrigger, 2500);
    }
    setStatus('トリガー発火: ネガティブ ' + sim.reachedNegative + ' 個', 'err');
  }

  function endTrigger() {
    const overlay = el('overlay'), video = el('trigVideo'), audio = el('trigAudio');
    video.pause(); audio.pause();
    video.onended = null; audio.onended = null;
    overlay.classList.remove('on', 'flash');
    overlay.hidden = true;
    playBlocked = false;
    el('overlayHint').textContent = 'クリック / Esc で閉じる';
    sim.paused = false;
    if (cfg.autoReset) {
      sim.reachedNegative = 0;
      clearPile();
    }
    sim.fired = false;
  }

  let actx = null;
  function beep() {
    try {
      actx = actx || new (window.AudioContext || window.webkitAudioContext)();
      const t0 = actx.currentTime;
      [0, 0.25, 0.5].forEach((off, i) => {
        const o = actx.createOscillator(), g = actx.createGain();
        o.type = 'square';
        o.frequency.value = 180 - i * 30;
        g.gain.setValueAtTime(0.0001, t0 + off);
        g.gain.exponentialRampToValueAtTime(0.25, t0 + off + 0.02);
        g.gain.exponentialRampToValueAtTime(0.0001, t0 + off + 0.22);
        o.connect(g).connect(actx.destination);
        o.start(t0 + off); o.stop(t0 + off + 0.25);
      });
    } catch (e) { /* 音が出せない環境なら無視 */ }
  }

  // ---- 操作 ---------------------------------------------------------------
  function clearPile() {
    sim.landed.length = 0;
    sim.fading.length = 0;
    sim.pileFull = false;
    rebuildHeightmap();
  }

  function resetAll() {
    sim.falling.length = 0;
    clearPile();
    sim.counts = { positive: 0, negative: 0, neutral: 0 };
    sim.reachedNegative = 0;
    sim.fired = false;
    setStatus('リセットしました', 'info');
  }

  function bindRange(id, key, fmt) {
    const input = el(id), out = el(id + 'Out');
    const apply = () => {
      cfg[key] = parseFloat(input.value);
      out.textContent = fmt ? fmt(cfg[key]) : input.value;
    };
    input.addEventListener('input', apply);
    apply();
  }

  // ---- フォント -----------------------------------------------------------
  function applyFont() {
    const sel = el('fontFamily').value;
    const custom = el('fontCustom').value.trim();
    cfg.fontFamily = sel === '__custom__' ? (custom || DEFAULT_FAMILY) : sel;
    cfg.fontWeight = parseInt(el('fontWeight').value, 10) || 600;
    el('fontCustom').disabled = sel !== '__custom__';
    remeasureAll();
  }

  async function loadFontFile(file) {
    if (!file) return;
    try {
      const buf = await file.arrayBuffer();
      const face = new FontFace('WordRainCustom', buf);
      await face.load();
      document.fonts.add(face);
      const opt = el('optFontFile');
      opt.textContent = '読み込んだファイル: ' + file.name;
      opt.hidden = false;
      el('fontFamily').value = '"WordRainCustom"';
      applyFont();
      setStatus('フォントを読み込みました: ' + file.name, 'ok');
    } catch (e) {
      setStatus('フォントを読み込めません: ' + e.message, 'err');
    }
  }

  // ---- 初期化 -------------------------------------------------------------
  function init() {
    if (!window.Sentiment || !window.WordSource) {
      showFatal('sentiment.js / wordsource.js が読み込めていません。3 つの .js が index.html と同じフォルダにあるか確認してください。');
      return;
    }
    resize();
    window.addEventListener('resize', resize);

    bindRange('speed', 'speed', v => v + ' px/s');
    bindRange('spawnMin', 'spawnMin', v => v + ' ms');
    bindRange('spawnMax', 'spawnMax', v => v + ' ms');
    bindRange('sizeMin', 'sizeMin', v => v + ' px');
    bindRange('sizeMax', 'sizeMax', v => v + ' px');
    bindRange('threshold', 'threshold', v => v + ' 個');

    el('showNeutral').addEventListener('change', e => { cfg.showNeutral = e.target.checked; });
    el('autoReset').addEventListener('change', e => { cfg.autoReset = e.target.checked; });

    ['negative', 'positive', 'neutral'].forEach(k => {
      const s = el('beh_' + k);
      s.value = cfg.behavior[k];
      s.addEventListener('change', () => { cfg.behavior[k] = s.value; });
    });

    // 判定の感度と、その場で語を試す欄
    const LABEL_JA = { negative: 'ネガティブ', positive: 'ポジティブ', neutral: '中立' };
    const runProbe = () => {
      const w = el('probe').value.trim();
      const out = el('probeOut');
      if (!w) { out.textContent = '—'; out.className = 'note'; return; }
      const label = window.Sentiment.classify(w);
      out.textContent = w + ' → ' + LABEL_JA[label];
      out.className = 'note ' + label;
    };
    el('sensitivity').value = window.Sentiment.sensitivity;
    el('sensitivity').addEventListener('change', e => {
      window.Sentiment.setSensitivity(e.target.value);
      runProbe();
      setStatus('判定の感度: ' + e.target.options[e.target.selectedIndex].text, 'info');
    });
    el('probe').addEventListener('input', runProbe);

    el('fontFamily').addEventListener('change', applyFont);
    el('fontCustom').addEventListener('input', applyFont);
    el('fontWeight').addEventListener('change', applyFont);
    el('fontFile').addEventListener('change', e => loadFontFile(e.target.files[0]));
    applyFont();

    el('btnStart').addEventListener('click', () => {
      primeMedia();                    // ユーザー操作のうちに自動再生を解錠しておく
      sim.running = !sim.running;
      el('btnStart').textContent = sim.running ? '停止' : '開始';
      el('btnStart').classList.toggle('on', sim.running);
      if (sim.running) {
        const now = performance.now();
        scheduleSpawn(now);
        sim.lastSpawnAt = now;
        sim.warned = false;
      }
    });
    el('btnClear').addEventListener('click', clearPile);
    el('btnReset').addEventListener('click', resetAll);
    el('btnTest').addEventListener('click', () => { sim.fired = true; fire(); });

    // 単語ソース
    document.querySelectorAll('input[name="src"]').forEach(r => {
      r.addEventListener('change', () => {
        const mode = document.querySelector('input[name="src"]:checked').value;
        window.WordSource.setMode(mode);
        el('paneText').hidden = mode !== 'text';
        el('paneX').hidden = mode !== 'x';
        el('paneMastodon').hidden = mode !== 'mastodon';
        setStatus('ソース: ' + mode, 'info');
        if (mode === 'x') window.WordSource.fetchX(true);
        if (mode === 'mastodon') window.WordSource.fetchMastodon(true);
      });
    });
    el('btnLoadText').addEventListener('click', () => window.WordSource.setPastedText(el('pasteText').value));

    const applyX = () => window.WordSource.setXConfig(el('xProxy').value, el('xQuery').value);
    el('xProxy').addEventListener('change', applyX);
    el('xQuery').addEventListener('change', applyX);
    el('btnFetchX').addEventListener('click', () => { applyX(); window.WordSource.fetchX(true); });
    el('btnDiagX').addEventListener('click', () => { applyX(); window.WordSource.diagX(); });
    applyX();

    // 公開ホスト上で開かれている場合、X はその閲覧者自身の PC でプロキシを動かす必要がある
    if (!/^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname) && location.protocol !== 'file:') {
      const n = document.createElement('div');
      n.className = 'note warn';
      n.textContent = 'このページは ' + location.hostname + ' から配信されています。X ソースは閲覧者自身の PC で x_proxy.py を起動している場合だけ使えます。';
      el('paneX').appendChild(n);
    }

    const applyMasto = () => window.WordSource.setMastodonHost(el('mHost').value);
    el('mHost').addEventListener('change', applyMasto);
    el('btnFetchM').addEventListener('click', () => { applyMasto(); window.WordSource.fetchMastodon(true); });
    applyMasto();

    // メディア
    el('videoFile').addEventListener('change', e => {
      const f = e.target.files[0];
      if (media.videoURL) URL.revokeObjectURL(media.videoURL);
      media.videoURL = f ? URL.createObjectURL(f) : null;
      el('videoName').textContent = f ? f.name : '未選択';
      primeMedia();
    });
    el('audioFile').addEventListener('change', e => {
      const f = e.target.files[0];
      if (media.audioURL) URL.revokeObjectURL(media.audioURL);
      media.audioURL = f ? URL.createObjectURL(f) : null;
      el('audioName').textContent = f ? f.name : '未選択';
      primeMedia();
    });

    // 自動再生が止められていた場合、最初のクリックは「閉じる」ではなく「再生」に使う
    el('overlay').addEventListener('click', () => {
      if (playBlocked) {
        playBlocked = false;
        el('overlayHint').textContent = 'クリック / Esc で閉じる';
        if (media.videoURL) el('trigVideo').play().catch(() => {});
        if (media.audioURL) el('trigAudio').play().catch(() => {});
        return;
      }
      endTrigger();
    });
    window.addEventListener('keydown', e => {
      if (e.key === 'Escape' && !el('overlay').hidden) endTrigger();
      if ((e.key === 'h' || e.key === 'H') && !/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) {
        el('panel').classList.toggle('hidden');
      }
    });
    el('btnToggle').addEventListener('click', () => el('panel').classList.toggle('hidden'));

    // 外部から触れるフック（デバッグ・自動化用）
    window.WordRain = { sim, cfg, spawn, update, render, clearPile, resetAll, fire, endTrigger, setStatus, remeasureAll, rebuildHeightmap };

    window.WordSource.onStatus = setStatus;
    setStatus('内蔵ワードプールで動作中。「開始」を押してください（H でパネル開閉）', 'info');
    requestAnimationFrame(t => { sim.lastT = t; requestAnimationFrame(frame); });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
