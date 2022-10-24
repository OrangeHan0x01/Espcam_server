#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include <time.h>
#include <SPI.h>
#include <nRF24L01_esp.h>
#include <RF24_esp.h>
//  hspi->begin(); （sclk=14,miso=12,mosi-13,ss=15),其中ss就是cs引脚
//！！目前关键是nrf/tcp传输中的停止符需不需要修改
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
/*
NRF      esp32
1 GND  GND
2 VCC 3.3(应该是3.3V不然会烧)   
3 CE  16//原本ce\csn是22、23，应该可以改成15、16
4 CSN 15
5 SCK 14
6 MOSI  13
7 MISO  12
8 IRQ 置空
*/
void shortTobytes(short i, uint8_t* bytes) {
  int size = sizeof(short);
  memset(bytes, 0, sizeof(uint8_t) * size);
  bytes[0] = (uint8_t)(0xff & i);
  bytes[1] = (uint8_t)((0xff00 & i) >> 8);
}

short bytesToshort(uint8_t* bytes) {
  short iRetVal = bytes[0] & 0xFF;
  iRetVal |= ((bytes[1] << 8) & 0xFF00);
  return iRetVal;
}

RF24_esp *radio;
SPIClass * hspi = NULL;
const byte pipe_addr[6] = {'0','0','0','0','0','1'};
camera_fb_t * fb = NULL;
esp_err_t res = ESP_OK;
size_t _jpg_buf_len = 0;
uint8_t * _jpg_buf = NULL;
auto time_flag=millis();

void setup(void)
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  //Serial.setDebugOutput(false);
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;//640*480分辨率
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  Serial.printf("ESP32CAM start,begin init camare.");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  Serial.println("Camera inited.");
  hspi = new SPIClass(HSPI);
  radio = new RF24_esp(16, 15, hspi);//ce,csn,hspi,其中ce随意，hspi固定，
  radio->begin();
  radio->setPALevel(RF24_PA_MAX);
  radio->setDataRate(RF24_2MBPS);
  radio->setChannel(80);
  radio->setAutoAck(1);
  radio->setRetries(1,2);//不重试
  radio->setCRCLength(RF24_CRC_8);
  Serial.println("using radio-write.");
  radio->openWritingPipe(pipe_addr);//设置目标地址，注意，这个标识码应当是不分主从的（或者说只有从机需要地址），相当于和通道一样的作用，模块自动区分应当接收什么数据
  Serial.println("pipe opened.");
  radio->stopListening();
}

void loop()
{
  auto dura=millis()-time_flag;
  if(dura>=2000){//2帧ps
    Serial.println("Start capture.");
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      return;
    }
    Serial.println("Camera Capture succeed,start format.");
    if(fb->format != PIXFORMAT_JPEG){
      //Serial.println("No Format,start format");
      bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if(!jpeg_converted){
        Serial.println("JPEG compression failed");
        res = ESP_FAIL;
      }
    } else {//若格式为JPEG
      //Serial.println("Format right.");
      Serial.println(fb->len);
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    if(res == ESP_OK){//！！！执行发送操作
        short last_index=((fb->len)-((fb->len)%30))/30;//short -32768~32767，一般jpeg 600-900个包，700左右比较多。这里假设总长65，代码中last_index为2,但是nrf传输的包号得是3
        short now_index=0;
        short counter=0;
        bool headsend=0;
        byte index_data[2];
        byte headdata[6];
        byte data[32];
        shortTobytes(now_index,index_data);
        headdata[0]=index_data[0];
        headdata[1]=index_data[1];
        shortTobytes(last_index+1,index_data);
        headdata[2]=index_data[0];
        headdata[3]=index_data[1];
        shortTobytes((short)((fb->len)%30),index_data);//总长使用包数量和末尾包长还原
        headdata[4]=index_data[0];
        headdata[5]=index_data[1];
        for(short i=0;i<3;i++){
          radio->writeFast(&headdata,6);
          delay(5);//没有设置ack时，只要发送出去，writefast就会返回1,发射3次头部包避免错过
        }
        //如果一直头部发送失败，考虑发送方重启，尽可能使得发送方向接收方时钟对齐。
        Serial.println('[+]Head sended');//接下来分块传输每个数据
        delay(2);//延迟2ms，等待接收方进行处理、申请空间等操作。
        unsigned long pauseTime = millis();//不能让NRF24在TX模式下保持FIFO队列满状态超过4ms. 启用了自动重发(auto retransmit)和自动应答(autoAck)后, NRF24保持TX模式的时间长度依然满足这个规则
        unsigned long startTime = millis();
        for(short i=0;i<last_index;i++){
          shortTobytes(i+1,index_data);//编号是i+1
          data[0]=index_data[0];
          data[1]=index_data[1];
          delayMicroseconds(120);//延迟120us，等待接收端处理，尽可能同步
            for(short j=0;j<30;j++){data[j+2]=fb->buf[i*30+j];}//如果使用memcpy代替遍历，速度可能更快，但也可能因为发送速度跟不上处理速度而产生额外错误。需要测试
            if(!radio->writeFast(&data,32)){
              counter++;
            }
            if(millis() - pauseTime > 3){//避免连续4ms的fifo队列满数据，但实际上可能需要通过在txstandby后等待一点时间来避免直接清空数据区导致的发送问题？
              pauseTime = millis();
              radio->txStandBy();
            }
        }
        //发送最后一个数据包。
        shortTobytes(last_index+1,index_data);
        data[0]=index_data[0];
        data[1]=index_data[1];
        for(short j=0;j<((short)((fb->len)%30));j++){data[j+2]=fb->buf[last_index*30+j];}
        if(!radio->writeFast(&data,32)){//依然是发射32字节，这是为了接收端对齐，接收端实际上会舍弃后面部分
          counter++;
        }
        //显示结果分析
        time_flag=millis();
        unsigned long endTime = millis()-startTime;
        Serial.print("[*]Diagnosis: total_time & error_counter:");
        Serial.print((int)endTime);
        Serial.print((int)counter);
        Serial.println(".");
        if(fb){//清空数据
          //Serial.println("Start clear data");
          esp_camera_fb_return(fb);
          fb = NULL;
          _jpg_buf = NULL;
        } else if(_jpg_buf){
          free(_jpg_buf);
          _jpg_buf = NULL;
        }
        delay(500);//延迟500ms，等待接收端上传tcp
    }else{Serial.println("JPEG or CAMARE_ERROR,try to reboot.");delay(5000);return;}
  }
}