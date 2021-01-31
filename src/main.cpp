/*
 * TODO:
 * - rec-level instelbaar maken?
 */ 
 
#define WDT_TIMEOUT 30

#include <Arduino.h>
// This ESP_VS1053_Library
#include <VS1053.h>
#include <SD.h>
#include <Wire.h>
#include "Adafruit_SSD1306.h"
#include "Adafruit_I2CDevice.h"

#define FS_NO_GLOBALS
#include <FS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <SPIFFS.h>
#include <AsyncSDServer.h>

// Wiring of VS1053 board (SPI connected in a standard way)
#define VS1053_CS     17
#define VS1053_DCS    16
#define VS1053_DREQ   4
#define SD_CS 5 // SD

#define VOLUME  100 // volume level 0-100

#define REC_BUTTON 15
#define ADDR_LENGTH 32

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);

bool useMic = false;

// -------------------------------------------------
uint8_t mac[ADDR_LENGTH];
char macaddr[ADDR_LENGTH*2];

char ssid[32] = "ESP-Recorder";
const char * hostName = "asyncSDtest";

IPAddress local_IP(192, 162, 4, 1);
IPAddress gateway(0, 0, 0, 0);
IPAddress netmask(255, 255, 255, 0);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

int sdInitialized = 0;

bool echo_on = false;

struct wsMessage {
    uint8_t id[2];
    uint8_t type;
    uint8_t delimiter;
    uint8_t data[240];
};

/*
  FORWARD-DEFINED FUNCTIONS
*/
struct wsMessage buildWSMessage(char data[240], size_t length){
    wsMessage message;
    memset(&message, 0, sizeof(message));
    memcpy(&message, data, length);
    return message;
}

void sendToWS(struct wsMessage message, size_t length){
    uint8_t msg[240];  
    memcpy(msg, &message, length);
    ws.binaryAll(msg, length);
}

/*
  CALLBACK FUNCTIONS
*/
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
    if(type == WS_EVT_CONNECT){
        Serial.printf("ws[%s][%u] connect\r\n", server->url(), client->id());
        wsMessage message1 = buildWSMessage("FFc|Welcome to DISASTER RADIO", 29);
        sendToWS(message1, 29);
        if(echo_on){
            wsMessage message2 = buildWSMessage("FFc|echo enabled, to turn off, enter '$' after logging in", 57);
            sendToWS(message2, 57);
        }
        client->ping();
    } else if(type == WS_EVT_DISCONNECT){
        Serial.printf("ws[%s][%u] disconnect: %u\r\n", server->url(), client->id());
    } else if(type == WS_EVT_ERROR){
        Serial.printf("ws[%s][%u] error(%u): %s\r\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    } else if(type == WS_EVT_PONG){
        Serial.printf("ws[%s][%u] pong[%u]: %s\r\n", server->url(), client->id(), len, (len)?(char*)data:"");
    } else if(type == WS_EVT_DATA){

        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        char msg_id[4];
        char usr_id[32];
        char msg[256];
        int msg_length;
        int usr_id_length = 0;
        int usr_id_stop = 0;

        if(info->final && info->index == 0 && info->len == len){
            //the whole message is in a single frame and we got all of it's data

            Serial.printf("ws[%s][%u] %s-message[%llu]: \r\n", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
            //cast data to char array
            for(size_t i=0; i < info->len; i++) {
                //TODO check if info length is bigger than allocated memory
                msg[i] = (char) data[i];
                msg_length = i; 
                    
                if(msg[i] == '$'){
                    echo_on = !echo_on;
                }

                // check for stop char of usr_id
                if(msg[i] == '>'){
                    usr_id_stop = i;  
                }
            }
            msg_length++;
            msg[msg_length] = '\0';

            //parse message id 
            memcpy( msg_id, msg, 2 );
            msg_id[2] = '!';
            msg_id[3] = '\0';   

            //parse username
            for( int i = 5 ; i < usr_id_stop ; i++){
                usr_id[i-5] = msg[i];
            }
            usr_id_length = usr_id_stop - 5;

            //print message info to serial
            Serial.printf("Message Length: %d\r\n", msg_length);
            Serial.printf("Message ID: %02d%02d %c\r\n", msg_id[0], msg_id[1], msg_id[2]);
            Serial.printf("Message:");
            for( int i = 0 ; i <= msg_length ; i++){
                Serial.printf("%c", msg[i]);
            }
            Serial.printf("\r\n");

            //TODO delay ack based on estimated transmit time
            //send ack to websocket
            ws.binary(client->id(), msg_id, 3);

            //echoing message to ws
            if(echo_on){
                ws.binaryAll(msg, msg_length);
            }
        } 
        else {
            //TODO message is comprised of multiple frames or the frame is split into multiple packets

        }
    }
}

/*
  SETUP FUNCTIONS
*/
void wifiSetup(){
    WiFi.mode(WIFI_AP);
    //WiFi.macAddress(mac);
    //sprintf(macaddr, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac [5]);
    //setLocalAddress(macaddr);
    //strcat(ssid, macaddr);
    WiFi.setHostname(hostName);
    WiFi.softAP(ssid);
}


void spiffsSetup(){
    if (SPIFFS.begin()) {
        Serial.print("ok\r\n");
        if (SPIFFS.exists("/index.htm")) {
            Serial.printf("The file exists!\r\n");
            fs::File f = SPIFFS.open("/index.htm", "r");
            if (!f) {
                Serial.printf("Some thing went wrong trying to open the file...\r\n");
            }
            else {
                int s = f.size();
                Serial.printf("Size=%d\r\n", s);
                String data = f.readString();
                Serial.printf("%s\r\n", data.c_str());
                f.close();
            }
        }
        else {
            Serial.printf("No such file found.\r\n");
        }
    }
}


void webServerSetup(){

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    events.onConnect([](AsyncEventSourceClient *client){
        client->send("hello!",NULL,millis(),1000);
    });

    server.addHandler(&events);

    //if(sdInitialized){
        server.addHandler(new AsyncStaticSDWebHandler("/", SD, "/", "max-age=1"));
    //}else{
    //    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");
    //}
    
    server.onNotFound([](AsyncWebServerRequest *request){
        Serial.printf("NOT_FOUND: ");
        if(request->method() == HTTP_GET)
        Serial.printf("GET");
        else if(request->method() == HTTP_POST)
        Serial.printf("POST");
        else if(request->method() == HTTP_DELETE)
        Serial.printf("DELETE");
        else if(request->method() == HTTP_PUT)
        Serial.printf("PUT");
        else if(request->method() == HTTP_PATCH)
        Serial.printf("PATCH");
        else if(request->method() == HTTP_HEAD)
        Serial.printf("HEAD");
        else if(request->method() == HTTP_OPTIONS)
        Serial.printf("OPTIONS");
        else
        Serial.printf("UNKNOWN");
        Serial.printf(" http://%s%s\r\n", request->host().c_str(), request->url().c_str());

        if(request->contentLength()){
            Serial.printf("_CONTENT_TYPE: %s\r\n", request->contentType().c_str());
            Serial.printf("_CONTENT_LENGTH: %u\r\n", request->contentLength());
        }

        int headers = request->headers();
        int i;
        for(i=0;i<headers;i++){
            AsyncWebHeader* h = request->getHeader(i);
            Serial.printf("_HEADER[%s]: %s\r\n", h->name().c_str(), h->value().c_str());
        }

        int params = request->params();
        for(i=0;i<params;i++){
            AsyncWebParameter* p = request->getParam(i);
            if(p->isFile()){
                Serial.printf("_FILE[%s]: %s, size: %u\r\n", p->name().c_str(), p->value().c_str(), p->size());
            } else if(p->isPost()){
                Serial.printf("_POST[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
            } else {
                Serial.printf("_GET[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
            }
        }

        request->send(404);
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        if(!index)
        Serial.printf("BodyStart: %u\r\n", total);
        Serial.printf("%s", (const char*)data);
        if(index + len == total)
        Serial.printf("BodyEnd: %u\r\n", total);
    });

    server.begin();

}


// -------------------------------------------------


uint8_t isRecording = false;
File recording;  // the file we will save our recording to

#define RECBUFFSIZE 128  // 64 or 128 bytes.
uint8_t recording_buffer[RECBUFFSIZE];


void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void deleteOggFiles(void) {
  File dir= SD.open("/");;
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < 1; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, 2);
    } else {
      if(strstr(entry.name(),".OGG") > 0) {
        SD.remove(entry.name());
      }
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}


uint16_t saveRecordedData(boolean isrecord) {
  uint16_t written = 0;
  
    // read how many words are waiting for us
  uint16_t wordswaiting = player.recordedWordsWaiting();
  
  // try to process 256 words (512 bytes) at a time, for best speed
  while (wordswaiting > 256) {
    //Serial.print("Waiting: "); Serial.println(wordswaiting);
    // for example 128 bytes x 4 loops = 512 bytes
    for (int x=0; x < 512/RECBUFFSIZE; x++) {
      // fill the buffer!
      for (uint16_t addr=0; addr < RECBUFFSIZE; addr+=2) {
        uint16_t t = player.recordedReadWord();
        //Serial.println(t, HEX);
        recording_buffer[addr] = t >> 8; 
        recording_buffer[addr+1] = t;
      }
      if (! recording.write(recording_buffer, RECBUFFSIZE)) {
            Serial.print("Couldn't write "); Serial.println(RECBUFFSIZE); 
            while (1);
      }
    }
    // flush 512 bytes at a time
    recording.flush();
    written += 256;
    wordswaiting -= 256;
  }
  
  wordswaiting = player.recordedWordsWaiting();
  if (!isrecord) {
    Serial.print(wordswaiting); 
    Serial.println(" remaining");
    // wrapping up the recording!
    uint16_t addr = 0;
    for (int x=0; x < wordswaiting-1; x++) {
      // fill the buffer!
      uint16_t t = player.recordedReadWord();
      recording_buffer[addr] = t >> 8; 
      recording_buffer[addr+1] = t;
      if (addr > RECBUFFSIZE) {
          if (! recording.write(recording_buffer, RECBUFFSIZE)) {
                Serial.println("Couldn't write!");
                while (1);
          }
          recording.flush();
          addr = 0;
      }
    }
    if (addr != 0) {
      if (!recording.write(recording_buffer, addr)) {
        Serial.println("Couldn't write!"); while (1);
      }
      written += addr;
    }
    player.read_register(SCI_AICTRL3);
    if (! (player.read_register(SCI_AICTRL3) & (1 << 2))) {
       recording.write(player.recordedReadWord() & 0xFF);
       written++;
    }
    recording.flush();
  }
  return written;
}

void testdrawchar(void) {
  display.clearDisplay();
  display.setTextSize(2); 
  display.setCursor(0, 0);   
  for(int16_t i=0; i<256; i++) {
    if(i == '\n') display.write(' ');
    else          display.write(i);
  }
  display.display();
  display.setTextSize(2); 
}

unsigned int lastUpdate=0, lastLevel=0;

void setup() {
  Serial.begin(115200);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(2);  
  display.setTextColor(WHITE); 
  display.cp437(true);  
  pinMode(REC_BUTTON, INPUT_PULLUP);
  // initialize SPI
  SPI.begin();
  Serial.println("Hello VS1053!\n");

  testdrawchar();

  delay(1000);

  Serial.print ("Init SD card... ");  // init SD card (FAT32)
  if (!SD.begin(SD_CS)) {
    Serial.println("failed!");
    display.clearDisplay();
    display.setCursor(0, 0);   
    display.println("SD FAIL");
    display.display();
    while(1);
  } else {
    Serial.println("done.");
    sdInitialized = 1;
  }
  if(digitalRead(REC_BUTTON)==0) {
    Serial.println("Deleting OGG files.");
    display.clearDisplay();
    display.setCursor(0, 0); 
    display.println("Deleting");  
    display.println("all .OGG");
    display.display();
    deleteOggFiles();
    delay(2000);
  }
  File root = SD.open("/");
  printDirectory(root, 0);

  btStop(); //stop bluetooth as it may cause memory issues
  wifiSetup();
  spiffsSetup();
  webServerSetup();

  // initialize a player
  player.begin();
  player.switchToMp3Mode(); // optional, some boards require this
  player.setVolume(VOLUME);

  Serial.println("done!");
  display.clearDisplay();
  display.setCursor(0, 0);   
  display.println("READY");
  display.display();

  //disableCore0WDT();
  //disableCore1WDT();
}

uint16_t sci_aictrl0;
const unsigned short linToDBTab[5] = {36781,41285,46341,52016,58386};

unsigned short LinToDB(unsigned short n) { 
  int res =  96, i;
  if (!n) { /* No signal should return minus infinity */
    return 0;
  }
  while(n < 32768U) { /* Amplify weak signals */
    res -= 6;
    n <<= 1;
  }
  for (i=0; i<5; i++) { /* Find exact scale */
    if (n >=   linToDBTab[i]) { 
      res++;
    }
  }  
  return res;
}



void loop() {
  
  if (!isRecording && !digitalRead(REC_BUTTON)) {

    Serial.println("Begin recording");
    display.clearDisplay();
    display.setCursor(0, 0);   
    display.println("RECORD");
    display.display();
    if (! player.prepareRecordSpiffs("/v44k2q05.img")) {
    //if (! player.prepareRecordOgg("/v44k2q05.img")) {
      Serial.println("Couldn't load plugin!");
      display.clearDisplay();
      display.setCursor(0, 0);   
      display.println(F("plugin")); 
      display.println("error");
      display.display();
      while (1);    
    }
    isRecording = true;
    char filename[15];
    strcpy(filename, "/RECORD00.OGG"); // Check if the file exists already
    for (uint8_t i = 0; i < 100; i++) {
      filename[7] = '0' + i/10;
      filename[8] = '0' + i%10;
      if (! SD.exists(filename)) { // create if does not exist, 
        break;                     // do not open existing, write, sync after write
      }
    }
    Serial.print("Recording to "); 
    Serial.println(filename);  
    display.setTextSize(5); 
    display.setCursor(0, 22);  
    display.print(filename[7]);
    display.println(filename[8]);
    display.setTextSize(1); 
    //display.fillCircle(85, 24, 15, WHITE);
    display.setTextSize(2); 
    display.drawRect(96,0,15,48,WHITE);
    display.drawRect(113,0,15,48,WHITE);    
    display.display();
    recording = SD.open(filename, FILE_WRITE);
    if (! recording) {
      Serial.println("Couldn't open file to record!");
      display.clearDisplay();
      display.setCursor(0, 0);   
      display.println(F("File open"));   
      display.println("error :(~~~");
      display.display();
      while (1);
    }
    player.startRecordOgg(useMic); // microphone = true,  linein = false
    lastUpdate = millis();
    lastLevel = lastUpdate;
    player.startLevel();
  }
  if (isRecording) {
    saveRecordedData(isRecording);
    if(lastUpdate < millis() - 1000) {
      lastUpdate = millis();
      display.fillRect(64,48,63,16,BLACK);
      display.setCursor(68, 50); 
      uint32_t rectime=player.recordingTime();
      display.print(rectime/60);
      display.print(":");
      display.printf("%02d",rectime % 60);
      display.display();
    }
    
    if(lastLevel < millis() - 100) {
      sci_aictrl0 = player.read_register( 0xC ); // 0xC = SCI_AICTRL0
      if( (sci_aictrl0 & 0x8080) == 0) {
        Serial.println(sci_aictrl0, HEX);
        uint16_t leftLevel = LinToDB(sci_aictrl0 & 0x7F00);
        uint16_t rightLevel = LinToDB((sci_aictrl0 & 0x7F) * 256);
        display.fillRect(97,1,13,46,BLACK);
        display.fillRect(114,1,13,46,BLACK);
        display.drawLine(96,8,110,8,WHITE);
        display.drawLine(96,16,110,16,WHITE);
        display.drawLine(113,8,127,8,WHITE);
        display.drawLine(113,16,127,16,WHITE);        
        if(leftLevel > 56) {
          uint16_t leftY=102-leftLevel;
          display.fillRect(96,leftY,15,48-leftY,WHITE);
        }
        if(rightLevel > 56) {
          uint16_t rightY=102-leftLevel;
          display.fillRect(113,rightY,15,48-rightY,WHITE);
        }
        display.display();
        lastLevel = millis();
        player.startLevel();
      }
    } 
  }
  if (isRecording && !digitalRead(REC_BUTTON)) {
    Serial.println("End recording");
    player.stopRecordOgg();
    isRecording = false;
    saveRecordedData(isRecording); // flush all the data!
    recording.close();     // close it up
    delay(500);
    display.clearDisplay();
    display.setCursor(0, 0);   
    display.println("READY");
    display.display();
  } 
}
