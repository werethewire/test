/* wordsource.js — 単語の供給源
   同じインターフェースで扱う:
     builtin   : 内蔵ワードプール（通信なし・即動く）
     text      : ユーザーが貼り付けたテキストを分かち書き
     mastodon  : Mastodon 公開タイムライン（認証不要・プロキシ不要）
     bluesky   : Bluesky の公開フィード（認証不要・プロキシ不要）
     hackernews: Hacker News のコメント（認証不要・英語）
     x         : ローカル Python プロキシ経由（有料 tier のトークンが必要）

   ブラウザから直接叩けるかは CORS 次第。実測で通らなかったもの:
     Misskey (misskey.io) / Reddit / Bluesky の searchPosts / mastodon.social の匿名公開TL / pawoo.net

   window.WordSource.take() -> 単語1個（無ければ null）
*/
(function (global) {
  'use strict';

  // ---- 内蔵プール ---------------------------------------------------------
  // 1 文字の語は少しだけ。落ちてくるのが単語に見えなくなるため。
  const BUILTIN = (
    '幸せ 笑顔 希望 未来 感謝 平和 自由 音楽 太陽 満開 快晴 優勝 合格 成功 感動 元気 健康 安心 ' +
    '満足 充実 友達 家族 仲間 結婚 誕生 祝福 応援 情熱 青空 虹色 花束 星空 美しい 楽しい 嬉しい ' +
    '優しい 温かい 明るい 面白い 可愛い 素敵 最高 ' +
    '不安 孤独 絶望 怒り 戦争 崩壊 恐怖 事故 地震 台風 猛暑 渋滞 遅延 停電 残業 徹夜 借金 赤字 ' +
    '倒産 解雇 逮捕 詐欺 炎上 中傷 差別 貧困 疲れた 最悪 無理 頭痛 風邪 骨折 入院 迷惑 故障 失敗 ' +
    '挫折 離婚 失恋 裏切り 悲しい 苦しい 痛い 怖い 汚い 危険 ' +
    '都市 電車 珈琲 書店 時計 椅子 階段 玄関 台所 寝室 倉庫 信号 歩道 線路 改札 座席 交差点 ' +
    '記憶 時間 沈黙 呼吸 心臓 皮膚 骨格 血液 陰影 反射 波紋 砂丘 灰色 煙突 雨音 雪原 風景 山脈 ' +
    '河川 石段 鉄橋 温度 湿度 距離 速度 名前 職業 学校 教室 ' +
    'memory silence breath shadow mirror future window bridge station coffee library machine ' +
    'garden season weather morning evening midnight distance velocity ' +
    'happiness freedom courage kindness sunlight laughter friendship ' +
    'anxiety loneliness despair disaster accident earthquake blackout overtime bankruptcy ' +
    '愛 夢 光 闇 花 星 海 空 猫 雨 雪 風 死 涙 罪 影 鏡 波'
  ).split(/\s+/).filter(Boolean);

  // ---- 語の採否 -----------------------------------------------------------
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
    '鍵傘鏡皿箸机車船駅店客王神仏死罪敵涙薬毒剣盾旗鐘笛歌絵詩墨筆線点面体形数字名前銀銅鉛錫' +
    '雷霧露霜虹泥壌畑田稲草苔蔦竹松梅桜菊蘭蓮藻貝蟹蛸鮫鯨鳩鷹梟猿熊狼狐兎鹿馬牛豚羊鶏蛇蛙亀'
  ));

  function isSingleWord(c) {
    if (SINGLE_OK.has(c)) return true;
    const S = global.Sentiment;
    return !!(S && (S.NEG.has(c) || S.POS.has(c)));
  }

  function acceptable(w) {
    if (!w) return false;
    w = w.trim();
    if (!w) return false;
    if (STOP.has(w.toLowerCase())) return false;
    if (/^\d+$/.test(w)) return false;
    if (/^[\x00-\x40\x5b-\x60\x7b-\x7f]+$/.test(w)) return false;

    const chars = Array.from(w);
    const len = chars.length;
    if (len < state.minLen) return false;
    if (len > 16) return false;

    if (len === 1) {
      const c = chars[0];
      if (!/\p{Script=Han}/u.test(c)) return false;   // 1 文字のカナ・英字はかけら
      return isSingleWord(c);
    }

    if (/^[ぁ-ん]{2}$/u.test(w)) return false;
    if (/^[a-z]{1,2}$/i.test(w)) return false;
    return true;
  }

  function tokenize(text) {
    if (!text) return [];
    const cleaned = String(text)
      .replace(/https?:\/\/\S+/g, ' ')
      .replace(/[@＠][A-Za-z0-9_.\-]+/g, ' ')
      .replace(/\bRT\b:?/g, ' ')
      .replace(/&amp;|&lt;|&gt;|&quot;/g, ' ');

    const out = [];
    if (typeof Intl !== 'undefined' && Intl.Segmenter) {
      const seg = new Intl.Segmenter('ja', { granularity: 'word' });
      for (const s of seg.segment(cleaned)) if (s.isWordLike) out.push(s.segment);
    } else {
      out.push(...cleaned.split(/[\s、。,.!?！？「」『』()（）\[\]【】…・:：;；"'’”\/\\|~〜\-—+*=#＃%&$^<>]+/u));
    }
    return out.filter(acceptable);
  }

  // 投稿本文が HTML の場合にタグを剥がす。live DOM に挿さず DOMParser で処理する。
  function htmlToText(html) {
    try {
      return new DOMParser().parseFromString(String(html), 'text/html').body.textContent || '';
    } catch (e) {
      return String(html).replace(/<[^>]+>/g, ' ');
    }
  }

  // ---- 状態 ---------------------------------------------------------------
  const BSKY_FEEDS = {
    jp:  'at://did:plc:cgl62jlhroosxyjkaffidnon/app.bsky.feed.generator/aaae4nczs635m',
    hot: 'at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.feed.generator/whats-hot'
  };

  const state = {
    mode: 'builtin',
    minLen: 1,
    buffer: [],
    pasted: [],
    mastodon:   { host: 'mstdn.jp', lastFetch: 0, busy: false, cooldownMs: 10000, seen: new Set() },
    bluesky:    { feed: 'jp',       lastFetch: 0, busy: false, cooldownMs: 10000, seen: new Set() },
    hackernews: {                   lastFetch: 0, busy: false, cooldownMs: 15000, seen: new Set() },
    x: { proxy: 'http://127.0.0.1:8787', query: 'lang:ja -is:retweet', lastFetch: 0, busy: false, cooldownMs: 20000 },
    onStatus: null
  };

  function report(msg, kind) {
    if (state.onStatus) state.onStatus(msg, kind || 'info');
  }

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
    state.buffer.length = 0;
    state.pasted = state.pasted.filter(acceptable);
    refreshBuiltin();
    return n;
  }

  function setPastedText(text) {
    state.pasted = tokenize(text);
    state.buffer.length = 0;
    report(state.pasted.length + ' 語を取り込みました', state.pasted.length ? 'ok' : 'warn');
    return state.pasted.length;
  }

  function setXConfig(proxy, query) {
    if (proxy) state.x.proxy = proxy.replace(/\/+$/, '');
    if (query !== undefined) state.x.query = query;
  }

  function setMastodonHost(host) {
    if (host) state.mastodon.host = String(host).trim().replace(/^https?:\/\//, '').replace(/\/+$/, '');
  }

  function setBlueskyFeed(feed) {
    if (feed) state.bluesky.feed = String(feed).trim();
  }

  // ---- 取得の共通処理 -----------------------------------------------------
  // クールダウン・多重実行の抑止・「応答が返る前にソースを切り替えられていたら
  // 古い結果でステータスを上書きしない」をまとめて面倒みる。
  async function guarded(key, mode, force, run) {
    const s = state[key];
    if (s.busy) return;
    const now = Date.now();
    if (!force && now - s.lastFetch < s.cooldownMs) return;
    s.busy = true;
    s.lastFetch = now;
    const say = (msg, kind) => { if (state.mode === mode) report(msg, kind); };
    try {
      await run(say, s);
    } finally {
      s.busy = false;
    }
  }

  // 同じ投稿を何度も数えないための重複除去
  function freshTexts(s, items, idOf, textOf) {
    const texts = [];
    for (const it of items) {
      const id = idOf(it);
      if (!id || s.seen.has(id)) continue;
      s.seen.add(id);
      const t = textOf(it);
      if (t) texts.push(t);
    }
    if (s.seen.size > 3000) s.seen.clear();
    return texts;
  }

  function ingest(say, label, texts) {
    const words = [];
    for (const t of texts) words.push(...tokenize(t));
    if (!words.length) {
      say(label + ': 新しい語がまだありません', 'warn');
      return 0;
    }
    shuffleInto(words);
    say(label + ' から ' + texts.length + ' 件 / ' + words.length + ' 語を取得', 'ok');
    return words.length;
  }

  // ---- Mastodon -----------------------------------------------------------
  function fetchMastodon(force) {
    return guarded('mastodon', 'mastodon', force, async (say, s) => {
      const label = 'Mastodon(' + s.host + ')';
      try {
        const res = await fetch('https://' + s.host + '/api/v1/timelines/public?limit=40&local=true', { cache: 'no-store' });
        if (!res.ok) { say(label + ': HTTP ' + res.status + '（別のインスタンスを試してください）', 'err'); return; }
        const posts = await res.json();
        if (!Array.isArray(posts) || !posts.length) { say(label + ': 公開タイムラインが空です', 'warn'); return; }
        ingest(say, label, freshTexts(s, posts, p => p && p.id, p => htmlToText(p.content)));
      } catch (e) {
        say(label + ': 接続できません（CORS 非対応のインスタンスの可能性）', 'err');
      }
    });
  }

  // ---- Bluesky ------------------------------------------------------------
  function fetchBluesky(force) {
    return guarded('bluesky', 'bluesky', force, async (say, s) => {
      const uri = BSKY_FEEDS[s.feed] || s.feed;      // プリセット名 or at:// URI
      const label = 'Bluesky(' + (BSKY_FEEDS[s.feed] ? s.feed : 'custom') + ')';
      try {
        const url = 'https://public.api.bsky.app/xrpc/app.bsky.feed.getFeed?feed=' +
                    encodeURIComponent(uri) + '&limit=50';
        const res = await fetch(url, { cache: 'no-store' });
        const j = await res.json().catch(() => null);
        if (!res.ok) {
          say(label + ': ' + ((j && (j.message || j.error)) || ('HTTP ' + res.status)) +
              '（フィードの at:// URI を確認してください）', 'err');
          return;
        }
        const items = (j && j.feed) || [];
        if (!items.length) { say(label + ': フィードが空です', 'warn'); return; }
        ingest(say, label,
          freshTexts(s, items, x => x.post && x.post.cid, x => x.post && x.post.record && x.post.record.text));
      } catch (e) {
        say(label + ': 接続できません', 'err');
      }
    });
  }

  // ---- Hacker News --------------------------------------------------------
  function fetchHackerNews(force) {
    return guarded('hackernews', 'hackernews', force, async (say, s) => {
      const label = 'Hacker News';
      try {
        const res = await fetch('https://hn.algolia.com/api/v1/search_by_date?tags=comment&hitsPerPage=50', { cache: 'no-store' });
        if (!res.ok) { say(label + ': HTTP ' + res.status, 'err'); return; }
        const j = await res.json();
        const hits = j.hits || [];
        if (!hits.length) { say(label + ': 取得できませんでした', 'warn'); return; }
        ingest(say, label, freshTexts(s, hits, h => h.objectID, h => htmlToText(h.comment_text)));
      } catch (e) {
        say(label + ': 接続できません', 'err');
      }
    });
  }

  // ---- X（ローカルプロキシ経由） ------------------------------------------
  function fetchX(force) {
    return guarded('x', 'x', force, async (say, s) => {
      try {
        const url = s.proxy + '/search?q=' + encodeURIComponent(s.query) + '&max_results=100';
        const res = await fetch(url, { cache: 'no-store' });
        const body = await res.json().catch(() => null);
        if (!res.ok) {
          say('X: ' + ((body && (body.hint || body.error)) || ('HTTP ' + res.status)), 'err');
          return;
        }
        ingest(say, 'X', body.texts || []);
      } catch (e) {
        say('X: プロキシに接続できません。x_proxy.py を起動してください', 'err');
      }
    });
  }

  // プロキシ側に診断させ、原因をそのまま表示する
  async function diagX() {
    try {
      const res = await fetch(state.x.proxy + '/diag', { cache: 'no-store' });
      const d = await res.json();
      report(d.verdict, d.ok ? 'ok' : 'err');
      return d;
    } catch (e) {
      report('診断できません: ' + state.x.proxy + ' に接続できません。x_proxy.py を起動してください', 'err');
      return null;
    }
  }

  // ---- 取り出し -----------------------------------------------------------
  const REFILL = {
    mastodon:   [fetchMastodon, 60],
    bluesky:    [fetchBluesky, 60],
    hackernews: [fetchHackerNews, 80],
    x:          [fetchX, 40]
  };

  function take() {
    if (state.mode === 'builtin') {
      return builtinPool[(Math.random() * builtinPool.length) | 0];
    }
    if (state.mode === 'text') {
      if (!state.pasted.length) return null;
      return state.pasted[(Math.random() * state.pasted.length) | 0];
    }

    const r = REFILL[state.mode];
    if (r && state.buffer.length < r[1]) r[0]();     // 非同期に補充

    if (!state.buffer.length) return null;
    const i = (Math.random() * state.buffer.length) | 0;
    const w = state.buffer[i];
    state.buffer.splice(i, 1);
    return w;
  }

  function fetchNow(mode) {
    const r = REFILL[mode];
    if (r) return r[0](true);
  }

  global.WordSource = {
    state, take, acceptable, tokenize, htmlToText,
    setMode, setMinLen, setPastedText, setXConfig, setMastodonHost, setBlueskyFeed,
    fetchNow, fetchMastodon, fetchBluesky, fetchHackerNews, fetchX, diagX,
    BSKY_FEEDS,
    set onStatus(fn) { state.onStatus = fn; }
  };
})(window);
