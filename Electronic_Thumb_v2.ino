#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <BMP180.h>

#define OLED_RESET LED_BUILTIN  //4
#define EncA 12
#define EncB 14
#define Btn 9
#define timeout_long 5000
#define timeout 20
#define NUMSTARS 3
#define STARLIFESPAN 50

static int default_sda_pin = 0;
static int default_scl_pin = 2;

Adafruit_SSD1306 display(OLED_RESET);
TinyGPSPlus gps;
BMP180 pressure;
volatile signed int EncPos=0;
volatile boolean buttonpressed=false;
int currentselected=0;
const char* menuitem[]={"Planetary","Interplanetary","Thumb Grid","Clock","BlaBlaCar","*"};
int itemsize[]={9,14,10,5,9};
const char* planets[]={"Mercury","Venus","Earth","Mars","Kepler-186f","*"};
const int mintemp[]={-184,380,-90,-150,-273};
const int maxtemp[]={800,500,70,35,1000};
const int minpres[]={0,4500000,92000,30,0};
const int maxpres[]={1,9300000,107000,1155,10000000};
const boolean hasgps[]={false,false,true,false,false};
const char* positives[]={"Could be","It might","This may be the one","A candidate we found","Verified","Agrees with data","*"};
const char* negatives[]={"Think not","Nope","No match for profile","Do not bet on it","Alas!","No go","*"};
const char* subsystem[]={"Checking temperature","Measuring pressure","GPS Status","*"};

int mode[4];

static const unsigned char star1_bmp[]={
  B00000000,
  B00000000,
  B00000000,
  B00000000,
  B00001000,
  B00000000,
  B00000000,
  B00000000,
};
static const unsigned char star2_bmp[]={
  B00000000,
  B00000000,
  B00000000,
  B00001000,
  B00010100,
  B00001000,
  B00000000,
  B00000000,
};
static const unsigned char star3_bmp[]={
  B00000000,
  B00000000,
  B00001000,
  B00010100,
  B00101010,
  B00010100,
  B00001000,
  B00000000,
};
static const unsigned char star4_bmp[]={
  B00000000,
  B00001000,
  B00001000,
  B00011100,
  B01111111,
  B00011100,
  B00001000,
  B00001000,
};
static const unsigned char star5_bmp[]={
  B00000000,
  B00000000,
  B00000000,
  B00001000,
  B00011100,
  B00001000,
  B00000000,
  B00000000,
};
int starlife[NUMSTARS];
int starx[NUMSTARS];
int stary[NUMSTARS];

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void setup() {
  
  Serial.begin(9600, SERIAL_8N1);
  Serial.swap();
  Serial1.begin(9600);
  pinMode(EncA, INPUT_PULLUP);
  pinMode(EncB, INPUT_PULLUP);
  attachInterrupt(EncA,doEnc,CHANGE);
  pinMode(Btn, INPUT_PULLUP);
  attachInterrupt(Btn,doBtn,CHANGE);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  // Clear the buffer.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  pressure = BMP180();
  if (pressure.EnsureConnected()){
    display.println("BMP180 connected");
    display.display();
    pressure.SoftReset();
    pressure.Initialize();
  }else {
    display.println("BMP180 unreachable");
    display.display();
    while(1); // Pause forever.
  }
  delay(200);
}

void loop() {
  int menuitems=0;int menustart=0;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  switch (mode[0]){ //choose planetary or interplanetary travel
    case 1: //planetary travel
      switch(mode[1]){
        case 1://Planet Mercurius
        case 2://Planet Venus
        case 4://Planet Mars
        case 5://Planet Kepler-186f
          //all these have no gps, so we can only use the degraded version of the thumb grid
          doThumbGrid(gps.satellites.isValid());
          break;
        case 3://Planet Earth
          switch(mode[2]){
            case 1://Thumb grid
              doThumbGrid(gps.satellites.isValid());
              break;
            case 2://Clock
              doClock();
              break;
            case 3://BlaBlaCar
              doBlaBlaCar();
              break;
            default:  
              menustart=3;
              menuitems=3;
             if(buttonpressed){
               mode[2]=currentselected+1;
               buttonpressed=false;
             }
          }
          break;
        default:
          //determine the planet we're on
          static long countsteps=0;
          static double p,t;
          static byte matches;
          static int response;
          int planet;
          boolean foundplanet;

          //first, do some measurements and show them on the display
          //we cycle through this loop several times, to give users a chance to read the information
          if(countsteps==0){ // we get the measurements first time through the loop
            t=pressure.GetTemperature();
            p=pressure.GetPressure();
          }
          for(int i=0;i<(countsteps/10)+1 && i<3;i++){
            if(i==1){
              display.print (t,2);
              display.write(167);
              display.println( "C");
            }else if(i==2){
              display.print (p,2);
              display.println( " Pa");
            }
            display.println(subsystem[i]);
          }
          if(countsteps<30){
            for(int j=0;j<countsteps%10;j++){
              display.print(".");
            }
          }else{
            if(gps.satellites.isValid()){
              display.print(gps.satellites.value());
              display.println(" satellites seen");
            }else{
              display.println("no gps detected");
            }
            
          }
          //determine which planet we're on, based on temperature, pressure and whether we get a GPS signal
          if(countsteps>=30 && countsteps<80){
            planet=(countsteps-30)/10;
            display.println(planets[planet]);
            if((countsteps-30)%10==0){
              response=random(5);
            }
            if((countsteps-30)%10>3){
              if(planet<4){ //we don't have enough data on planets with a higher index to validate a match
                if(t>mintemp[planet] && t<maxtemp[planet] && p>minpres[planet] && p<maxpres[planet] && gps.satellites.isValid()==hasgps[planet]){
                  display.println(positives[response]);
                  matches|=1<<planet; //set a bit in a value if the conditions measured match the values for the planet
                }else{
                  display.println(negatives[response]);
                }
              }else{
                if(t>mintemp[planet] && t<maxtemp[planet] && p>minpres[planet] && p<maxpres[planet] && gps.satellites.isValid()==hasgps[planet]){
                  display.println("Data insufficient");
                  matches|=0<<planet; //too much?
                } else{
                  display.println("Is that a planet?");
                }
              }
            }
          }else if (countsteps>80){
            if(matches==0){
              display.println("No matching profile");
              display.println("Rebooting");
            }else if (((matches & B00001)?1:0 + (matches & B00010)?1:0 + (matches & B00100)?1:0 + (matches & B01000)?1:0 + (matches & B10000)?1:0)==1){
              display.println("Single match!");
              display.print("You're on ");
              planet=log(matches)/log(2);
              foundplanet=true;
              display.println(planets[planet]);
            }else{
              display.println("Data inconclusive");
              display.println("Rebooting");
            }
          }
          if(countsteps>120 && foundplanet){
            mode[1]=planet+1;
          }else if (countsteps>120){
            ESP.reset();
          }
          countsteps+=1;
          break;
      }
      break;
    case 2:
      //interplanetary travel
      doSubEtherSignal();
      break;
    default:
      menustart=1;
      menuitems=2;
      if(buttonpressed){
        mode[0]=currentselected+1;
        buttonpressed=false;
      }
      break;
  }
  yield();
  if(menuitems>0){ //display a menu
    display.setTextColor(WHITE);
    for(int i=0;i<(8-menuitems)/2;i++){
      display.println();
    }
    for(int i=menustart;i<menustart+menuitems;i++){
      display.setTextColor(WHITE);
      for(int j=0;j<(22-itemsize[i-1])/2;j++){
        display.print(" ");
      }
      if(i-menustart==currentselected){
        display.setTextColor(BLACK, WHITE);
      }else{
        display.setTextColor(WHITE);
      }
      display.println(menuitem[i-1]);
    }
    if(EncPos>1) currentselected=++currentselected % menuitems;
    if(EncPos<-1) currentselected=(--currentselected+menuitems) % menuitems;
    EncPos=0;
  }
  for(int i=0;i<NUMSTARS;i++){
    if(starlife[i]>=random(STARLIFESPAN)+8){
    starx[i]=random(128);
    stary[i]=random(64);
    starlife[i]=0;
    }else{
      starlife[i]+=1;
    }
    switch (starlife[i]){
      case 0:
        display.drawBitmap(starx[i],stary[i],star1_bmp,8,8,WHITE);
        break;
      case 1:
        display.drawBitmap(starx[i],stary[i],star2_bmp,8,8,WHITE);
        break;
      case 2:
        display.drawBitmap(starx[i],stary[i],star3_bmp,8,8,WHITE);
        break;
      case 3:
        display.drawBitmap(starx[i],stary[i],star4_bmp,8,8,WHITE);
        break;
      case 4:
        display.drawBitmap(starx[i],stary[i],star5_bmp,8,8,WHITE);
        break;
    }
  }
  while (Serial.available()){
    gps.encode(Serial.read());
    yield();
  }
  display.display();
  yield();
}

void doEnc(){
  if(digitalRead(EncA)==digitalRead(EncB)){
    EncPos++;
  }else{
    EncPos--;
  }
}
void doBtn(){
  static unsigned long pressed;
  if(!digitalRead(Btn)){
    pressed=millis();//store time when button pulls pin low
  }else{ //determine duration when it's back to high
    unsigned long timepressed=millis()-pressed;
    if(timepressed>timeout_long) { //really long pressing the button resets the device
      ESP.reset();
    }else if(timepressed>timeout){
      buttonpressed=true;
    }
  }
}
void doThumbGrid(boolean gpsstatus){
  //we put the ESP8266 in AP mode with a service identifier, (partial) chipid and if available latitude and longitude in the ssid
  //so that we can show if another thumb is in range, and using gps at which course and distance
  display.println("Not implemented yet");

}
void doClock(){
  //Show a clock and some other interesting details on the screen
  //for now, we'll just display the current UTC time
  TinyGPSDate d = gps.date;
  TinyGPSTime t = gps.time;
  if(d.isValid()){
    display.print(d.day());
    display.print("-");
    display.print(d.month());
    display.print("-");
    display.println(d.year());
  }
  if(t.isValid()){
    display.print(t.hour());
    display.print(":");
    display.print(t.minute());
    display.print(":");
    display.println(t.second());
  }
}
void doBlaBlaCar(){
  //not implemented yet
  display.println("Not implemented yet");
}
void doSubEtherSignal(){
  //put out a signal on the sub-ether to flag down spaceships
  //we found that the signal required is a 433 mhz signal, and curiously it happens to influence some electrical equipment on planet Earth
  //basically, the signal is either turning all remote controlled outlets and lights in the area either on or off - we're not sure what setting to use so we'll let the user decide.
  display.println("Not implemented yet");
}

