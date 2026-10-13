# 真实片单（隐私保护版）—— 两种走法

本插件设计为**仓库公开 + Pages 永远只跑 example**。你的真实 juku 源
含直链/剧名/可能有隐私，**不要**提交到公开仓库。请选下面任一走法：

---

## 走法 A（推荐）：本地起一个静态 HTTP 服务，Switch 直连你电脑

最简单，零额外账号，零成本，隐私完全留在你机器上。

1. **本地用真实 list.json 重新生成**：

   ```bash
   cd juku-stremio
   cp /path/to/your/list.json ./list.json
   python build.py --in list.json --out .
   # 这会覆盖 catalog/movie/juku-hls.json 和 stream/movie/*.json
   ```

2. **在你电脑上起个 HTTP 服务**（仅局域网可访问）：

   ```bash
   # 必须监听 0.0.0.0，否则 Switch 拿不到
   cd juku-stremio
   python -m http.server 8090 --bind 0.0.0.0
   ```

3. **临时改 manifest 的资源指向**（用本机 IP）：

   打开 `juku-stremio/manifest.json`，把里面的 URL 全部改成：
   ```
   http://192.168.x.x:8090/
   ```
   把 `192.168.x.x` 换成你电脑的实际 IP（`ipconfig` 看 IPv4）。

4. **Switch 上装这个本地插件**：

   - 同样在 SwitchStream 的 Addons → 自定义插件
   - 填 manifest URL：`http://192.168.x.x:8090/manifest.json`
   - 注意：Switch 必须和你电脑**同 WiFi/同局域网**；首次访问会问"信任该 HTTP 源"，选确认。

5. **用完关掉服务**：`Ctrl+C` 即可，列表就消失了。

> 适合：临时用，或只想在家里用一段时间，隐私要求高。

---

## 走法 B：用 Netlify Drop / Cloudflare Pages 等"私有 Pages" 替代公开 Pages

如果你想 Switch 在家外也能用（4G/外网），但又不想让仓公开：

1. 同样先在本地 `python build.py --in list.json` 生成真实 catalog/stream。
2. 把整个 `juku-stremio/` 目录（含真实生成的 catalog/stream）拖到
   [Netlify Drop](https://app.netlify.com/drop)，得到一个 `xxx.netlify.app/manifest.json`。
3. SwitchStream 装这个 manifest URL。
4. **别把目录推回 GitHub**——只在 Netlify 上保留。如果更新源，重新拖一次即可。

> 适合：家里/外面都要用，但仓库想保持私有。

---

## ⚠️ 千万不要做的事

- **不要把真实 list.json 推到 `zjj3555/jksw-py` 公开仓**。
  本 workflow 已经锁定只用 `example_list.json` 部署，即使你不小心
  把 list.json 复制到 `juku-stremio/` 也没事，**但只要 `git add` 它就
  会出现在 commit 里**——请检查 `git status` 再 commit。
- **不要把真实生成后的 catalog/stream 推到这个仓**。
  同理，会暴露片单/直链给所有人。

---

## 仓公开之后用户会看到什么

GitHub Pages 上只有 example_list.json 的内容（一个公开 HLS 测试流），

没有你的真实片单，没有个人凭据，**安全**。
