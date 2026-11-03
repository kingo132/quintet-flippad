# 天地创造 / 创世封魔录 · Pixel 9 / FlipPad

两个独立 Android 应用，共享 Snes9x 执行核心，各自使用专用的完整地图显示器。手机桌面只显示游戏名。版本 0.1.3，Android 17 / API 37，arm64，支持 16 KB 内存页。

此仓库包含完整修改源码、构建脚本、图标及依赖许可。**不包含游戏 ROM、汉化文本、游戏美术、用户存档、签名密钥或含游戏内容的安装包。** 使用自己已有的游戏文件在本地构建。

## 画面与控制

- 默认竖屏、4 倍整数像素，Pixel 9 满宽 270 个真实世界像素；扩展区域读取游戏 WRAM 中的完整地图，不把 VRAM 环形缓冲重复铺到屏外。
- 游戏场景保留原始画面的像素、对话和动画。仅在识别到对应 HUD 时将其移到物理屏幕顶部；不避开摄像头。
- 房间边界以外保留黑色，加载和不支持的场景退回原始画面。原始视野可以在助手中切换。
- FlipPad 弧形底边；插拔手柄自动切换触屏按键。自动视野按屏幕可用高度计算。
- Select 短按：中文游戏助手；长按：原版 Select。Start 保留原版功能。
- L1 / L2：减速 / 加速；R1：缩放视野；R2：滤镜。
- 默认游戏 2 倍速，音乐正常速率（手机媒体静音状态会在助手中提示）；可选 0.5 / 1 / 2 / 3 / 4 倍速。
- 六个独立手动槽，独立后台续玩。写入采用同步临时文件和原子替换，上一份保留 `.bak`。

《天地创造》开场尚未获得箱子时，原版物品菜单不可用，沿用游戏本来的剧情限制。

## 构建

需要 JDK 17、Android SDK 37.0、Build Tools 37.0.0、NDK 28.2.13676358。默认 SDK 路径为 macOS 的 `~/Library/Android/sdk`，其他平台设置 `ANDROID_SDK_ROOT`。支持 macOS / Linux。

```sh
./tools/build_quintet_android.sh
./android-app/gradlew -p android-app assembleRelease \
  -PsoulblazerRom='/absolute/path/SoulBlazer_CN_v0.9.sfc' \
  -PterranigmaRom='/absolute/path/天地创造.sfc'
```

安装包位于 `android-app/app/build/outputs/apk/{soulblazer,terranigma}/release/`。本地发布默认使用 Android 开发签名；更新既有安装应使用同一密钥。发布构建关闭调试入口。

省略 ROM 参数可编译不含游戏的版本，供 CI 检查使用。这种版本不能启动游戏。源文件的 SHA256 在对应 flavor 的资源中固定，运行前会校验输入，避免读错游戏或汉化版本。

## 验证边界

验证包括从空存档启动、中文输入/对话、房间切换、试炼战斗、天地创造村庄及四方向滚动、六槽读写与后台恢复、五档速度、声音输出、触屏布局、图标及 Android 打包。完整原始状态与原始视野的对照检查可使用 `tools/headless.c` 和 `tools/verify_pair.c`，私有游戏和测试进度必须由本地提供。

这是游戏专用的实时扩展显示，尚未完成整部游戏的通关验证。复杂战斗、特殊地图或过场可能回退原始视野；后续发现问题应保留存档和场景记录再修复。

## 来源与许可

基于 [libretro/snes9x](https://github.com/libretro/snes9x) `fae2fea08f74180759ef540ee94259213f503480`。通用 Tall 显示器与 Android 宿主沿用本地 SFC Tall 项目，专用地图显示代码沿用本地 Babel Recomp FlipPad 适配。WRAM 格式独立参照 RustyBlazer / Crysta 交叉核对。

原始 Snes9x 说明保存在 [README-Snes9x.md](README-Snes9x.md)，许可见 [LICENSE](LICENSE)；滤镜及参考项目的具体来源、原始许可和通知见 [android/THIRD_PARTY_NOTICES.md](android/THIRD_PARTY_NOTICES.md) 与 `android-app/app/src/main/assets/notices/`。各组件保留其原有许可，此说明不对依赖重新授权。

私有游戏的自动对照可在本地运行（结果目录不得提交到仓库）：

```sh
make -C libretro platform=osx ARCH=arm64 -j8
clang -O2 -Ilibretro tools/verify_pair.c libretro/snes9x_libretro.dylib -o /tmp/quintet-verify-pair
DYLD_LIBRARY_PATH="$PWD/libretro" python3 tools/verify_quintet.py \
  --binary /tmp/quintet-verify-pair --rom '/absolute/path/天地创造.sfc' \
  --profile 2 --frames 600 --out /tmp/quintet-private-results \
  --state '/absolute/path/private-native.state' \
  --script '/absolute/path/private-controller.keys' --present-every 4
```

输入脚本每行 `开始帧 结束帧 按键十六进制掩码`，区间左闭右开，掩码使用 libretro 标准 SNES 位序。省略 `--state` 可从标题启动，省略 `--script` 则不输入。`--profile 1` 对应创世封魔录。对照完整 CPU/内存/音频状态，并逐行比较未有意改动的原始画面区域；地图审计独立比较 WRAM 完整地图与原版 VRAM。

## 0.1.2 显示修正

三款游戏现在共用 `shared/portrait_rules.h` 的窗口、10 位滚动、OBJ 图块回绕和 OAM 遮挡规则（盖亚仓库保留相同文件）。`tools/test_portrait_rules.c` 在 CI 检查边界与遮挡。具体的 WRAM 地图、动态美术、角色列表仍按游戏适配；此版本继续使用 Snes9x 执行原版游戏。

- 在可见帧开始锁定显示所需的角色数据，避免把下一帧动作接到当前原始画面上；修正 OBJ 垂直位置与相同深度排序。
- 颜色运算遵循本核心的 RGB565 输出与逐行亮度，消除创世封魔录云层的绿色精度接缝。
- 当前帧重置光栅缓存；加载中的地图需通过 VRAM 对照。不支持的马赛克/大图块布局回到原始视野。
- 原始画面中需要重绘的区域每帧只计算一次，降低重复判断开销。
- Debug 构建使用独立 `.qa` 包，测试进度与正式版隔离；正式版保持既有包名和存档。

开发诊断可设置 `QUINTET_RECONSTRUCT_NATIVE=1`，让扩图路径重绘原始视野，用独立的原版渲染检查地图、精灵和混色。这个模式会省略 BG3 对话与 HUD，仅用于本地开发，不能作为正常游玩模式。普通构建继续保留完整原始画面。完整对照状态应始终一致，但重绘目前只在有限场景验证，未承诺所有动作、特效和地图逐像素相同。

## 0.1.3 音频启动与恢复

冷启动必须先按硬件原速完成 SPC 程序上传，确认 DSP 退出复位/静音、完成初始化并有有效声部后，才启用正常音乐速率的倍速运行。先从已播放音乐的状态测试无法覆盖这个上传时序错误；新增 `tools/verify_audio_startup.py` 从冷启动验证 1/2/4 倍速。测试输出包含私有游戏数据，不能提交。

Android 17 宿主在获得前台窗口和音频焦点后创建 AudioTrack；离开前台或丢失焦点时标记旧轨道失效，恢复后重建，避免后台音频限制使旧轨道持续静音。写入失败时同样重建。

已经保存到旧续玩文件的损坏 SPC 状态不会仅凭音频焦点恢复；本轮在单独备份后对本地两个续玩文件只修复声音块，完整保留其他游戏状态。修复用的声音数据及用户存档不发布到仓库。
