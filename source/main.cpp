// juku.nro —— Switch 剧库播放器 v2: 实时片库 (libnx + libmpv + 纯C签名客户端)
// 模式:
//   A) 实时(默认): 设备签名直连 api.jukuku.xyz —— 首页推荐/搜索/详情多线路/失败自动换线
//   B) 离线兼容: sdmc:/juku/config.json 指定 "source" 时用旧 list.json 模式
// 密钥: sdmc:/juku/device_key.bin (96B = 32B d || 64B pubXY), 首次自动生成
#include <switch.h>
#include <mpv/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <ctime>
#include "juku_api.h"

// ---------------- 按键宏兜底 ----------------
#if !defined(KEY_A)
#define KEY_A    (1ULL<<0)
#define KEY_B    (1ULL<<1)
#define KEY_X    (1ULL<<2)
#define KEY_Y    (1ULL<<3)
#define KEY_L    (1ULL<<4)
#define KEY_R    (1ULL<<5)
#define KEY_ZL   (1ULL<<6)
#define KEY_ZR   (1ULL<<7)
#define KEY_PLUS (1ULL<<10)
#define KEY_MINUS (1ULL<<11)
#define KEY_LEFT (1ULL<<12)
#define KEY_UP   (1ULL<<13)
#define KEY_RIGHT (1ULL<<14)
#define KEY_DOWN (1ULL<<15)
#endif

// ---------------- 迷你 JSON (复用 v1) ----------------
struct JVal {
    enum T { NUL, STR, ARR, OBJ, NUM, BOL } t = NUL;
    std::string s;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;
    double num = 0; bool b = false;
    const JVal* get(const std::string& k) const {
        auto it = obj.find(k); return it == obj.end() ? nullptr : &it->second;
    }
};
struct JP {
    const char* p; const char* e;
    void ws(){ while(p<e && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t')) p++; }
    JVal parse(){
        ws(); JVal v;
        if(p>=e) return v;
        if(*p=='{'){ v.t=JVal::OBJ; p++; ws();
            while(p<e && *p!='}'){
                std::string k=readStr(); ws(); if(p<e&&*p==':')p++; ws();
                JVal val=parse(); v.obj[k]=val; ws();
                if(p<e&&*p==',')p++; ws();
            } if(p<e&&*p=='}')p++;
        } else if(*p=='['){ v.t=JVal::ARR; p++; ws();
            while(p<e && *p!=']'){ v.arr.push_back(parse()); ws();
                if(p<e&&*p==',')p++; ws(); }
            if(p<e&&*p==']')p++;
        } else if(*p=='"'){ v.t=JVal::STR; v.s=readStr();
        } else if(*p=='t'||*p=='f'){ v.t=JVal::BOL; v.b=(*p=='t');
            while(p<e&&*p!=' '&&*p!=','&&*p!='}'&&*p!=']')p++;
        } else { v.t=JVal::NUM; const char* s=p;
            while(p<e&&(((*p>='0'&&*p<='9')||*p=='.'||*p=='-')))p++;
            v.num = strtod(s,nullptr);
        }
        return v;
    }
    std::string readStr(){
        p++; std::string o;
        while(p<e && *p!='"'){
            if(*p=='\\'){ p++; if(p<e){ switch(*p){case 'n':o+='\n';break;case 't':o+='\t';break;
                case 'r':o+='\r';break; case '"':o+='"';break; case '\\':o+='\\';break;
                case '/':o+='/';break; case 'u': { // \uXXXX UTF-8
                    if(p+4<e){ unsigned cp=0;
                        for(int i=1;i<=4;i++){char c=p[i];cp<<=4;
                            if(c>='0'&&c<='9')cp|=c-'0';
                            else if(c>='a'&&c<='f')cp|=c-'a'+10;
                            else if(c>='A'&&c<='F')cp|=c-'A'+10;}
                        p+=4;
                        if(cp<0x80)o+=(char)cp;
                        else if(cp<0x800){o+=(char)(0xC0|(cp>>6));o+=(char)(0x80|(cp&0x3F));}
                        else{o+=(char)(0xE0|(cp>>12));o+=(char)(0x80|((cp>>6)&0x3F));o+=(char)(0x80|(cp&0x3F));}
                    } break;}
                    default:o+=*p;} p++; } }
            else { o+=*p; p++; }
        }
        if(p<e) p++;
        return o;
    }
};
static JVal json_parse(const std::string& txt){ JP jp; jp.p=txt.data(); jp.e=txt.data()+txt.size(); return jp.parse(); }

// ---------------- libcurl HTTP (NRO 端) ----------------
#ifdef HAVE_CURL
#include <curl/curl.h>
static size_t _cb(void* p, size_t s, size_t n, void* u){ ((std::string*)u)->append((char*)p,s*n); return s*n; }
static bool _http_raw(const std::string& url, const std::vector<std::string>& hdrs,
                      const std::string* post, std::string& out, long* code){
    CURL* c=curl_easy_init(); if(!c) return false;
    out.clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, _cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    struct curl_slist* sl=nullptr;
    for(auto& h: hdrs){ sl = curl_slist_append(sl, h.c_str()); }
    if(sl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, sl);
    if(post){ curl_easy_setopt(c, CURLOPT_POST, 1L);
               curl_easy_setopt(c, CURLOPT_POSTFIELDS, post->c_str()); }
    CURLcode rc = curl_easy_perform(c);
    if(code) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
    if(sl) curl_slist_free_all(sl);
    curl_easy_cleanup(c);
    return rc == CURLE_OK;
}
#endif

// ---------------- jk_http: 签名客户端的 HTTP 注入 ----------------
struct JkHttpUd { std::string* out; long code; };
static int jk_http(void *ud, const char *method, const char *url,
                    const char *headers, const char *body, char *resp, int resp_sz) {
#ifdef HAVE_CURL
    JkHttpUd* u = (JkHttpUd*)ud;
    std::vector<std::string> hdrs;
    std::string hs(headers ? headers : "");
    size_t pos = 0;
    while (pos < hs.size()) {
        size_t e = hs.find("\r\n", pos);
        std::string line = hs.substr(pos, (e==std::string::npos?hs.size():e)-pos);
        if (!line.empty()) hdrs.push_back(line);
        if (e == std::string::npos) break;
        pos = e + 2;
    }
    std::string bodystr = body ? body : "";
    std::string out;
    bool ok = _http_raw(url, hdrs, bodystr.empty()?nullptr:&bodystr, out, &u->code);
    int n = (int)out.size();
    if (n > resp_sz-1) n = resp_sz-1;
    if (n>0) memcpy(resp, out.data(), n);
    resp[n]=0;
    if(!ok) return -1;
    return (int)u->code;
#else
    (void)ud;(void)method;(void)url;(void)headers;(void)body;(void)resp;(void)resp_sz;
    return -2;
#endif
}

// libnx 强随机覆盖 jk_random 弱符号
extern "C" int jk_random(void *buf, int n) {
    Result rc = csrngGetRandomBytes(buf, n);   // libnx 硬件真随机
    return R_SUCCEEDED(rc) ? 0 : -1;
}

// ---------------- 数据模型 ----------------
struct Line { std::string name; std::vector<std::string> urls; };  // 一条线路多集
struct PanItem { std::string pan; std::string url; std::string code; };
struct Video { std::string id, title, pic; };
struct Episode { std::string name, url; };

// ---------------- 状态 ----------------
static JukuCtx g_ctx;
static bool g_live_mode = true;
static std::string g_status = "初始化...";

// 首页推荐 / 分类列表 / 搜索 —— 全部从实时 API
static std::vector<Video> fetch_video_list(const char* path_kv[][2], int nkv, const char* path) {
    std::vector<Video> out;
#ifdef HAVE_CURL
    std::string resp(65536, 0);
    int rc = jk_request(&g_ctx, jk_http, nullptr, "GET", path, path_kv, nkv, nullptr, &resp[0], (int)resp.size());
    if (rc != 200) { g_status = "API 错误: " + std::to_string(rc); return out; }
    JVal j = json_parse(resp.c_str());
    const JVal* data = j.get("data");
    if(!data) return out;
    const JVal* items = data->get("items");
    if(!items) {
        // getHomeData: bannerVideos / movieVideos / ...
        const char* fields[] = {"bannerVideos","movieVideos","tvVideos","animeVideos"};
        for(auto f: fields){
            const JVal* fv = data->get(f);
            if(fv) for(auto& v: fv->arr){
                Video vd;
                if(v.get("id")) vd.id = v.get("id")->s; else { char b[32]; snprintf(b,sizeof b,"%.0lf",v.get("num")?v.get("num")->num:0); vd.id=b; }
                if(v.get("title")) vd.title = v.get("title")->s;
                if(v.get("pic")) vd.pic = v.get("pic")->s;
                if(!vd.title.empty() && !vd.id.empty()) out.push_back(vd);
            }
        }
        return out;
    }
    for(auto& v: items->arr){
        Video vd;
        if(v.get("id")) vd.id = v.get("id")->s; else { char b[32]; snprintf(b,sizeof b,"%.0lf",v.get("num")?v.get("num")->num:0); vd.id=b; }
        if(v.get("title")) vd.title = v.get("title")->s;
        if(v.get("pic")) vd.pic = v.get("pic")->s;
        if(!vd.title.empty() && !vd.id.empty()) out.push_back(vd);
    }
#endif
    return out;
}

// 详情: playListByLine -> 多条线路(每线路多集) + panList 网盘
struct Detail { std::vector<Line> lines; std::vector<PanItem> pans; std::string title; };
static Detail fetch_detail(const std::string& vid) {
    Detail d;
#ifdef HAVE_CURL
    std::string resp(262144, 0);
    int rc = jk_get_detail(&g_ctx, jk_http, nullptr, vid.c_str(), &resp[0], (int)resp.size());
    if (rc != 200) { g_status = "详情错误: " + std::to_string(rc); return d; }
    JVal j = json_parse(resp.c_str());
    const JVal* data = j.get("data");
    if(!data) return d;
    if(data->get("title")) d.title = data->get("title")->s;
    const JVal* pl = data->get("playListByLine");
    if(pl && pl->t==JVal::OBJ){
        for(auto& kv: pl->obj){
            Line ln; ln.name = kv.first;
            for(auto& e: kv.second.arr){
                if(e.get("url")) ln.urls.push_back(e.get("url")->s);
            }
            if(!ln.urls.empty()) d.lines.push_back(ln);
        }
    }
    const JVal* pan = data->get("panList");
    if(pan && pan->t==JVal::OBJ){
        for(auto& kv: pan->obj){
            for(auto& e: kv.second.arr){
                PanItem pi; pi.pan = kv.first;
                if(e.get("url")) pi.url = e.get("url")->s;
                if(e.get("code")) pi.code = e.get("code")->s;
                if(!pi.url.empty()) d.pans.push_back(pi);
            }
        }
    }
#endif
    return d;
}

// ---------------- 播放 (v1 原样 + 0.8s 内失败返回 false 供换线) ----------------
static bool play_url_watch(const std::string& url, int timeout_fail_ms = 800) {
    mpv_handle* mpv = mpv_create();
    if(!mpv) return false;
    mpv_set_option_string(mpv, "vo", "switch");
    mpv_set_option_string(mpv, "hwdec", "switch");
    mpv_set_option_string(mpv, "fullscreen", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");
    if(mpv_initialize(mpv)<0){ mpv_terminate_destroy(mpv); return false; }
    const char* cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);
    PadState pad; padUpdate(&pad);
    bool ok = false;
    u64 start = armGetSystemTick();
    u64 fail_ticks = (u64)timeout_fail_ms * 19200000ULL / 1000ULL; // 19.2MHz ticks per ms
    while(true){
        mpv_event* ev = mpv_wait_event(mpv, 0);
        if(ev->event_id==MPV_EVENT_END_FILE){ ok = true; break; }           // 正常结束
        if(ev->event_id==MPV_EVENT_SHUTDOWN){ ok = false; break; }
        // MPV_EVENT_LOG_MESSAGE 里找错误
        if(ev->event_id==MPV_EVENT_LOG_MESSAGE){
            mpv_event_log_message* lm = (mpv_event_log_message*)ev->data;
            if(lm && lm->level && strcmp(lm->level,"error")==0){ ok=false; break; }
        }
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if(down & KEY_B){ ok = true; break; }   // 用户主动退出视为成功(不换线)
        u64 now = armGetSystemTick();
        if(!ok && (now-start) > fail_ticks){
            // 检查是否真的在播 (有视频输出) —— 简化: 超过观察期后不再判失败
            // 观察期内 EOF/error 都已在上面 break; 到这里说明还活着
            ok = true; break;
        }
        if((now-start) > fail_ticks) { ok = true; break; }
        svcSleepThread(16'000'000);
    }
    mpv_terminate_destroy(mpv);
    return ok;
}

// ---------------- UI ----------------
static int ui_list(const std::string& title, const std::vector<std::string>& items, PadState& pad, const char* hint) {
    int sel = 0;
    while(appletMainLoop()){
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        printf("\x1b[2J\x1b[1;1H");
        printf("=== %s (共%zu) ===\n", title.c_str(), items.size());
        size_t win = 20; size_t base = sel > 9 ? sel - 9 : 0;
        if(base + win > items.size()) base = items.size() > win ? items.size()-win : 0;
        for(size_t i=base; i<items.size() && i<base+win; ++i)
            printf("%s[%2zu] %s\n", i==(size_t)sel?" > ":"  ", i+1, items[i].c_str());
        printf("\n%s   | %s\n", hint, g_status.c_str());
        if(down & KEY_DOWN) sel=(sel+1)%items.size();
        if(down & KEY_UP)   sel=(sel+(int)items.size()-1)%items.size();
        if(down & KEY_A) return sel;
        if(down & KEY_B) return -1;
        if(down & KEY_PLUS) return -2;
        svcSleepThread(16'000'000);
    }
    return -2;
}

static void wait_any_key(PadState& pad, const char* msg){
    printf("\x1b[2J\x1b[1;1H%s\n按 A 继续...\n", msg);
    while(appletMainLoop()){ padUpdate(&pad);
        if(padGetButtonsDown(&pad)&KEY_A) return;
        if(padGetButtonsDown(&pad)&KEY_PLUS) return;
        svcSleepThread(16'000'000); }
}

// ---------------- 搜索 (系统键盘) ----------------
static std::string swkbd_get_text(const std::string& hint, const std::string& initial=""){
    SwkbdConfig k;
    if(R_FAILED(swkbdCreate(&k, 0))) return "";
    swkbdConfigMakeDefault(&k);
    swkbdConfigSetHeaderText(&k, hint.c_str());
    swkbdConfigSetInitialText(&k, initial.c_str());
    char buf[256] = {0};
    Result rc = swkbdShow(&k, buf, sizeof buf);
    swkbdClose(&k);
    return R_SUCCEEDED(rc) ? std::string(buf) : std::string();
}

// ---------------- main ----------------
int main(int argc, char** argv){
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    padUpdate(&pad);

    printf("\x1b[2J\x1b[1;1Hjuku.nro v2 启动...\n");

    // 1) 密钥 (sdmc:/juku/device_key.bin)
    memset(&g_ctx, 0, sizeof g_ctx);
    int kr = jk_load_or_create_key(&g_ctx, "sdmc:/juku/device_key.bin");
    if(kr){ printf("密钥初始化失败(%d)! 按A退出\n", kr);
        while(appletMainLoop()){padUpdate(&pad); if(padGetButtonsDown(&pad)&KEY_A)break; svcSleepThread(16'000'000);} consoleExit(NULL); return 0; }

    // 2) 离线兼容: config.json 有 source 则用旧模式提示
    {
        std::ifstream f("sdmc:/juku/config.json");
        if(f.good()){
            std::stringstream ss; ss<<f.rdbuf();
            JVal c = json_parse(ss.str());
            if(c.get("source")) g_live_mode = false;
        }
    }

#ifdef HAVE_CURL
    if(g_live_mode){
        g_status = "注册设备中...";
        printf("\x1b[1;1H注册设备中...\n");
        // 注册 (重试3次)
        int rc = -1;
        for(int i=0;i<3 && rc;i++){
            rc = jk_ensure_session(&g_ctx, jk_http, nullptr);
            if(rc){ g_status = "重试注册(" + std::to_string(i+1) + ")..."; svcSleepThread(1'000'000'000ULL); }
        }
        if(rc){
            printf("设备注册失败(%d)! 网络不通或服务器异常.\n按A退出\n", rc);
            while(appletMainLoop()){padUpdate(&pad); if(padGetButtonsDown(&pad)&KEY_A)break; svcSleepThread(16'000'000);} consoleExit(NULL); return 0;
        }
        g_status = "设备OK: " + std::string(g_ctx.device_id).substr(0,12) + "...";
    }
#else
    g_live_mode = false;
    g_status = "未编译 libcurl - 仅离线模式";
#endif

    // 3) 主菜单
    while(appletMainLoop()){
        std::vector<std::string> menu = g_live_mode
            ? std::vector<std::string>{"最近热门", "电影", "剧集", "动漫", "综艺", "纪录", "短剧", "搜索"}
            : std::vector<std::string>{"离线片单 (list.json)", "搜索(实时)"};
        int m = ui_list("剧库 juku", menu, pad, "A选择 B退出");
        if(m < 0) break;

        std::vector<Video> vids;
        if(g_live_mode){
            g_status = "加载中...";
            if(m==0){
                const char* kv0[][2] = {0};
                vids = fetch_video_list(kv0, 0, "/videos/recommend");
            } else if(m==7){
                std::string q = swkbd_get_text("搜索片名");
                if(q.empty()) continue;
                char qbuf[512]; snprintf(qbuf,sizeof qbuf,"%s",q.c_str());
                const char* kv[][2] = {{"q",qbuf},{"page","1"},{"pageSize","30"}};
                vids = fetch_video_list(kv, 3, "/videos/search");
            } else {
                static const char* typeids[] = {"1","2","3","4","5","7"};
                char tid[8]; snprintf(tid,sizeof tid,"%s",typeids[m-1]);
                const char* kv[][2] = {{"typeId",tid},{"page","1"},{"pageSize","30"}};
                vids = fetch_video_list(kv, 3, "/videos");
            }
        } else {
            // 离线: 读 list.json (旧模式简版: 只列片名)
            std::ifstream f("sdmc:/juku/list.json");
            std::stringstream ss; ss<<f.rdbuf();
            JVal root = json_parse(ss.str());
            if(root.get("items")){
                for(auto& it: root.get("items")->arr){
                    Video v;
                    if(it.get("name")) v.title = it.get("name")->s;
                    if(it.get("sources")){
                        auto* s0 = it.get("sources");
                        if(!s0->arr.empty() && s0->arr[0].get("url")) v.pic = s0->arr[0].get("url")->s; // 暂存 url
                    }
                    if(!v.title.empty()){ v.id = "OFFLINE"; vids.push_back(v); }
                }
            }
        }
        if(vids.empty()){ g_status = "无结果"; continue; }

        // 片单
        std::vector<std::string> names;
        for(auto& v: vids) names.push_back(v.title);
        int pick = ui_list(g_live_mode?"片单":"离线片单", names, pad, "A选片 B返回");
        if(pick < 0) continue;
        Video& v = vids[pick];

        if(!g_live_mode){
            // 离线模式: 直接播暂存的 url
            if(!v.pic.empty()){ consoleExit(NULL); play_url_watch(v.pic); consoleInit(NULL); }
            continue;
        }

        // 详情 -> 选线路
        g_status = "取详情...";
        Detail d = fetch_detail(v.id);
        if(d.lines.empty()){ g_status = "无可用线路"; continue; }

        std::vector<std::string> lnames;
        for(auto& l: d.lines) lnames.push_back(l.name + " (" + std::to_string(l.urls.size()) + "集)");
        if(!d.pans.empty()) lnames.push_back("[网盘] " + std::to_string(d.pans.size()) + "条链接 (v2)");
        int lp = ui_list(v.title + " - 线路", lnames, pad, "A选择 B返回");
        if(lp < 0) continue;
        if(lp >= (int)d.lines.size()){ g_status = "网盘功能开发中(v2)"; continue; }
        Line& ln = d.lines[lp];

        // 选集
        std::vector<std::string> eps;
        for(size_t i=0;i<ln.urls.size();++i) eps.push_back("第" + std::to_string(i+1) + "集");
        int ep = ln.urls.size()==1 ? 0 : ui_list(v.title, eps, pad, "A播放 B返回");
        if(ep < 0) continue;

        // 播放: 失败自动换线路 (当前线路起, 循环尝试所有线路第 ep 集)
        int cur = lp;
        while(true){
            g_status = "播放: 线路" + d.lines[cur].name + "...";
            consoleExit(NULL);
            bool ok = play_url_watch(d.lines[cur].urls[ep]);
            consoleInit(NULL);
            padUpdate(&pad);
            if(ok) break;
            // 失败 -> 下一线路
            g_status = "线路" + d.lines[cur].name + " 失败, 换线...";
            int next = (cur+1) % d.lines.size();
            if(next == lp){ g_status = "所有线路失败"; break; }
            // 询问是否继续换线
            std::vector<std::string> yn = {"继续换线", "放弃"};
            int yn_sel = ui_list("播放失败, 换线路?", yn, pad, "A选择");
            if(yn_sel != 0) break;
            cur = next;
        }
    }
    consoleExit(NULL);
    return 0;
}
