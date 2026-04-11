/*****************************************************************************
* | File      	:   LCD_1in69_Touch_test.c
* | Author      :   Waveshare team
* | Function    :   1.3inch LCD  test demo
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2024-03-25
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "LCD_Test.h"
Touch_1IN69_XY XY;
UBYTE flag = 0;
UWORD *BlackImage;
void setup()
{
    // put your setup code here, to run once:
    if (DEV_Module_Init() != 0)
    {
        return;
    }
    /* LCD Init */
    Serial.printf("1.69inch LCD demo...\r\n");
    DEV_SET_PWM(100);
    LCD_1IN69_Init(VERTICAL);
    
    attachInterrupt(DEV_I2C_INT, &Touch_INT_callback, RISING);

    UDOUBLE Imagesize = LCD_1IN69_HEIGHT * LCD_1IN69_WIDTH * 2;
    if ((BlackImage = (UWORD *)malloc(Imagesize)) == NULL)
    {
        Serial.printf("Failed to apply for black memory...\r\n");
        exit(0);
    }
    /*1.Create a new image cache named IMAGE_RGB and fill it with white*/
    Paint_NewImage((UBYTE *)BlackImage, LCD_1IN69_WIDTH, LCD_1IN69_HEIGHT, 90, WHITE);
    Paint_SetScale(65);
    Paint_Clear(WHITE);
#if 1
    Serial.printf("drawing...\r\n");
    /*2.Drawing on the image*/
    Paint_DrawPoint(12,28, BLACK, DOT_PIXEL_1X1,  DOT_FILL_RIGHTUP);
    Paint_DrawPoint(12,30, BLACK, DOT_PIXEL_2X2,  DOT_FILL_RIGHTUP);
    Paint_DrawPoint(12,33, BLACK, DOT_PIXEL_3X3, DOT_FILL_RIGHTUP);
    Paint_DrawPoint(12,38, BLACK, DOT_PIXEL_4X4, DOT_FILL_RIGHTUP);
    Paint_DrawPoint(12,43, BLACK, DOT_PIXEL_5X5, DOT_FILL_RIGHTUP);

    Paint_DrawLine( 20,  5, 80, 65, MAGENTA, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    Paint_DrawLine( 20, 65, 80,  5, MAGENTA, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    Paint_DrawLine( 148,  35, 208, 35, CYAN, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine( 178,   5,  178, 65, CYAN, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);

    Paint_DrawRectangle(20, 5, 80, 65, RED, DOT_PIXEL_2X2,DRAW_FILL_EMPTY);
    Paint_DrawRectangle(85, 5, 145, 65, BLUE, DOT_PIXEL_2X2,DRAW_FILL_FULL);

    Paint_DrawCircle(178, 35, 30, GREEN, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawCircle(240, 35, 30, GREEN, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawString_EN(11, 70, "AaBbCc123", &Font16, RED, WHITE);
    Paint_DrawString_EN(11, 85, "AaBbCc123", &Font20, 0x000f, 0xfff0);
    Paint_DrawString_EN(11, 105, "AaBbCc123", &Font24, RED, WHITE);   
    Paint_DrawString_CN(11, 125, "微雪电子Abc",  &Font24CN, WHITE, BLUE);
    LCD_1IN69_Display(BlackImage);
    DEV_Delay_ms(1000);
    
    Paint_NewImage((UBYTE *)BlackImage, LCD_1IN69_WIDTH, LCD_1IN69_HEIGHT, 0, WHITE);
    Paint_SetScale(65);
    Paint_DrawImage(gImage_1IN69_PIC,0,0,240,280);
    LCD_1IN69_Display(BlackImage);
    DEV_Delay_ms(1000);
#endif

#if 1
    Touch_Gesture();
    Touch_HandWriting();
#endif
    /* Module Exit */
    free(BlackImage);
    BlackImage = NULL;

    DEV_Module_Exit();
    return;
}

void Touch_HandWriting()
{
    XY.mode = 1;
    XY.color = BLACK;
    flag = TOUCH_INIT;
    uint8_t DOT_PIXEL = DOT_PIXEL_6X6;
    /* TP Init */
    if(Touch_1IN69_init(XY.mode) == true)
        Serial.printf("OK!\r\n");
    else
        Serial.printf("NO!\r\n");
    DEV_Delay_ms(10);
    Paint_Clear(WHITE);
    LCD_1IN69_Display(BlackImage);
    Paint_DrawPoint(120, 140, XY.color, DOT_PIXEL_6X6, DOT_FILL_AROUND);
    LCD_1IN69_DisplayWindows(120, 140, 120+DOT_PIXEL, 140+DOT_PIXEL, BlackImage);
    while(1)
    {
        DEV_Delay_us(10);
        if (flag == TOUCH_IRQ)
        {
            Serial.printf("X:%d Y:%d \r\n",XY.x_point,XY.y_point);
            Paint_DrawPoint(XY.x_point, XY.y_point, XY.color, DOT_PIXEL_6X6, DOT_FILL_AROUND);
            LCD_1IN69_DisplayWindows(XY.x_point, XY.y_point, XY.x_point+DOT_PIXEL, XY.y_point+DOT_PIXEL, BlackImage);
            flag = TOUCH_DRAW;
        }
    }
}

void Touch_Gesture()
{
    XY.mode = 0;
    /* TP Init */
    if(Touch_1IN69_init(XY.mode) == true)
        Serial.printf("OK!\r\n");
    else
        Serial.printf("NO!\r\n");
    Paint_Clear(WHITE);
    Paint_DrawString_EN(35, 90, "Gesture test", &Font20, BLACK, WHITE);
    Paint_DrawString_EN(10, 120, "Complete as prompted", &Font16, BLACK, WHITE);
    LCD_1IN69_Display(BlackImage);
    Paint_Clear(WHITE);
    DEV_Delay_ms(1500);
    Paint_DrawImage(gImage_up, 45, 30, 150, 150);
    Paint_DrawString_EN(105, 190, "Up", &Font24, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    
    while(XY.Gesture != UP)
    {
        DEV_Delay_ms(100);
    }
    Paint_Clear(WHITE);
    Paint_DrawImage(gImage_down, 45, 30, 150, 150);
    Paint_DrawString_EN(85, 190, "Down", &Font24, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    while(XY.Gesture != Down)
    {
        DEV_Delay_ms(100);
    }
    Paint_Clear(WHITE);
    Paint_DrawImage(gImage_left, 45, 30, 150, 150);
    Paint_DrawString_EN(85, 190, "Left", &Font24, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    while(XY.Gesture != LEFT)
    {
        DEV_Delay_ms(100);
    } 
    Paint_Clear(WHITE);
    Paint_DrawImage(gImage_right, 45, 30, 150, 150);
    Paint_DrawString_EN(80, 190, "Right", &Font24, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    while(XY.Gesture != RIGHT)
    {
        DEV_Delay_ms(100);
    }
    Paint_Clear(WHITE);
    Paint_DrawImage(gImage_long_press, 45, 30, 150, 150);
    Paint_DrawString_EN(47, 190, "Long Press", &Font20, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    while(XY.Gesture != LONG_PRESS)
    {
        DEV_Delay_ms(100);
    }
    Paint_Clear(WHITE);
    Paint_DrawImage(gImage_double_click, 45, 30, 150, 150);
    Paint_DrawString_EN(35, 185, "Double Click", &Font20, 0X647C, WHITE);
    LCD_1IN69_Display(BlackImage);
    while(XY.Gesture != DOUBLE_CLICK)
    {
        DEV_Delay_ms(100);
    }
    Paint_Clear(WHITE);
    flag = TOUCH_OUT_GESTURE; //exit gesture mode
}

void loop()
{
    DEV_Delay_ms(1000);
}

void Touch_INT_callback()
{
    if (XY.mode == 0)
    {
        XY.Gesture = DEV_I2C_Read_Byte(address,0x01);
        flag = TOUCH_IRQ;
    }
    else
    {
        flag = TOUCH_IRQ;
        XY = Touch_1IN69_Get_Point();  
    }
}