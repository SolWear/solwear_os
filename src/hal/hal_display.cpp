#include "hal_display.h"
#include <SPI.h>

HalDisplay display;

struct ProbePins {
    uint8_t cs;
    uint8_t dc;
    uint8_t rst;
};

static ProbePins g_probePins = {PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST};

#if defined(ARDUINO_ARCH_RP2040)
#define PANEL_SPI SPI1
#else
#define PANEL_SPI SPI
#endif

static inline void panelSelect() {
    digitalWrite(g_probePins.cs, LOW);
}

static inline void panelDeselect() {
    digitalWrite(g_probePins.cs, HIGH);
}

static inline void panelWriteCommand(uint8_t cmd) {
    digitalWrite(g_probePins.dc, LOW);
    panelSelect();
    PANEL_SPI.transfer(cmd);
    panelDeselect();
}

static inline void panelWriteDataByte(uint8_t data) {
    digitalWrite(g_probePins.dc, HIGH);
    panelSelect();
    PANEL_SPI.transfer(data);
    panelDeselect();
}

static void panelSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Waveshare 1.69" vertical mode uses +20 row offset.
    const uint16_t yOff = 20;
    const uint16_t ys = y0 + yOff;
    const uint16_t ye = y1 + yOff;

    panelWriteCommand(0x2A);
    panelWriteDataByte(x0 >> 8);
    panelWriteDataByte(x0 & 0xFF);
    panelWriteDataByte(x1 >> 8);
    panelWriteDataByte(x1 & 0xFF);

    panelWriteCommand(0x2B);
    panelWriteDataByte(ys >> 8);
    panelWriteDataByte(ys & 0xFF);
    panelWriteDataByte(ye >> 8);
    panelWriteDataByte(ye & 0xFF);

    panelWriteCommand(0x2C);
}

static void panelFillColor(uint16_t color565) {
    panelSetWindow(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    const uint8_t hi = (uint8_t)(color565 >> 8);
    const uint8_t lo = (uint8_t)(color565 & 0xFF);
    digitalWrite(g_probePins.dc, HIGH);
    panelSelect();
    for (uint32_t i = 0; i < (uint32_t)SCREEN_WIDTH * (uint32_t)SCREEN_HEIGHT; ++i) {
        PANEL_SPI.transfer(hi);
        PANEL_SPI.transfer(lo);
    }
    panelDeselect();
}

static void panelRawResetAndInit() {
    digitalWrite(g_probePins.rst, HIGH);
    delay(100);
    digitalWrite(g_probePins.rst, LOW);
    delay(100);
    digitalWrite(g_probePins.rst, HIGH);
    delay(100);

    panelWriteCommand(0x36); panelWriteDataByte(0x00);
    panelWriteCommand(0x3A); panelWriteDataByte(0x05);

    panelWriteCommand(0xB2);
    panelWriteDataByte(0x0B); panelWriteDataByte(0x0B); panelWriteDataByte(0x00);
    panelWriteDataByte(0x33); panelWriteDataByte(0x35);

    panelWriteCommand(0xB7); panelWriteDataByte(0x11);
    panelWriteCommand(0xBB); panelWriteDataByte(0x35);
    panelWriteCommand(0xC0); panelWriteDataByte(0x2C);
    panelWriteCommand(0xC2); panelWriteDataByte(0x01);
    panelWriteCommand(0xC3); panelWriteDataByte(0x0D);
    panelWriteCommand(0xC4); panelWriteDataByte(0x20);
    panelWriteCommand(0xC6); panelWriteDataByte(0x13);

    panelWriteCommand(0xD0); panelWriteDataByte(0xA4); panelWriteDataByte(0xA1);
    panelWriteCommand(0xD6); panelWriteDataByte(0xA1);

    const uint8_t e0[] = {0xF0, 0x06, 0x0B, 0x0A, 0x09, 0x26, 0x29, 0x33, 0x41, 0x18, 0x16, 0x15, 0x29, 0x2D};
    const uint8_t e1[] = {0xF0, 0x04, 0x08, 0x08, 0x07, 0x03, 0x28, 0x32, 0x40, 0x3B, 0x19, 0x18, 0x2A, 0x2E};
    const uint8_t e4[] = {0x25, 0x00, 0x00};
    panelWriteCommand(0xE0);
    for (uint8_t b : e0) panelWriteDataByte(b);
    panelWriteCommand(0xE1);
    for (uint8_t b : e1) panelWriteDataByte(b);
    panelWriteCommand(0xE4);
    for (uint8_t b : e4) panelWriteDataByte(b);

    panelWriteCommand(0x21);
    panelWriteCommand(0x11);
    delay(120);
    panelWriteCommand(0x29);
    delay(20);
}

static void configureProbePins(const ProbePins& p) {
    g_probePins = p;
    pinMode(g_probePins.cs, OUTPUT);
    pinMode(g_probePins.dc, OUTPUT);
    pinMode(g_probePins.rst, OUTPUT);
    panelDeselect();
}

static inline void sendCmdData(TFT_eSPI& tft, uint8_t cmd, const uint8_t* data, uint8_t len) {
    tft.writecommand(cmd);
    for (uint8_t i = 0; i < len; ++i) {
        tft.writedata(data[i]);
    }
}

static void applyWaveshareInitSequence(TFT_eSPI& tft) {
    // Waveshare RP2040-Touch-LCD-1.69 official ST7789V2 init sequence.
    // Matching this sequence avoids panel variants that light BL but show no pixels.
    const uint8_t c36[] = {0x00};
    const uint8_t c3a[] = {0x05};
    const uint8_t cb2[] = {0x0B, 0x0B, 0x00, 0x33, 0x35};
    const uint8_t cb7[] = {0x11};
    const uint8_t cbb[] = {0x35};
    const uint8_t cc0[] = {0x2C};
    const uint8_t cc2[] = {0x01};
    const uint8_t cc3[] = {0x0D};
    const uint8_t cc4[] = {0x20};
    const uint8_t cc6[] = {0x13};
    const uint8_t cd0[] = {0xA4, 0xA1};
    const uint8_t cd6[] = {0xA1};
    const uint8_t ce0[] = {0xF0, 0x06, 0x0B, 0x0A, 0x09, 0x26, 0x29, 0x33, 0x41, 0x18, 0x16, 0x15, 0x29, 0x2D};
    const uint8_t ce1[] = {0xF0, 0x04, 0x08, 0x08, 0x07, 0x03, 0x28, 0x32, 0x40, 0x3B, 0x19, 0x18, 0x2A, 0x2E};
    const uint8_t ce4[] = {0x25, 0x00, 0x00};

    sendCmdData(tft, 0x36, c36, sizeof(c36));
    sendCmdData(tft, 0x3A, c3a, sizeof(c3a));
    sendCmdData(tft, 0xB2, cb2, sizeof(cb2));
    sendCmdData(tft, 0xB7, cb7, sizeof(cb7));
    sendCmdData(tft, 0xBB, cbb, sizeof(cbb));
    sendCmdData(tft, 0xC0, cc0, sizeof(cc0));
    sendCmdData(tft, 0xC2, cc2, sizeof(cc2));
    sendCmdData(tft, 0xC3, cc3, sizeof(cc3));
    sendCmdData(tft, 0xC4, cc4, sizeof(cc4));
    sendCmdData(tft, 0xC6, cc6, sizeof(cc6));
    sendCmdData(tft, 0xD0, cd0, sizeof(cd0));
    sendCmdData(tft, 0xD6, cd6, sizeof(cd6));
    sendCmdData(tft, 0xE0, ce0, sizeof(ce0));
    sendCmdData(tft, 0xE1, ce1, sizeof(ce1));
    sendCmdData(tft, 0xE4, ce4, sizeof(ce4));

    tft.writecommand(0x21); // display inversion ON
    tft.writecommand(0x11); // sleep out
    delay(120);
    tft.writecommand(0x29); // display on
}

void HalDisplay::init() {
    // Backlight on FIRST so we can see whether the panel responds at all.
    pinMode(PIN_LCD_BL, OUTPUT);
    setBrightness(100);  // Full brightness during diagnostic phase

    // Explicit hardware reset pulse — don't rely on TFT_eSPI's implicit one.
    configureProbePins({PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST});
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(5);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(50);

    // Force SPI1 pin routing before TFT_eSPI init for RP2040 variants where
    // implicit routing is unreliable.
#if defined(ARDUINO_ARCH_RP2040)
    SPI1.setRX(PIN_LCD_MISO);
    SPI1.setTX(PIN_LCD_MOSI);
    SPI1.setSCK(PIN_LCD_CLK);
    SPI1.setCS(PIN_LCD_CS);
    SPI1.begin();
#else
    SPI.begin(PIN_LCD_CLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
#endif

    Serial.printf("[HAL] Display pins cs=%d dc=%d rst=%d bl=%d sck=%d mosi=%d miso=%d\n",
                  PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL,
                  PIN_LCD_CLK, PIN_LCD_MOSI, PIN_LCD_MISO);

    runPanelProbe();

    tft_.init();
    applyWaveshareInitSequence(tft_);
    tft_.setRotation(0);
    tft_.fillScreen(TFT_BLACK);

    // Diagnostic test pattern — proves SPI + driver + offsets are working
    // independently of the rest of the OS. If you see red/green/white,
    // the panel works and any later black-screen bug is in our code.
    tft_.fillRect(0, 0,   SCREEN_WIDTH, SCREEN_HEIGHT / 2, TFT_RED);
    tft_.fillRect(0, SCREEN_HEIGHT / 2, SCREEN_WIDTH, SCREEN_HEIGHT / 2, TFT_GREEN);
    tft_.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_WHITE);
    tft_.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, TFT_WHITE);
    tft_.setTextColor(TFT_WHITE, TFT_BLACK);
    tft_.drawString("DISPLAY OK", 70, SCREEN_HEIGHT / 2 - 8, 2);
    tft_.drawString("SPI1/10MHz", 72, SCREEN_HEIGHT / 2 + 16, 2);
    delay(500);
    tft_.fillScreen(TFT_BLACK);

    // Step brightness back down to the configured default.
    setBrightness(brightness_);

    // Try full-screen sprite first (~131 KB at 240x280x16bpp)
    canvas_.setColorDepth(16);
    void* ptr = canvas_.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);

    if (ptr != nullptr && canvas_.created()) {
        canvas_.fillSprite(TFT_BLACK);
        stripMode_ = false;
        lowColorMode_ = false;
        ready_ = true;
        Serial.println("[HAL] Display: full-frame sprite OK");
        return;
    }

    // Fallback to 8-bit full-frame (~67 KB). This keeps full-screen rendering
    // alive on low free-heap situations where 16-bit sprite fails.
    Serial.println("[HAL] Display: 16-bit sprite FAILED, trying 8-bit full-frame");
    canvas_.deleteSprite();
    canvas_.setColorDepth(8);
    ptr = canvas_.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (ptr != nullptr && canvas_.created()) {
        canvas_.fillSprite(TFT_BLACK);
        stripMode_ = false;
        lowColorMode_ = true;
        ready_ = true;
        Serial.println("[HAL] Display: 8-bit full-frame sprite OK");
        return;
    }

    Serial.println("[HAL] Display: full-frame sprite FAILED (strip mode disabled for safety)");
    ready_ = false;
    stripMode_ = false;
    lowColorMode_ = false;

    // Draw a red diagnostic bar directly to TFT so the user sees something.
    tft_.fillScreen(TFT_BLACK);
    tft_.setTextColor(TFT_RED);
    tft_.drawString("DISPLAY ALLOC FAIL", 20, 100, 2);
    tft_.drawString("Check serial log", 30, 130, 2);
}

void HalDisplay::runPanelProbe() {
    Serial.println("[HAL] Display probe: raw ST7789 fill test");

    PANEL_SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    panelRawResetAndInit();
    panelFillColor(0xF800); // red
    delay(150);
    panelFillColor(0x07E0); // green
    delay(150);
    panelFillColor(0x001F); // blue
    delay(150);
    panelFillColor(0x0000); // black
    PANEL_SPI.endTransaction();

    Serial.println("[HAL] Display probe: done");
}

void HalDisplay::runPanelSweep() {
    const ProbePins profiles[] = {
        {PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST}, // expected profile
        {PIN_LCD_CS, PIN_LCD_DC, 12},          // some variants strap RST on GP12
        {PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST}, // CS/DC swapped wiring fallback
        {PIN_LCD_DC, PIN_LCD_CS, 12}
    };

    Serial.println("[HAL] Display sweep: trying pin profiles");
    PANEL_SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < (sizeof(profiles) / sizeof(profiles[0])); ++i) {
        configureProbePins(profiles[i]);
        Serial.printf("[HAL] Display sweep profile %u: cs=%u dc=%u rst=%u\n",
                      (unsigned)i,
                      (unsigned)profiles[i].cs,
                      (unsigned)profiles[i].dc,
                      (unsigned)profiles[i].rst);
        panelRawResetAndInit();
        panelFillColor(0xF800);
        delay(120);
        panelFillColor(0x07E0);
        delay(120);
        panelFillColor(0x001F);
        delay(120);
        panelFillColor(0x0000);
        delay(120);
    }
    PANEL_SPI.endTransaction();

    // Restore configured/default control pin profile.
    configureProbePins({PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST});
    Serial.println("[HAL] Display sweep: done");
}

void HalDisplay::pushCanvas() {
    if (!awake_ || !ready_) return;

    if (!stripMode_) {
        canvas_.pushSprite(0, 0);
        return;
    }

    // Strip mode: this is a degenerate path because we only have one strip
    // worth of pixels in memory. We push the current contents to y=0 — the
    // app will need to redraw and re-push for each strip if it cares about
    // multi-strip rendering. For now this keeps the device usable.
    canvas_.pushSprite(0, 0);
}

void HalDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    brightness_ = percent;
    analogWrite(PIN_LCD_BL, map(percent, 0, 100, 0, 255));
}

void HalDisplay::sleep() {
    awake_ = false;
    analogWrite(PIN_LCD_BL, 0);
}

void HalDisplay::wake() {
    awake_ = true;
    setBrightness(brightness_);
}
