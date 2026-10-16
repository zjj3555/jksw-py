// juku_api.c —— 剧库 api.jukuku.xyz 设备签名客户端（纯 C, 零依赖）
// 逐字段对齐 juku-deploy-repo/juku_client.py (权威参照):
//   签名串 = METHOD \n PATH \n canonicalQuery \n sha256hex(body) \n ts \n nonce \n deviceId \n accessToken
//   X-Signature = base64( raw r(32)||s(32) )   (micro-ecc uECC_sign 直接输出 P1363)
//   头: X-Device-Id / X-App-Version / X-Timestamp / X-Nonce / X-Body-Hash / X-Access-Token / X-Signature
// 加密: micro-ecc (P-256) + B-Con sha256。HTTP 由外部注入 (PC 测试用 curl 子进程, NRO 用 switch-libcurl)。
#include "juku_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "third_party/uECC.h"
#include "third_party/sha256.h"

#define JK_BASE_URL   "https://api.jukuku.xyz"
#define JK_APP_VER    "1.15.0"
#define JK_PLATFORM   "desktop"
#define JK_INSTALL_ID "juku-switch"

static const struct uECC_Curve_t *curve;

// ---------------- base64 ----------------
static int b64_enc(const unsigned char *in, int inlen, char *out, int outsz) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < inlen; i += 3) {
        unsigned v = in[i] << 16;
        int rem = inlen - i;
        if (rem > 1) v |= in[i+1] << 8;
        if (rem > 2) v |= in[i+2];
        if (o + 5 > outsz) return -1;
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (rem > 1) ? T[(v >> 6) & 63] : '=';
        out[o++] = (rem > 2) ? T[v & 63] : '=';
    }
    if (o >= outsz) return -1;
    out[o] = 0;
    return o;
}

// ---------------- hex ----------------
static void to_hex(const unsigned char *in, int n, char *out) {
    for (int i = 0; i < n; i++) sprintf(out + 2*i, "%02x", in[i]);
    out[2*n] = 0;
}

void jk_sha256_hex(const char *body, size_t len, char out[65]) {
    SHA256_CTX c; unsigned char d[32];
    sha256_init(&c);
    if (body && len) sha256_update(&c, (const BYTE*)body, len);
    sha256_final(&c, d);
    to_hex(d, 32, out);
}

// ---------------- RFC3986 percent-encode ----------------
static int pct_enc(const char *in, char *out, int outsz) {
    int o = 0;
    for (const unsigned char *p = (const unsigned char*)in; *p; p++) {
        unsigned char c = *p;
        int safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
        if (safe) { if (o + 1 >= outsz) return -1; out[o++] = (char)c; }
        else { if (o + 3 >= outsz) return -1; sprintf(out + o, "%%%02X", c); o += 3; }
    }
    out[o] = 0;
    return o;
}

// canonical_query: 按 key 排序后 k=v&k2=v2
int jk_canonical_query(const char *const kv[][2], int n, char *out, int outsz) {
    int idx[32];
    for (int i = 0; i < n && i < 32; i++) idx[i] = i;
    for (int i = 1; i < n && i < 32; i++)
        for (int j = i; j > 0 && strcmp(kv[idx[j-1]][0], kv[idx[j]][0]) > 0; j--) {
            int t = idx[j-1]; idx[j-1] = idx[j]; idx[j] = t;
        }
    char ek[256], ev[2048];
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (pct_enc(kv[idx[i]][0], ek, sizeof ek) < 0) return -1;
        if (pct_enc(kv[idx[i]][1], ev, sizeof ev) < 0) return -1;
        int r = snprintf(out + o, outsz - o, "%s%s=%s", i ? "&" : "", ek, ev);
        if (r < 0 || o + r >= outsz) return -1;
        o += r;
    }
    out[o] = 0;
    return o;
}

// ---------------- 签名串 ----------------
void jk_build_signing_string(const JukuCtx *ctx, const char *method, const char *path,
                             const char *canon_q, const char *body, const char *ts,
                             const char *nonce, char *out, int outsz) {
    char bh[65];
    jk_sha256_hex(body, body ? strlen(body) : 0, bh);
    snprintf(out, outsz, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
             method, path, canon_q ? canon_q : "", bh, ts, nonce,
             ctx ? ctx->device_id : "", ctx ? ctx->access_token : "");
}

// ---------------- 随机 (NRO 上由 libnx 提供强随机覆盖此弱符号) ----------------
__attribute__((weak)) int jk_random(void *buf, int n) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        srand((unsigned)time(NULL));
        for (int i = 0; i < n; i++) ((unsigned char*)buf)[i] = (unsigned char)rand();
        return 0;
    }
    fread(buf, 1, n, f); fclose(f);
    return 0;
}
static int uRand(uint8_t *dest, unsigned size) { return jk_random(dest, (int)size) == 0; }

// ---------------- 密钥 ----------------
static int jk_init(void) {
    if (!curve) {
        uECC_set_rng(uRand);
        curve = uECC_secp256r1();
        if (!curve) return -1;
    }
    return 0;
}

int jk_load_or_create_key(JukuCtx *ctx, const char *key_path) {
    if (jk_init()) return -1;
    unsigned char buf[96];
    FILE *f = fopen(key_path, "rb");
    if (f) {
        size_t got = fread(buf, 1, 96, f); fclose(f);
        if (got == 96) {
            memcpy(ctx->priv, buf, 32);
            memcpy(ctx->pub, buf + 32, 64);
            return 0;
        }
    }
    if (!uECC_make_key(ctx->pub, ctx->priv, curve)) return -2;
    memcpy(buf, ctx->priv, 32);
    memcpy(buf + 32, ctx->pub, 64);
    f = fopen(key_path, "wb");
    if (f) { fwrite(buf, 1, 96, f); fclose(f); }
    return 0;
}

// P-256 SPKI DER: SEQ{ SEQ{OID ecPublicKey, OID prime256v1}, BITSTRING 00 04 X Y }
void jk_pub_spki_b64(const JukuCtx *ctx, char *out, int outsz) {
    static const unsigned char head[] = {
        0x30,0x59,0x30,0x13,0x06,0x07,0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01,
        0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07,
        0x03,0x42,0x00
    };
    unsigned char spki[91];
    memcpy(spki, head, 26);
    spki[26] = 0x04;
    memcpy(spki + 27, ctx->pub, 64);
    b64_enc(spki, 91, out, outsz);
}

// ---------------- 签名 ----------------
int jk_sign_b64(const JukuCtx *ctx, const char *ss, char *out, int outsz) {
    unsigned char hash[32], p1363[64];
    SHA256_CTX c;
    sha256_init(&c);
    sha256_update(&c, (const BYTE*)ss, strlen(ss));
    sha256_final(&c, hash);
    if (jk_init()) return -1;
    if (!uECC_sign(ctx->priv, hash, 32, p1363, curve)) return -2;
    return b64_enc(p1363, 64, out, outsz);
}

// ---------------- session ----------------
static long long now_ms(void) {
#ifdef _WIN32
    return (long long)(time(NULL)) * 1000LL;   // Win64 long 仍 32 位
#else
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
}

// 极简 JSON 字符串字段提取
static int json_get_str(const char *j, const char *key, char *out, int outsz) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), '"');
    if (!p) return -1;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return -1;
    int n = (int)(e - p);
    if (n >= outsz) { if (outsz > 0) out[0] = 0; return -1; }
    memcpy(out, p, n); out[n] = 0;
    return n;
}

static int do_register(JukuCtx *ctx, JkHttpFn http, void *ud, char *resp, int resp_sz) {
    char pub_b64[256];
    jk_pub_spki_b64(ctx, pub_b64, sizeof pub_b64);
    char body[512];
    snprintf(body, sizeof body,
             "{\"publicKey\":\"%s\",\"appVersion\":\"%s\",\"platform\":\"%s\",\"installId\":\"%s\"}",
             pub_b64, JK_APP_VER, JK_PLATFORM, JK_INSTALL_ID);
    char ss[1024], sig[128], url[512], headers[2048], ts[32], nonce[40];
    unsigned char nb[16];
    snprintf(ts, sizeof ts, "%lld", now_ms());
    jk_random(nb, 16);
    to_hex(nb, 16, nonce);
    char bh[65]; jk_sha256_hex(body, strlen(body), bh);
    snprintf(ss, sizeof ss, "POST\n/auth/device/register\n\n%s\n%s\n%s\n%s\n%s",
             bh, ts, nonce, "", "");
    jk_sign_b64(ctx, ss, sig, sizeof sig);
    snprintf(headers, sizeof headers,
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
             "Accept: application/json\r\nContent-Type: application/json\r\n"
             "X-Device-Id: \r\nX-App-Version: %s\r\nX-Timestamp: %s\r\n"
             "X-Nonce: %s\r\nX-Body-Hash: %s\r\nX-Access-Token: \r\nX-Signature: %s\r\n\r\n",
             JK_APP_VER, ts, nonce, bh, sig);
    snprintf(url, sizeof url, "%s/auth/device/register", JK_BASE_URL);
    int rc = http(ud, "POST", url, headers, body, resp, resp_sz);
    if (getenv("JK_DEBUG")) fprintf(stderr, "[jk] register http rc=%d resp: %.300s\n", rc, resp);
    if (rc != 200) return -10;
    if (json_get_str(resp, "deviceId", ctx->device_id, sizeof ctx->device_id) <= 0) return -11;
    if (json_get_str(resp, "accessToken", ctx->access_token, sizeof ctx->access_token) <= 0) return -12;
    if (json_get_str(resp, "refreshToken", ctx->refresh_token, sizeof ctx->refresh_token) <= 0) return -13;
    ctx->access_expires_ms = now_ms() + 24LL*3600*1000;
    ctx->refresh_expires_ms = now_ms() + 2592000000LL;
    return 0;
}

static int do_refresh(JukuCtx *ctx, JkHttpFn http, void *ud, char *resp, int resp_sz) {
    char body[1200];
    snprintf(body, sizeof body,
             "{\"deviceId\":\"%s\",\"refreshToken\":\"%s\"}",
             ctx->device_id, ctx->refresh_token);
    char ss[2048], sig[128], url[512], headers[2560], ts[32], nonce[40];
    unsigned char nb[16];
    snprintf(ts, sizeof ts, "%lld", now_ms());
    jk_random(nb, 16);
    to_hex(nb, 16, nonce);
    char bh[65]; jk_sha256_hex(body, strlen(body), bh);
    snprintf(ss, sizeof ss, "POST\n/auth/device/refresh\n\n%s\n%s\n%s\n%s\n%s",
             bh, ts, nonce, ctx->device_id, ctx->access_token);
    jk_sign_b64(ctx, ss, sig, sizeof sig);
    snprintf(headers, sizeof headers,
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
             "Accept: application/json\r\nContent-Type: application/json\r\n"
             "X-Device-Id: %s\r\nX-App-Version: %s\r\nX-Timestamp: %s\r\n"
             "X-Nonce: %s\r\nX-Body-Hash: %s\r\nX-Access-Token: %s\r\nX-Signature: %s\r\n\r\n",
             ctx->device_id, JK_APP_VER, ts, nonce, bh, ctx->access_token, sig);
    snprintf(url, sizeof url, "%s/auth/device/refresh", JK_BASE_URL);
    int rc = http(ud, "POST", url, headers, body, resp, resp_sz);
    if (getenv("JK_DEBUG")) fprintf(stderr, "[jk] refresh http rc=%d resp: %.300s\n", rc, resp);
    if (rc != 200) return -20;
    if (json_get_str(resp, "accessToken", ctx->access_token, sizeof ctx->access_token) <= 0) return -21;
    char rt[512];
    if (json_get_str(resp, "refreshToken", rt, sizeof rt) > 0)
        snprintf(ctx->refresh_token, sizeof ctx->refresh_token, "%s", rt);
    ctx->access_expires_ms = now_ms() + 24LL*3600*1000;
    return 0;
}

int jk_ensure_session(JukuCtx *ctx, JkHttpFn http, void *ud) {
    static char resp[16384];
    if (ctx->access_token[0] && now_ms() < ctx->access_expires_ms - 30000LL) return 0;
    if (ctx->refresh_token[0] && now_ms() < ctx->refresh_expires_ms) {
        if (do_refresh(ctx, http, ud, resp, sizeof resp) == 0) return 0;
    }
    return do_register(ctx, http, ud, resp, sizeof resp);
}

// ---------------- 通用请求 ----------------
int jk_request(JukuCtx *ctx, JkHttpFn http, void *ud,
               const char *method, const char *path,
               const char *const kv[][2], int nkv,
               const char *body, char *resp, int resp_sz) {
    char canon[2048];
    if (nkv && jk_canonical_query(kv, nkv, canon, sizeof canon) < 0) return -1;
    if (!nkv) canon[0] = 0;
    int rc = jk_ensure_session(ctx, http, ud);
    if (rc) return rc;
    char ss[4096], sig[128], url[2048], headers[4096], ts[32], nonce[40];
    unsigned char nb[16];
    snprintf(ts, sizeof ts, "%lld", now_ms());
    jk_random(nb, 16);
    to_hex(nb, 16, nonce);
    char bh[65]; jk_sha256_hex(body, body ? strlen(body) : 0, bh);
    snprintf(ss, sizeof ss, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
             method, path, canon, bh, ts, nonce, ctx->device_id, ctx->access_token);
    jk_sign_b64(ctx, ss, sig, sizeof sig);
    snprintf(headers, sizeof headers,
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
             "Accept: application/json\r\nContent-Type: application/json\r\n"
             "X-Device-Id: %s\r\nX-App-Version: %s\r\nX-Timestamp: %s\r\n"
             "X-Nonce: %s\r\nX-Body-Hash: %s\r\nX-Access-Token: %s\r\nX-Signature: %s\r\n\r\n",
             ctx->device_id, JK_APP_VER, ts, nonce, bh, ctx->access_token, sig);
    snprintf(url, sizeof url, "%s%s%s%s", JK_BASE_URL, path, canon[0] ? "?" : "", canon);
    rc = http(ud, method, url, headers, body ? body : "", resp, resp_sz);
    if (rc == 401) {
        ctx->access_token[0] = 0;
        if (jk_ensure_session(ctx, http, ud) == 0)
            rc = jk_request(ctx, http, ud, method, path, kv, nkv, body, resp, resp_sz);
    }
    return rc;
}

int jk_get_detail(JukuCtx *ctx, JkHttpFn http, void *ud, const char *vid, char *resp, int sz) {
    char path[128];
    snprintf(path, sizeof path, "/videos/%s", vid);
    return jk_request(ctx, http, ud, "GET", path, NULL, 0, NULL, resp, sz);
}
