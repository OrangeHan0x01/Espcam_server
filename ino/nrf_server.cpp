#include "esp_camera.h"
#include "esp_timer.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include <SPI.h>
#include <nRF24L01_esp.h>
#include <RF24_esp.h>
#include <WiFi.h>
//  hspi->begin(); （sclk=14,miso=12,mosi-13,ss=15),其中ss就是cs引脚
//！！目前关键是nrf/tcp传输中的停止符需不需要修改
/*
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
*/
/*
NRF      esp32
1 GND  GND
2 VCC 3.3/5V(应该是3.3V不然会烧)   
3 CE  16//原本ce\csn是22、23，应该可以改成15、16
4 CSN 15
5 SCK 14
6 MOSI  13
7 MISO  12
8 IRQ 置空
*/
WiFiClient client;
const char* ssid     = "your ssid";
const char* password = "your wifi password";
const char* host = "192.168.0.105";//your PC server ip
const int port = 21572;//your PC server port

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
//camera_fb_t * fb = NULL;
//esp_err_t res = ESP_OK;
auto time_flag=millis();
byte headdata[6];
byte index_data[2];
byte data[32];
short now_index;
short pack_number;
short last_length;
short counter;
int data_length;
//bool last_flag=0;

void setup(void)
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  /*
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
  */
  /*
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;//640*480分辨率
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }*/
  Serial.println("Start Wifi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  hspi = new SPIClass(HSPI);
  radio = new RF24_esp(16, 15, hspi);//ce,csn,hspi,其中ce随意，hspi固定，
  Serial.println("try to init radio");
  radio->begin();
  radio->setPALevel(RF24_PA_MAX);
  radio->setDataRate(RF24_2MBPS);
  radio->setChannel(80);
  radio->setAutoAck(1);
  radio->setRetries(1,2);
  radio->setCRCLength(RF24_CRC_8);
  Serial.println("using radio-read.");
  radio->openReadingPipe(1,pipe_addr);//设置目标地址，注意，这个标识码应当是不分主从的（或者说只有从机需要地址），相当于和通道一样的作用，模块自动区分应当接收什么数据
  Serial.println("pipe opened.");
  radio->startListening();
  Serial.println("listening..");
}

void loop()
{
  if(radio->available()){
    counter=0;
    radio->read(&headdata,6);
    index_data[0]=headdata[0];//截取首包包号
    index_data[1]=headdata[1];
    now_index=bytesToshort(index_data);
    if(now_index!=0){return;}//如果不是第一个包，则直接跳过这个帧等待下一个图像帧
    index_data[0]=headdata[2];//截取末尾包号，（末尾包号-1）*30+末尾包长=总长
    index_data[1]=headdata[3];
    pack_number=bytesToshort(index_data);
    index_data[0]=headdata[4];//截取末尾包长
    index_data[1]=headdata[5];
    last_length=bytesToshort(index_data);
    data_length=(pack_number-1)*30+last_length;
    byte *tempbuf=(byte *)malloc(data_length);
    time_flag=millis();//创建一个超时定时器，超过时间就结束之后的循环，由于nrf低数据多发，可以定短一些，例如5ms
    auto total_time=millis();//开始统计总共时间消耗
    while(1){//不会阻塞，只检查是否有数据
      if(millis()-time_flag>=100){Serial.println("[-]Time out Error!");break;}//超过100ms未收到数据，直接退出
      if(radio->available()){
        time_flag=millis();
        counter++;
        radio->read(&data,32);
        index_data[0]=data[0];
        index_data[1]=data[1];
        now_index=bytesToshort(index_data);
        if(now_index==0){continue;}//头部包，不处理
        if(now_index<pack_number){for(short j=0;j<30;j++){tempbuf[(now_index-1)*30+j]=data[j+2];}}//如果不是最后一个包,代替：memcpy(tempbuf+(now_index-1)*30+j,fb->buf+2,fb->len-2);
        else{
          for(short j=0;j<last_length;j++){tempbuf[(now_index-1)*30+j]=data[j+2];}
          break;//退出循环
        }
      }
    }//接收完成
    Serial.println(total_time-millis());
    if(counter<=0.9*last_length){Serial.println("Packets too rare, can't promise the quality.");return;}//完整性校验，测试0.7，正常使用可以0.9-0.95左右
    byte *ztext=(byte *)malloc(data_length+6);//准备tcp传输至服务器
    int lenofd=(data_length)+6;
    for(int i=0;i<6;i++){
      ztext[i]=pipe_addr[i];
    }
    memcpy(ztext+6,tempbuf,data_length);

    if(lenofd<=50){Serial.println("charlist_error!compare size:");Serial.println(sizeof(ztext));delay(500);return;}
    Serial.println(lenofd);
    int datapoint=0;
    int slicelen=lenofd;
    byte *tempbuf_1=(byte *)malloc(5500);
    while(!client.connect(host,port)){delay(100);Serial.print(".");}//只有在一行结束开始新行时，才将数据提交到tcp服务端，也在同时进行tcp连接和结束连接
    while(slicelen>5500){
      memcpy(tempbuf_1, ztext+datapoint,5500);
      client.write(tempbuf_1,5500);//已经确认sizeof会\00截断
      datapoint+=5500;
      slicelen-=5500;
    }
    memcpy(tempbuf_1, ztext+datapoint,slicelen);
    client.write(tempbuf_1,slicelen);
    client.stop();

    Serial.println("[+]Data sended.Pack_num & Receive_counter:");
    Serial.print((int)pack_number);
    Serial.print("  &  ");
    Serial.print((int)counter);//最稳定情况下pack_number和counter应当相等
    Serial.println(".");
    free(tempbuf);
    tempbuf=NULL;
    free(tempbuf_1);
    tempbuf_1=NULL;
    free(ztext);
    ztext=NULL;
    time_flag=millis();
    delay(1);
  }
}