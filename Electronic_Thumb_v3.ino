/*
 * Electronic_Thumb by Marco Saarloos
 * 
 * Code for the Electronic Thumb from HitchHiker's Guide To The Galaxy
 * Sending out sub-ether signals with a rather low range to flag down nearby spaceships
 * also providing an interfaces to some services on planet surfaces and other useful data
 * and implementing the thumb grid network to locate other electronic thumbs
 * 
 * *********************************************************************************
This program is free software: you can redistribute it and/or modify
it under the terms of the version 3 GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <BMP180.h>

#define OLED_RESET LED_BUILTIN  //Screen is started without reset connected, just giving it some harmless value
#define EncA 12 //pin numbers where the rotary encoder is attached to. rotary encoder has 3 pins on one side, and two on the other
#define EncB 14 //hook up the middle pin on the 3 pin side and one of the pins of the two pin side to ground 
#define Btn 9 //the other pins on the 3 pin side go to the EncA and EncB pins, the other pin on the 2 pin side to Btn
#define timeout 20 //minimum value in ms to register keypress, used for debouncing
#define NUMSTARS 3 //number of stars that flash through the screen
#define STARLIFESPAN 50 //number of frames that a star is 'alive'
#define NUMPLANETS 5 //number of planets we're checking for/have code for
#define PLANETDATAINSUFFICIENT 4 //planets with an index larger than this will never give a valid match on conditions. 

Adafruit_SSD1306 display(OLED_RESET);
TinyGPSPlus gps;
BMP180 pressure;
volatile signed int EncPos=0; //value of the encoder position and button are changed in interrupt service routines and need to be declared volatile
volatile boolean buttonpressed=false; 
const char* menuitem[]={"Planet surface","Interplanetary","Thumb Grid","Clock","BlaBlaCar","*"}; //items in scroll menu's in the user interface
const int itemsize[]={14,14,10,5,9}; //size of the items, used for centering the menu's in the screen
const char* planets[]={"Mercury","Venus","Earth","Mars","Kepler-186f","*"}; //the list of planets we have data on. Next lines are the data
const int mintemp[]={-184,380,-90,-150,-273}; 
const int maxtemp[]={800,500,70,35,1000};
const int minpres[]={0,4500000,92000,30,0};
const int maxpres[]={1,9300000,107000,1155,10000000};
const boolean hasgps[]={false,false,true,false,false};
const char* positives[]={"Could be","It might","This may be the one","A candidate we found","Verified","Agrees with data","Data insufficient","*"}; //responses when measurements agree with data
const char* negatives[]={"Think not","Nope","No match for profile","Do not bet on it","Alas!","No go","Is that a planet?","*"}; //last item in these arrays is used for planets with an index larger than PLANETDATAINSUFFICIENT
const char* subsystem[]={"Checking temperature","Measuring pressure","GPS Status","*"}; //headers for measurements
const char* tmonth[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec","*"};//month names
const char* tday[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat","*"};//day names. All char pointer arrays have a "*" element added to prevent out-of-bounds stuff
int currentselected=0;//used to identify which menu item is selected when a button is pressed
int mode[4];//used to browse the menu structure
int starlife[NUMSTARS]; //several variabeles and then some bitmaps to facilitate 'starts' shooting though the screen randomly
int starx[NUMSTARS];
int stary[NUMSTARS];
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

void setup() {
  Serial.begin(9600, SERIAL_8N1); //start the serial interface for the gps board
  Serial.swap(); //GPS is connected with tx only (we're not sending any data TO the gps module) on pin 13, the alternative UART0 rx pin on the ESP8266. Useful so the gps does not interfere with code uploads
  Serial1.begin(9600); //UART1 only has a tx port, all we need to enable some debugging output
  pinMode(EncA, INPUT_PULLUP); //pins for Encoder and Button are set up as inputs with pull-down resistors, and ISR's attached when the pin voltage level changes
  pinMode(EncB, INPUT_PULLUP);
  attachInterrupt(EncA,doEnc,CHANGE);
  pinMode(Btn, INPUT_PULLUP);
  attachInterrupt(Btn,doBtn,CHANGE);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //init the oled display
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Don't");
  display.println("Panic!");
  display.setTextSize(1);
  pressure = BMP180(); //init the pressure sensor object
  if (pressure.EnsureConnected()){
    display.println("BMP180 connected");
    display.display();
    pressure.SoftReset(); 
    pressure.Initialize(); //fetches calibration data from chip
  }else {
    display.println("BMP180 unreachable");
    display.display();
    while(1); // Pause forever. Check the pin connections for the SDA and SCL pins or check your Wire library if you get this error
  }
  delay(600); //gone before you know it.
}

void loop() {
  int menuitems=0;int menustart=0; //local variabeles for menu control
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
          //all these have no gps, only thing we can do is use the thumb grid to see if other thumbs are in wifi range
          doThumbGrid();
        break;
        case 3://Planet Earth. I know, it seems like all the good stuff is done at planet Earth but this is where we coded and this is the only place where this code was tested. So yeah, it is what it is
          switch(mode[2]){
            case 1://Thumb grid
              doThumbGrid();
            break;
            case 2://Clock
              doClock();
            break;
            case 3://BlaBlaCar
              doBlaBlaCar();
            break;
            default:  
              menustart=3;//set the menu control variabeles and define some action when the button is pressed
              menuitems=3;
              if(buttonpressed){
                mode[2]=currentselected+1;
                currentselected=0;
                buttonpressed=false;
              }
            break;
          }
          break;
        default:
          static int countsteps=0;
          //determine the planet we're on
          mode[1]=doFindPlanet(countsteps); //not a menu this time but a routine to try to find out which planet we're on. Again, this was only tested on Earth..
          countsteps++; //well this could all be done in an instant but what's the fun in that? So we have counter to have chance to display some useless funny stuff. Well it was meant to be funny. 
        break;
      }
      break;
    case 2:
      //interplanetary travel
      doSubEtherSignal();//You're right, this is the only thing the original HHGTTG Electronic Thumb was made for. And here it is, sitting at the bottom level of a sleazy menu structure. What do you know.
    break;
    default:
      menustart=1;//set the menu control variabeles and define some action when the button is pressed
      menuitems=2;
      if(buttonpressed){
        mode[0]=currentselected+1;
        currentselected=0;
        buttonpressed=false;
      }
    break;
  }
  yield(); //give that old ESP8266 a break. And a fighting chance to control the wifi stuff etc
  if(menuitems>0){ //display a menu if the menuitems variabele was set in the previous code
//    display.setTextColor(WHITE);
//    for(int i=0;i<(8-menuitems)/2;i++){
//      display.println(); //empty lines! Useful for centering stuff
//    }
    for(int i=menustart;i<menustart+menuitems;i++){
//      display.setTextColor(WHITE);
//      for(int j=0;j<(22-itemsize[i-1])/2;j++){
//        display.print(" "); //Spaces! Who could spell 'space ship' without them? Really useful for centering stuff on a display as well
//      }
      display.setCursor((128-(6*itemsize[i-1]))/2,((64-(8*menuitems))/2)+8*(i-menustart)); //on second thought, who needs empty lines and spaces when you can set the cursor wherever you want?
      if(i-menustart==currentselected){
        display.setTextColor(BLACK, WHITE); //fancy colors for the selected menu item
      }else{
        display.setTextColor(WHITE); 
      }
      display.println(menuitem[i-1]);
    }
    if(EncPos>1){
      currentselected=++currentselected % menuitems; //change the selected item to the next one when the encoder is spun clockwise. roll over to first item is there is no next one
      EncPos=0; 
    }else if(EncPos<-1){
      currentselected=(--currentselected+menuitems) % menuitems; //clockwise turns do just the opposite
      EncPos=0; 
    }
  }
  for(int i=0;i<NUMSTARS;i++){ //stars shooting though the screen to let you know there's still some code running. And we wanted to enhance the 'space' theme. How did that work out for you?
    if(starlife[i]==0){ //reset the star at the end of it's life
      starx[i]=random(128); //yep, all without calling a randomseed function. The ESP8266 is great like that
      stary[i]=random(64);  
      starlife[i]=random(STARLIFESPAN)+8; //we add just a little bit of life to be sure of a chance to get our bitmaps onscreen
    }else{
      starlife[i]--; // Some people spend their whole lives thinking about this stuff, writing thesises and all.. Sorry, I'm just having a philosophical moment here..
    }
    switch (starlife[i]){ //when you display bitmaps in quick succession, you really can't distinguish them from animation
      case 5:
        display.drawBitmap(starx[i],stary[i],star1_bmp,8,8,WHITE);
        break;
      case 4:
        display.drawBitmap(starx[i],stary[i],star2_bmp,8,8,WHITE);
        break;
      case 3:
        display.drawBitmap(starx[i],stary[i],star3_bmp,8,8,WHITE);
        break;
      case 2:
        display.drawBitmap(starx[i],stary[i],star4_bmp,8,8,WHITE);
        break;
      case 1:
        display.drawBitmap(starx[i],stary[i],star5_bmp,8,8,WHITE);
        break;
    }
  }
  while (Serial.available()){ //If the gps module has anything to say, it gets its chance to talk to the gps object here
    gps.encode(Serial.read()); //They talk NMEA between themselves.. Really hard to pick up girls with this shit.
    yield(); //Here's our ESP again, controlling wifi. Just in case it gets out of hand
  }
  display.display(); //we have another frame ready, put those pixels where they're supposed to go!
  yield(); //Did I mention that the ESP is really good at doing wifi?
}

void doEnc(){
  if(digitalRead(EncA)==digitalRead(EncB)){ //if the encoder is spun, both pins change voltage levels at different intervals. All we need to know is whether they have the same level or a different level when one of the pins changes
    EncPos++; //if the pins have the same level, we're spinning the encoder clockwise. So we increment the position value
  }else{
    EncPos--; //and decrement it when spinning counterclockwise
  }
}
void doBtn(){ //debouncing the button
  static unsigned long pressed;
  if(!digitalRead(Btn)){
    pressed=millis();//store time when button pulls pin low
  }else{ //determine duration when it's back to high
    unsigned long timepressed=millis()-pressed;
    if(timepressed>timeout){
      buttonpressed=true;
    }
  }
}
int doFindPlanet(int c){//routine to check measurements against data. c is a counter for the number of times this routine has been called, so we can use it to provide some user interaction on the display.
  static double p,t; 
  static byte matches;
  static int response;

  //first, do some measurements and show them on the display
  //we cycle through this loop several times, to give users a chance to read the information
  if(c==0){ // we get the measurements first time through the loop
    t=pressure.GetTemperature();
    p=pressure.GetPressure();
  }
  for(int i=0;i<(c/10)+1 && i<3;i++){ //first 30 loops display temperature, pressure and gps measurements and their headers
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
  if(c<30){
    for(int j=0;j<c%10;j++){
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
  if(c>30 && c<(NUMPLANETS*10)+30){ //10 loops/frames for each planet. Display the name and a line that indicates if it matches the measured data
    int planet=(c-30)/10;
    display.println(planets[planet]);
    if((c-30)%10==1){
      response=planet<PLANETDATAINSUFFICIENT?random(6):6; 
    }
    if((c-30)%10>4){
      if(t>mintemp[planet] && t<maxtemp[planet] && p>minpres[planet] && p<maxpres[planet] && gps.satellites.isValid()==hasgps[planet]){ //compare the data with the measurements
        display.println(positives[response]);
        matches|=(planet<PLANETDATAINSUFFICIENT?1:0)<<planet; //set a bit in 'matches' if the conditions measured match the values for the planet.   
      }else{
        display.println(negatives[response]);
      }
    }
  }else if (c>30){ //it's an else if, so it only gets executed after we've gone though all the planets
    int num_matches=0;
    int planet=0;
    for(int i=0;i<NUMPLANETS;i++){  //we count the number of matching planets we found first
      if(matches & 1<<i){
        num_matches++;
        planet=i;
      }
    }
    if(num_matches==0){ //and display information about it for 70 loops/frames afterwards
      display.println("No matching profile");
      display.println("Rebooting");
      if(c>(10*NUMPLANETS)+70){
        ESP.reset();
      }
    }else if (num_matches==1){
      display.println("Single match!");
      display.print("You're on ");
      display.println(planets[planet]);
      if(c>(10*NUMPLANETS)+70){
        return planet+1; //this sets the mode so next loop this routine will not be called anymore
      }
    }else{
      display.println("Data inconclusive");
      display.println("Rebooting");
      if(c>(10*NUMPLANETS)+70){
        ESP.reset();
      }
    }
  }
  return 0; //next loop will go though this routine again
}
void doThumbGrid(){
  static unsigned int c=0;
  int center_x,center_y,radius;
  //we put the ESP8266 in AP mode with a service identifier, (partial) chipid and if available latitude and longitude in the ssid
  //so that we can show if another thumb is in range, and using gps at which course and distance
  //display.println("Not implemented yet");
  display.println(c);
  drawCompass(false);
  drawArrow(radians(c),10,0,true);
  if(c%90==0)delay(1000);
  ++c%=360;

}
void doClock(){
  //Show a clock and some other interesting details on the screen
  static int frame;
  static double disp_pres;
  static double disp_temp;
  float temp=pressure.GetTemperature();
  float pres=pressure.GetPressure();
  TinyGPSDate d = gps.date;
  TinyGPSTime t = gps.time;
  
  if(gps.altitude.isValid()){ 
    disp_pres=(((pres/100.0)/pow(1-(gps.altitude.meters()/44330.0),5.255))+(1.0*min(19,frame)*disp_pres))/(1.0*min(20,frame+1)); //adjust the pressure for altitude so we get sea level pressure
  }
  disp_temp=(temp+(1.0*min(19,frame)*disp_temp))/(1.0*min(20,frame+1)); //also notice that we're displaying a moving average of the measurements, so the value remains fairly consistent
  
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  switch(frame/25){ //each 25 frames we display another data item. 
    case 0:  //calendar
      if(d.isValid()){
        display.println(tday[dotw(d.day(),d.month(),d.year())]);
        display.print(d.day()<10?"  ":" ");
        display.println(d.day());
        display.println(tmonth[d.month()-1]);
        display.println(d.year());
      }else{
        frame+=25;
      }
    break;
    case 1: //sea level pressure
      if(gps.altitude.isValid()){
        display.println();
        if(disp_pres<999.5){
          display.print(" ");
        }
        display.println(disp_pres,0);
        display.println("mbar");
      }else{
        frame+=25;
      }
    break;
    case 2: //latitude
    case 3: //longitude
      if(gps.location.isValid()){
        float fltLoc=(frame/25==2)?gps.location.lat():gps.location.lng();
        int intLoc=0;
        display.setTextSize(1);
        display.println((frame/25==2)?"latitude":"longitude");
        display.println(fltLoc,4);
        display.setTextSize(2);
        intLoc=abs(fltLoc);
        display.print(intLoc<10?"  ":(intLoc<100?" ":""));
        display.print(intLoc);
        display.write(167);
        display.println((frame/25==2)?(fltLoc<0?"S":"N"):(fltLoc<0?"E":"W"));
        intLoc=fltLoc;
        fltLoc-=intLoc;
        fltLoc*=60;
        intLoc=fltLoc;
        display.print(intLoc<10?"  ":" ");
        display.print(intLoc);
        display.write(39);
        display.println();
        fltLoc-=intLoc;
        fltLoc*=60;
        intLoc=fltLoc;
        display.print(intLoc<10?"  ":" ");
        display.print(intLoc);
        display.write(34);
      }else{
        frame+=25;
      }
    break;
    case 4: //temperature. I find that my BMP180 reports temperature a few degrees above ambient
      display.println();
      display.print(disp_temp>=0?" ":"");
      display.print(abs(disp_temp)<9.5?" ":"");
      display.println(disp_temp,1);
      display.print("   ");
      display.write(167);
      display.println("C");
    break;  
  }
  ++frame%=125; //increment the frame counter and roll over if it's over 125;
  if(t.isValid()){ //draw a clock if we have a good time source
    drawCompass(true);
    drawArrow((double)TWO_PI*((t.hour()/12.0)+(t.minute()/720.0)),4,display.height()/4,true);
    drawArrow((double)TWO_PI*((t.minute()/60.0)+(t.second()/3600.0)),3,0,true);
    drawArrow((double)TWO_PI*((t.second()/60.0)+(t.centisecond()/6000.0)),3,0,false);
  }
}
int dotw(int d,int m, int y){
  return (d+=m<3?y--:y-2,23*m/9+d+4+y/4-y/100+y/400)%7;
}
void drawCompass(boolean isClockface){
  //draws a compass rose or clock face on the right side of the screen.
  int x,y,x0,y0,x1,y1; //coordinates of the center and radius of the circle
  double a=0.0; //the arc between markings
  x=display.width()*3/4;
  y=display.height()/2;
  display.drawCircle(x,y,y-1,WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x-(isClockface?6:2),2);
  display.print(isClockface?"12":"n");
  display.setCursor(x-2,display.height()-10);
  display.print(isClockface?"6":"s");
  display.setCursor((display.width()/2)+4,(display.height()/2)-4);
  display.print(isClockface?"9":"w");
  display.setCursor(display.width()-8,(display.height()/2)-4);
  display.print(isClockface?"3":"e");
  while(a<TWO_PI){
    x0=x+((y-1)*sin(a));
    y0=y-((y-1)*cos(a));
    x1=x+((y-5)*sin(a));
    y1=y-((y-5)*cos(a));
    display.drawLine(x0,y0,x1,y1,WHITE);
    a+=PI/(isClockface?6:8);
  }
}
void drawArrow(double a,int d, int r,boolean isClockhand){//draw an arrow or clock hand under angle a (in radians) with radius r. d is half the length of the 'crossbar' part perpendicular to the shaft
  int x,y,x0,y0,x1,y1,dx,dy;
  x=display.width()*3/4;//the origin point of the arrow/clockhand
  y=display.height()/2;
  if(r==0)r=y-1; //default size if r is omitted
//  if(r<0)r=y+r; //if radius is negative shorten the radius by that much
  x0=x+(r*sin(a)); //position of the tip of the arrow
  y0=y-(r*cos(a));
  display.drawLine(x,y,x0,y0,WHITE); //draw the shaft of the arrow
  x1=isClockhand?x:x+((r-d)*sin(a));
  y1=isClockhand?y:y-((r-d)*cos(a)); 
  dx=d*cos(a);
  dy=d*sin(a);
  display.fillTriangle(x0,y0,x1+dx,y1+dy,x1-dx,y1-dy,WHITE); //draw the arrowhead
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

