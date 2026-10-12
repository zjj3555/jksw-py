# juku-switch-player

极简的 Switch 自制播放器（libnx + libmpv），**完全绕过 tv.nro 面板**，用来直接播放剧库的 HLS 直链片单。

## 它做什么
1. 启动时读取 `sdmc:/juku/config.json` 里的 `source` 字段（本地路径或 http(s) URL）。
2. 拉取片单 JSON：`{ "title": "...", "items": [ { "name":"片名", "sources":[{"name":"线路","url":"...m3u8"}] } ] }`。
3. 列表选片 → mpv 硬解播放 HLS。多线路可手动选源。

## 用法
- **零 PC 离线**：用 `gen_jukulist.py` 烤好直链 → `sdmc:/juku/list.json`，`config.json` 指向它。
- **实时**：VM 上跑 `juku_adapter.py`，`source` 指向 `http://VM_IP:8090/juku/list.json`。
- **先验证播放**：把 `SD/juku/list.json.example` 改名成 `list.json` 放进去即可（公开测试流，无需 VM）。

## 编译（两种）
- **本地**：装 devkitPro（`pacman -S switch-dev switch-libnx switch-libmpv switch-libcurl`）后 `make`。
- **云端（推荐）**：push 到本仓库 → 在 Actions 页手动触发 `build-juku-nro` → 下载 Artifacts 里的 `juku.nro`。

## 部署到 Switch（Atmosphere）
```
sdmc:/switch/juku/juku.nro        # 本体
sdmc:/juku/config.json            # {"source":"sdmc:/juku/list.json"}
sdmc:/juku/list.json              # 片单(离线模式需要)
```
HBMenu 里打开即玩。

## 操作
↑/↓ 选择 · A 播放/确认 · B 返回 · X 刷新片单 · + 退出
