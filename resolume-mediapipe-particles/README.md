# Resolume × MediaPipe Particle Plugin

カメラで捉えた人の動きを、Resolume の中で GPU パーティクルに変換する FFGL ソースプラグインです。

```
 カメラ ──► pose_osc.py ──── OSC/UDP ────► FFGL プラグイン (Resolume 内)
          MediaPipe Pose      33 landmarks     1€ フィルタ → 骨格エミッタ
                              約 60 fps        → GPU パーティクル(ping-pong FBO)
```

骨格データを Resolume の中まで持ち込むのがこの構成の要点です。Spout/Syphon で
映像を流し込む方式と違い、パーティクルは Resolume の解像度でネイティブに描画され、
レイヤーのブレンド・エフェクト・オートパイロットがそのまま効きます。

下は実際にこのリポジトリのコードをヘッドレスの OpenGL 4.1 で走らせた出力です
(合成ポーズ、320×180、Mesa ソフトウェアレンダラ)。

| Whole Body / 既定設定 | Limbs + Trails + Body Attract |
| --- | --- |
| ![whole body](docs/example_whole_body.png) | ![trails](docs/example_trails_limbs.png) |

## 構成

| パス | 役割 |
| --- | --- |
| `tracker/pose_osc.py` | カメラ → MediaPipe Pose → OSC 送信。依存は OpenCV と MediaPipe のみ |
| `tracker/send_test_pose.py` | カメラなしで合成ポーズを送るテスト送信機(標準ライブラリのみ) |
| `plugin/src/PoseProtocol.h` | ワイヤフォーマットと骨格定義(Python 側と対になる) |
| `plugin/src/OscPose.*` | 依存ライブラリなしの OSC/UDP 受信 |
| `plugin/src/PoseTracker.*` | 1€ フィルタ、関節速度、骨長重み付きエミッション表、presence エンベロープ |
| `plugin/src/ParticleSystem.*` | GPU パーティクル(シミュレーション / 描画 / トレイル / 合成) |
| `plugin/src/MediaPipeParticles.*` | FFGL プラグイン本体とパラメータ |
| `plugin/tests/test_pose.cpp` | GL を必要としない部分のユニットテスト |
| `plugin/tests/headless_render.cpp` | EGL でシェーダーを実際に走らせる描画チェック(Linux) |

## 必要環境

- Resolume Arena / Avenue 7.3.1 以降(FFGL 2.x, OpenGL 4.1 コアプロファイル)
- Windows 10/11 または macOS 11 以降
- Python 3.9 以降 + `opencv-python`, `mediapipe`
- ビルド: CMake 3.15 以降 + MSVC 2019 以降 / Xcode 13 以降

## ビルド

FFGL SDK は自動取得されます。既存のチェックアウトを使う場合は `-DFFGL_SDK_DIR=<path>` を指定してください。

```bash
# Windows (x64)
cmake -S plugin -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# macOS (Universal)
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

生成物:

- Windows: `build/Release/MediaPipeParticles.dll`
- macOS: `build/MediaPipeParticles.bundle`

### インストール

Resolume の `Preferences → Video → FFGL Plugins` にフォルダを追加して、そこに置くのが確実です。既定の場所は次の通りです。

- Windows: `C:\Program Files\Common Files\FreeFrame\`
- macOS: `/Library/Graphics/FreeFrame Plug-Ins/`

Resolume を再起動すると Sources に **MediaPipe Particles** が現れます。

## 使い方

1. プラグインをレイヤーに置く(`OSC Port` の既定値は `9010`)。
2. トラッカーを起動する。

```bash
pip install -r tracker/requirements.txt
python3 tracker/pose_osc.py --preview
```

3. カメラの前に立つ。パーティクルが体の輪郭から湧き出します。

別 PC のカメラを使う場合は送信先を指定します(受信側はプラグインの `OSC Port`)。

```bash
python3 tracker/pose_osc.py --target 192.168.1.20:9010
```

複数の Resolume インスタンスへ同時送信するときは `--target` を並べます。

### カメラなしで動作確認

```bash
python3 tracker/send_test_pose.py --port 9010
```

合成の「手を振る人」が送られます。パーティクルが出れば、受信とレンダリングは正常です。
出ない場合は切り分けがトラッカー側に絞れます。

## パラメータ

Resolume 上では全て 0〜100% のスライダーです(FFGL 2 のどのホストでも意味が変わらないため、
ホスト側レンジ宣言は使っていません)。内部で下表の実値に写像されます。

| パラメータ | 0% | 100% | 既定 | 説明 |
| --- | --- | --- | --- | --- |
| Particles | 4,096 | 262,144 | 65,536 | パーティクル数。変更時に全リセット |
| Life | 0.2 s | 8 s | 2 s | 寿命 |
| Life Random | 0 | 1 | 0.5 | 寿命のばらつき。0 にすると全体が脈打つ |
| Emit Spread | 0 | 2 | 0.35 | 発生時のランダム初速 |
| Inherit Motion | 0 | 2 | 1.0 | 関節速度をどれだけ受け継ぐか。動きの表現の中核 |
| Gravity | -2 | 2 | 0 | 負で上昇 |
| Turbulence | 0 | 3 | 0.5 | カールノイズの強さ |
| Turbulence Scale | 0.2 | 12 | 3.0 | ノイズの細かさ |
| Drag | 0 | 6 | 1.2 | 空気抵抗。上げるほど体に張り付く |
| Body Attract | -4 | 4 | 0 | 正で骨格に吸着、負で反発 |
| Size | 0.5 px | 24 px | 3 px | 1080p 基準。解像度に応じて自動スケール |
| Size Random | 0 | 1 | 0.5 | 粒径のばらつき |
| Trails | なし | 約 2 s | 0 | 残像の減衰時間。フレームレート非依存 |
| Brightness | 0 | 4 | 1.0 | 加算合成なので 1 超で発光する |
| Opacity | 0 | 1 | 1.0 | 出力全体の不透明度 |
| Color A / Color B | — | — | 白 / 青 | 若い粒 → 古い粒のグラデーション |
| Color By | Age / Speed | | Age | Speed は速い粒ほど Color B に寄る |
| Emit From | Whole Body / Limbs / Torso / Joints | | Whole Body | 発生源の絞り込み |
| Smoothing | 0 | 1 | 0.5 | 1€ フィルタ。0 は生に近く、1 は重い |
| Mirror | off / on | | on | 演者から見て鏡像にする |
| Zoom | 0.2 | 3.0 | 1.0 | カメラ画角の当てはめ |
| Position X / Y | -1 | 1 | 0 | 位置合わせ |
| Reset | — | — | — | パーティクルを全消去して再生成 |
| OSC Port | — | — | 9010 | 受信ポート(テキスト入力) |

Resolume のパラメータは OSC / MIDI にそのままマップできるので、
`Body Attract` や `Trails` をフェーダーに割り当てて手で押さえる運用が実用的です。

### 出だしの設定例

- **輪郭のきらめき**: Drag 高め、Turbulence 低め、Life 短め、Emit From = Whole Body
- **軌跡を引く翼**: Trails 0.6、Body Attract 高め、Emit From = Limbs、Color By = Speed
- **煙のように崩れる体**: Gravity 負、Drag 低め、Turbulence 高め、Life 長め

## OSC プロトコル

トラッカーとプラグインの間はこれだけです。他の OSC ソフトからも同じ形式で送れます。

```
/mp/pose   ,i f×132   frameId, 各ランドマークの (x, y, z, visibility)
/mp/clear  ,i         frameId  — 人が検出されていない
```

- ランドマークは MediaPipe Pose の 33 点、順序もそのまま
- `x`, `y` は 0..1(画像左上原点)、`visibility` は 0..1
- 32bit ビッグエンディアン、OSC 1.0 のバンドルにも対応

`/mp/clear` を受け取る、あるいは 0.5 秒パケットが途切れると、プラグインは
点滅せずにフェードアウトします。

## 設計上のポイント

- **1€ フィルタ**: 静止時は強く平滑化し、速い動きでは追従を優先します。固定ローパスと違い、
  手を振っても残像のように尾を引きません。速度推定は生入力の差分から取っており、
  フィルタ遅れの分だけ過大評価されないようにしています(パーティクルがこの速度を継承するため)。
- **骨長重み付きエミッション**: 骨ごとの CDF を毎フレーム CPU で作り、シェーダー側は
  乱数 1 つで発生位置を選びます。長い骨に比例して粒が乗るので、腕だけが濃くなりません。
  両端の visibility が低い骨は重み 0 になり、隠れた部位からは発生しません。
- **presence エンベロープ**: 立ち上がりは速く、消えるときは遅い非対称の包絡線。
  検出が 1 フレーム落ちても画が瞬きません。
- **発生のばらけ**: 寿命切れの粒は毎フレーム確率的に再生成されるため、
  人が入ってきたときに一斉には出ずに立ち上がります。
- **依存ゼロの OSC**: プラグインは Resolume のプロセス内にロードされるので、
  追加の共有ライブラリは配布事故のもとです。必要な範囲だけ自前で実装しています。

## 動作確認済みの内容

このリポジトリで実際に検証したこと:

- `plugin/tests/test_pose.cpp` — OSC パーサ(バンドル / 破損パケット / 途中切れ)、
  1€ フィルタの収束・ノイズ減衰・速度推定、座標変換とミラー、エミッション表、
  presence エンベロープ、実ソケットでの往復。`ctest` で全て通過。
- Python が実際に送るバイト列を C++ パーサに読ませ、値が一致することを確認。
- `send_test_pose.py` → `PoseReceiver` → `PoseTracker` を実際に 2 秒走らせ、
  119 パケット受信・presence 1.0・関節速度が出ることを確認。
- 6 本のシェーダーを `glslangValidator` で単体検証し、4 組のプログラムをリンク検証。
- Resolume FFGL SDK(master)に対してプラグイン全体をビルドし、`plugMain` の
  エクスポートを確認。
- Mesa の OpenGL 4.1 コアプロファイル(EGL surfaceless)で `ParticleSystem` を
  90 フレーム実行し、GL エラーなしでパーティクルが描画されることを確認。
  トレイル / 吸着 / 部位別発生 / 速度カラーの各分岐も実行済み。

Resolume 本体での動作は、この環境に Resolume も GPU もないため未検証です。
Windows / macOS の実機で確認してください。

## 既知の制約

- 人数は 1 名です。MediaPipe Pose が最も自信のある 1 人を返します。
- 奥行き (`z`) は送信していますが、現状パーティクルは 2D で走ります。
- `Particles` を動かすとシミュレーションテクスチャを作り直すため、その瞬間に粒が消えます。
  本番中はフェーダーに割り当てないでください。
- プラグインの FFGL ユニーク ID は `MPPT` です。他のプラグインと衝突する場合は
  `MediaPipeParticles.cpp` の `PluginInfo` で変更してください。

## ライセンス

FFGL SDK は Resolume のライセンスに従います。本リポジトリのコードはリポジトリ本体の
ライセンスに従います。
