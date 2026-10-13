#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
juku_to_list.py —— 把 juku 适配器的 static_juku.json (点播) 转成 build.py 能吃的 list.json

输入: juku_adapter/static_juku.json
  {
    "list": [
      { "vod_name": "...", "vod_play_url": "第1集$url$$$第1集$url2#第2集$url3", ... },
      ...
    ]
  }

vod_play_url 约定:
  - 用 $$$ 分隔不同「源/线路」
  - 每个源内可用 # 分隔不同集: 第N集$url
  - 这里只取每段「第1集」(第一个 $ 前的那段) 作为该线路的播放地址

输出: { "title": "...", "items": [ { "name", "sources": [ {name, url} ] } ] }
  sources.name 形如 "线路3 (fengbao13.com)"

用法:
  python juku_to_list.py --in ../juku_adapter/static_juku.json --out real_list.json
"""
import os
import sys
import json
import argparse
from urllib.parse import urlparse


def host_of(url):
    try:
        return urlparse(url).netloc or "src"
    except Exception:
        return "src"


def parse_play_url(play_url):
    """返回 [(线路名, url), ...]，每段 $$$ 取第1集。"""
    out = []
    for seg in (play_url or "").split("$$$"):
        seg = seg.strip()
        if not seg:
            continue
        # 取该源的第1集: 按 # 切, 取第一段
        first = seg.split("#")[0].strip()
        if "$" in first:
            ep, url = first.split("$", 1)
            url = url.strip()
        else:
            # 没有 第N集$ 前缀, 整段当作 url
            ep, url = "", first.strip()
        if url.startswith(("http://", "https://")):
            name = "线路%d (%s)" % (len(out) + 1, host_of(url))
            out.append((name, url))
    return out


def convert(in_path):
    with open(in_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    vods = data.get("list", [])
    items = []
    for v in vods:
        name = v.get("vod_name") or v.get("name") or "未命名"
        links = parse_play_url(v.get("vod_play_url", ""))
        if not links:
            continue
        items.append({
            "name": name,
            "sources": [{"name": n, "url": u} for n, u in links],
        })
    return {"title": "Juku 真实片单", "items": items}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True, help="static_juku.json 路径")
    ap.add_argument("--out", dest="out", default="real_list.json")
    a = ap.parse_args()
    items = convert(a.inp)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(items, f, ensure_ascii=False, indent=2)
    print("已转换: %d 部剧 -> %s" % (len(items["items"]), a.out))
    for it in items["items"][:5]:
        print("  - %s (%d 线路)" % (it["name"], len(it["sources"])))


if __name__ == "__main__":
    main()
