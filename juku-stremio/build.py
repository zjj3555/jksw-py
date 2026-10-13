#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build.py —— 把 juku 的免登录片单 (list.json) 转成静态 Stremio 插件文件。

输入 juku 片单格式:
{
  "title": "片单标题",
  "items": [
    { "name": "片名",
      "sources": [ { "name": "线路A", "url": "https://.../x.m3u8" } ] }
  ]
}

输出 (相对于本目录 juku-stremio/):
  catalog/movie/juku-hls.json        => { "metas": [ {id,type,name,...} ] }
  stream/movie/<id>.json             => { "streams": [ {url,title} ] }

用法:
  python build.py --in ../SD/juku/list.json
  python build.py --in example_list.json --out .

注意:
  - 只处理 http/https 的 HLS 直链 (免登录源)。需登录的网盘源不要放进 list.json。
  - 每个 item 的多个 source 会变成播放时的多条线路可选。
  - 生成的静态文件可直接放 GitHub Pages 托管，manifest URL 即插件安装地址。
"""
import os
import sys
import json
import argparse
import re


def slugify(idx):
    """生成稳定、纯 ASCII 且文件名安全的 id（中文只用于显示 name）。"""
    return "juku-%04d" % idx


def build(in_path, out_dir):
    with open(in_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    items = data.get("items", [])
    metas = []
    stream_count = 0

    catalog_dir = os.path.join(out_dir, "catalog", "movie")
    stream_dir = os.path.join(out_dir, "stream", "movie")
    os.makedirs(catalog_dir, exist_ok=True)
    os.makedirs(stream_dir, exist_ok=True)

    for i, it in enumerate(items):
        name = it.get("name", "未命名")
        sources = it.get("sources", []) or []
        # 只保留 http/https 直链
        links = [s for s in sources if isinstance(s, dict) and str(s.get("url", "")).startswith(("http://", "https://"))]
        if not links:
            continue
        sid = slugify(i)
        metas.append({
            "id": sid,
            "type": "movie",
            "name": name,
            # 给一个占位海报，避免 Stremio 客户端显示空白（可选）
            "poster": "https://raw.githubusercontent.com/zjj3555/jksw-py/main/juku-stremio/poster.png",
        })
        streams = []
        for s in links:
            streams.append({
                "url": s.get("url"),
                "title": s.get("name") or "默认线路",
            })
        stream_obj = {"streams": streams}
        with open(os.path.join(stream_dir, sid + ".json"), "w", encoding="utf-8") as f:
            json.dump(stream_obj, f, ensure_ascii=False, indent=2)
        stream_count += len(streams)

    catalog_obj = {"metas": metas}
    with open(os.path.join(catalog_dir, "juku-hls.json"), "w", encoding="utf-8") as f:
        json.dump(catalog_obj, f, ensure_ascii=False, indent=2)

    print("已生成: %d 个片单, %d 条播放线路" % (len(metas), stream_count))
    print("catalog -> %s" % os.path.join(catalog_dir, "juku-hls.json"))
    print("stream  -> %s/" % stream_dir)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", default="example_list.json",
                    help="juku 片单 list.json 路径")
    ap.add_argument("--out", dest="out", default=None,
                    help="输出目录(默认脚本所在目录)")
    a = ap.parse_args()
    out_dir = a.out or os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(a.inp):
        sys.stderr.write("找不到输入文件: %s\n" % a.inp)
        sys.exit(1)
    build(a.inp, out_dir)


if __name__ == "__main__":
    main()
