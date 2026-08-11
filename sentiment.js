/* sentiment.js — 語の極性判定（辞書 + 形態素ヒューリスティック）
   classic script（file:// で動かすため type="module" は使わない）
   window.Sentiment.classify(word) -> 'positive' | 'negative' | 'neutral'
*/
(function (global) {
  'use strict';

  // ---- 完全一致辞書 -------------------------------------------------------
  const NEG_WORDS = [
    // 日本語
    '死', '死ぬ', '殺す', '自殺', '嫌い', '嫌', '最悪', '地獄', '苦しい', '苦痛',
    '痛い', '辛い', '悲しい', '哀しい', '怒り', '激怒', '不安', '絶望', '孤独', '寂しい',
    '疲れた', '疲労', '無理', 'ダメ', '駄目', '失敗', '病気', '病', '事故', '戦争',
    '差別', '貧困', '崩壊', '破壊', '恐怖', '怖い', '憎しみ', 'ムカつく', 'うざい', 'きもい',
    'つまらない', '退屈', '面倒', 'めんどい', '損', '罪', '罰', '敵', '泣く', '涙',
    '闇', '暗い', '冷たい', '汚い', '醜い', '弱い', '遅い', '難しい', '混乱', '終わり',
    '別れ', '裏切り', '嘘', '詐欺', '炎上', '批判', '文句', '不満', '後悔', '恥',
    '焦り', '緊張', '不足', '減少', '悪化', '危険', '被害', '犠牲', '問題', '障害',
    '停止', '中止', '延期', '負け', '敗北', '暴力', '虐待', '毒', '汚染', '地震',
    '災害', '津波', '崩れる', '消える', '無視', '孤立', '格差', '不正', '汚職', '腐敗',
    'クソ', 'ゴミ', '最低', '悪い', '悪', '苦手', '不快', '不幸', '不運', '絶叫',
    '悲鳴', '叫び',
    // English
    'hate', 'kill', 'death', 'die', 'dead', 'sad', 'angry', 'anger', 'fear', 'pain',
    'hurt', 'bad', 'worse', 'worst', 'terrible', 'awful', 'horrible', 'ugly', 'stupid',
    'idiot', 'fail', 'failure', 'lost', 'lose', 'loser', 'war', 'crisis', 'danger',
    'sick', 'tired', 'alone', 'lonely', 'dark', 'cold', 'broken', 'break', 'cry',
    'tears', 'poison', 'toxic', 'trash', 'garbage', 'boring', 'annoying', 'disgusting',
    'cruel', 'violence', 'attack', 'destroy', 'collapse', 'poverty', 'debt', 'stress',
    'anxiety', 'depression', 'panic', 'problem', 'error', 'bug', 'crash', 'damage',
    'victim', 'abuse', 'hell', 'nightmare', 'regret', 'shame', 'guilt', 'betray',
    'lie', 'fake', 'scam', 'fraud', 'riot', 'crime', 'kill', 'dying', 'wound',
    // 記号・絵文字
    '😡', '😠', '😢', '😭', '💀', '☠️', '🤮', '😱', '👎', '💔'
  ];

  const POS_WORDS = [
    // 日本語
    '好き', '大好き', '愛', '愛してる', '幸せ', '嬉しい', '楽しい', '美しい', '綺麗', 'きれい',
    '素敵', '最高', '神', '感動', '感謝', 'ありがとう', '希望', '夢', '光', '笑顔',
    '笑い', '優しい', '温かい', '平和', '自由', '成功', '勝利', '勝つ', '達成', '成長',
    '誕生', '祝う', 'おめでとう', '応援', '元気', '健康', '安心', '満足', '充実', '豊か',
    '便利', '快適', '発見', '創造', '未来', '進化', '改善', '上昇', '増加', '回復',
    '解決', '天才', 'かわいい', '可愛い', 'かっこいい', 'すごい', '凄い', '面白い', '最強', '癒し',
    '幸運', '奇跡', '友達', '仲間', '家族', '恋', '結婚', '旅', '花', '星',
    '空', '海', '虹', '春', '陽', '太陽', '宝', '誇り', '信頼', '尊敬',
    '安全', '希少', '祭り', '音楽', '踊り', '歌', '自然', '緑', '新しい', '明るい',
    '柔らかい', '甘い', '美味しい', 'おいしい', '楽', 'good', 'ok',
    // English
    'love', 'happy', 'joy', 'beautiful', 'great', 'best', 'better', 'awesome', 'amazing',
    'wonderful', 'perfect', 'nice', 'kind', 'warm', 'light', 'hope', 'dream',
    'smile', 'laugh', 'peace', 'free', 'freedom', 'win', 'winner', 'success', 'growth',
    'birth', 'thanks', 'thank', 'grateful', 'congrats', 'healthy', 'safe', 'calm',
    'rich', 'easy', 'comfort', 'discovery', 'create', 'future', 'improve', 'rise',
    'recover', 'solve', 'genius', 'cute', 'cool', 'fun', 'funny', 'magic', 'miracle',
    'friend', 'family', 'star', 'sky', 'sea', 'flower', 'sun', 'bright', 'sweet',
    'delicious', 'brave', 'strong', 'proud', 'trust', 'respect', 'gift', 'bless',
    // 絵文字
    '😊', '😁', '😂', '🥰', '😍', '❤️', '💕', '✨', '🎉', '👍', '🙏', '🌸'
  ];

  // ---- 部分一致（形態素）ヒューリスティック -------------------------------
  // 辞書に無い語でも、この漢字を含めば概ね極性が推測できる
  const NEG_MORPH = [
    '死', '殺', '嫌', '怖', '痛', '悲', '怒', '苦', '病', '害', '敗', '損', '危',
    '汚', '暗', '悪', '罪', '毒', '恐', '孤', '貧', '崩', '壊', '滅', '闘', '争',
    '泣', '涙', '欠', '禁', '拒', '疲', '寂', '恥', '虐', '奪', '爆', '炎上'
  ];
  const POS_MORPH = [
    '良', '善', '幸', '美', '喜', '楽', '愛', '優', '安', '勝', '成', '新', '明',
    '温', '和', '福', '祝', '恵', '豊', '希望', '笑', '光', '輝', '賞', '祭', '夢',
    'friend', '好'
  ];

  const NEG = new Set(NEG_WORDS.map(w => w.toLowerCase()));
  const POS = new Set(POS_WORDS.map(w => w.toLowerCase()));

  const cache = new Map();

  function classifyRaw(word) {
    if (!word) return 'neutral';
    const lower = word.toLowerCase();

    // 1) 完全一致
    if (NEG.has(lower)) return 'negative';
    if (POS.has(lower)) return 'positive';

    // 2) 活用語尾を落として再試行（〜い / 〜な / 〜する / 〜だ）
    const stem = lower.replace(/(する|した|して|さ|い|な|だ|です|ます)$/u, '');
    if (stem && stem !== lower) {
      if (NEG.has(stem)) return 'negative';
      if (POS.has(stem)) return 'positive';
    }

    // 3) 形態素の部分一致
    let score = 0;
    for (const m of NEG_MORPH) { if (word.includes(m)) { score -= 1; break; } }
    for (const m of POS_MORPH) { if (word.includes(m)) { score += 1; break; } }

    // 4) 英語の接辞
    if (/^(un|dis|mis|non|anti|ir|im)/.test(lower) && lower.length > 5) score -= 1;
    if (/less$/.test(lower) && lower.length > 5) score -= 1;

    // 5) 否定接頭辞（日本語）
    if (/^[不無非未]/.test(word) && word.length >= 2) score -= 1;

    if (score < 0) return 'negative';
    if (score > 0) return 'positive';
    return 'neutral';
  }

  function classify(word) {
    if (cache.has(word)) return cache.get(word);
    const r = classifyRaw(word);
    cache.set(word, r);
    return r;
  }

  // ユーザーが辞書を後から足せるように
  function addWords(label, words) {
    const target = label === 'negative' ? NEG : POS;
    for (const w of words) target.add(String(w).toLowerCase());
    cache.clear();
  }

  global.Sentiment = { classify, addWords, NEG, POS };
})(window);
