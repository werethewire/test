/* sentiment.js — 語の極性判定
   classic script（file:// で動かすため type="module" は使わない）

   判定は次の順で行う。先に当たった段階で確定する。
     1. 完全一致辞書（カタカナ→ひらがな正規化した鍵でも引く）
     2. 活用語尾を落として再照合（だるかった → だるい）
     3. 否定形の反転（良くない → ネガ / 悪くない → ポジ）
        ※ 語幹が辞書にある場合だけ。「少ない」「情けない」を誤って反転させないため
     4. 形態素の重み付き合計（強い手がかり=2 / 弱い手がかり=1）
     5. 接頭辞（不・無・非・未 / un- dis- -less）※ 例外辞書を除く

   感度 setSensitivity('strict' | 'normal' | 'sensitive')
     strict    : 1〜2 のみ。誤判定を出したくないとき
     normal    : 1〜4（弱い手がかりは半分の重み）
     sensitive : 1〜5 すべて。取りこぼしを減らしたいとき（既定）

   window.Sentiment.classify(word) -> 'positive' | 'negative' | 'neutral'
*/
(function (global) {
  'use strict';

  // ===== 完全一致辞書 =====================================================
  const NEG_WORDS = (
    // 体調・健康
    '頭痛 腹痛 胃痛 歯痛 生理痛 発熱 高熱 風邪 インフル インフルエンザ 花粉症 咳 鼻水 吐き気 嘔吐 ' +
    '下痢 便秘 不眠 寝不足 睡眠不足 疲労 過労 過労死 だるい だるさ しんどい きつい 貧血 骨折 捻挫 ' +
    '打撲 火傷 傷 怪我 病気 病 入院 手術 副作用 感染 発症 重症 難病 持病 悪化 通院 点滴 虫歯 ' +
    '肩こり 腰痛 めまい 二日酔い アレルギー 鬱 憂鬱 うつ 不眠症 パニック 依存症 中毒 熱中症 ' +
    // 感情
    '悲しい 哀しい 悔しい 寂しい 淋しい 切ない 虚しい 情けない 恥ずかしい 怖い 恐ろしい 不安 心配 ' +
    '焦り 苛立ち イライラ ムカつく うんざり 呆れる 絶望 落胆 失望 後悔 罪悪感 劣等感 嫉妬 妬み ' +
    '恨み 憎い 憎悪 憎しみ 怒り 激怒 憤り 不満 不快 嫌悪 嫌い 嫌 苦しい 辛い 痛い 泣く 涙 号泣 ' +
    '孤独 孤立 疎外 疲れた 疲れ 面倒 億劫 退屈 つまらない 鬱陶しい 気持ち悪い 最悪 最低 ダメ 駄目 ' +
    '無理 うざい ウザい きもい キモい ダサい つらい ツラい 詰んだ メンブレ 病んだ 死にたい 消えたい ' +
    // 仕事・金
    '残業 徹夜 休日出勤 パワハラ セクハラ モラハラ いじめ 嫌がらせ クレーム 苦情 減給 降格 左遷 ' +
    '解雇 リストラ 失業 無職 倒産 破産 破綻 赤字 損失 損害 損 借金 負債 滞納 差押 貧乏 貧困 格差 ' +
    '値上げ 増税 物価高 インフレ 不景気 不況 ミス 過失 欠陥 不良 故障 障害 停止 遅延 中止 延期 ' +
    '締切 納期 プレッシャー ストレス 罰金 未払い 督促 ブラック企業 搾取 ' +
    // 社会・事件
    '事故 衝突 転落 火事 火災 爆発 崩落 停電 断水 事件 犯罪 犯人 逮捕 容疑 起訴 訴訟 裁判 詐欺 ' +
    '詐欺師 横領 汚職 腐敗 不正 隠蔽 改ざん 捏造 デマ 陰謀 中傷 誹謗 侮辱 罵倒 暴言 暴力 虐待 ' +
    '監禁 誘拐 殺人 殺害 死亡 死者 遺体 自殺 テロ 戦争 侵攻 空爆 砲撃 虐殺 難民 飢餓 迫害 弾圧 ' +
    '強盗 窃盗 万引き 放火 脅迫 恐喝 密輸 密売 買収 談合 偽装 偽造 悪用 乱用 濫用 ' +
    '攻撃 猛攻 倒れる 転倒 倒壊 打倒 違和感 手違い 間違い ' +
    '検閲 差別 偏見 対立 分断 抗議 暴動 炎上 荒らし 煽り 晒し 誤情報 風評 ' +
    // 災害・環境
    '地震 津波 台風 豪雨 洪水 土砂崩れ 噴火 竜巻 猛暑 酷暑 寒波 大雪 吹雪 干ばつ 山火事 汚染 ' +
    '公害 温暖化 異常気象 絶滅 枯渇 廃棄 ゴミ 汚水 有害 毒 放射能 ' +
    // 生活
    '渋滞 満員 遅刻 寝坊 忘れ物 紛失 盗難 空き巣 騒音 悪臭 汚い 汚れ カビ 害虫 ゴキブリ 雑草 ' +
    '停滞 迷惑 邪魔 失敗 挫折 不合格 落選 落第 留年 退学 離婚 別居 失恋 破局 浮気 不倫 別れ ' +
    '喧嘩 口論 誤解 孤独死 空腹 飢え 渇き 混雑 遅れ 欠航 運休 悪夢 くだらない ' +
    '失策 失態 失言 被災 老朽 劣化 混迷 不備 不調 稚拙 拙劣 憂慮 懸念 脅威 違反 違法 疲弊 衰退 ' +
    '煩雑 紛糾 齟齬 杜撰 過疎 難渋 逼迫 窮地 苦境 惨状 泥沼 難局 停頓 空転 ' +
    // 形容
    'ひどい 惨い 悲惨 残酷 冷酷 無慈悲 卑怯 醜い 煩わしい 危ない 危険 不吉 不運 不幸 悪い 悪 ' +
    '劣る 弱い 遅い 狭い 暗い 冷たい 冷淡 汚らしい 苦手 不便 不足 欠乏 ' +
    '圧力 抑圧 重圧 減少 減益 暴落 高騰 品切れ 欠品 割高 不振 苦戦 難航 悪質 粗悪 険悪 不穏 物騒 ' +
    // 絵文字
    '😡 😠 😢 😭 💀 ☠️ 🤮 😱 👎 💔 😰 😞 😔 🥲'
  ).split(/\s+/).filter(Boolean);

  const NEG_EN = (
    'hate hatred kill killed death die died dead dying sad sadness angry anger rage fear afraid ' +
    'pain painful hurt harm harmful bad worse worst terrible awful horrible dreadful miserable ' +
    'tragic brutal cruel toxic ugly stupid idiot fail failed failure lose lost loser losing ' +
    'war conflict invasion bombing massacre refugee oppression censorship discrimination ' +
    'harassment bullying abuse assault murder suicide funeral grief sorrow despair hopeless ' +
    'useless worthless anxiety depression insomnia fatigue exhaustion burnout stress pressure ' +
    'deadline overtime complaint blame guilt shame regret loneliness lonely isolation rejection ' +
    'breakup divorce betray betrayal lie liar cheat scam spam troll fraud scandal corrupt ' +
    'layoff bankrupt deficit debt recession inflation unemployment delay defect crash outage ' +
    'blackout disease illness injury wound cancer virus infection pandemic famine drought flood ' +
    'earthquake wildfire pollution extinction warming crisis emergency disaster catastrophe ' +
    'warning alarm risk threat damage loss waste garbage trash filth stink mold pest noise ' +
    'traffic jam late broken cracked rotten spoiled expired sick tired weak poor dirty cold ' +
    'dark heavy slow difficult impossible wrong error bug panic terror horror nightmare boring ' +
    'annoying disgusting violence destroy collapse poverty problem victim hell dangerous'
  ).split(/\s+/);

  const POS_WORDS = (
    // 感情
    '好き 大好き 愛 愛してる 恋 幸せ 幸福 嬉しい 楽しい 面白い 心地よい 気持ちいい 嬉し 喜び 歓喜 ' +
    '感動 感激 感謝 ありがとう 希望 夢 光 笑顔 笑い 微笑み 優しい 温かい 和やか 穏やか 安心 満足 ' +
    '充実 誇り 信頼 尊敬 憧れ 癒し 安らぎ 平和 自由 元気 健康 快調 上機嫌 ' +
    // 成果
    '成功 勝利 勝つ 優勝 達成 突破 記録 合格 内定 昇進 昇給 完成 完治 快復 回復 復活 開通 開店 ' +
    '開幕 受賞 表彰 称賛 絶賛 評価 好評 名作 傑作 名演 好調 順調 快進撃 躍進 飛躍 成長 進化 改善 ' +
    '上達 熟練 達人 名人 天才 有能 有望 期待 貢献 活躍 未来 希望 前進 快挙 ' +
    // 生活
    '快晴 満開 絶景 名所 豊作 豊富 潤沢 便利 快適 快眠 満腹 美味しい おいしい 美しい 綺麗 きれい ' +
    '素敵 可愛い かわいい かっこいい 最高 最強 神 すごい 凄い 円満 和解 団結 協力 支援 寄付 恩恵 ' +
    '特典 割引 節約 得 無料 安全 安定 清潔 丁寧 親切 誠実 寛容 謙虚 勤勉 情熱 熱意 名誉 栄光 ' +
    '祝福 祝 おめでとう 応援 励まし friend 家族 仲間 友達 誕生 結婚 出産 記念 旅 花 星 空 海 虹 ' +
    '慈悲 慈愛 温情 温厚 善意 好意 厚意 誠意 精進 融和 協調 賛同 吉報 朗報 全快 治癒 好転 改良 ' +
    '晴天 好天 増収 増益 上等 秀逸 卓越 華麗 美麗 精巧 優秀 優良 明朗 快活 幸運 敬愛 親愛 友愛 ' +
    '春 太陽 陽 宝 音楽 歌 踊り 祭り 自然 緑 新しい 明るい 甘い 柔らかい 軽い 広い 楽 ' +
    // 絵文字
    '😊 😁 😂 🥰 😍 ❤️ 💕 ✨ 🎉 👍 🙏 🌸 🥳 😄'
  ).split(/\s+/).filter(Boolean);

  const POS_EN = (
    'love happy happiness joy joyful beautiful great best better awesome amazing wonderful ' +
    'perfect nice kind warm light hope dream smile laugh peace peaceful free freedom win won ' +
    'winner success successful growth birth thanks thank grateful gratitude congrats ' +
    'congratulations healthy health safe safety calm rich easy comfort comfortable discovery ' +
    'create creative future improve improved rise recover recovery solve solved genius cute ' +
    'cool fun funny magic miracle friend friendly family star sky sea flower sun bright sweet ' +
    'delicious brave strong proud trust respect gift bless blessing helpful useful excellent ' +
    'brilliant clever elegant fresh clean smooth generous honest gentle graceful lucky win ' +
    'progress achievement award prize victory celebrate holiday vacation reward bonus discount'
  ).split(/\s+/);

  // ===== 形態素の手がかり（重み付き） =====================================
  // 強い = その字が入っていればほぼ極性が決まる
  const NEG_STRONG = '死 殺 痛 苦 悲 憎 怖 恐 嫌 毒 罪 犯 虐 暴 爆 崩 壊 滅 敗 貧 飢 疫 癌 鬱 詐 欺 侮 罵 惨 呪 悪 亡'.split(' ');
  // 弱い = 文脈次第で反転しうる（感度 normal では半分の重み）
  // 単漢字で中立語を巻き込むもの（圧→気圧・電圧、冷→冷蔵庫、減→削減、落→落語、停→停車）は
  // ここに入れず、弾圧・冷たい のように 2 字以上の語として完全一致辞書に持たせている。
  const NEG_WEAK = '害 障 患 傷 誤 迷 惑 疑 訴 逮 難 絶 欠 故 遅 争 闘 泣 涙 孤 寂 焦 恥 怒 憤 損 険 危 弱 暗 汚 狭 拒 禁 奪 荒 拙 弊 衰 朽 劣 脅 憂 懸'.split(' ');
  const POS_STRONG = '愛 幸 喜 楽 美 優 良 善 祝 福 恵 豊 賞 栄 誉 癒 輝 光 希 笑 好'.split(' ');
  // 中立語を巻き込む字（支→支柱、信→信号、温→温度、勤→通勤、名→名前、強→強盗、感→感想）は入れない。
  // それらは温情・温厚 のように 2 字以上の語として完全一致辞書に持たせている。
  const POS_WEAK = '安 和 新 快 満 成 勝 達 健 清 潔 順 頼 敬 謝 助 協 誠 寛 謙 熟 巧 妙 賢 夢'.split(' ');

  // 接頭辞ルールの例外（不・無・非・未 で始まるが否定的でない語）
  const PREFIX_EXCEPT = new Set((
    '無料 無限 無事 無償 無敵 無数 無地 無音 無我 無心 無論 未来 未知 未満 未定 未成年 ' +
    '不思議 非常 非公開 無糖 無添加 無印 無風 無二 ' +
    '未明 未婚 未使用 未読 未経験 未着 未収 未公開 無休 無言 無名 無臭 無色 無風 ' +
    '非公式 非常識 非対称 不定期 不特定 不可欠 不動産 不変'
  ).split(/\s+/));

  // ===== 索引 =============================================================
  // カタカナをひらがなに寄せた鍵も入れて、ウザい / うざい の両方を拾う
  function kanaNorm(s) {
    return String(s).replace(/[ァ-ヶ]/g, c => String.fromCharCode(c.charCodeAt(0) - 0x60));
  }

  function buildIndex(words) {
    const set = new Set();
    for (const w of words) {
      const k = String(w).toLowerCase();
      set.add(k);
      const n = kanaNorm(k);
      if (n !== k) set.add(n);
    }
    return set;
  }

  const NEG = buildIndex(NEG_WORDS.concat(NEG_EN));
  const POS = buildIndex(POS_WORDS.concat(POS_EN));

  function lookup(word) {
    const k = String(word).toLowerCase();
    if (NEG.has(k)) return 'negative';
    if (POS.has(k)) return 'positive';
    const n = kanaNorm(k);
    if (n !== k) {
      if (NEG.has(n)) return 'negative';
      if (POS.has(n)) return 'positive';
    }
    return null;
  }

  // ===== 活用と否定 =======================================================
  // だるかった → だるい、疲れました → 疲れ など
  const INFLECT = [
    [/かった$/u, 'い'], [/くて$/u, 'い'], [/ければ$/u, 'い'], [/そう$/u, 'い'], [/すぎ(る)?$/u, 'い'],
    [/(します|しました|してる|している|して|した|する)$/u, ''],
    [/(ました|まして|ます|ません)$/u, ''],
    [/(だった|でした|です|だ)$/u, ''],
    [/(られる|れる|たい|そうだ|がち|気味)$/u, '']
  ];

  function tryInflections(word) {
    for (const [re, rep] of INFLECT) {
      if (!re.test(word)) continue;
      const stem = word.replace(re, rep);
      if (stem && stem !== word) {
        const hit = lookup(stem);
        if (hit) return hit;
      }
    }
    return null;
  }

  // 「良くない」「悪くない」のように否定で極性が反転する形
  const NEGATION = /(くない|くなかった|ではない|じゃない|ません|ませんでした|ない|ぬ)$/u;

  // ===== 形態素スコア =====================================================
  function morphemeScore(word, weakFactor) {
    let s = 0;
    for (const m of NEG_STRONG) if (word.includes(m)) s -= 2;
    for (const m of POS_STRONG) if (word.includes(m)) s += 2;
    for (const m of NEG_WEAK) if (word.includes(m)) s -= weakFactor;
    for (const m of POS_WEAK) if (word.includes(m)) s += weakFactor;
    return s;
  }

  // ===== 本体 =============================================================
  let sensitivity = 'sensitive';
  const cache = new Map();

  // 辞書 → 活用 → 形態素・接辞 の順に見て極性を返す。決まらなければ null。
  function polarityOf(word) {
    const hit = lookup(word) || tryInflections(word);
    if (hit) return hit;
    if (sensitivity === 'strict') return null;

    const weak = sensitivity === 'sensitive' ? 1 : 0.5;
    let score = morphemeScore(word, weak);

    if (sensitivity === 'sensitive') {
      const lower = String(word).toLowerCase();
      if (/^[不無非未]/u.test(word) && word.length >= 2 && !PREFIX_EXCEPT.has(word)) score -= 2;
      if (/^(un|dis|mis|non|anti|ir|im)/.test(lower) && lower.length > 5) score -= 1;
      if (/less$/.test(lower) && lower.length > 5) score -= 1;
    }

    if (score <= -1) return 'negative';
    if (score >= 1) return 'positive';
    return null;
  }

  function classifyRaw(word) {
    if (!word) return 'neutral';

    // 1〜2. 完全一致 / 活用語尾を落として再照合。ここで当たれば否定判定より優先する
    // （「情けない」「危ない」「くだらない」を否定形として壊さないため）
    let hit = lookup(word) || tryInflections(word);
    if (hit) return hit;

    if (sensitivity === 'strict') return 'neutral';

    // 3. 否定形なら語幹の極性を出して反転する。語幹が中立なら反転しない
    if (NEGATION.test(word)) {
      const base = word.replace(NEGATION, '');
      if (base) {
        const b = polarityOf(base) || polarityOf(base + 'い');
        if (b) return b === 'negative' ? 'positive' : 'negative';
      }
    }

    // 4〜5. 形態素と接辞
    return polarityOf(word) || 'neutral';
  }

  function classify(word) {
    const key = sensitivity + ' ' + word;
    if (cache.has(key)) return cache.get(key);
    const r = classifyRaw(word);
    cache.set(key, r);
    return r;
  }

  // 語の追加（実行中に辞書を育てられる）
  function addWords(label, words) {
    const target = label === 'negative' ? NEG : POS;
    for (const w of words) {
      const k = String(w).toLowerCase();
      target.add(k);
      const n = kanaNorm(k);
      if (n !== k) target.add(n);
    }
    cache.clear();
  }

  function setSensitivity(level) {
    if (['strict', 'normal', 'sensitive'].indexOf(level) < 0) return false;
    sensitivity = level;
    return true;
  }

  global.Sentiment = {
    classify, addWords, setSensitivity,
    get sensitivity() { return sensitivity; },
    NEG, POS,
    _debug: { lookup, tryInflections, polarityOf, morphemeScore, kanaNorm }
  };
})(window);
