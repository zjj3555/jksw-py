// juku_api.h —— 剧库 api.jukuku.xyz 设备签名客户端
#ifndef JUKU_API_H
#define JUKU_API_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char priv[32];  // P-256 d (raw, big-endian)
    unsigned char pub[64];   // 未压缩点去掉 0x04: X(32)||Y(32)
    char device_id[128];
    char access_token[512];
    long long access_expires_ms;  // epoch ms (Win64 long 是 32 位, 必须 long long)
    char refresh_token[512];
    long long refresh_expires_ms;
} JukuCtx;

// HTTP 注入接口: 回调把 (url, headers, body) 发出去, 返回响应体; resp_code 写 HTTP 状态
// headers: 多行 "Key: Value\r\n" 拼接, 以空行结尾
typedef int (*JkHttpFn)(void *ud, const char *method, const char *url,
                       const char *headers, const char *body, char *resp, int resp_sz);

// 密钥: 生成或加载 (key_path 文件 = 32B priv || 64B pub)
int  jk_load_or_create_key(JukuCtx *ctx, const char *key_path);
// 会话: token 过期自动 refresh, 失败重新 register (参照 python ensure_session)
int  jk_ensure_session(JukuCtx *ctx, JkHttpFn http, void *ud);
// 通用签名请求 (path 以 / 开头; kv 参数数组; body 可为 NULL=GET)
int  jk_request(JukuCtx *ctx, JkHttpFn http, void *ud,
               const char *method, const char *path,
               const char *const kv[][2], int nkv,
               const char *body, char *resp, int resp_sz);
// 便捷: GET /videos/{id} 详情
int  jk_get_detail(JukuCtx *ctx, JkHttpFn http, void *ud, const char *vid, char *resp, int sz);

// ---- 测试辅助 (PC 验证用, 供测试程序逐字段对照 python) ----
void jk_build_signing_string(const JukuCtx *ctx, const char *method, const char *path,
                             const char *canon_q, const char *body, const char *ts,
                             const char *nonce, char *out, int outsz);
int  jk_sign_b64(const JukuCtx *ctx, const char *signing_string, char *out, int outsz);
void jk_pub_spki_b64(const JukuCtx *ctx, char *out, int outsz);   // 注册用 SPKI base64
int  jk_canonical_query(const char *const kv[][2], int n, char *out, int outsz);
void jk_sha256_hex(const char *body, size_t len, char out[65]);

#ifdef __cplusplus
}
#endif

#endif
