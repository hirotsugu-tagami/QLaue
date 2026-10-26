# QLaue — アプリから使える機能の監査（2026-10-26）

## 結果と対象

現行の未commitソースをビルドし、画面から利用できる機能を対象に調べた。
**実行・出力で6件の不具合を確認し、別に1件をコード上で確認した。**
無効化されているZoomの潜在問題は今回の指摘に含めない。
今回は監査であり、本体の修正・stage・commit・push・EXE/DMGの更新はしていない。

対象HEADは `8d9853bf012f42e71cf14e97720a1c5b9627bb9e`。
監査対象はこのコミットに以前からの未commit修正を加えた状態であり、公開済みr4インストーラーのバイナリーではない。
開始時の151ファイルの内容をSHA-256で記録し、監査後に変化がないことを照合した。

## 実行方法と制約

- macOS 26.7.1 / arm64 / Qt 5.15.15で本体を新規ビルドした。前回の一時Qt環境が削除されていたため、許可を得て同じQtバージョンを再取得した。
- `.app`の起動とプロセスの存在は確認できた。画面操作ツールはアプリを列挙する一方、接続時に`Invalid app`を返した。ユーザーの許可を得てAppleScriptにも切り替えたが、System EventsへのAppleEventがタイムアウト（-1712）した。
- そのため、**Macのネイティブ画面を実際に一通りクリックした検証は未完了**。ネイティブのファイル選択・Page Setup・Printの動作確認済みとはしない。
- 追加の画面試験は、製品と同じ`MainWindow`をQt offscreenで起動し、有効・表示中の`QAction`、ボタン、入力欄、マウス・キーイベントを操作した。非ネイティブのファイル選択で実ファイルを読み書きした。表示サイズは1600×1100の試験条件とした。
- PDFは、通常のPrintでダイアログ確定後に呼ばれる製品の`LaueFilm::print`で生成し、PopplerでPNGに変換して確認した。無効化された機能を強制実行したものではないが、ネイティブの印刷ダイアログ経由の検証でもない。
- カメラ通信はローカル模擬サーバーで検証した。実カメラ・実プリンター・Windowsでの実行は未実施。実測画像と独立した正解方位の照合も今回の対象外。
- 画面操作ツールでの識別を試すため、監査用`.app`の生成済みInfo.plistにBundle Identifier/Nameを付けた。本体ソース・製品のplistは変更していない。

## 指摘一覧

P2は通常操作で修正が必要な機能上の問題、P3は軽微な操作上の不整合。

| ID | 優先度 | 問題 | 確認方法 |
|---|---|---|---|
| UI01 | P2 | 画像・校正の変更後も、古い選択座標と索引結果が残る | 有効なメニュー、ファイル選択、クリック選択、Resolveを実行 |
| UI02 | P2 | 非表示にしたシミュレーションがPDFに印刷される | 実チェックボックスと製品の印刷描画、PDF再描画 |
| UI03 | P2 | Camera ControlsのSnap!・Binning・Int. Timeが撮影処理に接続されていない | 実コントロールの操作とCamera Setupへの反映を検査 |
| UI04 | P2 | Laue > Reset Orientationが無反応 | 実回転ボタンで方位を変更後、有効なメニューを実行 |
| UI05 | P3 | ツール欄を閉じてもWindowメニューのチェックが残る | ドックを閉じ、メニューを1回・2回操作 |
| UI06 | P2 | Page Setupの縦向きをPrint開始時に横向きへ上書きする | コード確認。ネイティブ画面での再現は未実施 |
| UI07 | P3 | PDFのDetector thresholdが2行重複する | 出力PDFの再描画とHTML生成処理を確認 |

### UI01 — 入力を変えても古い解析が残る

再現条件はP1、a=b=c=5.4 Å、検出器距離35 mm、既知の6点を持つ合成BMP。
画素サイズ500 µm、探索範囲H=3/K=1/L=1、角度許容3°、位置許容5 mmで手動選択した。
最初の結果は128候補、最良候補は6点一致、RMS 0.927004 mmだった。
画面上の画素丸めを含む試験であり、このRMSを科学的な精度保証に使わない。

1. Import Imageで画像を読み、Set Image Scaleで校正する。
2. Solve Orientationで6点を選択し、探索する。
3. Set Image Scaleで500 µmから1000 µmへ変更する。
4. 以前の128候補・6点一致が残る。Resolve Orientationを実行しても候補数・最良方位・RMSが完全に同一だった。
5. Import Imageで800×600の一様な灰色BMPへ置き換える。候補表128行が残り、Resolve後も「6点一致、RMS 0.927004 mm」と表示された。

選択位置は選択時のmm座標として保持される。校正の変更で元画像上の同じ点を再変換せず、画像の差し替えでも選択点・結果を無効化していないため、現在の画像・尺度で解析した結果と誤認できる。

根拠: [校正変更](/Users/hiro/Documents/QLaue/lauewidget.cpp:1238)、[保存済み選択座標によるResolve](/Users/hiro/Documents/QLaue/lauewidget.cpp:1362)、[画像差し替え](/Users/hiro/Documents/QLaue/lauewidget.cpp:1462)。

当面は、画像・原点・尺度を変更した後は、既存の候補表を使わず**Solve Orientationから全点を選び直す**。Resolveだけでは尺度変更に対応できない。

証拠: [校正変更前](../build-ui-audit-20261026/indexed-before-scale.png)、[変更後](../build-ui-audit-20261026/indexed-after-scale.png)、[無地画像でも6点一致](../build-ui-audit-20261026/blank-image-six-hits.png)。

### UI02 — Show simulationの非表示がPDFに反映されない

1. 画像を読み込み、Laue ControlsのShow simulationをOFFにする。
2. 画面では計算スポットが消える。チェックとモデルの表示フラグは両方falseであることを確認した。
3. 通常Printと同じ描画処理でPDFを作ると、赤い計算スポットが多数印刷された。

画面描画は`DisplayLaue`を確認するが、印刷は常に`paintLaue`を呼んでいる。
画面で選んだ表示状態と出力が一致せず、画像だけを出力したい場合に不要な計算点が重なる。

根拠: [画面側の条件](/Users/hiro/Documents/QLaue/lauewidget.cpp:310)、[印刷側の無条件描画](/Users/hiro/Documents/QLaue/lauewidget.cpp:354)。
証拠: [非表示の画面](../build-ui-audit-20261026/hidden-simulation-screen.png)、[PDFの描画結果](../build-ui-audit-20261026/hidden-simulation-print.png)。
今回の位置合わせ回帰試験は成功しており、この問題は以前の画像とスポットの位置ずれとは別である。

### UI03 — Camera Controlsが操作可能なまま未接続

1. 右側Camera ControlsでBinningを4、Int. Timeを0.5に変更する。
2. Snap!を押す。撮影スレッドは開始せず、状態通知も0回だった。
3. Laue > Camera Setupを開くと、撮影処理の値はBinning=2、露光=10のままだった。

`PSLCameraWidget`はUI作成とサイズ設定だけであり、ボタン・設定を撮影処理へつなぐ接続がない。
撮影したつもり、設定を変更したつもりになる問題。ローカル模擬サーバーによる通信処理自体の回帰試験は成功している。

根拠: [Camera Controlsの実装](/Users/hiro/Documents/QLaue/pslcamera.cpp:35)、[有効なメニュー経路](/Users/hiro/Documents/QLaue/mainwindow.cpp:398)。
当面の操作経路は**Laue > Camera Setupで設定し、Laue > Aquire Imageで取得**。実機との接続確認は別途必要。

### UI04 — Reset Orientationのメニューが無反応

Crystal RotationsのX矢印を2回押して方位が変わったことを確認し、Laue > Reset Orientationを実行した。
メニューは有効・表示中だが、実行前後で方位行列は完全に同じ。ダイアログ等も開かない。

`actionReset_Orientation`はメニューに配置されているが、処理への接続がない。
`LaueFilm::resetOrientation`という同名に近い関数は、2スポット指定後に指数を入力する別用途である。
修正時に名前だけを見てこの関数へ接続すると、意図しない操作になるので注意が必要。

根拠: [Laueメニューの接続](/Users/hiro/Documents/QLaue/mainwindow.cpp:411)、[2点入力後の処理](/Users/hiro/Documents/QLaue/lauewidget.cpp:1442)。
既知の方位を設定し直す場合はCrystal > Set Orientationを使う。

### UI05 — 閉じたツール欄とメニューのチェックが不一致

1. Crystal Rotationsをドックの閉じるボタン相当の操作で閉じる。
2. Window > Tools > Rotations ToolboxはチェックONのまま。
3. メニューを1回選ぶとチェックだけがOFFになり、欄は開かない。2回目に開く。

メニューからドックを表示する片方向の接続しかなく、ドック側の表示変更がチェックへ返されない。
同じ構成の他のツール欄にも同様の同期不足があるが、上記はRotationsで実行確認した。

根拠: [ツール表示の接続](/Users/hiro/Documents/QLaue/mainwindow.cpp:419)。
当面は該当メニューを2回選べば開き直せる。

### UI06 — Page Setupの用紙方向をPrintが上書きする（コード確認）

Page SetupはMainWindowが保持する`QPrinter`を編集する。
その後File > Printを選ぶと、印刷ダイアログを表示する直前に同じ`QPrinter`へ`Landscape`を設定している。
したがってPage Setupで指定したPortraitを保持できない。
さらにダイアログ確定後にもLandscapeを設定しているが、その時点は描画開始後であり、実際の挙動は印刷バックエンドにも依存する。

根拠: [Page Setup](/Users/hiro/Documents/QLaue/mainwindow.cpp:838)、[Print開始時の上書き](/Users/hiro/Documents/QLaue/mainwindow.cpp:797)。
この項目はソース上の経路からの確認であり、Macのネイティブ印刷画面での実行再現・出力方向の検証とは区別する。

### UI07 — PDFの検出しきい値の行が重複する

出力PDFのパラメーター欄にDetector threshold（ソース上の表記は`Detector threshhold`）が2行あり、両方とも同じ2%が印刷される。
HTML生成処理に同じ見出し・値の追加が2回ある。計算値の誤りではなく、出力表示の軽微な重複。

根拠: [同じ行を2回追加する箇所](/Users/hiro/Documents/QLaue/htmlgenerators.cpp:178)、[PDFの描画結果](../build-ui-audit-20261026/hidden-simulation-print.png)。

## 機能別の確認範囲

| 機能 | 実施した確認 | 結果・残る範囲 |
|---|---|---|
| New / Open / Save / Save As / Recent / Close | 保存先、未保存確認、Cancel/Discard/Save、破損入力、保存失敗、画像付き往復 | 既存persistenceと追加メニュー試験成功。ネイティブファイル選択は未確認 |
| 画像Import / Show / Invert / Rotate / 明暗調整 | BMP大小文字・日本語名・パレット・拡張子なし、PNG/JPEG/TIFF、取消し・失敗時保持、変換往復 | 成功。差し替え後の解析状態にUI01 |
| Set Lattice / Import CIF / CEL / 原子編集 | 提供CIF原本の15座標・24対称位置、各晶系、無効入力、正常入力の再編集 | 既存検査成功。CIFは有効なSet Lattice内のImport CIFが対象 |
| Crystal Rotations / 方位指定 / ドラッグ / 軸回り回転 | XYZ正負の反復、正しい回転行列、2スポット入力後の9成分同期、ドラッグの数値安全性 | 既存検査成功。Reset OrientationはUI04 |
| 原点 / 尺度 / Reset | hoverと確定、Esc取消し、ツール切替、幅測定、非零原点の尺度変更・保存 | 既存検査成功。解析後の座標再利用にUI01 |
| Solve / Resolve / 候補選択 / Cancel | 既知3晶系、外れ点、候補上限、順位、明示的適用、中断・再開、画像・校正変更 | 既存indexing成功。追加操作の組合せでUI01 |
| 検出器・強度・Accuracy / Preferences | UI端点、割合の復元、電圧・波長、補正設定、陽極元素 | settings-regressions等成功 |
| Labels / Reset Labels | 表示切替、199個の永続ラベル、上限時の保持とリセット | 既存image-state・追加切替成功 |
| Print / PDF / 結晶情報表示 | 縦横・72/300 dpi、画像表示/非表示の位置合わせ、画面状態復元、PDF再描画 | 位置合わせ成功。UI02、UI06、UI07。ネイティブ印刷と実プリンターは未確認 |
| Camera Setup / Aquire / Camera Controls | 設定画面の開閉、模擬通信・切断・中断・入力境界、ドックからの反映 | 通信試験成功。UI03。実カメラ未確認 |
| Window / Tools / About | 5種類のツール表示切替、閉じた後の再表示、About・各ダイアログ開閉 | UI05。Bring All To FrontのOS上での前面化は未確認 |

`Zoom`、`Reset Zoom`、File > Importの旧CIF項目は無効化されており、今回の不具合数に含めない。
自動スポット抽出・最小二乗精密化・等価候補の統合は未実装の機能であり、今回の新規不具合には数えていない。

## 実行結果・再現資料

既存11検査はすべて現行ソースからビルドして成功した。
`image_import` / `crystal_features` / `indexing` / `numerics` / `persistence` / `audit_fixes` /
`image_state` / `settings_regressions` / `lattice_constraints` / `print_alignment` / `camera`。
カメラの最初の実行はsandboxによるローカルlisten拒否で停止し、許可後の再実行で成功した。アプリの不具合として数えていない。
提供CIF原本を指定した座標検査も別途成功した。今回ASan/UBSanは再実行していない。

追加監査は不具合の再現を含めて最後まで終了した。
既存の検査が成功していることと、今回追加した操作の組合せに不具合がないことは同義ではない。

証拠はワークスペースの`build-ui-audit-20261026/`に保存した（Git管理対象外）。

- [本体・既存検査の実行スクリプト](../build-ui-audit-20261026/run_checks.py)、[11検査の結果](../build-ui-audit-20261026/results.json)
- [追加の画面操作プローブ](../build-ui-audit-20261026/reachable.cpp)、[qmakeプロジェクト](../build-ui-audit-20261026/reachable.pro)、[最終実行ログ](../build-ui-audit-20261026/reachable-run-final.log)
- [提供CIF原本の座標検査](../build-ui-audit-20261026/user-cif-run.log)、[カメラ模擬通信検査](../build-ui-audit-20261026/camera-run-authorized.log)
- [開始時のファイルハッシュ](../build-ui-audit-20261026/start-manifest.json)、[最終照合](../build-ui-audit-20261026/final-invariants.json)

追加プローブは対象ディレクトリでqmake・makeを実行し、`QT_QPA_PLATFORM=offscreen ./reachable-ui-audit <build-ui-audit-20261026の絶対パス>`で実行する。
`fixtures/`には読み込み用画像・CIF・CEL・不正入力と、合成スポットの元座標JSONがある。
Save Asの検査は新規ファイル名で実行し、再実行時はプローブの出力名を変えて上書き確認との混同を避ける。
