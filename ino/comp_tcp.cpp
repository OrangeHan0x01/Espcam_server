#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include <WiFi.h>

WiFiClient client;
const char* ssid     = "ssid";
const char* password = "password";
const char* host = "192.168.0.105";
const int port = 21572;

const uint8_t pipe_addr[] = "000001";
camera_fb_t * fb = NULL;
esp_err_t res = ESP_OK;
size_t _jpg_buf_len = 0;
uint8_t * _jpg_buf = NULL;
auto time_flag=millis();

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

void setup(void)
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
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
//init_op
    Serial.println("Start Wifi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void loop()
{
  esp_err_t res = ESP_OK;
  auto dura=millis()-time_flag;
  if(dura>=4000){//2帧ps
    Serial.println("Start capture.");
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      //Serial.println("Camera Capture succeed,start format.");
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
        //Serial.println(fb->len);
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
  }//首个else结尾
  if(res == ESP_OK){//！！！执行发送操作
    //Serial.println("try to init ztext");
    byte *ztext=(byte *)malloc((fb->len)+6);
    int lenofd=(fb->len)+6;
    //byte ztext[25000];
    //Serial.println("Start process ztext");
    for(int i=0;i<6;i++){
      ztext[i]=pipe_addr[i];
    }
    memcpy(ztext+6,fb->buf,fb->len);
    /*
    for(int j=0;j<fb->len;j++){
      ztext[j+6]=fb->buf[j];
    }
    */
    //Serial.println("ztext OK.");
    if(fb){//清空数据
      //Serial.println("Start clear data");
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
   Serial.println("start send data.");

   if(lenofd<=50){Serial.println("charlist_error!compare size:");Serial.println(sizeof(ztext));delay(500);return;}
   Serial.println(lenofd);
   int datapoint=0;
   int slicelen=lenofd;
   byte *tempbuf=(byte *)malloc(5500);
   while(!client.connect(host,port)){delay(100);Serial.print(".");}//只有在一行结束开始新行时，才将数据提交到tcp服务端，也在同时进行tcp连接和结束连接
   while(slicelen>5500){
     memcpy(tempbuf, ztext+datapoint,5500);
     client.write(tempbuf,5500);//已经确认sizeof会\00截断
     datapoint+=5500;
     slicelen-=5500;
   }
   memcpy(tempbuf, ztext+datapoint,slicelen);
   client.write(tempbuf,slicelen);
   client.stop();
   free(tempbuf);
   tempbuf=NULL;
   Serial.println("data sended.");
   free(ztext);
   ztext=NULL;
   time_flag=millis();
   delay(1);
  }else{Serial.println("JPEG or CAMARE_ERROR,try to reboot.");delay(5000);return;}//这里后期换成软重启
 }
}
