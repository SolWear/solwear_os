from machine import Pin,I2C,SPI,PWM,Timer
import framebuf
import time


#Pin definition  引脚定义
I2C_SDA = 6
I2C_SDL = 7
I2C_IRQ = 21
I2C_RST = 22

DC = 8
CS = 9
SCK = 10
MOSI = 11
MISO = 12
RST = 13

BL = 25

#LCD Driver LCD驱动
class LCD_1inch69(framebuf.FrameBuffer):
    def __init__(self): #SPI initialization  SPI初始化
        self.width = 240
        self.height = 280
        
        self.cs = Pin(CS,Pin.OUT)
        self.rst = Pin(RST,Pin.OUT)
        
        self.cs(1)
        self.spi = SPI(1,100_000_000,polarity=0, phase=0,bits= 8,sck=Pin(SCK),mosi=Pin(MOSI),miso=None)
        self.dc = Pin(DC,Pin.OUT)
        self.dc(1)
        self.buffer = bytearray(self.height * self.width * 2)
        super().__init__(self.buffer, self.width, self.height, framebuf.RGB565)
        self.init_display()
        
        #Define color, Micropython fixed to BRG format  定义颜色，Micropython固定为BRG格式
        self.red   =   0x07E0
        self.green =   0x001f
        self.blue  =   0xf800
        self.white =   0xffff
        self.black =   0x0000
        self.brown =   0X8430
        
        self.fill(self.white) #Clear screen  清屏
        self.show()#Show  显示

        self.pwm = PWM(Pin(BL))
        self.pwm.freq(5000) #Turn on the backlight  开背光
        
    def write_cmd(self, cmd): #Write command  写命令
        self.cs(1)
        self.dc(0)
        self.cs(0)
        self.spi.write(bytearray([cmd]))
        self.cs(1)

    def write_data(self, buf): #Write data  写数据
        self.cs(1)
        self.dc(1)
        self.cs(0)
        self.spi.write(bytearray([buf]))
        self.cs(1)
        
    def set_bl_pwm(self,duty): #Set screen brightness  设置屏幕亮度
        self.pwm.duty_u16(duty)#max 65535
        
    def init_display(self): #LCD initialization  LCD初始化
        """Initialize dispaly"""  
        self.rst(1)
        time.sleep(0.01)
        self.rst(0)
        time.sleep(0.01)
        self.rst(1)
        time.sleep(0.05)
        
        self.write_cmd(0x36);
        self.write_data(0x00);

        self.write_cmd(0x3A);
        self.write_data(0x05);

        self.write_cmd(0xB2);
        self.write_data(0x0B);
        self.write_data(0x0B);
        self.write_data(0x00);
        self.write_data(0x33);
        self.write_data(0x35);

        self.write_cmd(0xB7);
        self.write_data(0x11);

        self.write_cmd(0xBB);
        self.write_data(0x35);

        self.write_cmd(0xC0);
        self.write_data(0x2C);

        self.write_cmd(0xC2);
        self.write_data(0x01);

        self.write_cmd(0xC3);
        self.write_data(0x0D);

        self.write_cmd(0xC4);
        self.write_data(0x20);

        self.write_cmd(0xC6);
        self.write_data(0x13);

        self.write_cmd(0xD0);
        self.write_data(0xA4);
        self.write_data(0xA1);

        self.write_cmd(0xD6);
        self.write_data(0xA1);

        self.write_cmd(0xE0);
        self.write_data(0xF0);
        self.write_data(0x06);
        self.write_data(0x0B);
        self.write_data(0x0A);
        self.write_data(0x09);
        self.write_data(0x26);
        self.write_data(0x29);
        self.write_data(0x33);
        self.write_data(0x41);
        self.write_data(0x18);
        self.write_data(0x16);
        self.write_data(0x15);
        self.write_data(0x29);
        self.write_data(0x2D);

        self.write_cmd(0xE1);
        self.write_data(0xF0);
        self.write_data(0x04);
        self.write_data(0x08);
        self.write_data(0x08);
        self.write_data(0x07);
        self.write_data(0x03);
        self.write_data(0x28);
        self.write_data(0x32);
        self.write_data(0x40);
        self.write_data(0x3B);
        self.write_data(0x19);
        self.write_data(0x18);
        self.write_data(0x2A);
        self.write_data(0x2E);

        self.write_cmd(0xE4);
        self.write_data(0x25);
        self.write_data(0x00);
        self.write_data(0x00);

        self.write_cmd(0x21);

        self.write_cmd(0x11);
        time.sleep(0.12);
        self.write_cmd(0x29);
    
    #设置窗口    
    def setWindows(self,Xstart,Ystart,Xend,Yend): 
        self.write_cmd(0x2A)
        self.write_data(Xstart >> 8)
        self.write_data(Xstart)
        self.write_data((Xend-1) >> 8)
        self.write_data(Xend-1)
        
        self.write_cmd(0x2B)
        self.write_data((Ystart+20) >> 8)
        self.write_data(Ystart+20)
        self.write_data(((Ystart+20)-1) >> 8)
        self.write_data((Ystart+20)-1)
        
        self.write_cmd(0x2C)
     
    #Show  显示   
    def show(self): 
        self.setWindows(0,0,self.width,self.height)
        
        self.cs(1)
        self.dc(1)
        self.cs(0)
        self.spi.write(self.buffer)
        self.cs(1)
        
    '''
        Partial display, the starting point of the local
        display here is reduced by 10, and the end point
        is increased by 10
    '''
    #Partial display, the starting point of the local display here is reduced by 10, and the end point is increased by 10
    #局部显示，这里的局部显示起点减少10，终点增加10
    def Windows_show(self,Xstart,Ystart,Xend,Yend):
        if Xstart > Xend:
            data = Xstart
            Xstart = Xend
            Xend = data
            
        if (Ystart > Yend):        
            data = Ystart
            Ystart = Yend
            Yend = data
            
        if Xstart <= 10:
            Xstart = 10
        if Ystart <= 10:
            Ystart = 10
            
        Xstart -= 10;Xend += 10
        Ystart -= 10;Yend += 10
        
        self.setWindows(Xstart,Ystart,Xend,Yend)      
        self.cs(1)
        self.dc(1)
        self.cs(0)
        for i in range (Ystart,Yend):             
            Addr = (Xstart * 2) + (i * 240 * 2)                
            self.spi.write(self.buffer[Addr : Addr+((Xend-Xstart)*2)])
        self.cs(1)
        
    #Write characters, size is the font size, the minimum is 1  
    #写字符，size为字体大小,最小为1
    def write_text(self,text,x,y,size,color):
        ''' Method to write Text on OLED/LCD Displays
            with a variable font size

            Args:
                text: the string of chars to be displayed
                x: x co-ordinate of starting position
                y: y co-ordinate of starting position
                size: font size of text
                color: color of text to be displayed
        '''
        background = self.pixel(x,y)
        info = []
        # Creating reference charaters to read their values
        self.text(text,x,y,color)
        for i in range(x,x+(8*len(text))):
            for j in range(y,y+8):
                # Fetching amd saving details of pixels, such as
                # x co-ordinate, y co-ordinate, and color of the pixel
                px_color = self.pixel(i,j)
                info.append((i,j,px_color)) if px_color == color else None
        # Clearing the reference characters from the screen
        self.text(text,x,y,background)
        # Writing the custom-sized font characters on screen
        for px_info in info:
            self.fill_rect(size*px_info[0] - (size-1)*x , size*px_info[1] - (size-1)*y, size, size, px_info[2]) 
    
        
#Touch drive  触摸驱动
class Touch_CST816D(object):
    #Initialize the touch chip  初始化触摸芯片
    def __init__(self,address=0x15,mode=0,i2c_num=1,i2c_sda=I2C_SDA,i2c_scl=I2C_SDL,irq_pin=I2C_IRQ,rst_pin=I2C_RST,LCD=None):
        self._bus = I2C(id=i2c_num,scl=Pin(i2c_scl),sda=Pin(i2c_sda),freq=400_000) #Initialize I2C 初始化I2C
        self._address = address #Set slave address  设置从机地址
        self.int=Pin(irq_pin,Pin.IN, Pin.PULL_UP)         
        self.rst=Pin(rst_pin,Pin.OUT)
        self.Reset()
        bRet=self.WhoAmI()
        if bRet :
            print("Success:Detected CST816D.")
            Rev= self.Read_Revision()
            print("CST816D Revision = {}".format(Rev))
            self.Stop_Sleep()
        else    :
            print("Error: Not Detected CST816D.")
            return None
        self.Mode = mode
        self.Gestures="None"
        self.Flag =self.l = 0
        self.X_point = self.Y_point = 0
        self.int.irq(handler=self.Int_Callback,trigger=Pin.IRQ_FALLING)
      
    def _read_byte(self,cmd):
        rec=self._bus.readfrom_mem(int(self._address),int(cmd),1)
        return rec[0]
    
    def _read_block(self, reg, length=1):
        rec=self._bus.readfrom_mem(int(self._address),int(reg),length)
        return rec
    
    def _write_byte(self,cmd,val):
        self._bus.writeto_mem(int(self._address),int(cmd),bytes([int(val)]))

    def WhoAmI(self):
        if (0xB5) != self._read_byte(0xA7):
            return False
        return True
    
    def Read_Revision(self):
        return self._read_byte(0xA9)
      
    #Stop sleeping  停止睡眠
    def Stop_Sleep(self):
        self._write_byte(0xFE,0x01)
    
    #Reset  复位    
    def Reset(self):
        self.rst(0)
        time.sleep_ms(1)
        self.rst(1)
        time.sleep_ms(50)
    
    #Set mode  设置模式   
    def Set_Mode(self,mode,callback_time=10,rest_time=5): 
        # mode = 0 gestures mode 
        # mode = 1 point mode 
        # mode = 2 mixed mode 
        if (mode == 1):      
            self._write_byte(0xFA,0X41)
            
        elif (mode == 2) :
            self._write_byte(0xFA,0X71)
            
        else:
            self._write_byte(0xFA,0X11)
            self._write_byte(0xEC,0X01)
     
    #Get the coordinates of the touch  获取触摸的坐标
    def get_point(self):
        xy_point = self._read_block(0x03,4)
        
        x_point= ((xy_point[0]&0x0f)<<8)+xy_point[1]
        y_point= ((xy_point[2]&0x0f)<<8)+xy_point[3]
        
        self.X_point=x_point
        self.Y_point=y_point
    
    #Draw points and show  画点并显示  
    def Touch_HandWriting(self):
        x = y = data = 0
        color = 0
        self.Flag = 0
        self.Mode = 1
        self.Set_Mode(self.Mode)
        
        LCD.fill(LCD.white)
        LCD.fill_rect(120, 140, 6, 6, color)
        LCD.show()
        
        try:
            while True:              
                if self.Flag == 1:  
                    LCD.fill_rect(self.X_point, self.Y_point, 6, 6, color)
                    LCD.Windows_show(self.X_point, self.Y_point, self.X_point, self.Y_point)

        except KeyboardInterrupt:
            pass
    
    #Gesture  手势
    def Touch_Gesture(self):
        self.Mode = 0
        self.Set_Mode(self.Mode)
        LCD.write_text('Gesture test',70,90,1,LCD.black)
        LCD.write_text('Complete as prompted',35,120,1,LCD.black)
        LCD.show()
        time.sleep(1)
        LCD.fill(LCD.white)
        while self.Gestures != 0x02:
            LCD.fill(LCD.white)
            LCD.write_text('UP',100,110,3,LCD.black)
            LCD.show()
            
        while self.Gestures != 0x01:
            LCD.fill(LCD.white)
            LCD.write_text('DOWN',70,110,3,LCD.black)
            LCD.show()
            
        while self.Gestures != 0x03:
            LCD.fill(LCD.white)
            LCD.write_text('LEFT',70,110,3,LCD.black)
            LCD.show()
            
        while self.Gestures != 0x04:
            LCD.fill(LCD.white)
            LCD.write_text('RIGHT',60,110,3,LCD.black)
            LCD.show()
            
        while self.Gestures != 0x0C:
            LCD.fill(LCD.white)
            LCD.write_text('Long Press',40,110,2,LCD.black)
            LCD.show()
            
        while self.Gestures != 0x0B:
            LCD.fill(LCD.white)
            LCD.write_text('Double Click',25,110,2,LCD.black)
            LCD.show() 
        
    def Int_Callback(self,pin):
        if self.Mode == 0 :
            self.Gestures = self._read_byte(0x01)

        elif self.Mode == 1:           
            self.Flag = 1
            self.get_point()

    def Timer_callback(self,t):
        self.l += 1
        if self.l > 100:
            self.l = 50
            
if __name__=='__main__':
  
    LCD = LCD_1inch69()
    LCD.set_bl_pwm(65535)

    Touch=Touch_CST816D(mode=1,LCD=LCD)

    Touch.Touch_Gesture()
    Touch.Touch_HandWriting()














