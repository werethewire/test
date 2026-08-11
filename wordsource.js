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
  // 1 文字の語は入れない。落ちてくるのが単語に見えないため。
  const BUILTIN = (
    // ポジティブ
    '幸せ 笑顔 希望 未来 感謝 平和 自由 音楽 太陽 満開 快晴 優勝 合格 成功 感動 元気 健康 安心 ' +
    '満足 充実 友達 家族 仲間 結婚 誕生 祝福 応援 情熱 青空 虹色 花束 星空 美しい 楽しい 嬉しい ' +
    '優しい 温かい 明るい 面白い 可愛い 素敵 最高 ' +
    // ネガティブ
    '不安 孤独 絶望 怒り 戦争 崩壊 恐怖 事故 地震 台風 猛暑 渋滞 遅延 停電 残業 徹夜 借金 赤字 ' +
    '倒産 解雇 逮捕 詐欺 炎上 中傷 差別 貧困 疲れた 最悪 無理 頭痛 風邪 骨折 入院 迷惑 故障 失敗 ' +
    '挫折 離婚 失恋 裏切り 悲しい 苦しい 痛い 怖い 汚い 危険 ' +
    // 中立
    '都市 電車 珈琲 書店 時計 椅子 階段 玄関 台所 寝室 倉庫 信号 歩道 線路 改札 座席 交差点 ' +
    '記憶 時間 沈黙 呼吸 心臓 皮膚 骨格 血液 陰影 反射 波紋 砂丘 灰色 煙突 雨音 雪原 風景 山脈 ' +
    '河川 石段 鉄橋 温度 湿度 距離 速度 名前 職業 学校 教室 ' +
    // 英語
    'memory silence breath shadow mirror future window bridge station coffee library machine ' +
    'garden season weather morning evening midnight distance velocity ' +
    'happiness freedom courage kindness sunlight laughter friendship ' +
    'anxiety loneliness despair disaster accident earthquake blackout overtime bankruptcy ' +
    // 1 文字の語も少しだけ混ぜる
    '愛 夢 光 闇 花 星 海 空 猫 雨 雪 風 死 涙 罪 影 鏡 波'
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

  // 1 文字でも単語として通用する漢字。Intl.Segmenter は「弱まる」を 弱+まる のように割るので、
  // 1 文字の語はこの表（と極性辞書）に載っているものだけ通し、残りはかけらとして捨てる。
  const SINGLE_OK = new Set(Array.from(
    '猫犬鳥魚虫花木森山川海空雲雨雪風星月日火水土金石鉄紙本道橋窓扉家街町村島湖池谷岩砂灰煙泡波氷炎' +
    '光闇影夜朝昼夢恋愛命心骨血肌髪手足目耳口歯顔声音色味香熱時年春夏秋冬米塩油茶酒肉卵豆麦糸布服靴' +
    '鍵傘鏡皿箸机車船駅店客王神仏死罪敵涙薬毒剣盾旗鐘笛歌絵詩本紙墨筆線点面体形数字名前金銀銅鉛錫' +
    '雷霧露霜虹泥砂土壌畑田稲麦草苔蔦竹松梅桜菊蘭蓮藻貝蟹蛸鮫鯨鳩鷹梟猿熊狼狐兎鹿馬牛豚羊鶏蛇蛙亀'
  ));

  function isSingleWord(c) {
    if (SINGLE_OK.has(c)) return true;
    // 極性辞書に単独で載っている字（愛・悪・楽 など）も語として扱う
    const S = global.Sentiment;
    return !!(S && (S.NEG.has(c) || S.POS.has(c)));
  }

  function acceptable(w) {
    if (!w) return false;
    w = w.trim();
    if (!w) return false;
    if (STOP.has(w.toLowerCase())) return false;
    if (/^\d+$/.test(w)) return false;                 // 数字だけ
    if (/^[\x00-\x40\x5b-\x60\x7b-\x7f]+$/.test(w)) return false; // 記号だけ
    // 文字数はコードポイントで数える（絵文字やサロゲートペアを 2 文字と誤らないため）
    const chars = Array.from(w);
    const len = chars.length;
    if (len < state.minLen) return false;
    if (len > 16) return false;

    if (len === 1) {
      const c = chars[0];
      // 1 文字のカタカナ・ひらがな・英字は、ほぼ確実に分割のかけら（ホ ザ ロ）
      if (!/\p{Script=Han}/u.test(c)) return false;
      return isSingleWord(c);
    }

    if (/^[ぁ-ん]{2}$/u.test(w)) return false;         // 助詞など短いひらがな
    if (/^[a-z]{1,2}$/i.test(w)) return false;         // 1〜2文字の英字
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
    minLen: 1,           // 落とす語の最小文字数。1 でも「単語として通用する 1 文字」だけ通る
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

  let builtinPool = BUILTIN.slice();
  function refreshBuiltin() {
    const p = BUILTIN.filter(acceptable);
    builtinPool = p.length ? p : BUILTIN.slice();
  }
  refreshBuiltin();

  function setMode(mode) {
    state.mode = mode;
    state.buffer.length = 0;
  }

  function setMinLen(n) {
    n = Math.max(1, Math.min(8, parseInt(n, 10) || 1));
    state.minLen = n;
    state.buffer.length = 0;                       // 古い条件で溜めた語は捨てる
    state.pasted = state.pasted.filter(acceptable);
    refreshBuiltin();
    return n;
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
      return builtinPool[(Math.random() * builtinPool.length) | 0];
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
    state, take, setMode, setMinLen, setPastedText, setXConfig, setMastodonHost,
    tokenize, htmlToText, fetchX, diagX, fetchMastodon, acceptable,
    set onStatus(fn) { state.onStatus = fn; }
  };
})(window);
