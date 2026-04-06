#include "qr_display.h"
#include <qrcode.h>

void QrDisplay::render(TFT_eSprite& canvas, const char* data,
                        int16_t cx, int16_t cy, uint16_t maxSize,
                        uint16_t fgColor, uint16_t bgColor) {
    // QR version 3 can hold ~53 alphanumeric chars (enough for Solana addresses)
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, data);

    uint8_t moduleCount = qrcode.size;  // Typically 29 for version 3
    uint8_t moduleSize = maxSize / moduleCount;
    if (moduleSize < 1) moduleSize = 1;

    uint16_t totalSize = moduleSize * moduleCount;
    int16_t startX = cx - totalSize / 2;
    int16_t startY = cy - totalSize / 2;

    // Draw white background with padding
    canvas.fillRect(startX - 4, startY - 4, totalSize + 8, totalSize + 8, bgColor);

    // Draw QR modules
    for (uint8_t y = 0; y < moduleCount; y++) {
        for (uint8_t x = 0; x < moduleCount; x++) {
            uint16_t color = qrcode_getModule(&qrcode, x, y) ? fgColor : bgColor;
            canvas.fillRect(startX + x * moduleSize, startY + y * moduleSize,
                           moduleSize, moduleSize, color);
        }
    }
}
