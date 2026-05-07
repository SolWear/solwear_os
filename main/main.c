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
static int8_t       s_grid_sel = 0;   // 0..3 in 2x2 grid
static uint8_t      s_watchface = 0;  // 0=digital 1=analog 2=minimal

static bool     s_nfc_armed        = true;
static uint32_t s_nfc_widget_ms    = 0;
static bool     s_nfc_widget_armed = true;

// TX overlay
static bool     s_tx_overlay  = false;
typedef enum { TX_SHOW, TX_TIMER, TX_CONFIRM2 } tx_state_t;
static tx_state_t s_tx_state  = TX_SHOW;
static uint32_t   s_tx_timer  = 0;
static uint8_t    s_pending_sig[64];
static bool       s_pending_sig_valid = false;

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
    "Watchface: Digital","Watchface: Analog","Watchface: Minimal",
    "Change Password","About"
};
#define SETTINGS_COUNT 5

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
static const uint16_t kTC[7]={0x07FF,0xFFE0,0xF81F,0x07E0,0xF800,0x001F,0xFD20};

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
    ui_rounded_rect(x,y,w,h,8,sel?COLOR_SOL_PUR:COLOR_DARKBG);
    if(sel) ui_select_box(x,y,w,h,COLOR_SOL_GRN);
    int tw=ui_str_width(lbl,2); ui_str(x+(w-tw)/2,y+h/2-8,lbl,COLOR_WHITE,2);
}

// ============================================================
// Render functions
// ============================================================
static uint32_t s_anim=0;

static void render_watchface(void)
{
    ui_gradient_h(0,STATUS_BAR_H,LCD_W,LCD_H-STATUS_BAR_H-10,
                  COLOR_DARKBG,RGB565(0x06,0x06,0x28));
    char t[6]; snprintf(t,sizeof(t),"%02d:%02d",s_h,s_m);

    if(s_watchface==0){
        int tw=ui_str_width(t,4); ui_str(LCD_W/2-tw/2,88,t,COLOR_WHITE,4);
        ui_str_center(150,"2026 May 05",COLOR_GRAY,1);
    } else if(s_watchface==1){
        int cx=LCD_W/2, cy=STATUS_BAR_H+90, r=70;
        st7789_fb_circle(cx,cy,r,COLOR_LTGRAY);
        for(int i=0;i<12;i++){
            float a=i*3.14159f*30.f/180.f;
            int x1=cx+(int)((r-8)*sinf(a)), y1=cy-(int)((r-8)*cosf(a));
            int x2=cx+(int)(r*sinf(a)),     y2=cy-(int)(r*cosf(a));
            if(x1>x2){int tmp=x1;x1=x2;x2=tmp;}
            if(y1>y2){int tmp=y1;y1=y2;y2=tmp;}
            st7789_fb_hline(x1,y1,x2-x1+1,COLOR_LTGRAY);
        }
        float ha=((s_h%12)+s_m/60.f)*30.f*3.14159f/180.f;
        float ma=s_m*6.f*3.14159f/180.f;
        for(int i=0;i<3;i++){
            st7789_fb_hline(cx-(i==1?1:0),cy,(int)(42*sinf(ha)),COLOR_WHITE);
            st7789_fb_vline(cx+(int)(42*sinf(ha)),cy-(int)(42*cosf(ha)),(int)(42*fabsf(cosf(ha))),COLOR_WHITE);
            st7789_fb_hline(cx-(i==1?1:0),cy,(int)(60*sinf(ma)),COLOR_SOL_GRN);
            st7789_fb_vline(cx+(int)(60*sinf(ma)),cy-(int)(60*cosf(ma)),(int)(60*fabsf(cosf(ma))),COLOR_SOL_GRN);
        }
        st7789_fb_circle_fill(cx,cy,4,COLOR_SOL_PUR);
    } else {
        ui_str_center(80,"SolWear",COLOR_SOL_PUR,3);
        int tw=ui_str_width(t,3); ui_str(LCD_W/2-tw/2,112,t,COLOR_SOL_GRN,3);
        const char*nm=wallet_name(); if(nm&&nm[0]) ui_str_center(158,nm,COLOR_GRAY,1);
    }
    ui_nfc_icon(LCD_W-18,STATUS_BAR_H+4,s_nfc_armed);
}

static void render_home_grid(void)
{
    st7789_fb_rect(0,STATUS_BAR_H,LCD_W,LCD_H-STATUS_BAR_H,COLOR_BLACK);
    const int IW=108,IH=90,GAP=8;
    int x0=(LCD_W-2*IW-GAP)/2, y0=STATUS_BAR_H+12;
    const char *lbl[4]={"Settings","Txns","Stats","Games"};
    for(int i=0;i<4;i++){
        int col=i%2,row=i/2;
        draw_icon_card(x0+col*(IW+GAP),y0+row*(IH+GAP),IW,IH,lbl[i],s_grid_sel==i);
    }
}

static void render_home(void)
{
    st7789_fb_fill(COLOR_BLACK);
    draw_status(NULL);
    if(s_slide==SLIDE_WATCHFACE) render_watchface();
    else                          render_home_grid();
    // Dots
    for(int i=0;i<SLIDE_COUNT;i++){
        int dx=LCD_W/2-(SLIDE_COUNT-1)*8+i*16;
        if(i==s_slide) st7789_fb_circle_fill(dx,LCD_H-7,3,COLOR_SOL_PUR);
        else            st7789_fb_circle(dx,LCD_H-7,3,COLOR_GRAY);
    }
}

static void render_ob_choice(const char *title, const char *a, const char *b)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    ui_str_center(30,title,COLOR_WHITE,2);
    for(int i=0;i<2;i++){
        int y=80+i*80;
        ui_rounded_rect(20,y,200,60,8,(s_ob_sel==i)?COLOR_SOL_PUR:COLOR_DARKBG);
        const char*l=(i==0)?a:b; int tw=ui_str_width(l,2);
        ui_str(120-tw/2,y+22,l,COLOR_WHITE,2);
    }
    ui_str_center(220,"K1/K2=choose  K3=confirm",COLOR_GRAY,1);
}

static void render_onboard(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    switch(s_ob){
        case OB_WELCOME:
            ui_str_center(65,"SolWearOS",COLOR_SOL_PUR,3);
            ui_str_center(115,"Solana Hardware Wallet",COLOR_GRAY,1);
            ui_str_center(165,"Press K3 to begin",COLOR_SOL_GRN,2);
            break;
        case OB_CHOICE:
            render_ob_choice("Setup Wallet","Generate Key","Import Key");
            break;
        case OB_IMPORT_HOW:
            render_ob_choice("Import How?","Via NFC","Manual Entry");
            break;
        case OB_IMPORT_NFC:{
            ui_str_center(30,"Import via NFC",COLOR_WHITE,2);
            uint16_t pc=(uint16_t)(80+60.f*sinf(s_anim*0.004f));
            uint16_t nc=((pc>>3)<<11)|((pc>>2)<<5)|(pc>>3);
            st7789_fb_circle(120,130,24,nc); st7789_fb_circle(120,130,16,nc);
            st7789_fb_circle(120,130,8,nc);  st7789_fb_circle_fill(120,130,3,nc);
            ui_str_center(170,"Tap phone to watch",COLOR_GRAY,1);
            ui_str_center(190,"K4 = back",COLOR_GRAY,1);
            break;
        }
        case OB_IMPORT_HEX:
            ui_str_center(30,"Private Key (hex)",COLOR_WHITE,2);
            ui_str_center(48,"64 chars, A-F 0-9",COLOR_GRAY,1);
            roulette_render(&s_roulette);
            break;
        case OB_GEN_CONFIRM:
            ui_str_center(40,"Key Ready!",COLOR_SOL_GRN,2);
            ui_rounded_rect(16,70,208,28,6,COLOR_DARKBG);
            ui_str_center(79,s_ob_pub4,COLOR_SOL_PUR,2);
            ui_str_center(110,"...first 4 bytes shown",COLOR_GRAY,1);
            ui_str_center(155,"K3 = set password",COLOR_WHITE,2);
            ui_str_center(178,"K4 = restart",COLOR_GRAY,1);
            break;
        case OB_SET_PASS:
            ui_str_center(30,"Set Password",COLOR_WHITE,2);
            ui_str_center(48,"4-8 chars (1-8 A-Z)",COLOR_GRAY,1);
            roulette_render(&s_roulette);
            break;
        case OB_PASS_WARN:
            ui_rounded_rect(14,40,212,100,8,COLOR_DARKBG);
            ui_rounded_rect_outline(14,40,212,100,8,COLOR_ORANGE);
            ui_str_center(58,"Weak Password!",COLOR_ORANGE,2);
            ui_str_center(82,"Less than 8 chars.",COLOR_WHITE,1);
            ui_str_center(102,"K3 = continue anyway",COLOR_GRAY,1);
            ui_str_center(118,"K4 = set stronger",COLOR_GRAY,1);
            break;
        case OB_SET_NAME:
            ui_str_center(30,"Wallet Name",COLOR_WHITE,2);
            roulette_render(&s_roulette);
            break;
        case OB_DONE:
            ui_str_center(80,"Welcome!",COLOR_SOL_GRN,3);
            ui_str_center(130,s_ob_name,COLOR_WHITE,2);
            ui_str_center(170,"K3 to enter OS",COLOR_SOL_GRN,2);
            break;
    }
}

static void render_lock(void)
{
    st7789_fb_fill(COLOR_BLACK);
    ui_str_center(20,"SolWearOS",COLOR_SOL_PUR,2);
    ui_str_center(42,"Enter Password",COLOR_GRAY,1);
    roulette_render(&s_roulette);
    if(s_lock_err[0]) ui_str_center(LCD_H-16,s_lock_err,COLOR_RED,1);
}

static void render_settings(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Settings");
    for(int i=0;i<SETTINGS_COUNT;i++){
        int y=STATUS_BAR_H+6+i*28;
        if(y+24>LCD_H) break;
        ui_rounded_rect(6,y,LCD_W-12,24,4,(s_settings_sel==i)?COLOR_SOL_PUR:COLOR_DARKBG);
        ui_str(12,y+6,s_settings_items[i],COLOR_WHITE,1);
    }
    ui_str_center(LCD_H-12,"K1/K2=nav  K3=select  K4=back",COLOR_GRAY,1);
}

static void render_transactions(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Transactions");
    char bal[32]; snprintf(bal,sizeof(bal),"Bal: %.4f SOL",(double)s_balance);
    ui_rounded_rect(4,STATUS_BAR_H+2,LCD_W-8,22,4,COLOR_DARKBG);
    ui_str_center(STATUS_BAR_H+8,bal,(s_balance>=0)?COLOR_SOL_GRN:COLOR_RED,1);
    if(s_rx_n==0){
        ui_str_center(130,"No transactions",COLOR_GRAY,2);
        ui_str_center(158,"NFC to sign TX",COLOR_GRAY,1);
        return;
    }
    for(int i=0;i<5&&(s_rx_scroll+i)<s_rx_n;i++){
        int y=STATUS_BAR_H+28+i*30;
        ui_rounded_rect(4,y,LCD_W-8,26,4,COLOR_DARKBG);
        char tmp[40]; strncpy(tmp,s_rx[s_rx_scroll+i],39); tmp[39]='\0';
        ui_str(8,y+7,tmp,COLOR_WHITE,1);
    }
    char pg[12]; snprintf(pg,sizeof(pg),"%d/%d",s_rx_scroll+1,(int)s_rx_n);
    ui_str_center(LCD_H-12,pg,COLOR_GRAY,1);
}

static void render_stats(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Stats");
    char buf[48]; int y=STATUS_BAR_H+10;
    snprintf(buf,sizeof(buf),"Battery:  %d%%",hal_battery_percent());
    ui_str(8,y,buf,COLOR_SOL_GRN,2); y+=26;
    snprintf(buf,sizeof(buf),"Voltage:  %.2fV",hal_battery_mv()/1000.f);
    ui_str(8,y,buf,COLOR_WHITE,1); y+=18;
    snprintf(buf,sizeof(buf),"Charging: %s",hal_battery_charging()?"Yes":"No");
    ui_str(8,y,buf,COLOR_WHITE,1); y+=18;
    snprintf(buf,sizeof(buf),"Cell:     350mAh LW303040");
    ui_str(8,y,buf,COLOR_GRAY,1); y+=22;
    snprintf(buf,sizeof(buf),"NFC:      %s",s_nfc_armed?"Armed":"Disarmed");
    ui_str(8,y,buf,s_nfc_armed?COLOR_SOL_GRN:COLOR_RED,1); y+=18;
    snprintf(buf,sizeof(buf),"Wallet:   %s",wallet_name());
    ui_str(8,y,buf,COLOR_SOL_PUR,1); y+=18;
    snprintf(buf,sizeof(buf),"Heap:     %u B",(unsigned)esp_get_free_heap_size());
    ui_str(8,y,buf,COLOR_GRAY,1); y+=18;
    ui_str(8,y,"SolWearOS v1.0",COLOR_GRAY,1);
}

static int8_t s_games_sel=0;
static const char *s_game_names[]={"Ping Pong","Tetris","Tamagotchi"};

static void render_games_menu(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Games");
    for(int i=0;i<3;i++){
        int y=STATUS_BAR_H+20+i*64;
        ui_rounded_rect(20,y,200,54,8,(s_games_sel==i)?COLOR_SOL_PUR:COLOR_DARKBG);
        if(s_games_sel==i) ui_select_box(20,y,200,54,COLOR_SOL_GRN);
        int tw=ui_str_width(s_game_names[i],2);
        ui_str(120-tw/2,y+19,s_game_names[i],COLOR_WHITE,2);
    }
    ui_str_center(LCD_H-12,"K1/K2=select  K3=play  K4=back",COLOR_GRAY,1);
}

static void render_pong(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    if(pp.over){
        ui_str_center(90,(pp.sp>=5)?"YOU WIN!":"AI WINS",(pp.sp>=5)?COLOR_SOL_GRN:COLOR_RED,3);
        ui_str_center(142,"K3=again  K4=back",COLOR_GRAY,2);
        return;
    }
    for(int y=COURT_Y;y<LCD_H;y+=8) st7789_fb_pixel(LCD_W/2,y,COLOR_DARKBG);
    st7789_fb_rect(PAD_W+2,pp.py,PAD_W,PAD_H,COLOR_SOL_GRN);
    st7789_fb_rect(LCD_W-PAD_W*2-4,(int16_t)pp.ay,PAD_W,PAD_H,COLOR_RED);
    st7789_fb_circle_fill((int16_t)pp.bx,(int16_t)pp.by,BALL_R,COLOR_WHITE);
    char sc[16]; snprintf(sc,sizeof(sc),"%d  %d",pp.sp,pp.sa);
    ui_str_center(COURT_Y+4,sc,COLOR_LTGRAY,2);
    if(pp.paused) ui_str_center(LCD_H/2,"PAUSED",COLOR_ORANGE,2);
}

static void render_tetris(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status(NULL);
    if(tet.over){
        ui_str_center(80,"GAME OVER",COLOR_RED,3);
        char sc[20]; snprintf(sc,sizeof(sc),"Score: %lu",(unsigned long)tet.score);
        ui_str_center(122,sc,COLOR_WHITE,2);
        ui_str_center(155,"K3=again  K4=back",COLOR_GRAY,1);
        return;
    }
    st7789_fb_rect_outline(TBX-1,TBY-1,TC*TCW+2,TR*TCH+2,COLOR_DARKBG);
    for(int r=0;r<TR;r++) for(int c=0;c<TC;c++)
        if(tet.b[r][c]) st7789_fb_rect(TBX+c*TCW+1,TBY+r*TCH+1,TCW-2,TCH-2,kTC[tet.b[r][c]-1]);
    for(int c=0;c<4;c++){
        int8_t cx=tet.px+kP[tet.piece][tet.rot][c][0];
        int8_t cy=tet.py+kP[tet.piece][tet.rot][c][1];
        if(cy>=0) st7789_fb_rect(TBX+cx*TCW+1,TBY+cy*TCH+1,TCW-2,TCH-2,kTC[tet.piece]);
    }
    int sx=TBX+TC*TCW+6;
    char sc[12]; snprintf(sc,sizeof(sc),"%lu",(unsigned long)tet.score);
    ui_str(sx,TBY+8,"SC",COLOR_GRAY,1); ui_str(sx,TBY+18,sc,COLOR_SOL_GRN,1);
    char lv[4]; snprintf(lv,sizeof(lv),"%u",tet.level);
    ui_str(sx,TBY+38,"LV",COLOR_GRAY,1); ui_str(sx,TBY+48,lv,COLOR_CYAN,1);
    ui_str(sx,TBY+68,"K4",COLOR_GRAY,1); ui_str(sx,TBY+78,"drop",COLOR_GRAY,1);
}

static void draw_tama_pet(int cx,int cy)
{
    if(!tama.alive){
        ui_rounded_rect(cx-16,cy-28,32,44,6,COLOR_DARKBG);
        ui_str_center(cy-10,"RIP",COLOR_GRAY,2); return;
    }
    uint16_t bc=(tama.hunger<20||tama.happy<20||tama.energy<20)?COLOR_ORANGE:COLOR_SOL_PUR;
    st7789_fb_circle_fill(cx,cy,22,bc);
    if(tama_frame==0){
        st7789_fb_circle_fill(cx-8,cy-6,4,COLOR_BLACK);
        st7789_fb_circle_fill(cx+8,cy-6,4,COLOR_BLACK);
        st7789_fb_circle_fill(cx-7,cy-7,2,COLOR_WHITE);
        st7789_fb_circle_fill(cx+9,cy-7,2,COLOR_WHITE);
    } else {
        st7789_fb_hline(cx-12,cy-6,8,COLOR_BLACK);
        st7789_fb_hline(cx+4, cy-6,8,COLOR_BLACK);
    }
    if(tama.happy>=50){ for(int i=-5;i<=5;i++) st7789_fb_pixel(cx+i,cy+9+(i*i)/8,COLOR_BLACK); }
    else              { for(int i=-5;i<=5;i++) st7789_fb_pixel(cx+i,cy+12-(i*i)/8,COLOR_BLACK); }
    int fy=cy+20+(tama_frame?2:0);
    st7789_fb_circle_fill(cx-14,fy,6,bc); st7789_fb_circle_fill(cx+14,fy,6,bc);
}

static void draw_tama_bar(int x,int y,const char*lbl,uint8_t v){
    ui_str(x,y,lbl,COLOR_GRAY,1);
    st7789_fb_rect(x+20,y+1,60,6,COLOR_DARKBG);
    uint16_t c=(v>50)?COLOR_SOL_GRN:(v>25)?COLOR_ORANGE:COLOR_RED;
    st7789_fb_rect(x+20,y+1,60*v/100,6,c);
}

static void render_tamagotchi(void)
{
    st7789_fb_fill(COLOR_BLACK); draw_status("Tamagotchi");
    if(!tama.alive){
        ui_str_center(60,"Your pet died!",COLOR_RED,2);
        draw_tama_pet(LCD_W/2,130);
        ui_str_center(175,"K3=Resurrect",COLOR_GRAY,2);
        return;
    }
    draw_tama_pet(LCD_W/2,112);
    draw_tama_bar(10,168,"H:",tama.hunger);
    draw_tama_bar(10,182,"J:",tama.happy);
    draw_tama_bar(10,196,"E:",tama.energy);
    if(tama_msg_on) ui_str_center(152,tama_msg,COLOR_SOL_GRN,2);
    ui_str_center(LCD_H-12,"K1=Feed K2=Play K3=Nap",COLOR_GRAY,1);
}

static void render_tx_overlay(void)
{
    // dim bg
    for(int y=18;y<LCD_H-18;y+=2) for(int x=0;x<LCD_W;x+=2) st7789_fb_pixel(x,y,COLOR_BLACK);
    uint16_t border=(s_tx_state==TX_CONFIRM2)?COLOR_SOL_PUR:COLOR_ORANGE;
    ui_rounded_rect(8,20,LCD_W-16,LCD_H-40,10,COLOR_DARKBG);
    ui_rounded_rect_outline(8,20,LCD_W-16,LCD_H-40,10,border);

    if(s_tx_state==TX_SHOW){
        ui_str_center(30,"TRANSACTION",COLOR_ORANGE,2);
        char fr[14],to[14];
        snprintf(fr,sizeof(fr),"%.8s...",g_nfc_tx.from);
        snprintf(to,sizeof(to),"%.8s...",g_nfc_tx.to);
        float sol=(float)(g_nfc_tx.lamports/1000000000ULL)+(float)(g_nfc_tx.lamports%1000000000ULL)/1e9f;
        char amt[20]; snprintf(amt,sizeof(amt),"%.4f SOL",(double)sol);
        ui_str(16,58,"From:",COLOR_GRAY,1); ui_str(56,58,fr,COLOR_WHITE,1);
        ui_str(16,74,"To:",  COLOR_GRAY,1); ui_str(56,74,to,COLOR_WHITE,1);
        int tw=ui_str_width(amt,2); ui_str(LCD_W/2-tw/2,96,amt,COLOR_WHITE,2);
        ui_rounded_rect(14,138,96,34,6,COLOR_SOL_GRN);
        int t1w=ui_str_width("K3 Confirm",1);
        ui_str(14+(96-t1w)/2,149,"K3 Confirm",COLOR_BLACK,1);
        ui_rounded_rect(118,138,96,34,6,COLOR_RED);
        int t2w=ui_str_width("K4 Reject",1);
        ui_str(118+(96-t2w)/2,149,"K4 Reject",COLOR_WHITE,1);
    } else if(s_tx_state==TX_TIMER){
        ui_str_center(38,"Are you sure?",COLOR_ORANGE,2);
        uint32_t rem=(TX_CONFIRM_TIMER_MS-s_tx_timer+999)/1000;
        char sc[8]; snprintf(sc,sizeof(sc),"%u",(unsigned)rem);
        int tw=ui_str_width(sc,4); ui_str(LCD_W/2-tw/2,82,sc,COLOR_WHITE,4);
        ui_str_center(140,"seconds",COLOR_GRAY,1);
        int bw2=(int)((LCD_W-32)*(uint64_t)s_tx_timer/TX_CONFIRM_TIMER_MS);
        st7789_fb_rect(16,152,LCD_W-32,8,COLOR_DARKBG);
        st7789_fb_rect(16,152,bw2,8,COLOR_ORANGE);
        ui_str_center(170,"K4 to cancel",COLOR_GRAY,1);
    } else {
        ui_str_center(50,"SIGN?",COLOR_SOL_PUR,3);
        ui_str_center(96,"Press K3 to sign",COLOR_WHITE,2);
        ui_rounded_rect(20,128,88,34,6,COLOR_SOL_PUR);
        int t1w=ui_str_width("K3 Sign",1); ui_str(20+(88-t1w)/2,139,"K3 Sign",COLOR_WHITE,1);
        ui_rounded_rect(116,128,88,34,6,COLOR_DARKBG);
        int t2w=ui_str_width("K4 Cancel",1); ui_str(116+(88-t2w)/2,139,"K4 Cancel",COLOR_GRAY,1);
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
                if(s_ob_sel==0) ob_go(OB_IMPORT_NFC);
                else { roulette_init(&s_roulette,ROULETTE_MODE_HEX,64,64,false,70); ob_go(OB_IMPORT_HEX); }
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
                memset(s_ob_seed,0,32); memset(s_ob_pass,0,sizeof(s_ob_pass));
                ob_go(OB_DONE);
            } else if(r==ROULETTE_CANCELLED) ob_go(OB_PASS_WARN);
            break;
        }
        case OB_DONE:
            if(ev==BTN_K3_PRESS){
                roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,60);
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
                if(!hal_nfc_write_sign_response(sig,g_nfc_tx.nonce)){
                    memcpy(s_pending_sig,sig,sizeof(s_pending_sig));
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
    // Global double/triple
    if(ev==BTN_K4_DOUBLE){ s_tx_overlay=false; g_nfc_tx.valid=false; s_screen=SCR_HOME; s_slide=SLIDE_WATCHFACE; return; }
    if(ev==BTN_K4_TRIPLE){ wallet_lock(); s_tx_overlay=false; roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,60); s_lock_err[0]='\0'; s_screen=SCR_LOCK; return; }
    if(ev==BTN_K1_DOUBLE){ s_nfc_armed=!s_nfc_armed; if(s_nfc_armed)hal_nfc_ensure_init(); else hal_nfc_shutdown(); s_nfc_widget_ms=2000; s_nfc_widget_armed=s_nfc_armed; return; }

    if(s_tx_overlay){ handle_tx(ev); return; }

    switch(s_screen){
        case SCR_ONBOARD: handle_ob(ev); break;
        case SCR_LOCK:    handle_lock(ev); break;
        case SCR_HOME:
            if(ev==BTN_K2_PRESS){ if(s_slide==SLIDE_WATCHFACE){s_slide=SLIDE_GRID;s_grid_sel=0;} else if(s_grid_sel<3) s_grid_sel++; }
            if(ev==BTN_K1_PRESS){ if(s_slide==SLIDE_WATCHFACE) s_watchface=(s_watchface+2)%3; else if(s_grid_sel>0) s_grid_sel--; else s_slide=SLIDE_WATCHFACE; }
            if(ev==BTN_K3_PRESS){
                if(s_slide==SLIDE_WATCHFACE) s_watchface=(s_watchface+1)%3;
                else {
                    const screen_id_t t[]={SCR_SETTINGS,SCR_TRANSACTIONS,SCR_STATS,SCR_GAMES_MENU};
                    s_screen=t[s_grid_sel];
                    if(s_screen==SCR_TRANSACTIONS) load_receipts();
                    if(s_screen==SCR_GAMES_MENU) s_games_sel=0;
                }
            }
            if(ev==BTN_K4_PRESS && s_slide==SLIDE_GRID) s_slide=SLIDE_WATCHFACE;
            break;
        case SCR_SETTINGS:
            if(ev==BTN_K1_PRESS&&s_settings_sel>0) s_settings_sel--;
            if(ev==BTN_K2_PRESS&&s_settings_sel<SETTINGS_COUNT-1) s_settings_sel++;
            if(ev==BTN_K3_PRESS&&s_settings_sel<3) s_watchface=(uint8_t)s_settings_sel;
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
    ESP_LOGI(TAG,"=== SolWearOS v1.0 ===");

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
    wallet_load_public();

    if(!wallet_is_onboarded()){ s_screen=SCR_ONBOARD; s_ob=OB_WELCOME; }
    else { roulette_init(&s_roulette,ROULETTE_MODE_ALPHA,8,1,true,60); s_screen=SCR_LOCK; }
    s_btn_q=xQueueCreate(16,sizeof(btn_event_t));
    hal_buttons_init(on_button);

    ESP_LOGI(TAG,"Boot complete");
    uint32_t last=xTaskGetTickCount()*portTICK_PERIOD_MS;

    while(1){
        uint32_t now=xTaskGetTickCount()*portTICK_PERIOD_MS;
        uint32_t dt=now-last; last=now;

        // Timers
        s_anim+=dt;
        if(s_nfc_widget_ms>dt) s_nfc_widget_ms-=dt; else s_nfc_widget_ms=0;
        tama_anim+=dt; if(tama_anim>=500){tama_anim=0;tama_frame^=1;}
        if(tama_msg_on){tama_msg_t+=dt; if(tama_msg_t>=1500)tama_msg_on=false;}

        // Clock
        static uint32_t clk=0; clk+=dt;
        if(clk>=1000){clk-=1000; if(++s_s>=60){s_s=0;if(++s_m>=60){s_m=0;if(++s_h>=24)s_h=0;}}}

        // Battery
        static uint32_t bat=0; bat+=dt;
        if(bat>=30000){bat=0;hal_battery_update();}

        // TX timer
        if(s_tx_overlay&&s_tx_state==TX_TIMER){
            s_tx_timer+=dt; if(s_tx_timer>=TX_CONFIRM_TIMER_MS) s_tx_state=TX_CONFIRM2;}

        // New TX from NFC
        if(g_nfc_tx.valid&&!s_tx_overlay&&s_screen!=SCR_ONBOARD&&s_screen!=SCR_LOCK){
            s_tx_overlay=true; s_tx_state=TX_SHOW; s_tx_timer=0;}

        // Game updates
        if(s_screen==SCR_TETRIS&&!tet.over){
            tet.fall_ms+=dt;
            if(tet.fall_ms>=tet.fall_rate){tet.fall_ms=0;
                if(tet_fits(tet.px,tet.py+1,tet.rot))tet.py++; else tet_place();}}
        if(s_screen==SCR_PING_PONG) pp_update(dt);

        // NFC poll: on tag tap, try to process NDEF (sign request) OR
        // write wallet NDEF so Android can read pubkey
        if(s_nfc_armed&&hal_nfc_is_ready()){
            nfc_tag_t tag;
            if(hal_nfc_wait_tag(50,&tag)){
                if(s_pending_sig_valid&&hal_nfc_write_sign_response(s_pending_sig,NULL)){
                    memset(s_pending_sig,0,sizeof(s_pending_sig));
                    s_pending_sig_valid=false;
                    continue;
                }
                if(!hal_nfc_process_ndef()){
                    // No sign_request on tag — write our wallet pubkey for Android to read
                    hal_nfc_write_wallet_ndef(wallet_pubkey());
                }
            }
        }

        // Buttons
        btn_event_t ev;
        while(xQueueReceive(s_btn_q,&ev,0)==pdTRUE) handle_button(ev);

        // Render
        switch(s_screen){
            case SCR_HOME:         render_home();         break;
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
        if(s_tx_overlay) render_tx_overlay();
        if(s_nfc_widget_ms>0) ui_nfc_widget(s_nfc_widget_armed);

        st7789_flush();

        uint32_t el=xTaskGetTickCount()*portTICK_PERIOD_MS-now;
        if(el<FRAME_MS) vTaskDelay(pdMS_TO_TICKS(FRAME_MS-el));
    }
}
