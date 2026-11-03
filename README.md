# 天地创造 / 创世封魔录 · Pixel 9 / FlipPad

两个独立 Android 应用，共享 Snes9x 执行核心，各自使用专用的完整地图显示器。手机桌面只显示游戏名。版本 0.1.1，Android 17 / API 37，arm64，支持 16 KB 内存页。

此仓库包含完整修改源码、构建脚本、图标及依赖许可。**不包含游戏 ROM、汉化文本、游戏美术、用户存档、签名密钥或含游戏内容的安装包。** 使用自己已有的游戏文件在本地构建。

## 画面与控制

- 默认竖屏、4 倍整数像素，Pixel 9 满宽 270 个真实世界像素；扩展区域读取游戏 WRAM 中的完整地图，不把 VRAM 环形缓冲重复铺到屏外。
- 游戏场景保留原始画面的像素、对话和动画。仅在识别到对应 HUD 时将其移到物理屏幕顶部；不避开摄像头。
- 房间边界以外保留黑色，加载和不支持的场景退回原始画面。原始视野可以在助手中切换。
- FlipPad 弧形底边；插拔手柄自动切换触屏按键。自动视野按屏幕可用高度计算。
- Select 短按：中文游戏助手；长按：原版 Select。Start 保留原版功能。
- L1 / L2：减速 / 加速；R1：缩放视野；R2：滤镜。
- 默认游戏 2 倍速，音乐正常速率；可选 0.5 / 1 / 2 / 3 / 4 倍速。
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
