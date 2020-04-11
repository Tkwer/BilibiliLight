
#define BLYNK_PRINT Serial // This prints to Serial Monitor
#define BLYNK_HEARTBEAT 300
//Blynk include
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

//WiFiManager include
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
//获取NTP时间
#include <NTPClient.h>
//B站 json解析
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

//BSP include
#include <WS2812FX.h>
#define LED_COUNT     12

#include "LSM6DSM.h"
#define I2C_BUS          Wire                           // Define the I2C bus (Wire instance) you wish to use
I2Cdev                   i2c_0(&I2C_BUS);               // Instantiate the I2Cdev object and point to the desired I2C bus

void myinthandler1();//外部中断事件

//IO口定义
#define ANALOG_PIN    A0
#define LED_PIN       12

//LSM6DSM definitions
#define LSM6DSM_intPin1 13  // interrupt1 pin definitions, significant motion
#define LSM6DSM_intPin2 14  // interrupt2 pin definitions, data ready
#define LSM6DSM_SDA      5  // 
#define LSM6DSM_SCK      4  // 


/******************************************************************************************************************************************/
/* Specify sensor parameters (sample rate is twice the bandwidth)
 * choices are:
      AFS_2G, AFS_4G, AFS_8G, AFS_16G  
      GFS_245DPS, GFS_500DPS, GFS_1000DPS, GFS_2000DPS 
      AODR_12_5Hz, AODR_26Hz, AODR_52Hz, AODR_104Hz, AODR_208Hz, AODR_416Hz, AODR_833Hz, AODR_1660Hz, AODR_3330Hz, AODR_6660Hz            
      GODR_12_5Hz, GODR_26Hz, GODR_52Hz, GODR_104Hz, GODR_208Hz, GODR_416Hz, GODR_833Hz, GODR_1660Hz, GODR_3330Hz, GODR_6660Hz
*/ 
uint8_t Ascale = AFS_2G, Gscale = GFS_245DPS, AODR = AODR_208Hz, GODR = GODR_416Hz;

float aRes, gRes;              // scale resolutions per LSB for the accel and gyro sensor2
float accelBias[3] = {-0.00499, 0.01540, 0.02902}, gyroBias[3] = {-0.50, 0.14, 0.28}; // offset biases for the accel and gyro
int16_t LSM6DSMData[7];        // Stores the 16-bit signed sensor output
float   Gtemperature;           // Stores the real internal gyro temperature in degrees Celsius
float ax, ay, az, gx, gy, gz;  // variables to hold latest accel/gyro data values 

bool newLSM6DSMData = false;

LSM6DSM LSM6DSM(LSM6DSM_intPin1, LSM6DSM_intPin2, &i2c_0); // instantiate LSM6DSM class
/******************************************************************************************************************************************/

// WIFI
const char* WIFI_SSID   = "yourssid";
const char* WIFI_PWD    = "yourpassword";
#define HOSTNAME          "bilibili-light-"
#define BAUD_RATE       115200                   // serial connection speed

//define your default values here, if there are different values in config.json, they are overwritten.
//length should be max size + 1
char mqtt_server[40] = "XXX.XXX.XXX.XXX";
char mqtt_port[6] = "8080";
int port = 8080;
char blynk_token[33] = "你的token";

//  virtual IO定义
#define V_sleep       V0  //是否开启
#define V_brightness  V1  //亮度
#define V_zeRGBa      V2  //颜色
#define V_speed       V3  //速度
#define V_modeSet     V4  //模式设置
#define V_modeRead    V5  //模式读取
#define V_LightINT    V6  //外界光强
#define V_Function    V7  //功能切换

BlynkTimer timer;
WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB +  NEO_KHZ800);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com",60*60*8, 30*60*1000);

DynamicJsonBuffer jsonBuffer(256); // ArduinoJson V5
// bilibili api: follower, view, likes
char UID[10] = "36196721";
String c_uid = UID;
String followerUrl = "http://api.bilibili.com/x/relation/stat?vmid=" + c_uid;   // 粉丝数
 
long follower = 0;   // 粉丝
long last_follower = 1;   // 粉丝
//*********************************************************************************


void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());

}

//flag for saving data
bool shouldSaveConfig = false;
//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//获取粉丝数目
void getFollower(String url)
{
    HTTPClient http;
    http.begin(url);
 
    int httpCode = http.GET();
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
 
    if (httpCode == 200)
    {
        Serial.println("Get OK");
        String resBuff = http.getString();
 
        // ---------- ArduinoJson V5 ----------
        JsonObject &root = jsonBuffer.parseObject(resBuff);
        if (!root.success())
        {
          Serial.println("parseObject() failed");
          return;
        }
 
        follower = root["data"]["follower"];
        Serial.print("Fans: ");
        Serial.println(follower);
    }
    else
    {
        Serial.printf("[HTTP] GET... failed, error: %d\n", httpCode);
    }
 
    http.end();
}

void reconnectBlynk() // reconnect to server if disconnected, timer checks every 15 seconds
{
  
  if (!Blynk.connected()) {
     Blynk.connect();
     while (!Blynk.connected()) {
          Serial.print(".");
          delay(2000);
      }
    if (Blynk.connect()) {
      BLYNK_LOG("Reconnected");
    } else {
      BLYNK_LOG("Not reconnected");
      //全红显示!!
      //show();
    }
  }
}

bool isFirstConnect = true;
BLYNK_CONNECTED() {
  if (isFirstConnect) {
    Blynk.syncAll();
    Blynk.notify("ESP8266 Starting!!");
    isFirstConnect = false;
  }
}

//// This is called when Smartphone App is opened
//BLYNK_APP_CONNECTED() {
//  Serial.println("App Connected.");
//}
//
//// This is called when Smartphone App is closed
//BLYNK_APP_DISCONNECTED() {
//  Serial.println("App Disconnected.");
//}


//===== 休眠设置  =====
BLYNK_WRITE(V_sleep) { // START Blynk Function
  uint8_t a = 1; //亮度
  a = param.asInt();
  //将灯关闭
  if (a == 0)
  {
    ws2812fx.setBrightness(0);
    Serial.println("sleep now!");
  }
  else{
    ws2812fx.setBrightness(80);
    Serial.println("work now!");
  }
 }

//===== 亮度设置  =====
BLYNK_WRITE(V_brightness) { // START Blynk Function
  int fxbrightness;   //亮度
  fxbrightness = param.asInt();
  ws2812fx.setBrightness(fxbrightness);
  Serial.print(F("Set brightness to: "));
  Serial.println(ws2812fx.getBrightness());
}

//===== 颜色设置  =====
BLYNK_WRITE(V_zeRGBa) { // START Blynk Function
  int fxred, fxgreen, fxblue;  // 颜色设置
  fxred = param[0].asInt(); // get a RED channel value
  fxgreen = param[1].asInt(); // get a GREEN channel value
  fxblue = param[2].asInt(); // get a BLUE channel value
  ws2812fx.setColor(fxred, fxgreen, fxblue);
  Serial.print(F("Set color to: 0x"));
  Serial.println(ws2812fx.getColor(), HEX);

}

//===== 速度设置  =====
BLYNK_WRITE(V_speed) { // START Blynk Function
  int fxspeed;        //速度
  fxspeed = param.asInt();
  ws2812fx.setSpeed(fxspeed);
  Serial.print(F("Set speed to: "));
  Serial.println(ws2812fx.getSpeed());
}

//===== 模式设置  =====
BLYNK_WRITE(V_modeSet) { // START Blynk Function
  int fxmode;         //模式
  fxmode = param.asInt();
  ws2812fx.setMode(fxmode % ws2812fx.getModeCount());
  Serial.print(F("Set mode to: "));
  Serial.print(ws2812fx.getMode());
  Serial.print(" - ");
  Serial.println(ws2812fx.getModeName(ws2812fx.getMode()));
}


//===== 功能切换  =====
uint8_t Function_flag = 1;//
uint8_t last_Function_flag = 0;
BLYNK_WRITE(V_Function) {
  Function_flag = param.asInt();//获取功能
}

//===== 显示时间功能  =====
uint8_t h = 0;
uint8_t m = 0;
uint8_t last_h = 13;
uint8_t last_m = 61;
void show_time(){
  if(h!=last_h||m!=last_m||Function_flag!=last_Function_flag){
    ws2812fx.setBrightness(40);
    ws2812fx.strip_off();
    ws2812fx.setPixelColor((h+3)%12, WHITE); // white  (h+3)%12偏移修正
    ws2812fx.setPixelColor((m+3)%12, ORANGE); // orange
    last_h = h;
    last_m = m;
    last_Function_flag = Function_flag;
    ws2812fx.show();
  }
    //
}

//===== 显示粉丝功能  =====
void show_follower(){
  if(follower!=last_follower||Function_flag!=last_Function_flag){
      ws2812fx.setBrightness(40);
      ws2812fx.strip_off();
      //2020.04.10 修改显示机制
      if(follower>1000)//如果粉丝超过1728，显示不再适用
      {
        for(int i=0;i<LED_COUNT;i++)
        {
          ws2812fx.setPixelColor(i, 0xFF0000); //全红
        }
      }else{
          uint8_t number_100= (int)follower / 100;//代表100
          uint8_t number_10= (int)(follower / 10)%10;//代表10
          uint8_t number_1= (int)follower % 10;//代表1
          for(int i=0;i<number_100;i++)
          {
            ws2812fx.setPixelColor(i, RED); 
          }
          for(int i=number_100;i<number_100+number_10;i++)
          {
            ws2812fx.setPixelColor(i, YELLOW); 
          }
          for(int i=number_100+number_10;i<number_100+number_10+number_1;i++)
          {
            ws2812fx.setPixelColor(i, BLUE); 
          }  
      }
      last_follower =follower;
      last_Function_flag = Function_flag;
      ws2812fx.show();
  }
}


//双击中断事件
void myinthandler1()
{
  Serial.print("双击了！");  
  Serial.println(Function_flag);
  Function_flag++;
  if(Function_flag==4) ws2812fx.setBrightness(0);
  if(Function_flag>4) {Function_flag=1; ws2812fx.setBrightness(40); }
  
}



#define TIMER_MS 3000
void setup() {
  Serial.begin(BAUD_RATE);  // BLYNK_PRINT data
  Serial.println("\n Starting");
  ws2812fx.init();
  ws2812fx.setBrightness(30);
  ws2812fx.setSpeed(1000);
  ws2812fx.setColor(BLUE);
  ws2812fx.setMode(FX_MODE_RUNNING_LIGHTS);
  ws2812fx.start();
  unsigned long last_change = 0;
  unsigned long now = 0;
  last_change = millis();
  now = millis();
  //开机动画，白光，静止，2s
  while (now - last_change < TIMER_MS)
  {
    ws2812fx.service();
    now = millis();
  }

  Wire.begin(LSM6DSM_SDA, LSM6DSM_SCK, 400000); //(SDA, SCL) (21,22) are default on ESP32, 400 kHz I2C clock
  delay(1000);
 
  i2c_0.I2Cscan(); // which I2C device are on the bus?
  // Read the LSM6DSM Chip ID register, this is a good test of communication
  Serial.println("LSM6DSM accel/gyro...");
  byte c = LSM6DSM.getChipID();  // Read CHIP_ID register for LSM6DSM
  Serial.print("LSM6DSM "); Serial.print("I AM "); Serial.print(c, HEX); Serial.print(" I should be "); Serial.println(0x6A, HEX);
  Serial.println(" ");
  if(c == 0x6A ) // check if all I2C sensors have acknowledged
  {
     Serial.println("LSM6DSM is online..."); Serial.println(" ");
     
  
     LSM6DSM.reset();  // software reset LSM6DSM to default registers
  
     // get sensor resolutions, only need to do this once
  
     LSM6DSM.init(Ascale, Gscale, AODR, GODR);
//     LSM6DSM.selfTest();
//     LSM6DSM.offsetBias(gyroBias, accelBias);
  }
  else 
  {
    Serial.println(" LSM6DSM not functioning!");
  }
  
  attachInterrupt(LSM6DSM_intPin2, myinthandler1, RISING);  // define interrupt for intPin2 output of LSM6DSM

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 34);
  WiFiManagerParameter custom_bzhan_uid("uid", "Bilibili UID", UID, 10);//添加
  
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // Uncomment for testing wifi manager
  wifiManager.resetSettings();
  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_blynk_token);
  wifiManager.addParameter(&custom_bzhan_uid);//添加
  
  wifiManager.setAPCallback(configModeCallback);
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();
  //Manual Wifi
  //WiFi.begin(WIFI_SSID, WIFI_PWD);
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(blynk_token, custom_blynk_token.getValue());
  strcpy(UID, custom_bzhan_uid.getValue());//添加
  c_uid = UID;
  followerUrl = "http://api.bilibili.com/x/relation/stat?vmid=" + c_uid;
  port = atoi(mqtt_port);
  Blynk.config(blynk_token, mqtt_server, port);

  //等待wifi准备就绪动画，紫色，追逐
  ws2812fx.setColor(0x007BFF);
  ws2812fx.setMode(FX_MODE_FADE);
  while (WiFi.status() != WL_CONNECTED) {
    //  delay(500);
    //  Serial.print(".");

    ws2812fx.service();

  }
  
  while (Blynk.connect() == false) {        // wait here until connected to the server
    if ((millis() / 1000) > 10) {   // try to connect to the server for less than 9 seconds
      break;                                    // continue with the sketch regardless of connection to the server
    }
  }
  
  timeClient.begin();   // Start the NTP UDP client  
  timeClient.update();
  h = timeClient.getHours()%12;
  m = timeClient.getMinutes()/5;
  getFollower(followerUrl);
  timer.setInterval(15000, reconnectBlynk); // check every 15 seconds if we are connected to the server
  timer.setInterval(2000, GetLight);
  timer.setInterval(1000, Getmode);

}

void loop()
{
  if (Blynk.connected()) {   // to ensure that Blynk.run() function is only called if we are still connected to the server
    Blynk.run();
  }
  timer.run();

  switch (Function_flag)
  {
    case 1: {
        last_Function_flag = Function_flag;
        ws2812fx.service();// 控制功能
        break;
      }
    case 2: { 
         // 时钟功能
         show_time();
        break;
      }
    case 3: { //获取粉丝数目，教程来源:https://mc.dfrobot.com.cn/thread-303095-1-1.html
        show_follower();
        break;
      }
    case 4: { //熄屏。
        
        break;
      }
    default :{
      Function_flag = 0;//
    }
  }
  //ws2812fx.service();// 控制功能
}

String ModeName, LastModeName;
void Getmode()
{
  ModeName = ws2812fx.getModeName(ws2812fx.getMode());
  if (ModeName != LastModeName)
  {
    Blynk.virtualWrite(V_modeRead, String(ws2812fx.getMode())+"-"+ModeName);
    LastModeName = ModeName;
  }
}

uint8_t get_flag = 0;
void GetLight() {
  uint16_t LightINT;
  LightINT = analogRead(ANALOG_PIN);       //光照强度转化  4096*？
  Blynk.virtualWrite(V_LightINT, LightINT);
  //时间与粉丝获取放在这里吧
  if(Function_flag == 3){
    if((get_flag%4)==0)//2*4秒获取一次
    {
      getFollower(followerUrl);
      }  
  }
  if(Function_flag == 2){
    if((get_flag%60)==0)//2*60秒获取一次
    {
      timeClient.update();
      h = timeClient.getHours()%12;
      m = timeClient.getMinutes()/5;
      //uint8_t s = timeClient.getSeconds()/5;
      get_flag=0;
      }      
  }
  if(Function_flag==2||Function_flag==3)get_flag++;//只有这两个模式下才++


  //Serial.print("LightINT: "); Serial.print(LightINT); Serial.println(" lx  ");
}


