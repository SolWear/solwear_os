#include "config.h"
#include "st7789.h"
#include "ui_draw.h"
#include "ui_font.h"
#include "ui_roulette.h"
#include "hal_buttons.h"
#include "hal_nfc.h"
#include "hal_battery.h"
#include "wallet.h"
#include "crypto.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "main";

// ============================================================
// Screen IDs
// ============================================================
typedef enum {
    SCR_ONBOARD,
    SCR_LOCK,
    SCR_HOME,
    SCR_WALLET,
    SCR_RECEIVE,
    SCR_SETTINGS,
    SCR_TRANSACTIONS,
    SCR_STATS,
    SCR_GAMES_MENU,
    SCR_PING_PONG,
    SCR_TETRIS,
    SCR_TAMAGOTCHI,
} screen_id_t;

typedef enum { SLIDE_WATCHFACE=0, SLIDE_GRID, SLIDE_COUNT } home_slide_t;

// ============================================================
// Global OS State
// ============================================================
static screen_id_t  s_screen   = SCR_ONBOARD;
static home_slide_t s_slide    = SLIDE_WATCHFACE;
static int8_t       s_grid_sel = 0;   // launcher app index
#define WATCHFACE_COUNT 6
#define GRID_APP_COUNT 7
#define GRID_PAGE_COUNT ((GRID_APP_COUNT + 3) / 4)
static uint8_t      s_watchface = 0;  // 0=GM 1=GN 2=digital 3=analog 4=wallet 5=minimal

static bool     s_nfc_armed        = true;
static uint32_t s_nfc_widget_ms    = 0;
static bool     s_nfc_widget_armed = true;
static uint32_t s_sync_widget_ms   = 0;
static uint32_t s_sync_seen_counter = 0;

// TX overlay
static bool     s_tx_overlay  = false;
typedef enum { TX_SHOW, TX_TIMER, TX_CONFIRM2 } tx_state_t;
static tx_state_t s_tx_state  = TX_SHOW;
static uint32_t   s_tx_timer  = 0;
static uint8_t    s_pending_sig[64];
static bool       s_pending_sig_valid = false;
static char       s_pending_sig_nonce[37];

// Clock
static int s_h = 12, s_m = 0, s_s = 0;

// Button queue + roulette
static QueueHandle_t s_btn_q;
static roulette_t    s_roulette;
static char          s_lock_err[32] = {};

// ============================================================
// Onboarding
// ============================================================
typedef enum {
    OB_WELCOME, OB_CHOICE, OB_IMPORT_HOW, OB_IMPORT_NFC,
    OB_IMPORT_HEX, OB_GEN_CONFIRM, OB_SET_PASS, OB_PASS_WARN,
    OB_SET_NAME, OB_DONE,
} ob_step_t;

static ob_step_t s_ob    = OB_WELCOME;
static uint8_t   s_ob_sel = 0;
static uint8_t   s_ob_seed[32];
static char      s_ob_pass[33];
static char      s_ob_name[WALLET_NAME_MAX + 1];
static char      s_ob_pub4[9]; // first 4 bytes hex for display

static void ob_go(ob_step_t step) { s_ob = step; s_ob_sel = 0; }

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

// ============================================================
// Settings
// ============================================================
static int8_t s_settings_sel = 0;
static const char *s_settings_items[] = {
    "Watchface: GM","Watchface: GN","Watchface: Digital","Watchface: Analog",
    "Watchface: Wallet","Watchface: Minimal",
    "Change Password","About"
};
#define SETTINGS_COUNT 8

// ============================================================
// Transactions
// ============================================================
#define MAX_RX 50
static char     s_rx[MAX_RX][80];
static int8_t   s_rx_n      = 0;
static int8_t   s_rx_scroll = 0;
static float    s_balance   = 0.f;

static void load_receipts(void)
{
    s_rx_n    = 0; s_balance = 0.f;
    FILE *f = fopen(RECEIPTS_PATH, "r");
    if (!f) return;
    while (fgets(s_rx[s_rx_n], 80, f) && s_rx_n < MAX_RX) {
        float v; if (sscanf(s_rx[s_rx_n], "%f", &v) == 1) s_balance += v;
        s_rx_n++;
    }
    fclose(f);
}

static void save_receipt(const char *line)
{
    FILE *f = fopen(RECEIPTS_PATH, "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

// ============================================================
// Ping Pong
// ============================================================
#define COURT_Y STATUS_BAR_H
#define PAD_W   8
#define PAD_H   32
#define BALL_R  4
#define SPD     2.2f
#define AI_SPD  1.6f

static struct {
    float bx,by,vx,vy,ay;
    int16_t py;
    uint8_t sp,sa;
    bool paused,over;
} pp;

static void pp_reset(void)
{
    pp.bx=LCD_W/2.f; pp.by=LCD_H/2.f;
    pp.vx=(rand()&1)?SPD:-SPD; pp.vy=SPD*0.6f;
    pp.py=(LCD_H-PAD_H)/2; pp.ay=(LCD_H-PAD_H)/2.f;
}
static void pp_init(void) { memset(&pp,0,sizeof(pp)); pp_reset(); }
static void pp_update(uint32_t dt)
{
    if (pp.paused||pp.over) return;
    float sc=dt/33.f;
    pp.bx+=pp.vx*sc; pp.by+=pp.vy*sc;
    if (pp.by-BALL_R<COURT_Y){pp.by=COURT_Y+BALL_R; pp.vy=fabsf(pp.vy);}
    if (pp.by+BALL_R>LCD_H-2){pp.by=LCD_H-2-BALL_R; pp.vy=-fabsf(pp.vy);}
    // player (left)
    int lx=PAD_W+4;
    if (pp.bx-BALL_R<=lx+PAD_W&&pp.vx<0&&pp.by>=pp.py&&pp.by<=pp.py+PAD_H){
        pp.vx=fabsf(pp.vx)*1.04f;
        pp.vy=((pp.by-pp.py-PAD_H/2.f)/(PAD_H/2.f))*SPD*1.4f;
        pp.bx=lx+PAD_W+BALL_R+1;
    }
    // AI (right)
    float rx=LCD_W-PAD_W-6.f;
    if (pp.by>pp.ay+PAD_H/2.f+2) pp.ay+=AI_SPD*sc;
    else if (pp.by<pp.ay+PAD_H/2.f-2) pp.ay-=AI_SPD*sc;
    if (pp.ay<COURT_Y) pp.ay=COURT_Y;
    if (pp.ay+PAD_H>LCD_H-2) pp.ay=LCD_H-2-PAD_H;
    if (pp.bx+BALL_R>=rx&&pp.vx>0&&pp.by>=pp.ay&&pp.by<=pp.ay+PAD_H){
        pp.vx=-fabsf(pp.vx)*1.02f; pp.bx=rx-BALL_R-1;
    }
    if (pp.bx<0){pp.sa++; pp_reset();}
    if (pp.bx>LCD_W){pp.sp++; pp_reset();}
    if (pp.sp>=5||pp.sa>=5) pp.over=true;
}

// ============================================================
// Tetris
// ============================================================
#define TC 8
#define TR 16
#define TCW 18
#define TCH 13
#define TBX ((LCD_W-TC*TCW)/2)
#define TBY STATUS_BAR_H

static const int8_t kP[7][4][4][2]={
    {{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}},
    {{{0,0},{1,0},{0,1},{1,1}},{{0,0},{1,0},{0,1},{1,1}},{{0,0},{1,0},{0,1},{1,1}},{{0,0},{1,0},{0,1},{1,1}}},
    {{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}},
    {{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},{{1,1},{2,1},{0,2},{1,2}},{{0,0},{0,1},{1,1},{1,2}}},
    {{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{1,2},{2,2}},{{1,0},{0,1},{1,1},{0,2}}},
    {{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}},
    {{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}},
};
static const uint16_t kTC[7]={
    COLOR_WHITE, COLOR_LTGRAY, COLOR_GRAY, 0xBDD7,
    0x8C51, 0x6B4D, 0xDEDB
};

static struct {
    uint8_t b[TR][TC];
    uint8_t piece,next,rot; int8_t px,py;
    uint32_t fall_ms,fall_rate,score; uint8_t level,lines; bool over;
} tet;

static bool tet_fits(int8_t px,int8_t py,uint8_t rot){
    for(int c=0;c<4;c++){
        int8_t cx=px+kP[tet.piece][rot][c][0];
        int8_t cy=py+kP[tet.piece][rot][c][1];
        if(cx<0||cx>=TC||cy>=TR) return false;
        if(cy>=0&&tet.b[cy][cx]) return false;
    }
    return true;
}
static void tet_place(void){
    for(int c=0;c<4;c++){
        int8_t cx=tet.px+kP[tet.piece][tet.rot][c][0];
        int8_t cy=tet.py+kP[tet.piece][tet.rot][c][1];
        if(cy>=0&&cy<TR) tet.b[cy][cx]=tet.piece+1;
    }
    int cl=0;
    for(int r=TR-1;r>=0;r--){
        bool full=true;
        for(int c=0;c<TC;c++) if(!tet.b[r][c]){full=false;break;}
        if(full){memmove(tet.b[1],tet.b[0],r*TC);memset(tet.b[0],0,TC);cl++;r++;}
    }
    static const uint32_t sc[]={0,100,300,500,800};
    tet.score+=(cl>4?sc[4]:sc[cl])*tet.level;
    tet.lines+=cl; tet.level=1+tet.lines/10;
    tet.fall_rate=(tet.level>10)?80:700/tet.level;
    tet.piece=tet.next; tet.next=rand()%7;
    tet.px=TC/2-2; tet.py=0; tet.rot=0;
    if(!tet_fits(tet.px,tet.py,tet.rot)) tet.over=true;
}
static void tet_init(void){
    memset(&tet,0,sizeof(tet));
    tet.piece=rand()%7; tet.next=rand()%7;
    tet.px=TC/2-2; tet.fall_rate=700; tet.level=1;
}

// ============================================================
// Tamagotchi
// ============================================================
typedef struct { uint8_t hunger,happy,energy; bool alive; uint32_t ts; } tama_t;
static tama_t    tama;
static uint8_t   tama_frame=0;
static uint32_t  tama_anim=0;
static bool      tama_msg_on=false;
static uint32_t  tama_msg_t=0;
static char      tama_msg[24]={};
static uint32_t  s_anim=0;

static void tama_load(void){
    FILE*f=fopen(TAMA_PATH,"rb");
    if(!f){tama=(tama_t){60,80,70,true,0};return;}
    fread(&tama,sizeof(tama),1,f); fclose(f);
}
static void tama_save(void){
    FILE*f=fopen(TAMA_PATH,"wb");
    if(!f) return;
    fwrite(&tama,sizeof(tama),1,f);
    fclose(f);
}

// ============================================================
// Button callback
// ============================================================
static void on_button(btn_event_t ev){ xQueueSend(s_btn_q, &ev, 0); }

// ============================================================
// Draw helpers
// ============================================================
#define PBL_BG      COLOR_BLACK
#define PBL_FG      COLOR_WHITE
#define PBL_DIM     RGB565(0x7B,0x7B,0x7B)
#define PBL_LINE    RGB565(0x3A,0x3A,0x3A)
#define PBL_PANEL   RGB565(0x10,0x10,0x10)
#define SOLWEAR_OS_VERSION "SolWearOS v1.2-pv2"
#define UI_TRANSITION_MS 140

static uint32_t s_ui_transition_ms=0;
static int8_t s_ui_transition_dir=1;

static void draw_status(const char *title)
{
    char t[8]; snprintf(t,sizeof(t),"%02d:%02d",s_h,s_m);
    ui_status_bar(t, hal_battery_percent(), hal_battery_charging());
    if(title&&title[0]){
        int tw=ui_str_width(title,2);
        ui_str(LCD_W/2-tw/2,(STATUS_BAR_H-16)/2,title,COLOR_WHITE,2);
    }
}

static void draw_icon_card(int x,int y,int w,int h,const char*lbl,bool sel)
{
    uint16_t bg = sel ? PBL_FG : PBL_BG;
    uint16_t fg = sel ? PBL_BG : PBL_FG;
    st7789_fb_rect(x,y,w,h,bg);
    st7789_fb_rect_outline(x,y,w,h,sel?PBL_FG:PBL_LINE);
    if(sel && ((s_anim / 240) & 1)) st7789_fb_rect_outline(x+2,y+2,w-4,h-4,PBL_BG);
    int tw=ui_str_width(lbl,1);
    ui_str(x+(w-tw)/2,y+h-14,lbl,fg,1);
}

static void short_pk_hex(const uint8_t *pk, char *out, size_t out_len)
{
    if (!pk || !out || out_len < 18) return;
    snprintf(out, out_len, "%02X%02X...%02X%02X",
             pk[0], pk[1], pk[30], pk[31]);
}

static void short_text(const char *src, char *out, size_t out_len)
{
    if (!out || out_len < 12) return;
    if (!src || !src[0] || src[0] == '?') {
        strncpy(out, "unknown", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len <= 12) {
        strncpy(out, src, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    snprintf(out, out_len, "%.4s...%.4s", src, src + len - 4);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx=abs(x1-x0), sx=x0<x1?1:-1;
    int dy=-abs(y1-y0), sy=y0<y1?1:-1;
    int err=dx+dy;
    while(1){
        st7789_fb_pixel(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x0+=sx;}
        if(e2<=dx){err+=dx;y0+=sy;}
    }
}

static void draw_meter(int x, int y, int w, int h, uint8_t pct, uint16_t c)
{
    st7789_fb_rect_outline(x,y,w,h,c);
    int fill=(w-4)*pct/100;
    if(fill>0) st7789_fb_rect(x+2,y+2,fill,h-4,c);
}

static void draw_star(int x, int y, uint16_t c)
{
    st7789_fb_pixel(x,y,c);
    st7789_fb_pixel(x-1,y,c);
    st7789_fb_pixel(x+1,y,c);
    st7789_fb_pixel(x,y-1,c);
    st7789_fb_pixel(x,y+1,c);
}

static void draw_cloud_raw(int x, int y, int w, uint16_t c)
{
    int h=w/4;
    int cx1=x+w/5, cx2=x+w/2, cx3=x+w*3/4;
    int r1=h/2, r2=h*2/3;
    st7789_fb_circle_fill(cx1, y+h,   r1, c);
    st7789_fb_circle_fill(cx2, y+h-5, r2, c);
    st7789_fb_circle_fill(cx3, y+h,   r1, c);
    st7789_fb_rect(cx1-r1, y+h, cx3+r1-(cx1-r1), r1+2, c);
}

static void draw_cloud_wrap(int x, int y, int w, uint16_t c)
{
    const int span=LCD_W+96;
    while(x<-80) x+=span;
    while(x>LCD_W+32) x-=span;
    draw_cloud_raw(x,y,w,c);
    draw_cloud_raw(x-span,y,w,c);
    draw_cloud_raw(x+span,y,w,c);
}

static void draw_moon_scene(int cx, int cy)
{
    st7789_fb_circle_fill(cx,cy,21,PBL_FG);
    st7789_fb_circle_fill(cx+8,cy-5,21,PBL_BG);
    st7789_fb_circle(cx,cy,21,PBL_FG);
    if((s_anim/700)&1) st7789_fb_circle(cx,cy,15,PBL_DIM);
}

static void draw_night_sky(void)
{
    static const uint8_t STAR_X[48]={
        12,220, 45,178, 93,231, 67,154,
        23,198,118, 34,205, 88,147, 72,
       189, 11,233, 56,167,103, 38,215,
        79,142, 26,197, 62,183,108, 44,
       221, 85,169,130, 51,208, 17,174,
        96,138, 70,223, 31,159,114,  5};
    static const uint8_t STAR_Y[48]={
        42, 60, 35, 85,110, 75,130, 38,
        95, 55,148, 50,100, 65,140, 62,
       120, 80,155, 32, 90,115, 68,105,
        45,160, 58, 88,132, 72, 48,125,
        98,143, 34, 78,165,112, 70, 44,
       150, 83,108, 52,138, 29,168, 92};

    for(int i=0;i<48;i++){
        int drift=(int)(s_anim/(80+(i%7)*18));
        int x=(STAR_X[i]-(drift%LCD_W)+LCD_W)%LCD_W;
        int y=STAR_Y[i];
        if((i%5)==0) y+=(int)(1.5f*sinf((s_anim+i*200)*0.002f));

        uint32_t phase=s_anim/200+i*37;
        uint16_t c;
        if(phase%17==0)            c=PBL_FG;
        else if((phase/3)%11==0)   c=PBL_LINE;
        else                        c=PBL_DIM;

        if(i%7==0)       draw_star(x,y,c);
        else if(i%3==0){ st7789_fb_pixel(x,y,c);
                         st7789_fb_pixel(x-1,y,c);
                         st7789_fb_pixel(x+1,y,c); }
        else             st7789_fb_pixel(x,y,c);
    }

    for(int k=0;k<3;k++){
        uint32_t t=(s_anim+(uint32_t)k*1100)%4200;
        if(t<1000){
            int sx=220-(int)(t*18/100)-k*40;
            int sy=36+(int)(t*11/100)+k*22;
            draw_line(sx,    sy,   sx+28,sy-12,PBL_FG);
            draw_line(sx+4,  sy+2, sx+20,sy-5, PBL_DIM);
            draw_line(sx+8,  sy+3, sx+14,sy-2, PBL_LINE);
            st7789_fb_pixel(sx,   sy,   PBL_FG);
            st7789_fb_pixel(sx-1, sy,   PBL_FG);
            st7789_fb_pixel(sx,   sy+1, PBL_FG);
        }
    }
}

static void draw_sunrise_scene(int cx, int cy)
{
    int rise=(int)(4.f*sinf(s_anim*0.0018f));
    int sx=cx, sy=cy-rise;

    // Warm gradient sky above horizon
    if(sy+1>STATUS_BAR_H)
        ui_gradient_h(0,STATUS_BAR_H,LCD_W,sy+1-STATUS_BAR_H,
                      COLOR_BLACK, rgb565(0x3C,0x12,0x00));

    // Filled sun at horizon
    st7789_fb_circle_fill(sx,sy,22,PBL_FG);
    st7789_fb_circle_fill(sx,sy,12,rgb565(0x60,0x28,0x00));

    // Horizon cut: erase lower sun half + below
    st7789_fb_rect(0,sy+1,LCD_W,42,PBL_BG);

    // Radial rays above horizon only
    int pulse=(s_anim/180)%9;
    for(int i=0;i<9;i++){
        float a=(-80.f+i*20.f)*3.14159f/180.f;
        int inner=26+(i==pulse?2:0);
        int outer=38+(i==pulse?5:0);
        int x1=sx+(int)(inner*sinf(a));
        int y1=sy-(int)(inner*cosf(a));
        int x2=sx+(int)(outer*sinf(a));
        int y2=sy-(int)(outer*cosf(a));
        if(y2<sy+4) draw_line(x1,y1,x2,y2,(i==pulse)?PBL_FG:PBL_DIM);
    }

    // Filled clouds in front of sun (depth: front=white, mid=dim, back=dim)
    draw_cloud_wrap(32-(int)((s_anim/65)%336),  sy-14, 58, PBL_FG);
    draw_cloud_wrap(154-(int)((s_anim/88)%336), sy+2,  72, PBL_DIM);
    draw_cloud_wrap(246-(int)((s_anim/54)%336), sy-6,  50, PBL_FG);
}

static void draw_app_icon(int icon, int cx, int cy, uint16_t c, uint16_t bg)
{
    switch(icon){
        case 0: // wallet
            st7789_fb_rect_outline(cx-18,cy-10,36,24,c);
            st7789_fb_hline(cx-16,cy-3,32,c);
            st7789_fb_circle_fill(cx+10,cy+2,2,c);
            break;
        case 1: // receive
            draw_line(cx,cy-18,cx,cy+4,c);
            draw_line(cx-9,cy-5,cx,cy+4,c);
            draw_line(cx+9,cy-5,cx,cy+4,c);
            st7789_fb_rect_outline(cx-18,cy+9,36,10,c);
            break;
        case 2: // activity
            for(int i=0;i<3;i++){
                st7789_fb_circle_fill(cx-15,cy-12+i*12,2,c);
                st7789_fb_hline(cx-8,cy-12+i*12,28,c);
            }
            break;
        case 3: // stats
            st7789_fb_rect(cx-17,cy+4,6,14,c);
            st7789_fb_rect(cx-7,cy-4,6,22,c);
            st7789_fb_rect(cx+3,cy-14,6,32,c);
            st7789_fb_rect(cx+13,cy-8,6,26,c);
            break;
        case 4: // games
            st7789_fb_rect_outline(cx-20,cy-10,22,20,c);
            st7789_fb_hline(cx-16,cy,14,c);
            st7789_fb_vline(cx-9,cy-7,14,c);
            st7789_fb_circle_fill(cx+13,cy-4,3,c);
            st7789_fb_circle_fill(cx+22,cy+5,3,c);
            break;
        case 5: // settings
            st7789_fb_circle(cx,cy,13,c);
            st7789_fb_circle(cx,cy,5,c);
            st7789_fb_hline(cx-20,cy,7,c); st7789_fb_hline(cx+14,cy,7,c);
            st7789_fb_vline(cx,cy-20,7,c); st7789_fb_vline(cx,cy+14,7,c);
            break;
        default: // lock
            st7789_fb_rect_outline(cx-15,cy-1,30,20,c);
            st7789_fb_circle(cx,cy-4,12,c);
            st7789_fb_rect(cx-12,cy-4,24,8,bg);
            st7789_fb_vline(cx,cy+6,8,c);
            break;
    }
}

static void draw_app_tile(int x, int y, int icon, const char *label, bool sel)
{
    draw_icon_card(x, y, 94, 78, label, sel);
    uint16_t fg = sel ? PBL_BG : PBL_FG;
    uint16_t bg = sel ? PBL_FG : PBL_BG;
    draw_app_icon(icon, x + 47, y + 31, fg, bg);
}

static void ui_transition_mark(int8_t dir)
{
    s_ui_transition_ms=UI_TRANSITION_MS;
    s_ui_transition_dir=(dir<0)?-1:1;
}

static void render_system_transition(void)
{
    if(s_ui_transition_ms==0) return;
    uint32_t elapsed=UI_TRANSITION_MS-s_ui_transition_ms;
    // Quadratic ease-out: snappy start, smooth landing
    float t=(float)elapsed/(float)UI_TRANSITION_MS;
    float ease=1.f-(1.f-t)*(1.f-t);
    int edge=(int)(ease*LCD_W);
    int x=(s_ui_transition_dir>0)?edge:(LCD_W-edge);
    if(x<0) x=0;
    if(x>=LCD_W) x=LCD_W-1;

    // Crisp leading edge
    st7789_fb_vline(x,STATUS_BAR_H,LCD_H-STATUS_BAR_H,PBL_FG);
    // Motion-blur shadow (3 trailing lines, gradient dim→line→panel)
    const uint16_t shadow[3]={PBL_DIM,PBL_LINE,PBL_PANEL};
    for(int i=0;i<3;i++){
        int xx=x-s_ui_transition_dir*(i+1)*4;
        if(xx>=0&&xx<LCD_W) st7789_fb_vline(xx,STATUS_BAR_H,LCD_H-STATUS_BAR_H,shadow[i]);
    }
}

// ============================================================
// Render functions
// ============================================================

static void render_watchface(void)
{
    st7789_fb_rect(0,STATUS_BAR_H,LCD_W,LCD_H-STATUS_BAR_H,PBL_BG);
    char t[6]; snprintf(t,sizeof(t),"%02d:%02d",s_h,s_m);
    char sec[4]; snprintf(sec,sizeof(sec),":%02d",s_s);
    const char *nm=wallet_name();
    const uint8_t *pk=wallet_pubkey();
    char short_pk[20]; short_pk_hex(pk, short_pk, sizeof(short_pk));
    uint8_t bat=hal_battery_percent();

    if(s_watchface==0){
        draw_sunrise_scene(LCD_W/2,112);
        ui_str_center(30,"GOOD MORNING",PBL_DIM,1);
        int gmw=ui_str_width("GM",5);
        ui_str(LCD_W/2-gmw/2,56,"GM",PBL_FG,5);
        int tw=ui_str_width(t,3); ui_str(LCD_W/2-tw/2,126,t,PBL_FG,3);
        ui_str(LCD_W/2+tw/2+2,134,sec,PBL_DIM,1);
        st7789_fb_rect_outline(28,164,184,34,PBL_LINE);
        ui_str(38,174,"TODAY",PBL_DIM,1);
        ui_str_right(202,174,s_nfc_armed?"NFC READY":"NFC OFF",PBL_FG,1);
        draw_meter(48,186,144,7,bat,PBL_FG);
    } else if(s_watchface==1){
        draw_night_sky();
        ui_str_center(42,"GOOD NIGHT",PBL_DIM,1);
        draw_moon_scene(67,74);
        ui_str(118,64,"GN",PBL_FG,4);
        int tw=ui_str_width(t,3); ui_str(LCD_W/2-tw/2,134,t,PBL_FG,3);
        ui_str_center(180,"HOLD K4 LOCKS",PBL_DIM,1);
        draw_meter(54,196,132,7,bat,PBL_FG);
    } else if(s_watchface==2){
        ui_str_center(40,"SOLWEAR",PBL_DIM,1);
        int tw=ui_str_width(t,4); ui_str(LCD_W/2-tw/2,78,t,PBL_FG,4);
        ui_str_center(122,sec,PBL_DIM,2);
        st7789_fb_rect_outline(34,158,172,34,PBL_LINE);
        ui_str_center(168,nm&&nm[0]?nm:"PROTO V2",PBL_FG,1);
        ui_str_center(182,short_pk,PBL_DIM,1);
    } else if(s_watchface==3){
        int cx=LCD_W/2, cy=STATUS_BAR_H+90, r=70;
        st7789_fb_circle(cx,cy,r,PBL_FG);
        st7789_fb_circle(cx,cy,r-9,PBL_LINE);
        for(int i=0;i<12;i++){
            float a=i*3.14159f*30.f/180.f;
            int x1=cx+(int)((r-13)*sinf(a)), y1=cy-(int)((r-13)*cosf(a));
            int x2=cx+(int)((r-4)*sinf(a)),  y2=cy-(int)((r-4)*cosf(a));
            draw_line(x1,y1,x2,y2,(i%3)==0?PBL_FG:PBL_DIM);
        }
        float ha=((s_h%12)+s_m/60.f)*30.f*3.14159f/180.f;
        float ma=s_m*6.f*3.14159f/180.f;
        float sa=s_s*6.f*3.14159f/180.f;
        draw_line(cx,cy,cx+(int)(38*sinf(ha)),cy-(int)(38*cosf(ha)),PBL_FG);
        draw_line(cx,cy,cx+(int)(58*sinf(ma)),cy-(int)(58*cosf(ma)),PBL_FG);
        draw_line(cx,cy,cx+(int)(62*sinf(sa)),cy-(int)(62*cosf(sa)),PBL_DIM);
        st7789_fb_circle_fill(cx,cy,4,PBL_FG);
        ui_str_center(184,t,PBL_DIM,1);
    } else if(s_watchface==4) {
        ui_str_center(38,"WALLET",PBL_DIM,1);
        char bal[28]; snprintf(bal,sizeof(bal),"%.4f SOL",(double)s_balance);
        int bw=ui_str_width(bal,3); ui_str(LCD_W/2-bw/2,72,bal,PBL_FG,3);
        st7789_fb_rect_outline(34,124,172,42,PBL_LINE);
        ui_str_center(134,short_pk,PBL_FG,1);
        ui_str_center(150,s_nfc_armed?"PHONE READS TAG":"NFC OFF",PBL_DIM,1);
        ui_str_center(190,"K3 CYCLES FACE",PBL_DIM,1);
    } else {
        st7789_fb_rect(34,48,172,40,PBL_FG);
        ui_str_center(60,"SOLWEAR",PBL_BG,2);
        int tw=ui_str_width(t,3); ui_str(LCD_W/2-tw/2,108,t,PBL_FG,3);
        ui_str_center(150,"TINY TRUSTED SIGNER",PBL_DIM,1);
        st7789_fb_hline(54,176,132,PBL_LINE);
        ui_str_center(188,s_nfc_armed?"READY":"OFFLINE",PBL_FG,1);
    }
    ui_nfc_icon(LCD_W-18,STATUS_BAR_H+4,s_nfc_armed);
}

static void render_home_grid(void)
{
    st7789_fb_rect(0,STATUS_BAR_H,LCD_W,LCD_H-STATUS_BAR_H,PBL_BG);
    const char *lbl[GRID_APP_COUNT]={"Wallet","Receive","Activity","Stats","Games","Settings","Lock"};
    int page=s_grid_sel/4;
    int start=page*4;
    const int W=94,H=78,GX=14,GY=10;
    int x0=(LCD_W-2*W-GX)/2, y0=STATUS_BAR_H+12;
    for(int pos=0;pos<4;pos++){
        int idx=start+pos;
        if(idx>=GRID_APP_COUNT) break;
        int col=pos%2,row=pos/2;
        draw_app_tile(x0+col*(W+GX),y0+row*(H+GY),idx,lbl[idx],s_grid_sel==idx);
    }
    for(int i=0;i<GRID_PAGE_COUNT;i++){
        int dx=LCD_W/2-(GRID_PAGE_COUNT-1)*8+i*16;
        if(i==page) ui_rounded_rect(dx-8,LCD_H-23,16,6,3,PBL_FG);
        else        st7789_fb_circle_fill(dx,LCD_H-20,2,PBL_LINE);
    }
}

static void render_home(void)
{
    st7789_fb_fill(COLOR_BLACK);
    draw_status(NULL);
    if(s_slide==SLIDE_WATCHFACE) render_watchface();
    else                          render_home_grid();
    // Pill indicator dots
    for(int i=0;i<SLIDE_COUNT;i++){
        int dx=LCD_W/2-(SLIDE_COUNT-1)*8+i*16;
        if(i==s_slide) ui_rounded_rect(dx-8,LCD_H-10,16,6,3,PBL_FG);
        else           st7789_fb_circle_fill(dx,LCD_H-7,2,PBL_LINE);
    }
}

static void render_ob_choice(const char *title, const char *a, const char *b)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    ui_str_center(30,title,PBL_FG,2);
    for(int i=0;i<2;i++){
        int y=80+i*80;
        uint16_t bg=(s_ob_sel==i)?PBL_FG:PBL_BG;
        uint16_t fg=(s_ob_sel==i)?PBL_BG:PBL_FG;
        st7789_fb_rect(20,y,200,60,bg);
        st7789_fb_rect_outline(20,y,200,60,(s_ob_sel==i)?PBL_FG:PBL_LINE);
        const char*l=(i==0)?a:b; int tw=ui_str_width(l,2);
        ui_str(120-tw/2,y+22,l,fg,2);
    }
    ui_str_center(220,"K1/K2 choose  K3 confirm",PBL_DIM,1);
}

static void render_onboard(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    switch(s_ob){
        case OB_WELCOME:
            st7789_fb_rect(28,54,184,42,PBL_FG);
            ui_str_center(66,"SolWearOS",PBL_BG,3);
            ui_str_center(115,"Solana Hardware Wallet",PBL_DIM,1);
            ui_str_center(165,"Press K3 to begin",PBL_FG,2);
            break;
        case OB_CHOICE:
            render_ob_choice("Setup Wallet","Generate Key","Import Key");
            break;
        case OB_IMPORT_HOW:
            render_ob_choice("Import Key","Manual Entry","Back");
            break;
        case OB_IMPORT_NFC:{
            ui_str_center(30,"NFC Import Removed",PBL_FG,2);
            uint16_t nc=((s_anim/180)&1)?PBL_FG:PBL_DIM;
            st7789_fb_circle(120,130,24,nc); st7789_fb_circle(120,130,16,nc);
            st7789_fb_circle(120,130,8,nc);  st7789_fb_circle_fill(120,130,3,nc);
            ui_str_center(170,"Use manual entry",PBL_DIM,1);
            ui_str_center(190,"K4 = back",PBL_DIM,1);
            break;
        }
        case OB_IMPORT_HEX:
            ui_str_center(30,"Private Key (hex)",PBL_FG,2);
            ui_str_center(48,"64 chars, A-F 0-9",PBL_DIM,1);
            roulette_render(&s_roulette);
            break;
        case OB_GEN_CONFIRM:
            ui_str_center(40,"Key Ready",PBL_FG,2);
            st7789_fb_rect_outline(16,70,208,28,PBL_LINE);
            ui_str_center(79,s_ob_pub4,PBL_FG,2);
            ui_str_center(110,"first 4 bytes shown",PBL_DIM,1);
            ui_str_center(155,"K3 = set password",PBL_FG,2);
            ui_str_center(178,"K4 = restart",PBL_DIM,1);
            break;
        case OB_SET_PASS:
            ui_str_center(30,"Set Password",PBL_FG,2);
            ui_str_center(48,"4-8 chars (1-8 A-Z)",PBL_DIM,1);
            roulette_render(&s_roulette);
            break;
        case OB_PASS_WARN:
            st7789_fb_rect_outline(14,40,212,100,PBL_FG);
            ui_str_center(58,"Weak Password",PBL_FG,2);
            ui_str_center(82,"Less than 8 chars.",PBL_FG,1);
            ui_str_center(102,"K3 = continue anyway",PBL_DIM,1);
            ui_str_center(118,"K4 = set stronger",PBL_DIM,1);
            break;
        case OB_SET_NAME:
            ui_str_center(30,"Wallet Name",PBL_FG,2);
            roulette_render(&s_roulette);
            break;
        case OB_DONE:
            ui_str_center(80,"Welcome",PBL_FG,3);
            ui_str_center(130,s_ob_name,PBL_FG,2);
            ui_str_center(170,"K3 to enter OS",PBL_DIM,2);
            break;
    }
}

static void render_lock(void)
{
    st7789_fb_fill(COLOR_BLACK);
    draw_status("LOCK");
    draw_app_icon(6,LCD_W/2,45,PBL_DIM,PBL_BG);
    roulette_render(&s_roulette);
    if(s_lock_err[0]) ui_str_center(LCD_H-16,s_lock_err,PBL_FG,1);
}

static void render_settings(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Settings");
    for(int i=0;i<SETTINGS_COUNT;i++){
        int y=STATUS_BAR_H+4+i*24;
        if(y+21>LCD_H-12) break;
        uint16_t bg=(s_settings_sel==i)?PBL_FG:PBL_BG;
        uint16_t fg=(s_settings_sel==i)?PBL_BG:PBL_FG;
        st7789_fb_rect(6,y,LCD_W-12,21,bg);
        st7789_fb_rect_outline(6,y,LCD_W-12,21,(s_settings_sel==i)?PBL_FG:PBL_LINE);
        ui_str(12,y+6,s_settings_items[i],fg,1);
    }
    ui_str_center(LCD_H-12,"K1/K2 nav  K3 select  K4 back",PBL_DIM,1);
}

static void render_transactions(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Transactions");
    char bal[32]; snprintf(bal,sizeof(bal),"Bal: %.4f SOL",(double)s_balance);
    st7789_fb_rect_outline(4,STATUS_BAR_H+2,LCD_W-8,22,PBL_LINE);
    ui_str_center(STATUS_BAR_H+8,bal,PBL_FG,1);
    if(s_rx_n==0){
        ui_str_center(130,"No transactions",PBL_DIM,2);
        ui_str_center(158,"Phone prepares, watch signs",PBL_DIM,1);
        return;
    }
    for(int i=0;i<5&&(s_rx_scroll+i)<s_rx_n;i++){
        int y=STATUS_BAR_H+28+i*30;
        st7789_fb_rect_outline(4,y,LCD_W-8,26,PBL_LINE);
        char tmp[40]; strncpy(tmp,s_rx[s_rx_scroll+i],39); tmp[39]='\0';
        ui_str(8,y+7,tmp,PBL_FG,1);
    }
    char pg[12]; snprintf(pg,sizeof(pg),"%d/%d",s_rx_scroll+1,(int)s_rx_n);
    ui_str_center(LCD_H-12,pg,PBL_DIM,1);
}

static void render_wallet_app(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Wallet");
    const uint8_t *pk=wallet_pubkey();
    char p1[32], p2[32], bal[32];
    snprintf(bal,sizeof(bal),"%.4f SOL",(double)s_balance);
    snprintf(p1,sizeof(p1),"%02X%02X%02X%02X %02X%02X%02X%02X",pk[0],pk[1],pk[2],pk[3],pk[4],pk[5],pk[6],pk[7]);
    snprintf(p2,sizeof(p2),"%02X%02X%02X%02X %02X%02X%02X%02X",pk[24],pk[25],pk[26],pk[27],pk[28],pk[29],pk[30],pk[31]);

    st7789_fb_rect_outline(10,34,220,62,PBL_LINE);
    ui_str_center(46,"Balance",PBL_DIM,1);
    int bw=ui_str_width(bal,3); ui_str(LCD_W/2-bw/2,60,bal,PBL_FG,3);

    st7789_fb_rect_outline(10,106,220,58,PBL_LINE);
    ui_str(18,116,"Pubkey",PBL_DIM,1);
    ui_str(18,134,p1,PBL_FG,1);
    ui_str(18,148,p2,PBL_FG,1);

    uint16_t c=s_nfc_armed?PBL_FG:PBL_DIM;
    st7789_fb_rect_outline(10,176,220,34,PBL_LINE);
    ui_str(18,188,"Phone:",PBL_DIM,1);
    ui_str(82,188,"Tap to share",c,1);
    ui_str_center(224,"K4=back",PBL_DIM,1);
}

static void render_receive_app(void)
{
    st7789_fb_fill(PBL_BG);
    draw_status("Receive");

    const uint8_t *pk=wallet_pubkey();
    char pshort[20]; short_pk_hex(pk, pshort, sizeof(pshort));
    const char *nm=wallet_name();

    ui_str_center(38,nm&&nm[0]?nm:"SolWear",PBL_FG,2);
    ui_str_center(62,"PHONE READS WATCH",PBL_DIM,1);

    int cx=LCD_W/2, cy=116;
    for(int r=18;r<=52;r+=17) st7789_fb_circle(cx,cy,r,s_nfc_armed?PBL_FG:PBL_DIM);
    st7789_fb_circle_fill(cx,cy,6,s_nfc_armed?PBL_FG:PBL_DIM);
    st7789_fb_rect_outline(32,158,176,30,PBL_LINE);
    ui_str_center(168,pshort,PBL_FG,1);

    ui_str_center(200,s_nfc_armed?"Hold phone within 3 cm":"NFC disabled",s_nfc_armed?PBL_FG:PBL_DIM,1);
    ui_str_center(224,"K4=back",PBL_DIM,1);
}

static void render_sync_effect(void)
{
    if(s_sync_widget_ms==0) return;
    uint16_t fg=PBL_FG;
    st7789_fb_rect(18,52,204,126,PBL_BG);
    st7789_fb_rect_outline(18,52,204,126,fg);

    int wx=72, px=168, cy=104;
    st7789_fb_rect_outline(wx-17,cy-25,34,50,fg);
    st7789_fb_rect_outline(px-18,cy-28,36,56,fg);
    int phase=(s_anim/140)%4;
    for(int i=0;i<4;i++){
        int x=102+i*12;
        st7789_fb_rect(x,cy-2,5,5,(i==phase)?fg:PBL_DIM);
    }
    st7789_fb_circle_fill(wx,cy,3,fg);
    st7789_fb_circle_fill(px,cy,3,fg);

    const char *msg = g_nfc_sync.message[0] ? g_nfc_sync.message : "Wallet shared";
    ui_str_center(148,msg,fg,1);
    ui_str_center(162,"finish in phone app",PBL_DIM,1);
}

static void render_stats(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Stats");
    char buf[48]; int y=STATUS_BAR_H+10;
    snprintf(buf,sizeof(buf),"Battery:  %d%%",hal_battery_percent());
    ui_str(8,y,buf,PBL_FG,2); y+=26;
    draw_meter(10,y,220,8,hal_battery_percent(),PBL_FG); y+=18;
    snprintf(buf,sizeof(buf),"Voltage:  %.2fV",hal_battery_mv()/1000.f);
    ui_str(8,y,buf,PBL_FG,1); y+=18;
    snprintf(buf,sizeof(buf),"Charging: %s",hal_battery_charging()?"Yes":"No");
    ui_str(8,y,buf,PBL_FG,1); y+=18;
    snprintf(buf,sizeof(buf),"Cell:     350mAh LW303040");
    ui_str(8,y,buf,PBL_DIM,1); y+=22;
    snprintf(buf,sizeof(buf),"NFC:      %s",s_nfc_armed?"Armed":"Disarmed");
    ui_str(8,y,buf,s_nfc_armed?PBL_FG:PBL_DIM,1); y+=18;
    snprintf(buf,sizeof(buf),"Wallet:   %s",wallet_name());
    ui_str(8,y,buf,PBL_FG,1); y+=18;
    snprintf(buf,sizeof(buf),"Heap:     %u B",(unsigned)esp_get_free_heap_size());
    ui_str(8,y,buf,PBL_DIM,1); y+=18;
    ui_str(8,y,SOLWEAR_OS_VERSION,PBL_DIM,1);
}

static int8_t s_games_sel=0;
static const char *s_game_names[]={"Ping Pong","Tetris","Tamagotchi"};

static void render_games_menu(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Games");
    for(int i=0;i<3;i++){
        int y=STATUS_BAR_H+20+i*64;
        uint16_t bg=(s_games_sel==i)?PBL_FG:PBL_BG;
        uint16_t fg=(s_games_sel==i)?PBL_BG:PBL_FG;
        st7789_fb_rect(20,y,200,54,bg);
        st7789_fb_rect_outline(20,y,200,54,(s_games_sel==i)?PBL_FG:PBL_LINE);
        int tw=ui_str_width(s_game_names[i],2);
        ui_str(120-tw/2,y+19,s_game_names[i],fg,2);
    }
    ui_str_center(LCD_H-12,"K1/K2 select  K3 play  K4 back",PBL_DIM,1);
}

static void render_pong(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    if(pp.over){
        ui_str_center(90,(pp.sp>=5)?"YOU WIN":"AI WINS",PBL_FG,3);
        ui_str_center(142,"K3 again  K4 back",PBL_DIM,2);
        return;
    }
    for(int y=COURT_Y;y<LCD_H;y+=8) st7789_fb_pixel(LCD_W/2,y,PBL_LINE);
    st7789_fb_rect(PAD_W+2,pp.py,PAD_W,PAD_H,PBL_FG);
    st7789_fb_rect(LCD_W-PAD_W*2-4,(int16_t)pp.ay,PAD_W,PAD_H,PBL_DIM);
    st7789_fb_circle_fill((int16_t)pp.bx,(int16_t)pp.by,BALL_R,PBL_FG);
    char sc[16]; snprintf(sc,sizeof(sc),"%d  %d",pp.sp,pp.sa);
    ui_str_center(COURT_Y+4,sc,PBL_DIM,2);
    if(pp.paused) ui_str_center(LCD_H/2,"PAUSED",PBL_FG,2);
}

static void render_tetris(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    if(tet.over){
        ui_str_center(80,"GAME OVER",PBL_FG,3);
        char sc[20]; snprintf(sc,sizeof(sc),"Score: %lu",(unsigned long)tet.score);
        ui_str_center(122,sc,PBL_FG,2);
        ui_str_center(155,"K3 again  K4 back",PBL_DIM,1);
        return;
    }
    st7789_fb_rect_outline(TBX-1,TBY-1,TC*TCW+2,TR*TCH+2,PBL_LINE);
    for(int r=0;r<TR;r++) for(int c=0;c<TC;c++)
        if(tet.b[r][c]) st7789_fb_rect(TBX+c*TCW+1,TBY+r*TCH+1,TCW-2,TCH-2,kTC[tet.b[r][c]-1]);
    for(int c=0;c<4;c++){
        int8_t cx=tet.px+kP[tet.piece][tet.rot][c][0];
        int8_t cy=tet.py+kP[tet.piece][tet.rot][c][1];
        if(cy>=0) st7789_fb_rect(TBX+cx*TCW+1,TBY+cy*TCH+1,TCW-2,TCH-2,kTC[tet.piece]);
    }
    int sx=TBX+TC*TCW+6;
    char sc[12]; snprintf(sc,sizeof(sc),"%lu",(unsigned long)tet.score);
    ui_str(sx,TBY+8,"SC",PBL_DIM,1); ui_str(sx,TBY+18,sc,PBL_FG,1);
    char lv[4]; snprintf(lv,sizeof(lv),"%u",tet.level);
    ui_str(sx,TBY+38,"LV",PBL_DIM,1); ui_str(sx,TBY+48,lv,PBL_FG,1);
    ui_str(sx,TBY+68,"K4",PBL_DIM,1); ui_str(sx,TBY+78,"drop",PBL_DIM,1);
}

static void draw_tama_pet(int cx,int cy)
{
    if(!tama.alive){
        st7789_fb_rect_outline(cx-16,cy-28,32,44,PBL_LINE);
        ui_str_center(cy-10,"RIP",PBL_DIM,2); return;
    }
    uint16_t bc=(tama.hunger<20||tama.happy<20||tama.energy<20)?PBL_DIM:PBL_FG;
    st7789_fb_circle_fill(cx,cy,22,bc);
    if(tama_frame==0){
        st7789_fb_circle_fill(cx-8,cy-6,4,PBL_BG);
        st7789_fb_circle_fill(cx+8,cy-6,4,PBL_BG);
        st7789_fb_circle_fill(cx-7,cy-7,2,PBL_FG);
        st7789_fb_circle_fill(cx+9,cy-7,2,PBL_FG);
    } else {
        st7789_fb_hline(cx-12,cy-6,8,PBL_BG);
        st7789_fb_hline(cx+4, cy-6,8,PBL_BG);
    }
    if(tama.happy>=50){ for(int i=-5;i<=5;i++) st7789_fb_pixel(cx+i,cy+9+(i*i)/8,PBL_BG); }
    else              { for(int i=-5;i<=5;i++) st7789_fb_pixel(cx+i,cy+12-(i*i)/8,PBL_BG); }
    int fy=cy+20+(tama_frame?2:0);
    st7789_fb_circle_fill(cx-14,fy,6,bc); st7789_fb_circle_fill(cx+14,fy,6,bc);
}

static void draw_tama_bar(int x,int y,const char*lbl,uint8_t v){
    ui_str(x,y,lbl,PBL_DIM,1);
    st7789_fb_rect_outline(x+20,y+1,60,6,PBL_LINE);
    uint16_t c=(v>50)?PBL_FG:PBL_DIM;
    st7789_fb_rect(x+20,y+1,60*v/100,6,c);
}

static void render_tamagotchi(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Tamagotchi");
    if(!tama.alive){
        ui_str_center(60,"Your pet died",PBL_FG,2);
        draw_tama_pet(LCD_W/2,130);
        ui_str_center(175,"K3 Resurrect",PBL_DIM,2);
        return;
    }
    draw_tama_pet(LCD_W/2,112);
    draw_tama_bar(10,168,"H:",tama.hunger);
    draw_tama_bar(10,182,"J:",tama.happy);
    draw_tama_bar(10,196,"E:",tama.energy);
    if(tama_msg_on) ui_str_center(152,tama_msg,PBL_FG,2);
    ui_str_center(LCD_H-12,"K1 Feed K2 Play K3 Nap",PBL_DIM,1);
}

static void render_tx_overlay(void)
{
    // dim bg
    for(int y=18;y<LCD_H-18;y+=2) for(int x=0;x<LCD_W;x+=2) st7789_fb_pixel(x,y,PBL_BG);
    uint16_t border=PBL_FG;
    st7789_fb_rect(8,20,LCD_W-16,LCD_H-40,PBL_BG);
    st7789_fb_rect_outline(8,20,LCD_W-16,LCD_H-40,border);

    if(s_tx_state==TX_SHOW){
        ui_str_center(30,"VERIFY REQUEST",PBL_FG,2);
        char fr[18],to[18];
        short_text(g_nfc_tx.from, fr, sizeof(fr));
        short_text(g_nfc_tx.to, to, sizeof(to));
        float sol=(float)(g_nfc_tx.lamports/1000000000ULL)+(float)(g_nfc_tx.lamports%1000000000ULL)/1e9f;
        char amt[20]; snprintf(amt,sizeof(amt),g_nfc_tx.lamports?"%.4f SOL":"TX REQUEST",(double)sol);
        ui_str(16,55,"From",PBL_DIM,1); ui_str(70,55,fr,PBL_FG,1);
        ui_str(16,72,"To",  PBL_DIM,1); ui_str(70,72,to,PBL_FG,1);
        int tw=ui_str_width(amt,2); ui_str(LCD_W/2-tw/2,96,amt,PBL_FG,2);
        char net[32]; snprintf(net,sizeof(net),"%s  fee %.6f",g_nfc_tx.network[0]?g_nfc_tx.network:"devnet",
                               (double)g_nfc_tx.fee_lamports/1000000000.0);
        ui_str_center(120,net,PBL_DIM,1);
        st7789_fb_rect(14,145,96,34,PBL_FG);
        int t1w=ui_str_width("K3 Review",1);
        ui_str(14+(96-t1w)/2,156,"K3 Review",PBL_BG,1);
        st7789_fb_rect_outline(118,145,96,34,PBL_FG);
        int t2w=ui_str_width("K4 Reject",1);
        ui_str(118+(96-t2w)/2,156,"K4 Reject",PBL_FG,1);
    } else if(s_tx_state==TX_TIMER){
        ui_str_center(38,"CHECK DETAILS",PBL_FG,2);
        uint32_t rem=(TX_CONFIRM_TIMER_MS-s_tx_timer+999)/1000;
        char sc[8]; snprintf(sc,sizeof(sc),"%u",(unsigned)rem);
        int tw=ui_str_width(sc,4); ui_str(LCD_W/2-tw/2,82,sc,PBL_FG,4);
        ui_str_center(140,"seconds to final sign",PBL_DIM,1);
        int bw2=(int)((LCD_W-32)*(uint64_t)s_tx_timer/TX_CONFIRM_TIMER_MS);
        st7789_fb_rect_outline(16,152,LCD_W-32,8,PBL_LINE);
        st7789_fb_rect(16,152,bw2,8,PBL_FG);
        ui_str_center(170,"K4 to cancel",PBL_DIM,1);
    } else {
        ui_str_center(42,"TRUSTED SIGN",PBL_FG,2);
        ui_str_center(78,"SolWear will sign",PBL_FG,1);
        ui_str_center(96,"only this request",PBL_DIM,1);
        st7789_fb_rect(20,128,88,34,PBL_FG);
        int t1w=ui_str_width("K3 Sign",1); ui_str(20+(88-t1w)/2,139,"K3 Sign",PBL_BG,1);
        st7789_fb_rect_outline(116,128,88,34,PBL_FG);
        int t2w=ui_str_width("K4 Cancel",1); ui_str(116+(88-t2w)/2,139,"K4 Cancel",PBL_DIM,1);
    }
}

// ============================================================
// Button handlers
// ============================================================
static void handle_ob(btn_event_t ev)
{
    switch(s_ob){
        case OB_WELCOME:
            if(ev==BTN_K3_PRESS) ob_go(OB_CHOICE);
            break;
        case OB_CHOICE:
            if(ev==BTN_K1_PRESS) s_ob_sel=0;
            if(ev==BTN_K2_PRESS) s_ob_sel=1;
            if(ev==BTN_K3_PRESS){
                if(s_ob_sel==0){
                    esp_fill_random(s_ob_seed,32);
                    // pub derivation stubbed until Ed25519 complete
                    snprintf(s_ob_pub4,sizeof(s_ob_pub4),"%02X%02X%02X%02X",
                             s_ob_seed[0],s_ob_seed[1],s_ob_seed[2],s_ob_seed[3]);
                    ob_go(OB_GEN_CONFIRM);
                } else { ob_go(OB_IMPORT_HOW); }
            }
            break;
        case OB_IMPORT_HOW:
            if(ev==BTN_K1_PRESS) s_ob_sel=0;
            if(ev==BTN_K2_PRESS) s_ob_sel=1;
            if(ev==BTN_K3_PRESS){
                if(s_ob_sel==0) { roulette_init(&s_roulette,ROULETTE_MODE_HEX,64,64,false,70); ob_go(OB_IMPORT_HEX); }
                else ob_go(OB_CHOICE);
            }
            if(ev==BTN_K4_PRESS) ob_go(OB_CHOICE);
            break;
        case OB_IMPORT_NFC:
            if(g_nfc_key_import.valid){
                memcpy(s_ob_seed,g_nfc_key_import.seed,32);
                g_nfc_key_import.valid=false;
                snprintf(s_ob_pub4,sizeof(s_ob_pub4),"%02X%02X%02X%02X",
                         s_ob_seed[0],s_ob_seed[1],s_ob_seed[2],s_ob_seed[3]);
                ob_go(OB_GEN_CONFIRM);
            }
            if(ev==BTN_K4_PRESS) ob_go(OB_IMPORT_HOW);
            break;
        case OB_IMPORT_HEX:{
            roulette_result_t r=roulette_feed(&s_roulette,ev);
            if(r==ROULETTE_DONE){
                const char *h = s_roulette.buf;
                for (int i = 0; i < 32; i++) {
                    s_ob_seed[i] = (uint8_t)((hex_nibble(h[i*2]) << 4) | hex_nibble(h[i*2+1]));
                }
                snprintf(s_ob_pub4,sizeof(s_ob_pub4),"%02X%02X%02X%02X",
                         s_ob_seed[0],s_ob_seed[1],s_ob_seed[2],s_ob_seed[3]);
                ob_go(OB_GEN_CONFIRM);
            } else if(r==ROULETTE_CANCELLED) ob_go(OB_IMPORT_HOW);
            break;
        }
        case OB_GEN_CONFIRM:
            if(ev==BTN_K3_PRESS){
                roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,4,true,70);
                ob_go(OB_SET_PASS);
            }
            if(ev==BTN_K4_PRESS) ob_go(OB_CHOICE);
            break;
        case OB_SET_PASS:{
            roulette_result_t r=roulette_feed(&s_roulette,ev);
            if(r==ROULETTE_DONE){
                strncpy(s_ob_pass,s_roulette.buf,sizeof(s_ob_pass)-1);
                if(strlen(s_ob_pass)<8) ob_go(OB_PASS_WARN);
                else { roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,WALLET_NAME_MAX,1,false,70); ob_go(OB_SET_NAME); }
            } else if(r==ROULETTE_CANCELLED) ob_go(OB_GEN_CONFIRM);
            break;
        }
        case OB_PASS_WARN:
            if(ev==BTN_K3_PRESS){ roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,WALLET_NAME_MAX,1,false,70); ob_go(OB_SET_NAME); }
            if(ev==BTN_K4_PRESS){ roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,4,true,70); ob_go(OB_SET_PASS); }
            break;
        case OB_SET_NAME:{
            roulette_result_t r=roulette_feed(&s_roulette,ev);
            if(r==ROULETTE_DONE){
                strncpy(s_ob_name,s_roulette.buf,sizeof(s_ob_name)-1);
                wallet_import_seed(s_ob_seed,s_ob_pass,s_ob_name);
                wallet_load_public();
                hal_nfc_set_wallet_pubkey(wallet_pubkey());
                memset(s_ob_seed,0,32); memset(s_ob_pass,0,sizeof(s_ob_pass));
                ob_go(OB_DONE);
            } else if(r==ROULETTE_CANCELLED) ob_go(OB_PASS_WARN);
            break;
        }
        case OB_DONE:
            if(ev==BTN_K3_PRESS){
                roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,76);
                s_lock_err[0]='\0'; s_screen=SCR_LOCK;
            }
            break;
    }
}

static void handle_lock(btn_event_t ev)
{
    roulette_result_t r=roulette_feed(&s_roulette,ev);
    if(r==ROULETTE_DONE){
        if(wallet_unlock(s_roulette.buf)){ s_lock_err[0]='\0'; s_screen=SCR_HOME; }
        else { strncpy(s_lock_err,"Wrong password!",sizeof(s_lock_err)-1); roulette_reset(&s_roulette); }
    }
}

static void handle_tx(btn_event_t ev)
{
    if(s_tx_state==TX_SHOW){
        if(ev==BTN_K3_PRESS){ s_tx_state=TX_TIMER; s_tx_timer=0; }
        if(ev==BTN_K4_PRESS){ s_tx_overlay=false; g_nfc_tx.valid=false; }
    } else if(s_tx_state==TX_TIMER){
        if(ev==BTN_K4_PRESS){ s_tx_overlay=false; g_nfc_tx.valid=false; }
    } else {
        if(ev==BTN_K3_PRESS){
            uint8_t sig[64]={};
            if(wallet_is_unlocked()){
                wallet_sign(g_nfc_tx.tx_bytes,g_nfc_tx.tx_len,sig);
                if(!hal_nfc_set_sign_response_target(sig,g_nfc_tx.nonce)){
                    memcpy(s_pending_sig,sig,sizeof(s_pending_sig));
                    strncpy(s_pending_sig_nonce,g_nfc_tx.nonce,sizeof(s_pending_sig_nonce)-1);
                    s_pending_sig_nonce[sizeof(s_pending_sig_nonce)-1]='\0';
                    s_pending_sig_valid=true;
                }
                char line[80]; float sol=(float)(g_nfc_tx.lamports/1000000000ULL)+(float)(g_nfc_tx.lamports%1000000000ULL)/1e9f;
                snprintf(line,sizeof(line),"-%.4f SOL to %.8s...",(double)sol,g_nfc_tx.to);
                save_receipt(line); load_receipts();
            }
            s_tx_overlay=false; g_nfc_tx.valid=false;
        }
        if(ev==BTN_K4_PRESS){ s_tx_overlay=false; g_nfc_tx.valid=false; }
    }
}

static void handle_button(btn_event_t ev)
{
    // Global deliberate shortcuts
    if(ev==BTN_K1_HOLD){
        s_nfc_armed=!s_nfc_armed;
        hal_nfc_set_service_enabled(s_nfc_armed);
        if(s_nfc_armed)hal_nfc_ensure_init(); else hal_nfc_shutdown();
        s_nfc_widget_ms=2000; s_nfc_widget_armed=s_nfc_armed; return;
    }
    if(ev==BTN_K4_HOLD){
        if(wallet_is_onboarded()){
            wallet_lock(); s_tx_overlay=false; roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,76);
            s_lock_err[0]='\0'; s_screen=SCR_LOCK;
        }
        return;
    }
    if(ev==BTN_K4_DOUBLE){ s_tx_overlay=false; g_nfc_tx.valid=false; s_screen=SCR_HOME; s_slide=SLIDE_WATCHFACE; return; }
    if(ev==BTN_K1_DOUBLE){
        return;
    }

    if(s_tx_overlay){ handle_tx(ev); return; }

    switch(s_screen){
        case SCR_ONBOARD: handle_ob(ev); break;
        case SCR_LOCK:    handle_lock(ev); break;
        case SCR_HOME:
            if(ev==BTN_K2_PRESS){ if(s_slide==SLIDE_WATCHFACE){s_slide=SLIDE_GRID;s_grid_sel=0;} else if(s_grid_sel<GRID_APP_COUNT-1) s_grid_sel++; }
            if(ev==BTN_K1_PRESS){ if(s_slide==SLIDE_WATCHFACE) s_watchface=(s_watchface+WATCHFACE_COUNT-1)%WATCHFACE_COUNT; else if(s_grid_sel>0) s_grid_sel--; else s_slide=SLIDE_WATCHFACE; }
            if(ev==BTN_K3_PRESS){
                if(s_slide==SLIDE_WATCHFACE) s_watchface=(s_watchface+1)%WATCHFACE_COUNT;
                else {
                    const screen_id_t t[GRID_APP_COUNT]={SCR_WALLET,SCR_RECEIVE,SCR_TRANSACTIONS,SCR_STATS,SCR_GAMES_MENU,SCR_SETTINGS,SCR_LOCK};
                    s_screen=t[s_grid_sel];
                    if(s_screen==SCR_LOCK){ wallet_lock(); roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,76); }
                    if(s_screen==SCR_WALLET) load_receipts();
                    if(s_screen==SCR_TRANSACTIONS) load_receipts();
                    if(s_screen==SCR_GAMES_MENU) s_games_sel=0;
                }
            }
            if(ev==BTN_K4_PRESS && s_slide==SLIDE_GRID) s_slide=SLIDE_WATCHFACE;
            break;
        case SCR_SETTINGS:
            if(ev==BTN_K1_PRESS&&s_settings_sel>0) s_settings_sel--;
            if(ev==BTN_K2_PRESS&&s_settings_sel<SETTINGS_COUNT-1) s_settings_sel++;
            if(ev==BTN_K3_PRESS&&s_settings_sel<WATCHFACE_COUNT) s_watchface=(uint8_t)s_settings_sel;
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_WALLET:
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_RECEIVE:
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_TRANSACTIONS:
            if(ev==BTN_K1_PRESS&&s_rx_scroll>0) s_rx_scroll--;
            if(ev==BTN_K2_PRESS&&s_rx_scroll<s_rx_n-1) s_rx_scroll++;
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_STATS:
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_GAMES_MENU:
            if(ev==BTN_K1_PRESS&&s_games_sel>0) s_games_sel--;
            if(ev==BTN_K2_PRESS&&s_games_sel<2) s_games_sel++;
            if(ev==BTN_K3_PRESS){
                if(s_games_sel==0){pp_init(); s_screen=SCR_PING_PONG;}
                if(s_games_sel==1){tet_init(); s_screen=SCR_TETRIS;}
                if(s_games_sel==2){tama_load(); s_screen=SCR_TAMAGOTCHI;}
            }
            if(ev==BTN_K4_PRESS) s_screen=SCR_HOME;
            break;
        case SCR_PING_PONG:
            if(!pp.over){
                if(ev==BTN_K1_PRESS){pp.py-=14; if(pp.py<COURT_Y)pp.py=COURT_Y;}
                if(ev==BTN_K2_PRESS){pp.py+=14; if(pp.py+PAD_H>LCD_H-2)pp.py=LCD_H-2-PAD_H;}
                if(ev==BTN_K3_PRESS) pp.paused=!pp.paused;
            } else {
                if(ev==BTN_K3_PRESS) pp_init();
            }
            if(ev==BTN_K4_PRESS) s_screen=SCR_GAMES_MENU;
            break;
        case SCR_TETRIS:
            if(!tet.over){
                if(ev==BTN_K1_PRESS&&tet_fits(tet.px-1,tet.py,tet.rot)) tet.px--;
                if(ev==BTN_K2_PRESS&&tet_fits(tet.px+1,tet.py,tet.rot)) tet.px++;
                if(ev==BTN_K3_PRESS){
                    uint8_t nr=(tet.rot+1)%4;
                    if(tet_fits(tet.px,tet.py,nr)) tet.rot=nr;
                    else if(tet_fits(tet.px-1,tet.py,nr)){tet.px--;tet.rot=nr;}
                    else if(tet_fits(tet.px+1,tet.py,nr)){tet.px++;tet.rot=nr;}
                }
                if(ev==BTN_K4_PRESS){ while(tet_fits(tet.px,tet.py+1,tet.rot))tet.py++; tet_place(); tet.fall_ms=0; }
            } else {
                if(ev==BTN_K3_PRESS) tet_init();
                if(ev==BTN_K4_PRESS) s_screen=SCR_GAMES_MENU;
            }
            break;
        case SCR_TAMAGOTCHI:
            if(!tama.alive){
                if(ev==BTN_K3_PRESS){tama=(tama_t){50,80,70,true,0};tama_save();}
            } else {
                if(ev==BTN_K1_PRESS){tama.hunger=tama.hunger+30>100?100:tama.hunger+30; strncpy(tama_msg,"Nom nom!",sizeof(tama_msg)-1); tama_msg_on=true;tama_msg_t=0;}
                if(ev==BTN_K2_PRESS){tama.happy=tama.happy+30>100?100:tama.happy+30;     strncpy(tama_msg,"Wheee!",sizeof(tama_msg)-1);   tama_msg_on=true;tama_msg_t=0;}
                if(ev==BTN_K3_PRESS){tama.energy=tama.energy+30>100?100:tama.energy+30;  strncpy(tama_msg,"Zzz...",sizeof(tama_msg)-1);   tama_msg_on=true;tama_msg_t=0;}
            }
            if(ev==BTN_K4_PRESS){tama_save();s_screen=SCR_GAMES_MENU;}
            break;
    }
}

// ============================================================
// app_main
// ============================================================
void app_main(void)
{
    ESP_LOGI(TAG,"=== %s ===",SOLWEAR_OS_VERSION);

    // Display first — always shows something on screen
    ESP_ERROR_CHECK(st7789_init());

    // Disable WiFi at runtime to reclaim ~30KB RAM (BT already off via sdkconfig)
    esp_wifi_stop();
    esp_wifi_deinit();

    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();nvs_flash_init();}

    esp_vfs_spiffs_conf_t sp={.base_path="/spiffs",.partition_label="spiffs",.max_files=10,.format_if_mount_failed=true};
    esp_err_t sp_err=esp_vfs_spiffs_register(&sp);
    if(sp_err!=ESP_OK) ESP_LOGW(TAG,"SPIFFS mount failed (%s) — receipts disabled",esp_err_to_name(sp_err));

    hal_battery_init(); hal_battery_update();
    hal_nfc_init();
    if (s_nfc_armed && !hal_nfc_ensure_init()) {
        ESP_LOGW(TAG, "NFC init will retry in background");
    }
    wallet_load_public();
    if(wallet_is_onboarded()) hal_nfc_set_wallet_pubkey(wallet_pubkey());
    else hal_nfc_set_wallet_pubkey(NULL);
    hal_nfc_set_service_enabled(s_nfc_armed);
    load_receipts();

    if(!wallet_is_onboarded()){ s_screen=SCR_ONBOARD; s_ob=OB_WELCOME; }
    else { roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,76); s_screen=SCR_LOCK; }
    s_btn_q=xQueueCreate(16,sizeof(btn_event_t));
    hal_buttons_init(on_button);

    ESP_LOGI(TAG,"Boot complete");
    uint32_t last=xTaskGetTickCount()*portTICK_PERIOD_MS;
    uint32_t render_accum=0;

    while(1){
        uint32_t now=xTaskGetTickCount()*portTICK_PERIOD_MS;
        uint32_t dt=now-last; last=now;
        bool force_render=false;
        render_accum+=dt;

        // Timers
        s_anim+=dt;
        if(s_ui_transition_ms>dt){s_ui_transition_ms-=dt; force_render=true;} else s_ui_transition_ms=0;
        if(s_nfc_widget_ms>dt) s_nfc_widget_ms-=dt; else s_nfc_widget_ms=0;
        if(s_sync_widget_ms>dt) s_sync_widget_ms-=dt; else s_sync_widget_ms=0;
        if(g_nfc_sync.counter!=s_sync_seen_counter){
            s_sync_seen_counter=g_nfc_sync.counter;
            s_sync_widget_ms=2400;
            force_render=true;
        }
        tama_anim+=dt; if(tama_anim>=500){tama_anim=0;tama_frame^=1;}
        if(tama_msg_on){tama_msg_t+=dt; if(tama_msg_t>=1500)tama_msg_on=false;}

        // Clock
        static uint32_t clk=0; clk+=dt;
        if(clk>=1000){clk-=1000; force_render=true; if(++s_s>=60){s_s=0;if(++s_m>=60){s_m=0;if(++s_h>=24)s_h=0;}}}

        // Battery
        static uint32_t bat=0; bat+=dt;
        if(bat>=30000){bat=0;hal_battery_update();force_render=true;}

        // NFC is armed by default; keep trying if PN532 was not ready at boot.
        static uint32_t nfc_retry=0; nfc_retry+=dt;
        if(s_nfc_armed&&!hal_nfc_is_ready()&&nfc_retry>=1000){
            nfc_retry=0;
            hal_nfc_ensure_init();
        }

        // TX timer
        if(s_tx_overlay&&s_tx_state==TX_TIMER){
            s_tx_timer+=dt; if(s_tx_timer>=TX_CONFIRM_TIMER_MS){s_tx_state=TX_CONFIRM2;force_render=true;}}

        // New TX from NFC
        if(g_nfc_tx.valid&&!s_tx_overlay&&s_screen!=SCR_ONBOARD&&s_screen!=SCR_LOCK){
            s_tx_overlay=true; s_tx_state=TX_SHOW; s_tx_timer=0; force_render=true;}

        // Game updates
        if(s_screen==SCR_TETRIS&&!tet.over){
            tet.fall_ms+=dt;
            if(tet.fall_ms>=tet.fall_rate){tet.fall_ms=0;
                if(tet_fits(tet.px,tet.py+1,tet.rot))tet.py++; else tet_place();}}
        if(s_screen==SCR_PING_PONG) pp_update(dt);

        if(s_nfc_armed&&hal_nfc_is_ready()){
            if(s_pending_sig_valid&&hal_nfc_set_sign_response_target(s_pending_sig,s_pending_sig_nonce)){
                memset(s_pending_sig,0,sizeof(s_pending_sig));
                memset(s_pending_sig_nonce,0,sizeof(s_pending_sig_nonce));
                s_pending_sig_valid=false;
            }
        }

        // Buttons
        btn_event_t ev;
        while(xQueueReceive(s_btn_q,&ev,0)==pdTRUE) { handle_button(ev); force_render=true; }

        static bool ui_snap_ready=false;
        static screen_id_t last_screen;
        static home_slide_t last_slide;
        static ob_step_t last_ob;
        static int8_t last_grid,last_settings,last_games;
        static uint8_t last_watchface;
        static bool last_tx_overlay;
        if(!ui_snap_ready){
            ui_snap_ready=true;
        } else if(s_screen!=last_screen || s_slide!=last_slide || s_ob!=last_ob ||
                  s_grid_sel!=last_grid || s_settings_sel!=last_settings ||
                  s_games_sel!=last_games || s_watchface!=last_watchface ||
                  s_tx_overlay!=last_tx_overlay){
            int8_t dir=1;
            if(s_screen<last_screen || s_slide<last_slide ||
               s_grid_sel<last_grid || s_settings_sel<last_settings ||
               s_games_sel<last_games || s_watchface<last_watchface) dir=-1;
            ui_transition_mark(dir);
            force_render=true;
        }
        last_screen=s_screen;
        last_slide=s_slide;
        last_ob=s_ob;
        last_grid=s_grid_sel;
        last_settings=s_settings_sel;
        last_games=s_games_sel;
        last_watchface=s_watchface;
        last_tx_overlay=s_tx_overlay;

        bool game_fast=(s_screen==SCR_PING_PONG);
        bool animated_watchface=(s_screen==SCR_HOME && s_slide==SLIDE_WATCHFACE &&
                                 (s_watchface==0 || s_watchface==1));
        bool active_render=game_fast
            || animated_watchface
            || s_ui_transition_ms>0
            || s_screen==SCR_TETRIS
            || s_screen==SCR_TAMAGOTCHI
            || s_tx_overlay
            || s_nfc_widget_ms>0
            || s_sync_widget_ms>0
            || (s_screen==SCR_HOME && s_slide==SLIDE_GRID);
        uint32_t render_period=game_fast ? FRAME_MS : (animated_watchface ? 50 : (active_render ? 50 : 250));
        if(!force_render && render_accum<render_period){
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        render_accum=0;

        // Render
        switch(s_screen){
            case SCR_HOME:         render_home();         break;
            case SCR_WALLET:       render_wallet_app();   break;
            case SCR_RECEIVE:      render_receive_app();  break;
            case SCR_ONBOARD:      render_onboard();      break;
            case SCR_LOCK:         render_lock();         break;
            case SCR_SETTINGS:     render_settings();     break;
            case SCR_TRANSACTIONS: render_transactions(); break;
            case SCR_STATS:        render_stats();        break;
            case SCR_GAMES_MENU:   render_games_menu();   break;
            case SCR_PING_PONG:    render_pong();         break;
            case SCR_TETRIS:       render_tetris();       break;
            case SCR_TAMAGOTCHI:   render_tamagotchi();   break;
        }
        render_system_transition();
        if(s_tx_overlay) render_tx_overlay();
        if(s_nfc_widget_ms>0) ui_nfc_widget(s_nfc_widget_armed);
        render_sync_effect();

        st7789_flush();

        uint32_t el=xTaskGetTickCount()*portTICK_PERIOD_MS-now;
        if(el<FRAME_MS) vTaskDelay(pdMS_TO_TICKS(FRAME_MS-el));
    }
}
