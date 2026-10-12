#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_jukulist.py —— 生成离线片单 list.json (零 PC 播放模式用)

用法(在能访问适配器的机器上运行):
    python gen_jukulist.py --out SD/juku/list.json \
        --url http://<适配器IP>:8090/juku/list.json

它会把适配器返回的 {title,items} 原样写盘。然后把 list.json 拷到
Switch SD 卡:  sdmc:/juku/list.json
juku.nro 启动时读 sdmc:/juku/config.json 里的 source 指向它即可。

注意: 生成的直链是带时效的 HLS 链接(通常数小时有效)。要长期看就
改用「实时模式」: 把 config.json 的 source 指向 http://<VM_IP>:8090/juku/list.json
(需 Switch 能直连该地址, 即 VM 桥接或云主机)。
"""
import sys, json, argparse, urllib.request

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8090/juku/list.json")
    ap.add_argument("--out", default="SD/juku/list.json")
    a = ap.parse_args()
    req = urllib.request.Request(a.url, headers={"User-Agent": "juku-gen/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            data = json.loads(r.read().decode("utf-8", "replace"))
    except Exception as e:
        sys.stderr.write("拉取失败: %s\n" % e); sys.exit(1)
    import os
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    items = data.get("items", [])
    print("已写入 %s  (共 %d 个可播项)" % (a.out, len(items)))

if __name__ == "__main__":
    main()
