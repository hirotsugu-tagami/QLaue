# QLaue — アプリ機能監査7件の修正（2026-10-26）

[同日の監査](AUDIT-2026-10-26-APP-FUNCTIONS.md)で報告したUI01〜UI07をすべて修正した。
現行ソースからMac開発用アプリをビルドし、通常の12検査とASan/UBSan付き6検査が成功した。
以前からの未commit修正は維持し、stage・commit・push・EXE/DMGの更新はしていない。
開始時HEADは`8d9853bf012f42e71cf14e97720a1c5b9627bb9e`。

## 変更と確認

| ID | 修正後の動作 | 確認した条件 |
|---|---|---|
| UI01 | 画像の差し替え・転置、原点・尺度・Resetで古い選択点と候補を消す。距離・波長の変更では候補を消し、再探索用の選択点を保持する。 | 実メニュー・ファイル選択から画像を取り込み、各変更後の候補表とResolveを検査。古い選択点を使った再探索を拒否し、選び直せば探索できる。距離・波長変更では再探索画面を開ける。 |
| UI02 | 印刷もShow simulationに従う。画像の座標準備、原点、表示中画像、画面状態の復元を維持する。 | 画像ON/OFFとシミュレーションOFF、縦横・72/300 dpi。画面・印刷に赤い計算点が出ず、画像位置と画面状態が変わらない。PDFも生成・再描画した。 |
| UI03 | Camera Controlsを撮影処理へ接続。Camera Setupと設定を共有し、Snap!とメニューを同じ検証・撮影経路へつなぐ。 | 実MainWindowのCamera Setup、メニュー取得、ドックSnap、撮影画像のプレビュー確定・取り込み。模擬サーバーが編集後のBinning・露光時間を1回だけ受信する。不正露光の拒否と、成功・失敗後の操作復帰。 |
| UI04 | Reset Orientationで現在の格子に対する初期の方位へ戻し、ゴニオメーター角をゼロにする。方位表示・計算も更新する。 | 自由回転・ゴニオメーター両モード、非直交格子、繰り返しReset。画像・原点・尺度・格子・原子を保持し、方位パネル9成分が一致する。 |
| UI05 | ドック側の表示変更を5種類のWindow/Toolsメニューへ同期する。 | 閉じた後に1回の操作で再表示。浮動ドック、親ウィンドウのhide/show、タブ切替、保存レイアウトの復元。 |
| UI06 | プリンターの既定値は初回作成時だけ設定し、Page Setup・Print・結晶情報の印刷で用紙設定を保持する。 | 向きの強制上書きと描画開始後の変更をコードから除去。プリンターを保持してMainWindow破棄時に解放し、取消し時は元のQPageLayoutを復元。縦横の印刷描画検査は成功。ネイティブダイアログ経由は未検証。 |
| UI07 | PDFのDetector thresholdを1行にし、見出しの綴りも修正した。 | 生成HTMLで1回だけ現れることを検査し、PDFの再描画でも確認した。 |

## 解析状態の扱い

選択点は選択時のmm座標で保存されている。画像の幾何が変わった後はその値を再利用できないため、両方の選択方法（Solve Orientationと2スポット方位指定）を共通の処理で消す。
原点のプレビューや取消し、同値・不正値の尺度入力では有効な解析を消さない。画像の明暗・反転・表示切替でも選択点と候補を保持する。

距離・波長の変更では画像上の選択点のmm座標は有効なので、候補の点数・誤差だけを無効化する。Resolve Orientationで新しい条件の探索へ進める。

入力変更時には実行中の探索へ中断を要求し、入力変更前に開始した計算が後から完了しても採用しない。
探索パラメーター画面、探索中、2スポットの指数入力画面それぞれで入力変更を検出する。
画像が2スポットの指数入力中に差し替わった場合も、古い座標を現在の結晶へ適用しない。

XTLの読込では、既存の解析を消してから保存された原点と尺度をそのまま復元する。以前修正した「校正時にビーム画素を保持する処理」をXTL復元へ二重適用しない。

## 撮影と方位リセット

撮影中はドックのSnapと入力欄を無効化し、状態と進捗を表示する。0・負数・NaN・無限大・処理できない巨大な露光時間を拒否し、以前の有効値を保持する。
露光時間の単位は秒。Camera Setupの無変更往復で精度が失われないよう表示桁も保持する。
通信プロトコル、実行中リクエストの設定コピー、フレーム検証、終了時の中断処理は維持した。

Reset Orientationは、現在の格子で100をビーム軸、001をカメラ側の面へ合わせる初期配置を復元する。読込時の任意の方位を保存して戻す機能ではない。
2スポット入力後に指数を指定する既存の`LaueFilm::resetOrientation()`へ誤接続せず、独立したメニュー処理で実装した。

## 独立レビュー

撮影、MainWindowの操作、印刷を分担して修正し、別担当が解析状態の無効化と撮影連携を確認した。
レビューで次の2点を追加で修正した。

- 2スポットの指数入力中に画像が変わった場合も、変更前の座標を適用しないガードが必要だった。入力変更と選択数の検証を追加し、実際にダイアログ中で画像を差し替える回帰検査を追加した。
- カメラ欄を別の撮影処理へ再接続するとき、旧撮影処理から状態表示欄への接続も解除する必要があった。再接続時の解除対象を揃えた。

修正後のレビューで、今回の変更経路に追加の修正必須事項は報告されていない。

## 検証結果

実行環境はmacOS 26.7.1 / arm64 / Qt 5.15.15 / Qt offscreen。
各検査は修正後のソースからビルドした。

| 検査 | 通常 | ASan/UBSan |
|---|---|---|
| image-import-check | 成功 | 今回未実行 |
| crystal-features-check | 成功 | 今回未実行 |
| indexing-check（入力変更・古い計算・2スポット中の変更を追加） | 成功 | 成功 |
| numerics-check | 成功 | 今回未実行 |
| persistence-check | 成功 | 成功 |
| audit-fixes-check | 成功 | 今回未実行 |
| image-state-check | 成功 | 成功 |
| settings-regressions-check | 成功 | 今回未実行 |
| lattice-constraints-check | 成功 | 今回未実行 |
| print-alignment-check（非表示シミュレーション・しきい値重複の検査を追加） | 成功 | 成功 |
| camera-check（撮影欄と実MainWindow連携の検査を追加） | 成功 | 成功 |
| app-controls-check（新規：方位リセット・ドック表示） | 成功 | 成功 |

Mac開発用`QLaue.app`のビルド・リンクも成功した。
印刷の既存の位置合わせ誤差は4条件で0.835〜1.920 px。シミュレーションを非表示にしたPDFをPopplerで再描画し、画像・原点が残り、赤い計算点がなく、しきい値が1行だけであることを確認した。

ASan/UBSanの6検査は診断エラーなし。使用した設定はコンパイル時`-fsanitize=address,undefined -fno-omit-frame-pointer`、リンク時`-fsanitize=address,undefined`、実行時`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`と`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。リーク検出は無効であり、メモリーリーク全般の検証ではない。

Windows配布スクリプトに新しいapp-controls-checkを追加した。
Windowsの実行、Macのネイティブファイル選択・印刷ダイアログ、実カメラ、実プリンター、実測像と独立した正解方位の精度照合は今回未実施。
ネイティブ画面への接続制約は同日の監査記録のとおりで、今回解消したとは扱わない。
公開済みr4のEXE・DMGは更新しておらず、今回の修正は未commitのソースとMac開発用ビルドにある。

## ソースと実行記録

主なソースは`lauewidget.cpp/.h`、`mainwindow.cpp/.h`、`pslcamera.cpp/.h`、`htmlgenerators.cpp`。
検証用の変更は`tests/indexing.cpp`、`tests/camera.cpp`、`tests/print_alignment.cpp`、新規`tests/app_controls.cpp/.pro`。

新規検査は従来と同じqmake方式で実行できる。

```sh
mkdir -p build-app-controls
cd build-app-controls
"$QT_BIN/qmake" ../tests/app_controls.pro
make -j4
QT_QPA_PLATFORM=offscreen ./app-controls-check
```

このMacのSDK構成ではqmakeへ`QMAKE_CXXFLAGS+=-isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1`も指定した。
カメラ検査にはローカル模擬サーバーのlisten許可が必要。

Git管理対象外の`build-app-fixes-20261026/`に今回の実行記録を保存した。

- [通常12検査の結果](../build-app-fixes-20261026/results.json)、[実行スクリプト](../build-app-fixes-20261026/run_checks.py)
- [ASan/UBSan 6検査の結果](../build-app-fixes-20261026/asan/results.json)、[実行スクリプト](../build-app-fixes-20261026/run_sanitizers.py)
- [入力変更の通常検査ログ](../build-app-fixes-20261026/indexing-run.log)、[ASan/UBSanログ](../build-app-fixes-20261026/asan/indexing-run-final.log)
- [撮影の通常検査ログ](../build-app-fixes-20261026/camera-run-authorized.log)、[ASan/UBSanログ](../build-app-fixes-20261026/asan/camera-run-authorized.log)
- [修正後PDFの再描画](../build-app-fixes-20261026/print-output/landscape-300dpi-nosim-rendered.png)
- [最終状態の照合](../build-app-fixes-20261026/final-invariants.json)
