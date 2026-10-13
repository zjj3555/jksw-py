# Juku 片单 → SwitchStream 插件（静态 HLS）

把 juku 的**免登录 HLS 直链片单**变成 Stremio 插件，接入 SwitchStream，
在 Switch 上按住 `[R]` 进应用模式启动 SwitchStream 后，即可看到 juku 片单并直接播 HLS。

> 网盘需登录源（阿里云盘/夸克/百度/115 等）本次**不含**在内，需要单独轻量服务解析，后续再做。

---

## 1. 目录结构

```
juku-stremio/
  manifest.json          # 插件声明（Stremio addon manifest）
  build.py               # 把 list.json 转成静态 catalog/stream 文件
  example_list.json      # 样例片单（juku list.json 格式）
  catalog/movie/juku-hls.json   # 生成：片单卡片
  stream/movie/juku-0000.json   # 生成：每片的播放直链
  .github/workflows/build-stremio.yml  # 推送后自动 build 并发布到 Pages
```

## 2. 准备你的片单（免登录 HLS）

把你 juku 的 `list.json` 准备好，格式：

```json
{
  "title": "我的片单",
  "items": [
    {
      "name": "片名",
      "sources": [
        { "name": "线路A", "url": "https://example.com/x.m3u8" },
        { "name": "线路B", "url": "https://example.com/y.m3u8" }
      ]
    }
  ]
}
```

**只放 http/https 直链（.m3u8）。** 需要登录才能拿到的网盘直链不要放这里。

## 3. 生成静态文件

```bash
cd juku-stremio
python build.py --in /path/to/your/list.json --out .
```

会生成 `catalog/movie/juku-hls.json` 和 `stream/movie/juku-XXXX.json`。

## 4. 托管到 GitHub Pages（零服务器）

1. 把 `juku-stremio/` 整个目录推到你的 `zjj3555/juku-switch-player` 仓库（main 分支）。
2. 仓库 **Settings → Pages**：
   - Source: **GitHub Actions**（本目录已提供 `build-stremio.yml`，会自动 build 并发布）。
   - 或选 **Deploy from a branch → main → / (root)** 也行（静态文件已生成）。
3. 等待部署完成，你的插件 manifest 地址是：
   ```
   https://zjj3555.github.io/juku-switch-player/juku-stremio/manifest.json
   ```

## 5. 在 SwitchStream 里安装

> ⚠️ **必须应用模式启动**：在 Switch 主菜单**按住 `[R]`** 启动任意游戏进入 HBMenu，
> 再从 HBMenu 启动 SwitchStream。相册(Applet)模式内存只有 ~400MB，mpv 解码必崩。

1. 进入 SwitchStream → **Addons**（插件）界面；
2. 选 **“安装自定义插件 / Enter addon manifest URL”**（会弹出键盘）；
3. 输入上面的 manifest URL：
   ```
   https://zjj3555.github.io/juku-switch-player/juku-stremio/manifest.json
   ```
4. 安装完成后，首页/片单里会出现 **“Juku 片单 (HLS)”**；
5. 点进去选片 → 选线路 → 直接播放 HLS。

## 6. 更新片单

改好 `list.json` 后：
- 本地先 `python build.py --in list.json` 重新生成；
- 推送到仓库，GitHub Actions 会自动重新 build 并发布到 Pages；
- SwitchStream 侧刷新片单即可（或在 Addons 里重新安装一次该插件）。

---

## 排错

- **装了插件但片单空白**：确认 manifest URL 能在浏览器直接打开（GitHub Pages 已部署），
  且 `catalog/movie/juku-hls.json` 的 `metas` 非空。
- **点播放没反应/报错**：确认直链是 `http(s)://...m3u8` 且公网可访问（Switch 能直连）。
- **SwitchStream 一启动就提示 Applet Mode**：说明没按 `[R]` 进应用模式，退出重来。
- **中文乱码不显示**：不影响播放，只是卡片标题；如需海报可放 `poster` 字段。
