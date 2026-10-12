// juku.nro —— 极简 Switch 剧库播放器 (libnx + libmpv)
// 用途: 读一个片单(JSON) -> 列表 -> 选片 -> mpv 播 HLS。不用 tv.nro 面板。
// 片单来源: sdmc:/juku/config.json 里的 "source" 字段, 可为本地路径或 http(s) URL。
//   零 PC 模式:   source = "sdmc:/juku/list.json"  (用 gen_jukulist.py 烤好直链传进去)
//   实时模式:     source = "http://VM_IP:8090/juku/list.json" (适配器实时从 juku 拉)
// 片单格式 list.json:
//   { "title":"剧库", "items":[ {"name":"潜伏6","url":"https://x/index.m3u8"},
//                               {"name":"某剧","sources":[{"name":"第1集","url":"..."}]} ] }
#include <switch.h>
#include <mpv/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

// ---------------- 最小 JSON 解析 (仅支持本项目用到的结构) ----------------
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
        } else { // number
            v.t=JVal::NUM; const char* s=p;
            while(p<e&&(((*p>='0'&&*p<='9')||*p=='.'||*p=='-')))p++;
            v.num = strtod(s,nullptr);
        }
        return v;
    }
    std::string readStr(){
        // 调用前 *p=='"'
        p++; std::string o;
        while(p<e && *p!='"'){
            if(*p=='\\'){ p++; if(p<e){ switch(*p){case 'n':o+='\n';break;case 't':o+='\t';break;
                case 'r':o+='\r';break; case '"':o+='"';break; case '\\':o+='\\';break;
                case '/':o+='/';break; default:o+=*p;} p++; } }
            else { o+=*p; p++; }
        }
        if(p<e) p++; // skip closing "
        return o;
    }
};
static JVal json_parse(const std::string& txt){ JP jp; jp.p=txt.data(); jp.e=txt.data()+txt.size(); return jp.parse(); }

// ---------------- 数据模型 ----------------
struct Source { std::string name; std::string url; };
struct Item { std::string name; std::vector<Source> sources; };
struct Playlist { std::string title; std::vector<Item> items; };

static Playlist build_playlist(const JVal& root){
    Playlist pl;
    if(auto* t=root.get("title")) pl.title = t->s;
    if(auto* its=root.get("items")){
        for(const auto& it: its->arr){
            Item item;
            if(auto* n=it.get("name")) item.name = n->s;
            if(item.name.empty()) item.name = "未命名";
            if(auto* u=it.get("url")){
                item.sources.push_back({"默认", u->s});
            } else if(auto* ss=it.get("sources")){
                for(const auto& s: ss->arr){
                    std::string sn = s.get("name")? s.get("name")->s : "源";
                    std::string su = s.get("url")? s.get("url")->s : "";
                    if(!su.empty()) item.sources.push_back({sn, su});
                }
            }
            if(!item.sources.empty()) pl.items.push_back(item);
        }
    }
    return pl;
}

// ---------------- 加载片单 ----------------
static std::string read_file(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss<<f.rdbuf(); return ss.str();
}
#ifdef HAVE_CURL
#include <curl/curl.h>
static size_t _cb(void* p, size_t s, size_t n, void* u){ ((std::string*)u)->append((char*)p,s*n); return s*n; }
static std::string http_get(const std::string& url){
    std::string out; CURL* c=curl_easy_init();
    if(!c) return out;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, _cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_perform(c); curl_easy_cleanup(c);
    return out;
}
#endif
static std::string load_source_text(const std::string& src){
#ifdef HAVE_CURL
    if(src.rfind("http",0)==0) return http_get(src);
#endif
    return read_file(src);
}

// ---------------- 播放 ----------------
static void play_url(const std::string& url){
    mpv_handle* mpv = mpv_create();
    if(!mpv){ printf("mpv_create 失败\n"); return; }
    mpv_set_option_string(mpv, "vo", "switch");
    mpv_set_option_string(mpv, "hwdec", "switch");
    mpv_set_option_string(mpv, "fullscreen", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");
    if(mpv_initialize(mpv)<0){ printf("mpv 初始化失败\n"); mpv_terminate_destroy(mpv); return; }
    const char* cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);
    PadState pad; padUpdate(&pad);
    printf("\x1b[2J\x1b[1;1H播放中... (B 退出  A 暂停)\n%s\n", url.c_str());
    while(true){
        mpv_event* ev = mpv_wait_event(mpv, 0);
        if(ev->event_id==MPV_EVENT_SHUTDOWN) break;
        if(ev->event_id==MPV_EVENT_END_FILE) break;
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if(down & KEY_B){ const char* q[]={"quit",NULL}; mpv_command(mpv,q); }
        if(down & KEY_A){ int paused=0; mpv_get_property(mpv,"pause",MPV_FORMAT_FLAG,&paused);
            paused=!paused; mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&paused); }
        svcSleepThread(16'000'000);
    }
    mpv_terminate_destroy(mpv);
}

// ---------------- 菜单 ----------------
static void draw_list(const Playlist& pl, int sel){
    printf("\x1b[2J\x1b[1;1H");
    printf("=== %s (共 %zu) ===\n", pl.title.c_str(), pl.items.size());
    for(size_t i=0;i<pl.items.size();++i){
        printf("%s[%2zu] %s\n", i==(size_t)sel?" > ":"  ", i+1, pl.items[i].name.c_str());
    }
    printf("\n↑/↓ 选择   A 播放/进入   B 返回   X 刷新片单\n");
}
static void draw_src(const std::string& item, const std::vector<Source>& ss, int sel){
    printf("\x1b[2J\x1b[1;1H");
    printf("=== %s (共 %zu 源) ===\n", item.c_str(), ss.size());
    for(size_t i=0;i<ss.size();++i){
        printf("%s[%2zu] %s\n", i==(size_t)sel?" > ":"  ", i+1, ss[i].name.c_str());
    }
    printf("\n↑/↓ 选择   A 播放   B 返回\n");
}

int main(int argc, char** argv){
    consoleInit();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    padUpdate(&pad);

    std::string cfg_path = "sdmc:/juku/config.json";
    std::string src = "sdmc:/juku/list.json";
    std::string cfg = read_file(cfg_path);
    if(!cfg.empty()){
        JVal c = json_parse(cfg);
        if(auto* s=c.get("source")) src = s->s;
    }
    printf("片单来源: %s\n", src.c_str());
    printf("按 A 加载...\n");
    while(appletMainLoop()){
        padUpdate(&pad);
        if(padGetButtonsDown(&pad) & KEY_A) break;
        if(padGetButtonsDown(&pad) & KEY_PLUS) break;
        svcSleepThread(16'000'000);
    }

reload:
    Playlist pl = build_playlist(json_parse(load_source_text(src)));
    if(pl.items.empty()){
        printf("\x1b[2J\x1b[1;1H片单为空或加载失败!\n来源:%s\nX 重试   + 退出\n", src.c_str());
        while(appletMainLoop()){ padUpdate(&pad); u64 d=padGetButtonsDown(&pad);
            if(d&KEY_X) goto reload; if(d&KEY_PLUS) break; svcSleepThread(16'000'000); }
        consoleFinish(); return 0;
    }

    int sel=0;
    enum {LIST, SRC} mode=LIST;
    int srcsel=0; const Item* cur=nullptr;
    while(appletMainLoop()){
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if(mode==LIST){
            draw_list(pl, sel);
            if(down & KEY_DOWN) sel=(sel+1)%pl.items.size();
            if(down & KEY_UP)   sel=(sel+pl.items.size()-1)%pl.items.size();
            if(down & KEY_A){
                cur=&pl.items[sel];
                if(cur->sources.size()==1){ consoleExit(); play_url(cur->sources[0].url); consoleInit(); }
                else { mode=SRC; srcsel=0; }
            }
            if(down & KEY_X) goto reload;
            if(down & KEY_PLUS) break;
        } else {
            draw_src(cur->name, cur->sources, srcsel);
            if(down & KEY_DOWN) srcsel=(srcsel+1)%cur->sources.size();
            if(down & KEY_UP)   srcsel=(srcsel+cur->sources.size()-1)%cur->sources.size();
            if(down & KEY_A){ consoleExit(); play_url(cur->sources[srcsel].url); consoleInit(); mode=LIST; }
            if(down & KEY_B) mode=LIST;
            if(down & KEY_PLUS) break;
        }
        svcSleepThread(16'000'000);
    }
    consoleFinish();
    return 0;
}
