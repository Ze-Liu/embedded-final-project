// Main.c
// Runs on LM4F120/TM4C123
// You may use, edit, run or distribute this file 
// You are free to change the syntax/organization of this file

// Jonathan W. Valvano 2/20/17, valvano@mail.utexas.edu
// Modified by Sile Shu 10/4/17, ss5de@virginia.edu
// Modified by Mustafa Hotaki 7/29/18, mkh3cf@virginia.edu

#include <stdint.h>
#include "OS.h"
#include "tm4c123gh6pm.h"
#include "LCD.h"
#include <string.h> 
#include "UART.h"
#include "FIFO.h"
#include "joystick.h"
#include "PORTE.h"

// Constants
#define BGCOLOR     					LCD_BLACK
#define CROSSSIZE            	5
#define PERIOD               	4000000   // DAS 20Hz sampling period in system time units
#define PSEUDOPERIOD         	8000000
#define LIFETIME             	1000
#define RUNLENGTH            	600 // 30 seconds run length
#define BULLETMAX							100
#define ENEMYMAX							10
#define BULLETWIDTH						1
#define BULLETHEIGHT					5
#define LCDWIDTH							128
#define LCDHEIGHT							128
#define BULLETSPEED						6
#define TRAININGWIDTH					17
#define TRAININGHEIGHT				7

#define tx
//#define rx

enum faction {
	null,
	self,
	enemy,
	used
};
typedef enum faction FacType;

enum enemytype {
	enull,
	training,
	doomed
};
typedef enum enemytype EnemyType;

struct enemies {
	int16_t x;
	int16_t y;
	EnemyType type;
};
typedef struct enemies Enemy;
int8_t enemyTest;
Enemy enemies[ENEMYMAX];
int16_t enemyNum;

struct bullet{
	int16_t x;
	int16_t y;
	FacType dir;										//bullet direction, self:forward; enemy:backward
};
typedef struct bullet bulletType;
bulletType bullets[BULLETMAX];
bulletType previousBullets[BULLETMAX];
int16_t bulletNum;								//number of bullets

extern Sema4Type LCDFree;
uint16_t origin[2]; 	// The original ADC value of x,y if the joystick is not touched, used as reference
int16_t x = 63;  			// horizontal position of the crosshair, initially 63
int16_t y = 63;  			// vertical position of the crosshair, initially 63
int16_t prevx, prevy;	// Previous x and y values of the crosshair
uint8_t select;  			// joystick push
uint8_t area[2];
uint32_t PseudoCount;
uint32_t test1;
uint32_t score;
uint32_t uart1testtx;
uint32_t uart1testrx;

unsigned long NumCreated;   		// Number of foreground threads created
unsigned long NumSamples;   		// Incremented every ADC sample, in Producer
unsigned long UpdateWork;   		// Incremented every update on position values
unsigned long Calculation;  		// Incremented every cube number calculation
unsigned long DisplayCount; 		// Incremented every time the Display thread prints on LCD 
unsigned long ConsumerCount;		// Incremented every time the Consumer thread prints on LCD
unsigned long Button1RespTime; 	// Latency for Task 2 = Time between button1 push and response on LCD 
unsigned long Button2RespTime; 	// Latency for Task 7 = Time between button2 push and response on LCD
unsigned long Button1PushTime; 	// Time stamp for when button 1 was pushed
unsigned long Button2PushTime; 	// Time stamp for when button 2 was pushed

//---------------------User debugging-----------------------
unsigned long DataLost;     // data sent by Producer, but not received by Consumer
long MaxJitter;             // largest time jitter between interrupts in usec
#define JITTERSIZE 64
unsigned long const JitterSize=JITTERSIZE;
unsigned long JitterHistogram[JITTERSIZE]={0,};
unsigned long TotalWithI1;
unsigned short MaxWithI1;

//Add bullet to bullets array
//Return 1 if successfully added
//Return 0 if array is full
int16_t addBullet(int16_t bulx, int16_t buly, FacType dir) {
	int16_t i;
	bulletType temBullet;
	temBullet.x = bulx;
	temBullet.y = buly;
	temBullet.dir = dir;
	for(i = 0;i < BULLETMAX;i ++) {
		if(bullets[i].dir == null) {
			bullets[i] = temBullet;
			bulletNum++;
			return 1;
		}
	}
	return 0;
}

int16_t deleteBullet(int16_t num) {
	bulletType temBullet = {0,0,null};
	bullets[num] = temBullet;
	bulletNum--;
	return 1;
}

void drawBullet(uint16_t color) {
	int16_t i;
	int16_t curBullet = 0;
	for(i = 0;curBullet < bulletNum;i ++) {
		if(bullets[i].dir != null) {
			curBullet ++;
			BSP_LCD_DrawFastVLine(previousBullets[i].x, previousBullets[i].y, BULLETHEIGHT, LCD_BLACK);
			if(bullets[i].dir != used) {
				BSP_LCD_DrawFastVLine(bullets[i].x, bullets[i].y, BULLETHEIGHT, color);
			}
		}
	}
}

int16_t addEnemy(int16_t enex, int16_t eney, EnemyType t) {
	int16_t i;
	Enemy tem;
	tem.x = enex;
	tem.y = eney;
	tem.type = t;
	for(i = 0;i < ENEMYMAX;i ++) {
		if(enemies[i].type == enull) {
			enemies[i] = tem;
			enemyNum++;
			return 1;
		}
	}
	return 0;
}

int16_t deleteEnemy(int16_t num) {
	Enemy tem = {0,0,enull};
	enemies[num] = tem;
	enemyNum--;
	return 1;
}

void drawBlack(Enemy e) {
	BSP_LCD_FillRect(e.x, e.y, TRAININGWIDTH, TRAININGHEIGHT, LCD_BLACK);
}

void drawEnemy(uint16_t color) {
	int16_t i;
	for(i = 0;i < ENEMYMAX;i ++) {
		if(enemies[i].type != enull) {
			BSP_LCD_FillRect(enemies[i].x, enemies[i].y, TRAININGWIDTH, TRAININGHEIGHT, LCD_BLACK);
			if(enemies[i].type != doomed) {
				BSP_LCD_FillRect(enemies[i].x, enemies[i].y, TRAININGWIDTH, TRAININGHEIGHT, color);
			}
		}
	}
}

void generateEnemy(void){
	unsigned long time;
	int16_t xpos;
	while(1){
		if(enemyNum == 0) {
			time = OS_Time();
			xpos = time % (LCDWIDTH - TRAININGWIDTH);
			addEnemy(xpos,7,training);
		}
	}
  //OS_Kill();  // done
}

int8_t trainingJudge(Enemy e, bulletType b) {
	if((b.x >= e.x && b.x <= e.x + TRAININGWIDTH) && 
		(b.y <= (e.y + TRAININGHEIGHT + (BULLETHEIGHT/2)) && b.y >= (e.y - (BULLETHEIGHT/2)))) {
			return 1;
		}
	return 0;
}

void Device_Init(void){
	UART_Init();
	UART1_Init();
	BSP_LCD_OutputInit();
	BSP_Joystick_Init();
}
//------------------Task 1--------------------------------
// background thread executed at 20 Hz
//******** Producer *************** 
int UpdatePosition(uint16_t rawx, uint16_t rawy, jsDataType* data){
	if (rawx > origin[0]){
		x = x + ((rawx - origin[0]) >> 9);
	}
	else{
		x = x - ((origin[0] - rawx) >> 9);
	}
	if (rawy < origin[1]){
		y = y + ((origin[1] - rawy) >> 9);
	}
	else{
		y = y - ((rawy - origin[1]) >> 9);
	}
	if (x > 127){
		x = 127;}
	if (x < 0){
		x = 0;}
	if (y > 112 - CROSSSIZE){
		y = 112 - CROSSSIZE;}
	if (y < 0){
		y = 0;}
	data->x = x; data->y = y;
	return 1;
}

void Producer(void){
	uint16_t rawX,rawY; // raw adc value
	uint8_t select;
	jsDataType data;
	uint16_t curBullet = 0;
	int16_t i = 0, j = 0;
	unsigned static long LastTime;  // time at previous ADC sample
	unsigned long thisTime;         // time at current ADC sample
	long jitter;                    // time between measured and expected, in us
	//if (NumSamples < RUNLENGTH){
		BSP_Joystick_Input(&rawX,&rawY,&select);
		thisTime = OS_Time();       // current time, 12.5 ns
		UpdateWork += UpdatePosition(rawX,rawY,&data); // calculation work
		NumSamples++;               // number of samples
		if(JsFifo_Put(data) == 0){ // send to consumer
			DataLost++;
		//}
	//calculate jitter
		if(UpdateWork > 1){    // ignore timing of first interrupt
			unsigned long diff = OS_TimeDifference(LastTime,thisTime);
			if(diff > PERIOD){
				jitter = (diff-PERIOD+4)/8;  // in 0.1 usec
			}
			else{
				jitter = (PERIOD-diff+4)/8;  // in 0.1 usec
			}
			if(jitter > MaxJitter){
				MaxJitter = jitter; // in usec
			}       // jitter should be 0
			if(jitter >= JitterSize){
				jitter = JITTERSIZE-1;
			}
			JitterHistogram[jitter]++; 
		}
		LastTime = thisTime;

	}
	//calculate bullets
		for(i = 0,curBullet = 0;curBullet < bulletNum && i < BULLETMAX;i ++) {
			if(bullets[i].dir == used) {
					deleteBullet(i);
			}
			if(bullets[i].dir == self) {
				curBullet ++;
				previousBullets[i] = bullets[i];
				bullets[i].y -= BULLETSPEED;
				if(bullets[i].y <= ((BULLETHEIGHT + 1)/2)) {
					//previousBullets[i] = bullets[i];
					bullets[i].dir = used;
				}
				for(j = 0;j < ENEMYMAX;j ++) {
					if(enemies[j].type != enull) {
						if(enemies[j].type == doomed) {
							deleteEnemy(j);
						}
						if(trainingJudge(enemies[j], bullets[i])) {
							enemies[j].type = doomed;
							score ++;
							bullets[i].dir = used;
						}
					}
				}
			}
			if(bullets[i].dir == enemy) {
				curBullet ++;
				previousBullets[i] = bullets[i];
				bullets[i].y += BULLETSPEED;
				if(bullets[i].y <= ((BULLETHEIGHT + 1)/2)) {
					//previousBullets[i] = bullets[i];
					bullets[i].dir = used;
				}
			}
		}
	//calculate enemies
		
}

//--------------end of Task 1-----------------------------

//------------------Task 2--------------------------------
// background thread executes with SW1 button
// one foreground task created with button push
// foreground treads run for 2 sec and die
// ***********ButtonWork*************
void ButtonWork(void){
	addBullet(x,y,self);
	uart1testtx ++;
  OS_Kill();  // done, OS does not return from a Kill
} 

//************SW1Push*************
// Called when SW1 Button pushed
// Adds another foreground task
// background threads execute once and return
void SW1Push(void){
  if(OS_MsTime() > 20 ){ // debounce
		if(OS_AddThread(&ButtonWork,128,4)){
			OS_ClearMsTime();
      //NumCreated++; 
    }
    OS_ClearMsTime();  // at least 20ms between touches
  }
}

//--------------end of Task 2-----------------------------

//------------------Task 3--------------------------------

//******** Consumer *************** 
// foreground thread, accepts data from producer
// Display crosshair and its positions
// inputs:  none
// outputs: none
void Consumer(void){
	while(1/*NumSamples < RUNLENGTH*/){
		jsDataType data;
		JsFifo_Get(&data);
		OS_bWait(&LCDFree);
			
		BSP_LCD_DrawCrosshair(prevx, prevy, LCD_BLACK); // Draw a black crosshair
		BSP_LCD_DrawCrosshair(data.x, data.y, LCD_RED); // Draw a red crosshair

		BSP_LCD_Message(1, 5, 3, "Score: ", uart1testrx);		
		//BSP_LCD_Message(1, 5, 12, "Y: ", y);
		drawBullet(LCD_WHITE);
		drawEnemy(LCD_WHITE);
		OS_bSignal(&LCDFree);
		prevx = data.x; 
		prevy = data.y;
		test1 ++;
	}
  //OS_Kill();  // done
}


//--------------end of Task 3-----------------------------

//------------------Task 4--------------------------------
// foreground thread that runs without waiting or sleeping
// it executes some calculation related to the position of crosshair 
//******** CubeNumCalc *************** 
// foreground thread, calculates the virtual cube number for the crosshair
// never blocks, never sleeps, never dies
// inputs:  none
// outputs: none

void CubeNumCalc(void){ 
	uint16_t CurrentX,CurrentY;
  while(1) {
		//if(NumSamples < RUNLENGTH){
			CurrentX = x; CurrentY = y;
			area[0] = CurrentX / 22;
			area[1] = CurrentY / 20;
			Calculation++;
		//}
  }
}
//--------------end of Task 4-----------------------------

//------------------Task 5--------------------------------
// UART background ISR performs serial input/output
// Two software fifos are used to pass I/O data to foreground
// The interpreter runs as a foreground thread
// inputs:  none
// outputs: none

void Interpreter(void){
	
	char command[80];
  while(1){
		#ifdef tx
			if(uart1testtx) {
				OutCRLF();
				UART_OutString("77777");
				uart1testtx --;
				OutCRLF();
			}
		#endif
		#ifdef rx
			UART_InString(command,79);
			if(!strcmp(command, "77777")) {
				uart1testrx ++;
			}
		#endif
    /*OutCRLF(); UART_OutString(">>");
		//
		UART_InString(command,79);
		OutCRLF();
		if(!strcmp(command, "1")) {
			uart1testrx ++;
		}
		if (!(strcmp(command,"NumSamples"))){
			UART_OutString("NumSamples: ");
			UART_OutUDec(NumSamples);
		}
		else if (!(strcmp(command,"NumCreated"))){
			UART_OutString("NumCreated: ");
			UART_OutUDec(NumCreated);
		}
		else if (!(strcmp(command,"MaxJitter"))){
			UART_OutString("MaxJitter: ");
			UART_OutUDec(MaxJitter);
		}
		else if (!(strcmp(command,"DataLost"))){
			UART_OutString("DataLost: ");
			UART_OutUDec(DataLost);
		}
		else if (!(strcmp(command,"UpdateWork"))){
			UART_OutString("UpdateWork: ");
			UART_OutUDec(UpdateWork);
		}
	  else if (!(strcmp(command,"Calculations"))){
			UART_OutString("Calculations: ");
			UART_OutUDec(Calculation);
		}
		else if (!(strcmp(command,"FifoSize"))){
			UART_OutString("JSFifoSize: ");
			UART_OutUDec(JSFIFOSIZE);
		}
	  else if (!(strcmp(command,"Display"))){
			UART_OutString("DisplayWork: ");
			UART_OutUDec(DisplayCount);
		}
		else if (!(strcmp(command,"Consumer"))){
			UART_OutString("ConsumerWork: ");
			UART_OutUDec(ConsumerCount);
		}
		else{
			UART_OutString("Command incorrect!");
		}*/
		
  }
}
//--------------end of Task 5-----------------------------

//------------------Task 6--------------------------------

//************ PeriodicUpdater *************** 
// background thread, do some pseudo works to test if you can add multiple periodic threads
// inputs:  none
// outputs: none
void PeriodicUpdater(void){
	PseudoCount++;
}

//--------------end of Task 6-----------------------------

//------------------Task 7--------------------------------
// background thread executes with button2
// one foreground task created with button push
// ***********ButtonWork2*************
void Restart(void){
	uint32_t StartTime,CurrentTime,ElapsedTime;
	//NumSamples = RUNLENGTH; // first kill the foreground threads
	OS_Sleep(50); // wait
	StartTime = OS_MsTime();
	ElapsedTime = 0;
	OS_bWait(&LCDFree);
	BSP_LCD_FillScreen(BGCOLOR);
	while (ElapsedTime < 500){
		CurrentTime = OS_MsTime();
		ElapsedTime = CurrentTime - StartTime;
		BSP_LCD_DrawString(5,6,"Restarting",LCD_WHITE);
	}
	BSP_LCD_FillScreen(BGCOLOR);
	OS_bSignal(&LCDFree);
	// restart
	DataLost = 0;        // lost data between producer and consumer
  //NumSamples = 0;
  UpdateWork = 0;
	MaxJitter = 0;       // in 1us units
	PseudoCount = 0;
	x = 63; y = 63;
	NumCreated += OS_AddThread(&Consumer,128,1); 
	//NumCreated += OS_AddThread(&Display,128,3);
  OS_Kill();  // done, OS does not return from a Kill
} 

//************SW2Push*************
// Called when Button2 pushed
// Adds another foreground task
// background threads execute once and return
void SW2Push(void){
  if(OS_MsTime() > 20 ){ // debounce
    if(OS_AddThread(&Restart,128,4)){
			OS_ClearMsTime();
      NumCreated++; 
    }
    OS_ClearMsTime();  // at least 20ms between touches
  }
}

//--------------end of Task 7-----------------------------

// Fill the screen with the background color
// Grab initial joystick position to bu used as a reference
void CrossHair_Init(void){
	BSP_LCD_FillScreen(BGCOLOR);
	BSP_Joystick_Input(&origin[0],&origin[1],&select);
}

//******************* Main Function**********
int main(void){ 
  OS_Init();           // initialize, disable interrupts
	Device_Init();
  CrossHair_Init();
  DataLost = 0;        // lost data between producer and consumer
  NumSamples = 0;
  MaxJitter = 0;       // in 1us units
	PseudoCount = 0;
	score = 0;
	uart1testtx = 1;

//********initialize communication channels
  JsFifo_Init();

//*******attach background tasks***********
  OS_AddSW1Task(&SW1Push, 4);
	OS_AddSW2Task(&SW2Push, 4);
  OS_AddPeriodicThread(&Producer, PERIOD, 3); // 2 kHz real time sampling of PD3
	//OS_AddPeriodicThread(&PeriodicUpdater, PSEUDOPERIOD, 3);
	
  NumCreated = 0 ;
// create initial foreground threads
  NumCreated += OS_AddThread(&Interpreter, 128, 2); 
  NumCreated += OS_AddThread(&Consumer, 128, 1); 
	//NumCreated += OS_AddThread(&CubeNumCalc, 128, 3); 
	NumCreated += OS_AddThread(&generateEnemy, 128, 3);
 
  OS_Launch(TIME_2MS); // doesn't return, interrupts enabled in here
	return 0;            // this never executes
}
