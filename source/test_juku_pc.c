// test_juku_pc.c —— PC 端验证: 与 juku_client.py 逐字段对照
// 1) 同一密钥 SPKI b64 <-> python diff (确定性)
// 2) 固定 ts/nonce/body 签名串 <-> python diff (确定性)
// 3) C 侧签名自验 (uECC_verify)
// 4) --live: 直连 api.jukuku.xyz register + getHomeData + detail (经系统 curl)
#include "juku_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/uECC.h"
#include "third_party/sha256.h"

static int http_via_curl(void *ud, const char *method, const char *url,
                         const char *headers, const char *body, char *resp, int resp_sz) {
    (void)ud;
    static char cmd[16384];
    char bodyfile[256], respfile[256];
    // 沙箱下子进程写 %TEMP% 被拦, 改写到当前目录
    snprintf(bodyfile, sizeof bodyfile, "jk_body.bin");
    snprintf(respfile, sizeof respfile, "jk_resp.bin");
    FILE *f = fopen(bodyfile, "wb");
    if (!f) { fprintf(stderr, "[http] fopen body fail: %s\n", bodyfile); return -1; }
    if (body) fputs(body, f);
    fclose(f);
    int o = snprintf(cmd, sizeof cmd, "curl.exe -sS -k --retry 3 --retry-all-errors --connect-timeout 15 -X %s", method);
    for (const char *p = headers; *p; ) {
        const char *e = strstr(p, "\r\n");
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len > 0 && len < 512)
            o += snprintf(cmd + o, sizeof cmd - o, " -H \"%.*s\"", len, p);
        if (!e) break;
        p = e + 2;
    }
    snprintf(cmd + o, sizeof cmd - o,
             " --data-binary @\"%s\" \"%s\" -o \"%s\"", bodyfile, url, respfile);
    if (getenv("JK_DEBUG")) fprintf(stderr, "[http] cmd: %.400s\n", cmd);
    int src = system(cmd);
    f = fopen(respfile, "rb");
    int n = 0;
    if (f) { n = (int)fread(resp, 1, resp_sz - 1, f); fclose(f); }
    resp[n] = 0;
    remove(bodyfile);
    if (getenv("JK_DEBUG")) fprintf(stderr, "[http] system rc=%d got %d bytes: %.200s\n", src, n, resp);
    return 200;
}

static int b64_dec(const char *in, unsigned char *out, int outmax) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned acc = 0; int bits = 0, o = 0;
    for (const char *p = in; *p && *p != '='; p++) {
        const char *q = strchr(T, *p);
        if (!q) continue;
        acc = (acc << 6) | (unsigned)(q - T); bits += 6;
        if (bits >= 8) { bits -= 8; if (o >= outmax) return -1; out[o++] = (unsigned char)(acc >> bits); }
    }
    return o;
}

int main(int argc, char **argv) {
    int live = argc > 1 && strcmp(argv[1], "--live") == 0;

    JukuCtx ctx; memset(&ctx, 0, sizeof ctx);
    int kr = jk_load_or_create_key(&ctx, "test_key.bin");
    if (kr) { printf("key load fail: %d\n", kr); return 1; }
    printf("[C] key OK (pub XY first bytes: %02x%02x%02x%02x...)\n",
           ctx.pub[0], ctx.pub[1], ctx.pub[2], ctx.pub[3]);

    char spki[256];
    jk_pub_spki_b64(&ctx, spki, sizeof spki);
    printf("[C] SPKI: %s\n", spki);

    const char *ts = "1760000000000", *nonce = "00112233445566778899aabbccddeeff";
    const char *body = "{\"publicKey\":\"TEST\",\"appVersion\":\"1.15.0\",\"platform\":\"desktop\",\"installId\":\"x\"}";
    char bh[65]; jk_sha256_hex(body, strlen(body), bh);
    char ss[1024];
    strcpy(ctx.device_id, "DEV-TEST-123");
    strcpy(ctx.access_token, "TOK-TEST-456");
    jk_build_signing_string(&ctx, "POST", "/auth/device/register", "", body, ts, nonce, ss, sizeof ss);
    printf("[C] bodyhash: %s\n", bh);
    printf("[C] signing_string:\n%s\n<<END\n", ss);

    char sig[128];
    if (jk_sign_b64(&ctx, ss, sig, sizeof sig) < 0) { printf("sign fail\n"); return 1; }
    printf("[C] signature: %s\n", sig);

    // 自验: sha256(ss) + uECC_verify(raw r||s)
    {
        SHA256_CTX c; unsigned char hash[32], p1363[64];
        sha256_init(&c); sha256_update(&c, (const BYTE*)ss, strlen(ss)); sha256_final(&c, hash);
        if (b64_dec(sig, p1363, 64) != 64) { printf("b64 decode fail\n"); return 1; }
        const struct uECC_Curve_t *curve = uECC_secp256r1();
        int ok = uECC_verify(ctx.pub, hash, 32, p1363, curve);
        printf("[C] verify: %s\n", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    if (live) {
        printf("\n== LIVE: register + home + detail ==\n");
        char resp[262144];
        int rc = jk_ensure_session(&ctx, http_via_curl, NULL);
        printf("session rc=%d\n", rc);
        if (rc) {
            fprintf(stderr, "[dbg] register/refresh resp dump: (see stderr from http cb)\n");
        }
        if (rc == 0) {
            const char *const kv0[][2] = {0};
            rc = jk_request(&ctx, http_via_curl, NULL, "GET", "/videos/getHomeData", kv0, 0, NULL, resp, sizeof resp);
            printf("getHomeData rc=%d resp: %.100s\n", rc, resp);
            rc = jk_get_detail(&ctx, http_via_curl, NULL, "215231", resp, sizeof resp);
            printf("detail(215231) rc=%d resp: %.140s\n", rc, resp);
        }
    }
    printf("done\n");
    return 0;
}
