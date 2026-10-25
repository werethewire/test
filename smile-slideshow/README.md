# Smile Slideshow

世界中のさまざまな国籍・人種・性別・年齢の人々の「笑顔・応援・拍手」写真で、30秒のスライド映像（1920×1080 / 30fps / H.264）を作ります。

- 画像の取得元: Wikimedia Commons（CC0 / Public domain / CC BY / CC BY-SA のみ。NC・ND は除外）
- 各スライドの右下に作者とライセンスを表示し、`CREDITS.md` も出力
- 15枚 × 約2.5秒、クロスフェード0.5秒、ゆっくりズーム（Ken Burns）

## 使い方

```sh
python3 -I make_slideshow.py fetch   # 画像取得 -> work/raw, work/credits.json
python3 -I make_slideshow.py sheet   # work/contact_sheet.jpg で内容を目視確認
python3 -I make_slideshow.py build   # slideshow.mp4 を生成
```

不適切な画像があれば `work/credits.json` から該当エントリを削除して `build` し直す。
並び順や検索語は `make_slideshow.py` の `QUERIES` で調整。

## 必要なもの

- Python 3 + Pillow、ffmpeg
- `commons.wikimedia.org` と `upload.wikimedia.org` へのネット接続

## 注意

写っている人物の肖像権は著作権ライセンスとは別です。商用・広告利用する場合は個別に確認してください。
