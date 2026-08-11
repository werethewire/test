/* wordsource.js — 単語の供給源
   4 系統を同じインターフェースで扱う:
     builtin  : 内蔵ワードプール（トークン不要・即動く）
     text     : ユーザーが貼り付けたテキストを分かち書き
     x        : ローカル Python プロキシ経由で X (Twitter) の検索結果を取得
     mastodon : Mastodon の公開タイムライン（認証不要・プロキシ不要）
   window.WordSource.take() -> 単語1個（無ければ null）
*/
(function (global) {
  'use strict';

  // ---- 内蔵プール ---------------------------------------------------------
  const BUILTIN = (
    '愛 幸せ 光 夢 笑顔 希望 未来 感謝 平和 自由 音楽 花 星 海 空 虹 春 太陽 friend love hope ' +
    '死 闇 不安 孤独 絶望 怒り 涙 戦争 崩壊 恐怖 病 事故 嘘 裏切り 炎上 貧困 差別 疲れた 最悪 無理 ' +
    'hate fear pain broken lonely crisis danger stress panic error ' +
    '街 電車 珈琲 猫 犬 本 雨 雪 風 山 川 石 鉄 紙 時計 椅子 窓 扉 道 橋 ' +
    'city train coffee cat dog book rain snow wind mountain river stone iron paper clock chair window door road bridge ' +
    '美しい 優しい 楽しい 嬉しい 温かい 明るい 新しい 強い 静か 豊か ' +
    '痛い 苦しい 悲しい 汚い 冷たい 暗い 弱い 遅い 危険 不満 ' +
    '記憶 時間 沈黙 呼吸 心臓 皮膚 骨 血 影 鏡 波 砂 灰 煙 泡 ' +
    'memory time silence breath heart skin bone blood shadow mirror wave sand ash smoke'
  ).split(/\s+/).filter(Boolean);

  // ---- 分かち書き ---------------------------------------------------------
  const STOP = new Set((
    'これ それ あれ この その あの ここ そこ です ます でした ない なる する した して いる ' +
    'ある など まで から より ので のに けど しかし そして また ため よう こと もの とき ' +
    'ところ さん ちゃん くん たち という ください できる わけ ほど だけ でも ' +
    'the a an and or but if then of to in on at for with is are was were be been am ' +
    'it this that i you he she they we my your his her their our me him them us ' +
    'not no so as by from about into out up down all any some just very too rw rt'
  ).split(/\s+/));

  function acceptable(w) {
    if (!w) return false;
    w = w.trim();
    if (!w) return false;
    if (STOP.has(w.toLowerCase())) return false;
    if (/^\d+$/.test(w)) return false;                 // 数字だけ
    if (/^[\x00-\x40\x5b-\x60\x7b-\x7f]+$/.test(w)) return false; // 記号だけ
    if (/^[ぁ-ん]{1,2}$/u.test(w)) return false;       // 助詞など短いひらがな
    if (/^[a-z]{1,2}$/i.test(w)) return false;         // 1〜2文字の英字
    if (w.length > 16) return false;
    return true;
  }

  function tokenize(text) {
    if (!text) return [];
    const cleaned = String(text)
      .replace(/https?:\/\/\S+/g, ' ')
      .replace(/[@＠][A-Za-z0-9_]+/g, ' ')
      .replace(/\bRT\b:?/g, ' ')
      .replace(/&amp;|&lt;|&gt;|&quot;/g, ' ');

    const out = [];
    if (typeof Intl !== 'undefined' && Intl.Segmenter) {
      const seg = new Intl.Segmenter('ja', { granularity: 'word' });
      for (const s of seg.segment(cleaned)) {
        if (s.isWordLike) out.push(s.segment);
      }
    } else {
      out.push(...cleaned.split(/[\s、。,.!?！？「」『』()（）\[\]【】…・:：;；"'’”\/\\|~〜\-—+*=#＃%&$^<>]+/u));
    }
    return out.filter(acceptable);
  }

  // 投稿本文は HTML なのでタグを剥がす。live DOM に挿さず DOMParser で処理する。
  function htmlToText(html) {
    try {
      return new DOMParser().parseFromString(String(html), 'text/html').body.textContent || '';
    } catch (e) {
      return String(html).replace(/<[^>]+>/g, ' ');
    }
  }

  // ---- 状態 ---------------------------------------------------------------
  const state = {
    mode: 'builtin',
    buffer: [],
    pasted: [],
    x: { proxy: 'http://127.0.0.1:8787', query: 'lang:ja -is:retweet', lastFetch: 0, busy: false, cooldownMs: 20000 },
    mastodon: { host: 'mstdn.jp', lastFetch: 0, busy: false, cooldownMs: 10000, seen: new Set() },
    onStatus: null
  };

  function report(msg, kind) {
    if (state.onStatus) state.onStatus(msg, kind || 'info');
  }
  const status = report;   // ソースに紐付かない通知はそのまま出す

  function shuffleInto(words) {
    for (let i = words.length - 1; i > 0; i--) {
      const j = (Math.random() * (i + 1)) | 0;
      [words[i], words[j]] = [words[j], words[i]];
    }
    state.buffer.push(...words);
    if (state.buffer.length > 4000) state.buffer.splice(0, state.buffer.length - 4000);
  }

  function setMode(mode) {
    state.mode = mode;
    state.buffer.length = 0;
  }

  function setPastedText(text) {
    state.pasted = tokenize(text);
    state.buffer.length = 0;
    status(state.pasted.length + ' 語を取り込みました', state.pasted.length ? 'ok' : 'warn');
    return state.pasted.length;
  }

  function setXConfig(proxy, query) {
    if (proxy) state.x.proxy = proxy.replace(/\/+$/, '');
    if (query !== undefined) state.x.query = query;
  }

  function setMastodonHost(host) {
    if (host) state.mastodon.host = String(host).trim().replace(/^https?:\/\//, '').replace(/\/+$/, '');
  }

  // ---- X ------------------------------------------------------------------
  async function fetchX(force) {
    if (state.x.busy) return;
    const now = Date.now();
    if (!force && now - state.x.lastFetch < state.x.cooldownMs) return;
    state.x.busy = true;
    state.x.lastFetch = now;
    // 応答が返る前にソースを切り替えられていたら、古い結果でステータスを上書きしない
    const status = (msg, kind) => { if (state.mode === 'x') report(msg, kind); };
    try {
      const url = state.x.proxy + '/search?q=' + encodeURIComponent(state.x.query) + '&max_results=100';
      const res = await fetch(url, { cache: 'no-store' });
      const body = await res.json().catch(() => null);
      if (!res.ok) {
        const msg = (body && (body.hint || body.error)) || ('HTTP ' + res.status);
        status('X: ' + msg, 'err');
        return;
      }
      const words = [];
      for (const t of (body.texts || [])) words.push(...tokenize(t));
      if (!words.length) { status('X: 語を抽出できませんでした（クエリを緩めてください）', 'warn'); return; }
      shuffleInto(words);
      status('X から ' + (body.texts || []).length + ' 件 / ' + words.length + ' 語を取得', 'ok');
    } catch (e) {
      status('X: プロキシに接続できません。x_proxy.py を起動してください', 'err');
    } finally {
      state.x.busy = false;
    }
  }

  // プロキシ側に診断させ、原因をそのまま表示する
  async function diagX() {
    try {
      const res = await fetch(state.x.proxy + '/diag', { cache: 'no-store' });
      const d = await res.json();
      status(d.verdict, d.ok ? 'ok' : 'err');
      return d;
    } catch (e) {
      status('診断できません: ' + state.x.proxy + ' に接続できません。x_proxy.py を起動してください', 'err');
      return null;
    }
  }

  // ---- Mastodon（認証不要・プロキシ不要） ---------------------------------
  async function fetchMastodon(force) {
    const m = state.mastodon;
    if (m.busy) return;
    const now = Date.now();
    if (!force && now - m.lastFetch < m.cooldownMs) return;
    m.busy = true;
    m.lastFetch = now;
    // 応答が返る前にソースを切り替えられていたら、古い結果でステータスを上書きしない
    const status = (msg, kind) => { if (state.mode === 'mastodon') report(msg, kind); };
    try {
      const url = 'https://' + m.host + '/api/v1/timelines/public?limit=40&local=true';
      const res = await fetch(url, { cache: 'no-store' });
      if (!res.ok) {
        status('Mastodon: ' + m.host + ' が HTTP ' + res.status + ' を返しました（別のインスタンスを試してください）', 'err');
        return;
      }
      const posts = await res.json();
      if (!Array.isArray(posts) || !posts.length) {
        status('Mastodon: ' + m.host + ' の公開タイムラインが空です', 'warn');
        return;
      }
      const words = [];
      let fresh = 0;
      for (const p of posts) {
        if (!p || m.seen.has(p.id)) continue;
        m.seen.add(p.id);
        fresh++;
        words.push(...tokenize(htmlToText(p.content)));
      }
      if (m.seen.size > 3000) m.seen.clear();
      if (!words.length) { status('Mastodon: 新しい投稿がまだありません', 'warn'); return; }
      shuffleInto(words);
      status('Mastodon(' + m.host + ') から ' + fresh + ' 件 / ' + words.length + ' 語を取得', 'ok');
    } catch (e) {
      status('Mastodon: ' + m.host + ' に接続できません（CORS 非対応のインスタンスの可能性）', 'err');
    } finally {
      m.busy = false;
    }
  }

  // ---- 取り出し -----------------------------------------------------------
  function take() {
    if (state.mode === 'builtin') {
      return BUILTIN[(Math.random() * BUILTIN.length) | 0];
    }
    if (state.mode === 'text') {
      if (!state.pasted.length) return null;
      return state.pasted[(Math.random() * state.pasted.length) | 0];
    }
    if (state.mode === 'mastodon') {
      if (state.buffer.length < 60) fetchMastodon();
    } else { // x
      if (state.buffer.length < 40) fetchX();
    }
    if (!state.buffer.length) return null;
    const i = (Math.random() * state.buffer.length) | 0;
    const w = state.buffer[i];
    state.buffer.splice(i, 1);
    return w;
  }

  global.WordSource = {
    state, take, setMode, setPastedText, setXConfig, setMastodonHost,
    tokenize, htmlToText, fetchX, diagX, fetchMastodon,
    set onStatus(fn) { state.onStatus = fn; }
  };
})(window);
