# TrimFast Implementation Plan (Phase D)

本書の完了後に限り Phase E（コード生成）を許可する。各ステップは「単体で動作確認できる」単位に分ける。
優先度: P0=必須 / P1=初期リリースに含める / P2=将来。

---

## Step 0（事前確認・コードなし）— **完了（結果は architecture.md の §5・§8 と、§13〜§15 に反映）**

| 項目 | 内容 |
|---|---|
| Haiku 環境で Qt6 Widgets / FFmpeg / CMake が使えるか確認 | HaikuDepot パッケージと devel パッケージの有無 |
| Qt6 Multimedia の有無確認 | 無ければ LibavBackend 一本で進める |
| 360度サンプルの入手 | Theta / GoPro Max(.360) / Insta360(.insv) の短尺 |
| `ffmpeg -c copy` の挙動を CLI で先に検証 | 下記「検証マトリクス」 |

検証マトリクス（CLI のみで実施）:
- 通常 mp4（音声・字幕・チャプター付き）→ 全保持されるか
- Theta mp4 → Spherical uuid box が残るか（残らなければ MetadataPreserver 確定）
- GoPro `.360` → 全ストリーム（HEVC x2、gpmd）が残るか
- Insta360 `.insv` → 末尾トレーラが残るか

この結果で MetadataPreserver の範囲を確定する。

---

## Step 1: UI 枠組み — **完了（Haiku・Linux でビルド＆テスト確認済み）**

**目的**: モックアップどおりの静的ウィンドウ。動作はしない。

| 作業 | 受け入れ基準 |
|---|---|
| CMake プロジェクト雛形（core / playback / app / tests のターゲット） | Linux と Haiku でビルドが通る |
| MainWindow（メニュー、プレビュー領域、タイムライン領域、IN/OUT 表示、ボタン4つ、ステータスバー） | `mockup_main_window.png` と構造が一致 |
| ショートカット登録（design.md の表どおり。動作は空） | 全キーがログで発火確認できる |
| ファイルダイアログ（Open のみ）とパス表示 | 選んだファイル名がタイトルとステータスに出る |
| TimeFormat（ms⇄文字列）と単体テスト | `tst_timeformat` 緑 |

---

## Step 2: 動画再生 — **完了（Haiku で全テスト通過。Linux は qt6-multimedia-dev 導入待ち）**

| 作業 | 受け入れ基準 |
|---|---|
| ~~PlaybackBackend 抽象と LibavBackend~~ → VideoPlayer（QMediaPlayer ラッパー）と VideoSurface の映像描画 | mp4/mov/mkv を開いて映像が表示される（Haiku で確認済み） |
| VideoPlayer: 再生/一時停止/シーク/1フレーム送り | Space, ←→, Shift+←→ が動く |
| 現在位置シグナルと時刻ラベル | 表示が再生に追従（ミリ秒表示） |
| MediaProbe（ffprobe JSON → MediaInfo）duration/fps/ストリーム | ステータスバーに情報表示 |
| 起動時間計測 | 空起動 < 0.5 秒（目標。Haiku 実機で確認） |
| （P1後半）音声再生 | Linux は既定ON。Haiku は VM でクラッシュするため既定OFF（architecture.md §8） |

---

## Step 3: タイムライン — **完了（Haiku・Linux Qt6.10.3 で全テスト通過）**

| 作業 | 受け入れ基準 |
|---|---|
| TimelineWidget 自前描画（ルーラー、トラック、再生ヘッド） | 長さに応じた目盛り間隔（動画長で自動調整） |
| クリック/ドラッグでシーク | VideoPlayer が追従 |
| KeyframeIndex（ffprobe からキーフレーム一覧） | 長尺（1時間級）でも数秒以内に取得。非同期ロード、UIは固まらない |
| Ctrl+←/→ でキーフレーム移動 | 直前/直後に正しく移動 |

---

## Step 4: IN / OUT — **完了（Haiku・Linux Qt6.10.3 で全9テスト通過）**

| 作業 | 受け入れ基準 |
|---|---|
| MarkerManager（IN/OUT 保持・検証・通知）＋テスト | IN>=OUT を拒否/警告。`tst_markermanager` 緑 |
| I / O キー、Set IN / Set OUT ボタン | 現在位置が設定される |
| IN のキーフレーム吸着（直前）と表示反映 | 吸着後の値が IN 表示に出て、ステータスに通知 |
| タイムライン上の範囲描画とマーカードラッグ | ドラッグで IN/OUT が更新される |
| Alt+I / Alt+O ジャンプ、Duration 表示 | 値が一致 |
| SessionManager（ファイルと IN/OUT の保存・復元） | 再起動後に復元（ローカルのみ） |

---

## Step 5: ffmpeg 連携 — **完了（Haiku・Linux Qt6.10.3 で全12テスト通過）**

| 作業 | 受け入れ基準 |
|---|---|
| SettingsManager（ffmpeg/ffprobe パス探索：設定→PATH） | 未検出時に分かりやすいエラー |
| FFmpegRunner 引数ビルダー（`-c copy` 固定、`-map 0`、`-map_metadata 0`、`-map_chapters 0`、`-copy_unknown`、`-avoid_negative_ts make_zero`） | `tst_ffmpegargs`: 再エンコード系引数が絶対に生成されない、引数のスペース/日本語パスが安全 |
| QProcess 実行、`-progress` 解析、キャンセル | 進捗%がステータスバーに出る。Esc で中断し部分ファイルを削除 |
| 上書き保護 | 既存ファイルは確認なしで上書きしない |

---

## Step 6: 出力 — **完了（Haiku・Linux Qt6.10.3 で全16テスト通過）。ただし下記2項目は未実施**

> 未実施: ①SphericalDetector / Theta・GoPro Max 向けのメタデータ保持（サンプル未入手のため後回し、ユーザー了承済み）、②(P2) `File > Verify Output` メニュー（検証自体は書き出し直後に自動実行済み）。

| 作業 | 受け入れ基準 |
|---|---|
| 保存先決定（既定: 同じフォルダ `元名_trim.拡張子`、連番回避） | 衝突時に `_trim_2` |
| Export フロー（Ctrl+E → 保存ダイアログ → 実行 → 完了表示） | 出力が単体で再生可能 |
| 出力後の ffprobe 照合（簡易版：ストリーム数・チャプター・タグ） | 不一致なら警告 |
| Insta360Trailer（検出・追記）と MetadataPreserver への組込み（P1、実測で必須と確定） | `.insv` を切り出した出力の末尾にトレーラが存在し、ffprobe で正常に開ける |
| InsvPairResolver と ペア同時書き出し（方式B）、全成功/全削除 | `_00_`/`_10_` 双方に同区間を適用。片方失敗時は両方削除 |
| SphericalDetector＋MetadataPreserver（Step 0 の結果に基づく範囲） | Theta/GoPro/Insta360 サンプルで Spherical/Projection/Camera が出力に残る |
| 4GB 超と出力先 FS の警告 | FAT 系で事前警告 |
| （P2）OutputVerifier 完全版、File > Verify Output | PASS/WARN/FAIL レポート |

---

## Step 7: テスト — **完了（Haiku・Linux で全18テスト通過）。ただし360度実サンプル（Theta / GoPro Max）は未実施**

> 成果物: 失敗系 `tst_failures`（22項目）、`tst_playbackmemory`、長尺書き出し、`scripts/run_tests.sh`、`README.md`、`docs/manual_test_checklist.md`（実機の手動確認リスト）、`docs/performance.md`（計測結果）。警告ゼロ（`-Wall -Wextra -Wpedantic`）、ASan/UBSan クリーン。

| 区分 | 内容 |
|---|---|
| 単体（QtTest） | TimeFormat、MarkerManager、FFmpegRunner 引数、MediaProbe パース（固定 JSON）、SphericalDetector（固定 JSON） |
| 統合 | `make_test_media.sh` で生成した短尺動画を実際にトリム → ffprobe で比較（ストリーム数・codec・チャプター・タグが一致、duration が期待範囲内） |
| 無劣化検証 | 出力の映像パケットが入力と一致（`ffmpeg -f framemd5 -c copy` 等でパケット/ハッシュ比較）。再エンコードされていないことの確認 |
| Insta360 | `tst_insta360trailer`（トレーラ検出・追記の往復）、`tst_insvpair`（命名規則・不整合検出）、実サンプルで追記後の ffprobe 正常確認 |
| 360度 | 実サンプル3種で Spherical/Projection/Camera を ffprobe 比較（手動＋可能なら自動） |
| 性能 | 起動時間、1時間動画のオープン〜操作可能までの時間、メモリ使用量 |
| 手動 | Haiku 実機でのキー操作・表示確認、Linux との差分確認 |
| ビルド | Linux（CI 可）、Haiku（実機/VM） |
| 失敗系 | 壊れたファイル、ffmpeg 未導入、書き込み不可、途中キャンセル、日本語/空白パス |

---

## マイルストーン

| MS | 到達点 | 含むステップ |
|---|---|---|
| M0 | 事前検証完了（方針確定） | Step 0 |
| M1 | 開いて見てトリム範囲を決められる | 1–4 |
| M2 | 無劣化で書き出せる（通常動画） | 5–6（Spherical 除く） |
| M3 | 360度メタデータ保持を保証 | 6（Spherical）, 7 |
| M4 | Haiku パッケージ化（.hpkg）と Linux 配布 | packaging — **Haiku の .hpkg は完了**（`dist/TrimFast-0.1.0-1-x86_64.hpkg`、MIT）。**Linux 配布は AppImage 完了**（`dist/TrimFast-0.1.0-x86_64.AppImage`、Ubuntu 22.04 / Debian 12 / Ubuntu 24.04 の素の環境で確認）|

## 依存と順序の注意

- Step 2 のバックエンド選定が最大リスク。Step 0 の結果でゲートする。
- ffmpeg 書き出し（5–6）は再生（2）に依存しない。再生が難航した場合、先に CLI 的な書き出しを動かして M2 を先行できる。
- 360度保持は実ファイルでしか確認できない。Step 0 で入手できなければ M3 は保留とする。

---

## Phase E への移行条件

- [ ] design.md / architecture.md / implementation_plan.md をユーザーが承認
- [ ] Step 0 の事前確認結果（Haiku の Qt/FFmpeg、ffmpeg の uuid/gpmd 保持）を共有
- [x] Step 0 事前確認（Haiku に Qt 6.10.3 / ffmpeg 8.1.2 導入済み）
- [x] ペアファイル方針：B（自動検出・同時書き出し）
- [x] 再生バックエンド：Qt Multimedia を採用（Haiku 実機で確認、architecture.md §8）

承認後、Step 1 から順にコード生成する。

---

## 公開準備（完了）

README の全面改訂、`CHANGELOG.md`（Keep a Changelog 形式、0.1.0 を記載）、バージョン管理（定義元は `CMakeLists.txt` の1か所。`scripts/version.py` が `show` / `check` / `set` を提供し、`check` と `set` の挙動はテスト `version_consistency` / `version_script` で保証）、リリース手順書 `docs/releasing.md`。パッケージに README と CHANGELOG を同梱。
