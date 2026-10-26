# QLaue 再監査10件の修正記録 — 2026-09-28

対象は[2026-09-27の再監査](AUDIT-2026-09-27-POSTFIX.md)で確定したR01〜R10。
**10件すべてを修正し、通常の回帰テスト10本とASan/UBSan付き6本を実行して成功した。**
これは未commitのソース変更であり、公開済みインストーラーには含まれない。
開始時HEADは`8d9853bf012f42e71cf14e97720a1c5b9627bb9e`。以前からの未commit修正は維持し、stage・commit・push・配布物更新は行っていない。

## 修正と再現条件の検証

| 指摘 | 修正 | 実行した回帰検査 |
|---|---|---|
| R01：Accuracy 0の保存拒否 | 保存検証の内部精度上限を11に揃え、UI 0〜10と一致させた。 | 実コントロールの両端0/10を変更し、XTLの保存・再読込で精度を保持。 |
| R02：読込時の割合の変化 | 表示値は整数%へ丸める。モデルからUIへ復元する間はsignalを抑止し、slider・関連コントロールの状態も明示的に更新。 | MainWindowの実読込で29/57、29/58、29.4/57.5%を検査。表示は丸めてもモデルの端数は保持。 |
| R03：格子と空間群の不整合 | 結晶系・群設定の変更時に従属値を同期。確定時はCIFと同じ格子計量の整合性・相対許容誤差1e-3で検証。 | Cubicの不等辺、各結晶系、P3/R:H/R:R、単斜晶各軸の切替と編集。All Space Groupsの不整合拒否、取消しと元データ保持、丸めを含む正常CIFの反復無変更確定。 |
| R04：画像非表示時の印刷縮尺 | 画像の座標準備を描画から分離し、非表示でも印刷領域に合わせる。印刷後は画面側の状態を復元。 | 画像の表示/非表示、画面リサイズ、72/300dpi、縦/横向き。非表示前後の印刷スポット位置差は1px未満。 |
| R05：Preferencesの無変更確定で補正変更 | 線源radioを復元し、編集対象外の補正bitと元の波長精度を保持。保存済み陽極元素も正しく表示・保持。 | 補正mask 0/4/513/1023、端数波長、Fe陽極の無変更往復。Cu→Wの適用、W再表示で重複しないことも確認。 |
| R06：2スポット指定後の古い方位表示 | 成功時に既存の方位更新通知へ接続した。 | 実MainWindowと2スポット入力ダイアログを操作し、計算終了後の方位パネル9成分がモデルと一致。 |
| R07：不正な距離・波長の受理 | UIに有効な下限を設け、モデルsetterは不正入力を拒否して以前の有効値を保持。計算開始時も不正状態を拒否。 | 0/NaN距離、負・逆転波長、低すぎる電圧、有効値への復帰。未設定波長の計算失敗と、不正XTL・保存時の検証も維持。 |
| R08：Esc後に校正変更・一時状態が残る | 原点hoverはプレビューに限定。クリックで確定し、ツールの開始・終了処理でcursor・追跡・選択枠を復元。一時的な非表示は描画時だけ適用する。 | hover中の実原点不変、Esc、押下中の取消し、取消し後のrelease、再開、ツール間切替、新画像への切替。作業中のスポット非表示と終了後の元画像への復帰。 |
| R09：Show Imageチェック不一致 | 画像コントロールの更新時に実際の表示状態をチェックへ同期し、不要なsignalは抑止。 | 実Import Imageで表示とチェックが一致し、最初の切替で画像が消え、次の切替で戻る。 |
| R10：尺度変更でビーム原点が移動 | UIの校正変更は原点の画像画素位置を保持。XTL復元用のsetterは保存された原点と尺度をそのまま復元。 | 画像上で原点をクリック後、Pixel size・DPI・幅測定を順に変更し、原点の描画移動1px未満、画像マーカーとのずれ2px未満を確認。画像付きXTLの非零原点・端数尺度も実MainWindowで復元。 |

原点のプレビューは保存値や印刷の原点へ混入しない。校正の開始・終了で、ユーザーが明示したシミュレーション表示/非表示も維持する。
All Space Groupsの編集値は自動補正せず、不整合なら確定前に説明する。正しく読み込めたCIFを厳密な等値比較で拒否しない。

## 独立レビューで修正中に検出した回帰

実装は設定・保存と格子制約を分担し、画像・校正・印刷は統括が担当した。別担当が変更後の経路を読み、次の3点を統合前に検出した。

1. 索引のスポット選択から校正へ切り替えると、前のツールの非表示・cursorを復元してしまう。開始・終了処理を選択ツールにも共通化し、一時的な非表示をモデルの表示設定から分離した。複数の先行ツールと明示的な表示ON/OFFをテストに追加した。
2. 既存の`getBit()`は全bit一致であり、複数モードのいずれかを調べる条件に使えなかった。直接mask判定へ修正し、既知の赤いスポットがツール作業中に実際の描画から消える検査を追加した。
3. Wの表示名から空白を除くと、既存元素表の完全一致検索と食い違い、選択を適用できなかった。元のcanonical名を保持し、Cu→Wの実選択・適用と、再表示時の重複防止を検査に追加した。

これらは最終検証前に修正済み。独立レビューだけでなく、修正後の実行検査で確認した。

## 最終検証

macOS arm64、Qt 5.15.15、Qt offscreenで実行。各検査を現行ソースから再ビルドした。

| 検査 | 通常 | ASan/UBSan |
|---|---|---|
| image-import-check | 成功 | 今回は未実行 |
| crystal-features-check | 成功 | 今回は未実行 |
| indexing-check | 成功 | 成功 |
| numerics-check | 成功 | 今回は未実行 |
| persistence-check | 成功 | 成功 |
| audit-fixes-check | 成功 | 今回は未実行 |
| image-state-check | 成功 | 成功 |
| settings-regressions-check（新規） | 成功 | 成功 |
| lattice-constraints-check（新規） | 成功 | 成功 |
| print-alignment-check | 成功 | 成功 |

- 開発用`QLaue.app`のビルド・リンクも成功。インストーラーは生成していない。
- ASan/UBSanの6本は診断エラーなし。Qtの終了時オブジェクトの影響を避けるためリーク検出は無効にした。メモリーリーク全般の検証済みとはしない。
- 既存CIFの報告座標15成分・対称サイト24個、XYZの連続回転、自動索引の既知方位・外れ点・取消し・再開、326313スポット、保存失敗時の保持、印刷時の画面状態保持も回帰検査で成功した。
- Windows配布スクリプトへ新規2チェックを追加した。Windowsでのビルド・実行、実カメラ・実プリンター、macOSのネイティブダイアログは今回実行していない。カメラのコードは変更せず、その模擬サーバー検査も今回は再実行していない。
- 実測像と既知方位を使った科学的精度の網羅検証ではない。今回の10件と追加した操作の組合せに対する修正・検証結果である。

## ファイルと再現方法

主な実装:
[lauewidget.cpp](/Users/hiro/Documents/QLaue/lauewidget.cpp)、
[mainwindow.cpp](/Users/hiro/Documents/QLaue/mainwindow.cpp)、
[crystaldialog.cpp](/Users/hiro/Documents/QLaue/crystaldialog.cpp)、
[lauecontrols.cpp](/Users/hiro/Documents/QLaue/lauecontrols.cpp)、
[preferences.cpp](/Users/hiro/Documents/QLaue/preferences.cpp)、
[laue.cpp](/Users/hiro/Documents/QLaue/laue.cpp)、
[datafile.cpp](/Users/hiro/Documents/QLaue/datafile.cpp)。

新規検査は従来と同じqmake方式で実行できる。例えば:

```sh
mkdir -p build-settings-regressions
cd build-settings-regressions
"$QT_BIN/qmake" ../tests/settings_regressions.pro
make -j2
QT_QPA_PLATFORM=offscreen ./settings-regressions-check
```

`lattice_constraints.pro`の実行名は`lattice-constraints-check`。
このMacのSDK構成ではqmakeへ`QMAKE_CXXFLAGS+=-isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1`を追加した。
ASan/UBSanには`-fsanitize=address,undefined -fno-omit-frame-pointer`をコンパイル時に、`-fsanitize=address,undefined`をリンク時に追加し、`ASAN_OPTIONS=detect_leaks=0`、`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`で実行した。

今回の実行記録は[通常検査結果](/private/tmp/qlaue-round2-root/results.json)、[ASan/UBSan結果](/private/tmp/qlaue-round2-asan/results.json)、[実行スクリプト](/private/tmp/qlaue-round2-root/run_checks.py)に保存した。
各ディレクトリの`*-run.log`、`*-build.log`と`print-output-final/`が詳細証拠。これらは一時ファイルであり、再実行に必要なテスト本体はリポジトリに含めた。
