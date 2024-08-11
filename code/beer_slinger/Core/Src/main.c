/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "buttons.h"
#include "flash.h"
#include "lcd.h"
#include "audio.h"
#include "gfxData.h"
#include <string.h>

LTDC_HandleTypeDef hltdc;
OSPI_HandleTypeDef hospi1;
RNG_HandleTypeDef hrng;
SAI_HandleTypeDef hsai_BlockA1;
DMA_HandleTypeDef hdma_sai1_a;
SPI_HandleTypeDef hspi2;

uint16_t audiobuffer[48000] __attribute__((section (".audio")));

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_LTDC_Init(void);
static void MX_SPI2_Init(void);
static void MX_OCTOSPI1_Init(void);
static void MX_SAI1_Init(void);
static void MX_RNG_Init(void);
static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */

//GRAPHICS FILL FUNCTIONS--------------------------------------------------------------

int buffer = 0;				//Current buffer being drawn, change the Layer 2 reference when done (one screen, 2 memory locations)
uint32_t buttonDebounce;
int barRow = 0;		//Used to be lazy about drawing the 3 rows of action (scales the X and Y for 0-2 rows)

void fillRLE(const char *pointer, int xPos, int yPos, int shadow, int alpha) {

	int imageWidth = (*pointer++ << 8) | *pointer++;
	int imageHeight = (*pointer++ << 8) | *pointer++;

	if (imageHeight + yPos > 240) {						//Check for scroll off bottom
		imageHeight = 240 - yPos;
	}

	xPos += shadow;
	yPos += shadow;

	char countRLE = 1;
	uint16_t currentPixel = 0;

	int line = yPos * 320;

	for (int y = 0 ; y < imageHeight ; y++) {

		for (int x = 0 ; x < imageWidth ; x++) {

			 if (--countRLE == 0) {										//No more RLE pixels?
				 currentPixel = (*pointer++ << 8) | *pointer++;			//Get next 16 bit color
				 countRLE = *pointer++;									//Get # of pixels of this color RLE
			 }

			uint32_t outPixel = ((currentPixel >> 15) * 255) << 24;		//Turns the single alpha bit into 0 or 255
			outPixel |= (currentPixel & 0x001F) << 3 | (currentPixel & 0x03E0) << 6 | (currentPixel & 0x7C00) << 9;

			int xTemp = x + xPos;
			int yTemp = y + yPos;

			if (xTemp > -1 && xTemp < 320 && yTemp > -1 && yTemp < 240) {						//Prevent edge rollover
				if (shadow) {
					if (currentPixel & 0x8000) {
						framebufferBG[line + x + xPos] = alpha << 24;
					}
				}
				else {
					if (outPixel & 0xFF000000) {						//Only draw in our solid pixel parts
						framebufferBG[line + x + xPos] = outPixel;
					}
				}
			}

		}
		line += 320;
	}


}

void fillRLEshadow(const char *pointer, int xPos, int yPos, int shadow, int alpha) {

	  fillRLE(pointer, xPos-3, yPos, 3, 8);
	  fillRLE(pointer, xPos+3, yPos, 3, 8);
	  fillRLE(pointer, xPos, yPos - 3, 3, 8);
	  fillRLE(pointer, xPos, yPos + 3, 3, 8);

	  fillRLE(pointer, xPos, yPos, 3, 32);
	  fillRLE(pointer, xPos, yPos, 0, 0);

}

void fillSegment(const char *pointer, int xPos, int yPos) {

	int imageWidth = *pointer++;

	int imageHeight = *pointer++;

	if (barRow) {						//Non-zero? Mux up
		xPos -= (barRow * 10);
		yPos += (barRow * 63);
	}

	if (imageHeight + yPos > 240) {						//Check for scroll off bottom
		imageHeight = 240 - yPos;
	}

	int line = yPos * 320;

	int bitCount = 7;								//7 triggers first byte load

	uint16_t byteOut = 0;

	for (int y = 0 ; y < imageHeight ; y++) {

		for (int x = 0 ; x < imageWidth ; x++) {

			if (++bitCount == 8) {					//Time to load new byte from flash?
				byteOut = *pointer++ << 8;				//Get the byte
				bitCount = 0;
			}

			int xTemp = x + xPos;
			int yTemp = y + yPos;

			if (xTemp > -1 && xTemp < 320 && yTemp > -1 && yTemp < 240) {						//Prevent edge rollover

				if (buffer) {
					framebufferSEG1[line + x + xPos] |= byteOut & 0x8000;	//AND the current MSB of byte into alpha bit of this pixel
				}
				else {
					framebufferSEG0[line + x + xPos] |= byteOut & 0x8000;	//AND the current MSB of byte into alpha bit of this pixel
				}

			}

			byteOut <<= 1;							//Shift in new MSB for next run

		}
		line += 320;
	}


}

void drawRect(int xStart, int yStart, int xEnd, int yEnd, uint16_t theAlpha) {

	if (barRow) {
		xStart -= (barRow * 10);
		yStart += (barRow * 63);
		xEnd -= (barRow * 10);
		yEnd += (barRow * 63);
	}

	int line = yStart * 320;

	for (int y = yStart ; y < yEnd ; y++) {

		for (int x = xStart ; x < xEnd ; x++) {

			if (buffer) {
				framebufferSEG1[line + x] = theAlpha;
			}
			else {
				framebufferSEG0[line + x] = theAlpha;
			}

		}

		line += 320;
	}

}

void decimal(uint16_t theNumber, int xPos, int yPos) {

	uint16_t divider = 1000;
	uint8_t zero = 0;

	if (theNumber == 0) {
		fillSegment(digits, xPos + 45, yPos);
		return;
	}


	for (int g = 0 ; g < 4 ; g++) {
		if (theNumber >= divider) {
			zero = 1;
			fillSegment(&digits[(theNumber/divider) * 40], xPos, yPos);
			theNumber %= divider;
		}
		else if (zero) {
			fillSegment(digits, xPos, yPos);
		}

		divider /= 10;
		xPos += 15;

	}

}

//Beer Slinger Game variables--------------------------------------------------------------

state gameState = attract;
int frame = 0;				//Global frame timer for stuff and things

int diffMux = 1;			//1= game A, 2 = game B. Used for random scalers
int game = 1;				//A sound, A no sound, B sound, B no sound
int startingLevel = 1;		//Can be changed
int level = 0;
int levelTimer = 0;			//For end of level
int levelTimer2 = 0;
int lives = 3;				//When = 0, game is OVAH

int keepRow = 0;			//What row the barkeep is on
bool dying = false;			//Bartender getting ass kicked
int deathType = 0;			//0 = none, 1 = glass to nobody, 2 = glass return break, 3 = punched
int deathTimer = 0;			//Dead bartender (use to animate death)

state barKeepState = dead;
int mugFillAnimation = 0;
int beerPouring = 0;		//Whether or not we're pouring a beer
int beerFull = 0;
int barDash = 6;			//6 = tending bar, 0-5 dash positions (0 being leftmost)

uint8_t whoosh[2];			//Draws the "vertical movement whoosh" when barkeep moves up and down
uint8_t dashWhoosh[3];		//Draws the horizontal movement whooch when barkeep dashes for the cash

struct mugType {			//Creature structure mugType

	int state;				//What mug is doing
	int8_t xCoarse;		//The course (segmented) position of mug. Compare this to patrons to find collect
	int16_t yPos;
	uint8_t counter;
	uint8_t speed;
	uint8_t whichRow;

};

struct mugType mug[36];		//Create 36 possible mugs

struct patronType {

	state status;			//What patron is doing
	int8_t xCoarse;			//What segment the patron is on (0-11) 12= punch
	int8_t whichRow;
	int8_t owned;			//What mug this customer owns
	uint8_t counter;
	uint8_t speed;
	uint8_t beersNeeded;			//How many beers they need, starts at 1 for lower levers. Dec per serve sated when down to 1
	int8_t drinkProgress;			//Starts at drinkTimeStart, at 0 patron throws mug back

};

int8_t drinkTimeStart;

struct patronType patron[36];

uint8_t patronSlot[3][12];		//Which patron in each slot (99 if none)

bool moveFlag = false;

uint8_t patronMoveTimer = 0;
uint8_t patronSpeed = 35;					//Speeds up per round (dec by 5, min 10?)
uint16_t patronMovePitch = 120;
uint8_t patronsPerLevelToSpawn = 0;
uint8_t patronsPerLevelServed = 0;		//Need 2 counters, they are equal start of level
uint8_t patronSubCount = 0;				//When this reaches 7, speed shit up

uint32_t patronRespawnTimer = 0;		//After a patron spawns, how many frames before next one spawns (starts low on level start)

uint32_t rando;

uint8_t tipsTotal = 0;
uint8_t tipsTen = 0;			//Every 10 tips enables dancing girls
uint16_t score = 0;
uint16_t highScore = 0;

uint16_t patronXpos[13] = {	15, 40, 59, 73, 92, 116, 135, 156, 169, 189, 206, 225, 225};			//X screen offset position of patrons (last one is same because PUNCH)

const char *patronGFX[13];								//Pointer for the 13 possible patron graphics (12 positions + punch)

uint8_t tips[3][5];								//Tips left behind

bool girlButton = false;						//9 tips earned a Girl button, hit B to spawn dancers and free patrons
bool girlsActive = false;
int girlsTimer = 0;
int girlBeep = 0;
int girlBeepTimer = 0;

uint32_t buttons;

int stateTimer = 0;				//Timer for pauses between state changes

int noteIndex = 0;
int noteFrameGap = 0;

uint16_t melody[] = {
	    {523},  // C (Middle C)
	    {587},  // D
	    {622},  // D#
	    {659},  // E
	    {523},  // C
	    {659},  // E
	    {698},  // F
	    {784},  // G
	    {622},  // D#
	    {698},  // F
	    {523},  // C
	    {659},  // E
	    {784},  // G
	    {698},  // F
	    {622},  // D#

	    {523},  // C
	    {659},  // E
	    {784},  // G
	    {698},  // F
	    {622},  // D#
	    {698},  // F
	    {523},  // C
	    {622},  // D#
	    {784},  // G
	    {698},  // F
	    {622},  // D#
	    {523},  // C
};

void updateRandomNumber() {

    if (HAL_RNG_GenerateRandomNumber(&hrng, &rando) != HAL_OK) {
    	Error_Handler();
    }
}

int getRandom(int min, int max) {

    if (min >= max) {
        return min; // or handle error
    }

    // Scale rando to the range [min, max]
    int range = max - min + 1;
    int randomValue = (rando % range) + min;

    // Update rando for the next call
    updateRandomNumber();

    return randomValue;

}

void gameReset() {

	buttonDebounce = 0xFFFFFFFF;
	gameState = attract;

	for (int x = 0 ; x < 36 ; x++) {		//Clear all patrons
		patron[x].status = off;
	}

	//reset vars

}

void stageSetup() {				//BEEP, pause, and indicate level using score then start level

	squareWave(500, 523, 50);
	gameState = stageOpen;
	stateTimer = 100;
	tipsTotal = 0;

}

void stageStart() {					//Start start, or reset if you die (score stays, tips etc resets to 0)

	frame = 0;
	dying = false;
	deathType = 0;						//Being punched

	barKeepState = tending;
	keepRow = 1;					//Spawn top
	barDash = 6;					//Not dashing

	girlButton = false;
	girlsActive = false;
	girlsTimer = 0;

	tipsTotal = 0;
	tipsTen = 0;

	patronSpeed = 35 - (level * diffMux);

	if (patronSpeed < 10) {
		patronSpeed = 10;
	}

	patronMovePitch = 120 + (level * 10);

	if (patronMovePitch > 500) {
		patronMovePitch = 500;
	}

	patronsPerLevelToSpawn = 15 + (level * diffMux);	//Each level adds 2 or 4 depending on diff
	patronsPerLevelServed = patronsPerLevelToSpawn;	//Same

	patronSubCount = 5;

	patronRespawnTimer = 25;			//First spawns in half a second no matter what

	drinkTimeStart = 60 - (diffMux * 10) - (level * 2);

	if (drinkTimeStart < 30) {
		drinkTimeStart = 30;
	}

	for (int g = 0 ; g < 36 ; g++) {
		patron[g].status = off;
	}

	for (int g = 0 ; g < 36 ; g++) {
		mug[g].state = 0;
	}

	memset(tips, 0, sizeof(tips));
	memset(patronSlot, 99, sizeof(patronSlot));

	gameState = active;

}

void stageEnd() {

	gameState = stageClose;
	levelTimer = 1;				//Count down tips

}

void gameStart() {

	score = 0;
	level = startingLevel;	//Hold TIME and left/right to change during attract mode
	lives = 3;				//When = 0, game is OVAH

	stageSetup();

}

void gameEnd() {

	if (score > highScore) {
		highScore = score;
	}

	stateTimer = 150;

	gameState = gameOver;


}

void loseLife() {

	if (--lives == 0) {
		gameEnd();
		return;
	}

	stageStart();

}

void speedUp() {

	patronSpeed -= 2;

	if (patronSpeed < 10) {
		patronSpeed = 10;
	}

	patronMovePitch += 10;

}

void gameResetControls() {

	if(buttons & B_POWER) {
		GW_EnterDeepSleep();
	}

	if (gameState == attract) {				//Press B or A to start game in attract mode

		if (buttons & B_A) {

			if (buttonDebounce & B_A) {
				buttonDebounce &= !B_A;
				gameStart();
			}
		}
		else {
			buttonDebounce |= B_A;
		}

		if (buttons & B_B) {

			if (buttonDebounce & B_B) {

				buttonDebounce &= !B_B;
				gameStart();

			}
		}
		else {
			buttonDebounce |= B_B;
		}

		if (buttons & B_PAUSE) {			//Hold PAUSE/SET in attract mode, left/right changes volumes (if sound on)

			if (buttons & B_Left) {

				if (buttonDebounce & B_Left) {
					buttonDebounce &= !B_Left;
					volumeDown();
				}

			}
			else {
				buttonDebounce |= B_Left;
			}

			if (buttons & B_Right) {

				if (buttonDebounce & B_Right) {
					buttonDebounce &= !B_Right;
					volumeUp();
				}

			}
			else {
				buttonDebounce |= B_Right;
			}

		}

		if (buttons & B_TIME) {			//Hold PAUSE/SET in attract mode, left/right changes volumes (if sound on)

			if (buttons & B_Left) {

				if (buttonDebounce & B_Left) {
					buttonDebounce &= !B_Left;
					if (startingLevel > 1) {
						startingLevel--;
					}
				}

			}
			else {
				buttonDebounce |= B_Left;
			}

			if (buttons & B_Right) {

				if (buttonDebounce & B_Right) {
					buttonDebounce &= !B_Right;
					if (startingLevel < 20) {
						startingLevel++;
					}
				}

			}
			else {
				buttonDebounce |= B_Right;
			}

		}

	}
	else {		//PAUSE during game, but only if game active (not dead etc)

		if (buttons & B_PAUSE) {

			if (buttonDebounce & B_PAUSE) {

				buttonDebounce &= !B_PAUSE;

				switch(gameState) {

					case active:
						gameState = paused;
						squareWave(300, 3000, 25);
						//TODO: SFX
						break;

					case paused:
						gameState = active;
						squareWave(300, 4000, 25);
						break;

				}

			}

		}
		else {
			buttonDebounce |= B_PAUSE;
		}

	}

	if (buttons & B_GAME) {

		if (buttonDebounce & B_GAME) {

			buttonDebounce &= !B_GAME;

			if (gameState != attract) {			//In game? Reset to attract
				gameReset();
			}

			if (++game > 4) {
				game = 1;
			}

			musicControl(game & 0x01);

		}

	}
	else {
		buttonDebounce |= B_GAME;
	}

	switch(game) {							//Draw game text and notes (gotta redraw entire display every frame) ALWAYS

		case 1:
			fillSegment(game_A, 78, 2);
			fillSegment(note, 78, 21);
			diffMux = 1;
			break;

		case 2:
			fillSegment(game_A, 78, 2);
			diffMux = 1;
			break;

		case 3:
			fillSegment(game_B, 78, 11);
			fillSegment(note, 78, 21);
			diffMux = 2;
			break;

		case 4:
			fillSegment(game_B, 78, 11);
			diffMux = 2;
			break;
	}


	if (gameState != attract) {					//Always show lives except attract

		int lifeX = 233;

		for (int g = 0 ; g < lives ; g++) {		//MESA OWE YOUSA A LIFE DEBT!

			fillSegment(life_head, lifeX, 20);

			lifeX += 14;

		}
	}

}

void spawnPatron() {

	patronsPerLevelToSpawn--;

	int maxTime = 175 - (level * 15);		//Faster each time

	if (maxTime < 30) {
		maxTime = 30;
	}

	patronRespawnTimer = getRandom(20, maxTime);		//When next patron spawns (if none left, nothing happens)

	int spawnRow = getRandom(0, 2);				//Random row

	for (int x = 0 ; x < 36 ; x++) {			//Find next available patron to spawn

		if (patron[x].status == off) {			//If null can spawn

			patron[x].status = thirsty;
			patron[x].xCoarse = 0;
			patron[x].whichRow = spawnRow;

			//CHANGES PER GAME/LEVEL:
			patron[x].beersNeeded = (getRandom(0, 85) < (level * (3 * diffMux))) ? 2 : 1;		//Game B patrons twice as likely to want >1 beer

			return;
		}

	}



}

void spawnMug() {			//Bartender throws a mug

	for (int x = 0 ; x < 36 ; x++) {			//Find next available mug to spawn

		if (mug[x].state == 0) {				//Found slot? Spawn slung mug

			mug[x].state = 1;
			mug[x].xCoarse = 11;
			mug[x].yPos = 73;
			mug[x].counter= 0;
			mug[x].speed = 2;
			mug[x].whichRow = keepRow;

			return;
		}
	}

}

void drawBarkeep() {

	if (dying == false) {

		if (girlButton == true) {

			barRow = 0;

			if ((frame >> 3) & 0x01) {
				fillSegment(b_arrow, 96, 20);
				fillSegment(distract_text, 74, 39);
			}

			if (buttons & B_B) {				//Button active? B spawns girl distraction

				if (buttonDebounce & B_B) {

					buttonDebounce &= !B_B;

					girlButton = false;
					girlsActive = true;
					girlsTimer = 1;
					tipsTotal -= 5;				//Risk/reward

					noteIndex = 0;
					noteFrameGap = 7;

				}
			}
			else {
				buttonDebounce |= B_B;
			}

		}

		if (buttons & B_Left) {

			if (barDash > 1 && buttonDebounce & B_Left) {
				barKeepState = dashing;							//Switch to dash state
				buttonDebounce &= !B_Left;

				buttons &= vertMask;							//If left or right, mask out up and down (so dia won't trigger dash end)
				buttonDebounce &= vertMask;						//Set up/down debounce

				if (barDash == 6) {							//Starting the whoosh?
					dashWhoosh[keepRow] = 4;
				}

				barDash -= 1;
			}
		}
		else {
			buttonDebounce |= B_Left;
		}

		if (buttons & B_Right) {

			if (barDash < 6 && buttonDebounce & B_Right) {
				barKeepState = dashing;							//Switch to dash status
				buttonDebounce &= !B_Right;

				buttons &= vertMask;							//If left or right, mask out up and down (so dia won't trigger dash end)
				buttonDebounce &= vertMask;						//Set up/down debounce

				if (++barDash == 6) {						//Moved all the way to right? Tending bar again
					dashWhoosh[keepRow] = 4;
					barKeepState = tending;
				}
			}
		}
		else {
			buttonDebounce |= B_Right;
		}

		if (buttons & B_A) {		//Start filling glass?

			if (buttonDebounce & B_A) {

				buttonDebounce &= !B_A;

				if (barKeepState != pouring) {

					if (barDash < 6) {
						dashWhoosh[keepRow] = 4;	//Whoosh!
						barDash = 6;			//End tip dash if any
					}

					barKeepState = pouring;
					mugFillAnimation = 0;
					beerFull = 0;
				}

			}
		}
		else {
			buttonDebounce |= B_A;
		}

	}

	barRow = keepRow;

	switch(barKeepState) {

		case tending:						//Tending bar
			fillSegment(barKeepTend, 253, 53);

			if (mugFillAnimation < 20) {
				fillSegment(wipe1, 240, 74);
			}
			else {
				fillSegment(wipe2, 240, 74);
			}

			if (++mugFillAnimation > 40) {
				mugFillAnimation = 0;
			}

		break;

		case pouring:						//Filling glass

			fillSegment(barKeepPour, 274, 52);
			fillSegment(mugFilling, 297, 70);

			mugFillAnimation++;

			if (mugFillAnimation == 1) {
				squareWave(100, 200, 50);
			}

			//MAKE POUR SLOWER
			if (mugFillAnimation > 3) {						//After a couple ticks show his arm pulling lever
				fillSegment(armPull, 293, 51);
				drawRect(310, 48, 312, 60, 0);			//Erase the previously drawn erect tapper lever
			}

			if (mugFillAnimation == 4) {
				squareWave(100, 220, 50);
			}

			if (mugFillAnimation > 6) {
				drawRect(303, 81, 308, 83, 0x8000);
			}

			if (mugFillAnimation == 7) {
				squareWave(100, 240, 50);
			}

			if (mugFillAnimation > 9) {
				drawRect(303, 77, 308, 79, 0x8000);
			}

			if (mugFillAnimation == 10) {
				squareWave(100, 260, 50);
			}

			if (mugFillAnimation > 12) {
				drawRect(303, 73, 308, 75, 0x8000);

				if (beerFull == 0) {				//One shot
					squareWave(50, 300, 50);
				}

				beerFull = 1;
				mugFillAnimation = 20;
			}

			if(!(buttons & B_A)) {						//Stop filling glass?

				barKeepState = tending;

				if (beerFull == 1) {
					beerFull = 0;
					squareWave(100, 400, 50);
					spawnMug();
				}

				mugFillAnimation = 0;
			}

		break;

		case dashing:											//Dash for cash. 38 pixel H spacing x 6 dash positions. 87 byte offset per dash position (0-5)
			fillSegment(&barDashGFX[barDash * 87], 17 + (barDash * 38), 97);			//Draw the dashing bartender

			if (tips[barRow][barDash - 1]) {			//Grabbed a tip?
				tipsTotal += tips[barRow][barDash - 1];	//Add them to the pile

				if (tipsTotal > 99) {
					tipsTotal = 99;				//99 max bonux per level
				}

				tips[barRow][barDash - 1] = 0;			//Clear tips

				if (++tipsTen > 4) {			//Every 5 bucks? Enable dancing girls smart bomb
					tipsTen = 0;
					girlButton = true;
					squareWave(50, 2000, 50);
				}
				else {
					squareWave(250, 2500, 50);
				}

			}


			//Collect monies or mugs here---------------------------

		break;

		case dead:									//DEAD FROZEN!

			if ((stateTimer >> 2) & 0x01) {
				fillSegment(barKeepTend, 253, 53);
			}

			if (--stateTimer == 0) {
				loseLife();
			}

		break;

	}

	if (dying == false) {

		if (buttons & B_Up) {

			if (keepRow > 0 && buttonDebounce & B_Up) {
				buttonDebounce &= !B_Up;
				keepRow -= 1;
				whoosh[keepRow] = 4;
				mugFillAnimation = 16;
				beerFull = 0;
				barDash = 6;
				barKeepState = tending;
			}
		}
		else {
			buttonDebounce |= B_Up;
		}

		if (buttons & B_Down) {
			if (keepRow < 2 && buttonDebounce & B_Down) {
				buttonDebounce &= !B_Down;
				keepRow += 1;
				whoosh[keepRow - 1] = 4;
				mugFillAnimation = 16;
				beerFull = 0;
				barDash = 6;
				barKeepState = tending;
			}
		}
		else {
			buttonDebounce |= B_Down;
		}

	}

	if (whoosh[0]) {
		--whoosh[0];
		barRow = 0;
		fillSegment(vWhoosh, 267, 105);
	}
	if (whoosh[1]) {
		--whoosh[1];
		barRow = 1;
		fillSegment(vWhoosh, 267, 105);
	}

	for (int x = 0 ; x < 3 ; x++) {

		if (dashWhoosh[x]) {
			--dashWhoosh[x];
			barRow = x;
			fillSegment(hWhoosh, 241, 105);
		}

	}

}

bool checkOpening(int x, int r) {		//Pass the location we want to move into, returns TRUE if it's open (else a person is stopping to drink)

	for (int p = 0 ; p < 36 ; p++) {

		if (patron[p].whichRow == r) {		//Check any patrons on this row
			if (patron[p].xCoarse == x) {		//Is patron in this spot?

				if (patron[p].status == drinking) {
					return false;
				}

			}
		}

	}

	return true;		//No matches, clear to move

}

int checkCollisions(int x, int y) {		//Check if a thirsty patron is at position X Y (0-11 0-2)

	for (int g = 0 ; g < 36 ; g++) {	//Scan all possible patrons (not efficient but who cares?)

		if (patron[g].whichRow == y) {	//Match separately to save a tiny bit of time
			if (patron[g].xCoarse == x) {
				if (patron[g].status == thirsty) {
					return g;			//Return patron index
				}
			}
		}

	}

	return 99;			//Return NO MATCH

}

void drawMugs() {

	for (int x = 0 ; x < 36 ; x++) {

		barRow = mug[x].whichRow;			//For drawing

		switch(mug[x].state) {

			case 1:													//Going towards customer?
				if (++mug[x].counter == mug[x].speed) {				//Time to move this mug?

					mug[x].counter = 0;
					mug[x].xCoarse--;

					if (mug[x].xCoarse < 0) {					//Off left edge? Bad!

						mug[x].state = 0;							//Despawn mug

						if (dying == false) {						//Cause of death?
							barKeepState = dead;
							stateTimer = 150;					//Punch for 3 seconds then do lives
							frame = 0;
							dying = true;
							squareWave(1000, 60, 50);
							deathType = 1;						//Mug off left end
							keepRow = mug[x].whichRow;		   //Move keep here
						}
					}
					else {
						fillSegment(mugThrown, 18 + (mug[x].xCoarse * 19), mug[x].yPos);		//Draw new pos
					}
				}
				else {
					fillSegment(mugThrown, 18 + (mug[x].xCoarse * 19), mug[x].yPos);		//No change this frame just draw= it
				}

			break;

			case 2:													//Customer has beer and is leaving?
				fillSegment(mugFull, 18 + (mug[x].xCoarse * 19), mug[x].yPos + 6);	//Not as tall as mug thrown
			break;

			case 3:																//Customer is drinking this beer and will throw mug back?
				fillSegment(mugFull, 18 + (mug[x].xCoarse * 19), mug[x].yPos + 6);
				break;
				
			case 4:;												//Empty Mug being thrown back?

				bool intact = true;

				int xHalf = (mug[x].xCoarse >> 1);

				if (barKeepState == dashing && mug[x].whichRow == keepRow) {	//Is barkeep dashing on this row?

					if (xHalf >= barDash) {				//Is dashing barkeep aligned with mug? (or past him)
						mug[x].counter = 0;				//Ensure the move logic after this won't trip
						mug[x].state = 0;				//Despawn
						score++;						//A whole point!
						squareWave(100, 2500, 30);
					}
				}

				if (++mug[x].counter == mug[x].speed) {				//Time to move this mug?
					mug[x].counter = 0;
					mug[x].xCoarse++;

					if (mug[x].xCoarse > 11) {

						if (keepRow == mug[x].whichRow && barKeepState != dashing) {    //Caught return mug?

							mug[x].state = 0;				//Despawn
							score++;							//A whole point!
							squareWave(100, 2500, 30);

						}
						else {												//FALL BLOG!

							mug[x].state = 5;							//Broken mug

							if (dying == false) {						//Cause of death?

								fillSegment(mugSmashed, 241, 83);

								intact = false;						//Broken, don't draw it this frame
								barKeepState = dead;
								stateTimer = 150;					//Punch for 3 seconds then do lives
								frame = 0;
								dying = true;
								squareWave(1000, 60, 50);
								deathType = 2;						//Mug off left end

							}

						}

					}

				}

				if (intact) {
					fillSegment(mugReturn, 12 + (mug[x].xCoarse * 19), mug[x].yPos + 6);		//Offset left (swoosh)
				}

				break;

			case 5:

				if (frame & 0x02) {
					fillSegment(mugSmashed, 241, 83);			//Flicker broken
				}

				break;

		}

	}

}

void drawTips() {

	for (int y = 0 ; y < 3 ; y++) {

		barRow = y;

		for (int x = 0 ; x < 5 ; x++) {
			if (tips[y][x]) {
				fillSegment(money, 42 + (x * 38), 97);
			}
		}

	}

}

void drawPatrons() {

	moveFlag = false;

	if (girlsTimer == 0 && dying == false) {			//Girls stop patrons from moving

		if (++patronMoveTimer >= patronSpeed) {
			patronMoveTimer = 0;
			moveFlag = true;
			squareWave(100, patronMovePitch, 20);			//TODO: SLIGHTLY HIGHER PITCH WHEN MOVING FASTER
		}
	}

	for (int x = 0 ; x < 36 ; x++) {

		barRow = patron[x].whichRow;

		switch(patron[x].status) {

			case thirsty:;

				if (moveFlag == true) {

					if (patron[x].xCoarse == 11) {				//Was at right edge, and it's time to move? PUNCH!

						if (barKeepState != dead) {				//First? Punch bartender!
							moveFlag = false;					//Freeze rest of scan
							barKeepState = dead;
							stateTimer = 150;					//Punch for 3 seconds then do lives
							frame = 0;
							dying = true;
							squareWave(1000, 60, 50);
							deathType = 3;						//Being punched
							patron[x].status = punching;
							keepRow = patron[x].whichRow;		//Move keep here
						}

					}
					else {
						if (checkOpening(patron[x].xCoarse + 1, patron[x].whichRow) == true) {
							patron[x].xCoarse++;

							if (patron[x].xCoarse > 9) {	//Last 2 slots, rubber band down to 1 beer else it's impossible
								patron[x].beersNeeded = 1;	//Override any beer needed settings
							}

						}
					}
				}

				fillSegment(patronGFX[patron[x].xCoarse], patronXpos[patron[x].xCoarse], 54); //Patron, if punch that's drawn next frame

				break;

			case sated:
				fillSegment(patronGFX[patron[x].xCoarse], patronXpos[patron[x].xCoarse], 54);

				if (++patron[x].counter == patron[x].speed) {
					patron[x].counter = 0;

					if (--patron[x].xCoarse < 0) {
						patron[x].status = off;				//Despawn patron
						mug[patron[x].owned].state = 0;		//and mug

						if (--patronsPerLevelServed == 0) {
							stageEnd();							//Trigger end
						}

					}
					else {
						mug[patron[x].owned].xCoarse = patron[x].xCoarse;	//Mug moves with the patron
					}
				}

				break;

			case drinking:

				fillSegment(patronGFX[patron[x].xCoarse], patronXpos[patron[x].xCoarse], 54);		//Static patron

				mug[patron[x].owned].xCoarse = patron[x].xCoarse;	//Ensure mug is thrown from correct spot
				mug[patron[x].owned].whichRow = patron[x].whichRow;

				if (--patron[x].drinkProgress == 0) {		//Beer finished? Throw back!
					squareWave(200, 850, 50);
					patron[x].drinkProgress = 0;
					patron[x].status = thirsty;
					mug[patron[x].owned].state = 4;			//Throw empty back
					mug[patron[x].owned].counter = 0;
					mug[patron[x].owned].speed = 12 - (diffMux * 2);	//G

					for (int g = 0 ; g < 36 ; g++) {			//Scan all other players

						if (patron[g].status == drinking) {		//If they are drinking
							patron[g].drinkProgress += drinkTimeStart;	//Delay how long before they finish drink (so 2 aren't sent back at once)
						}

					}


				}
				else {
					mug[patron[x].owned].state = 3;			//Ensure beer held
				}

				break;

			case punching:

				if ((frame >> 4) & 0x01) {
					fillSegment(patronGFX[11], patronXpos[patron[x].xCoarse], 54);		//Un-punch
				}
				else {
					fillSegment(patronGFX[12], patronXpos[patron[x].xCoarse], 54);		//Punch!
					fillSegment(punch_head, 255, 50);									//Lines around barkeep head
				}

				break;

		}

	}

}

void scanCollisions() {					//Do this after all possible mug/patron moves have occured this frame

	for (int x = 0 ; x < 36 ; x++) {

		if (mug[x].state == 1) {			//Mug slinging towards customers only hits that count

			int p = checkCollisions(mug[x].xCoarse, mug[x].whichRow);

			if (p != 99) {		//Lined up with a thirsty customer? p is their index

				if (patron[p].beersNeeded == 1) {			//Last or only one they need?

					mug[x].state = 2;							//Change state of captured mug to being carried away
					squareWave(200, 1000, 50);

					if(--patronSubCount == 0) {
						patronSubCount = 5;
						speedUp();
					}

					patron[p].status = sated;								//Patron will leave with beer
					patron[p].owned = x;									//Assign this mug to this patron (used later)
					patron[p].counter = 0;
					patron[p].speed = 2;
					score += 3;												//3 bucks for serving a beer
					int divX = patron[p].xCoarse / 2.75;					//TODO: RANDOMIZE TIPS - NO TIPS IF GIRLS DANCING (since tips freeze patrons)

					int value = 1;

					if (diffMux == 2) {
						value = (getRandom(0, 100) < 25) ? 0 : 1;		//Game B, 25% chance they don't leave a tip. BASTARDS!
					}

					tips[mug[x].whichRow][divX] = value;						//Leave a tip
				}
				else {
					squareWave(200, 700, 50);
					mug[x].state = 3;							//Change state of captured mug to being drank
					patron[p].owned = x;						//Assign this mug to this patron (used later)
					patron[p].beersNeeded--;
					patron[p].status = drinking;
					patron[p].drinkProgress = drinkTimeStart;	//Reset this, counts down to zero
				}
			}
		}
	}

}

void drawGirls() {

	int framer = girlsTimer >> 4;

	if (framer & 0x01) {
		fillSegment(girls_0, 126, 2);
		fillSegment(girls_0, 160, 2);
		fillSegment(girls_0, 195, 2);
	}
	else {
		fillSegment(girls_1, 126, 2);
		fillSegment(girls_1, 160, 2);
		fillSegment(girls_1, 195, 2);
	}

//	if (--girlBeepTimer == 0) {
//
//		girlBeepTimer = 16;
//
//		if (++girlBeep > 2) {
//			girlBeep = 1;
//		}
//
//		squareWave(3000, girlBeep * 1500, 50);
//	}

	if (++girlsTimer > 200) {
		girlsTimer = 0;
	}

	if (--noteFrameGap == 0) {
		noteFrameGap = 7;

        size_t melodySize = sizeof(melody) / sizeof(melody[0]);

        if (noteIndex < melodySize) {
            squareWave(140, melody[noteIndex], 50);
            noteIndex++;
        }

	}

}

void activeGameLoop() {		//When player has control and game is not paused-----------------------------

	if (girlsTimer > 0) {							//Do this first so it doesn't get affected by barRow
		drawGirls();
	}

	for (int x = 0 ; x < 3 ; x++) {
		barRow = x;
		drawRect(310, 48, 312, 60, 0x8000);			//Draw the 3 tapper levers (we erase this is keep is pulling one)
	}

	drawBarkeep();

	drawMugs();										//Draw/move mugs and see if they collide with thirsty patrons
	scanCollisions();								//Check for new pings mugs vs customers
	drawPatrons();									//Draw/move patrons
	scanCollisions();								//Check for new pings mugs vs customers
	drawTips();										//Result of mug/patron collisions

	if (patronRespawnTimer > 0) {					//X time must pass before new patron. Then you must wait til a move happens to ensure a slot is open on left
		patronRespawnTimer--;
	}

	if (moveFlag == true && dying == false) {		//Active state?

		if (girlsTimer == 0 && patronsPerLevelToSpawn > 0 && patronRespawnTimer == 0) {
			spawnPatron();
		}

	}

	barRow = 0;											//This is a base vertical counter that makes it easier to draw dupe patrons, barkeep and tips
	fillSegment(score_text, 20, 5);
	decimal(score, 10, 13);								//Draw score
	decimal(tipsTotal, 252, 13);								//Draw tips

}

void endofStageLoop() {

	for (int x = 0 ; x < 3 ; x++) {
		barRow = x;
		drawRect(310, 48, 312, 60, 0x8000);			//Draw the 3 tapper levers (we erase this is keep is pulling one)
	}

	barRow = keepRow;

	fillSegment(barKeepTend, 253, 53);		//Draw barkeep, don't allow movement though

	if (mugFillAnimation < 20) {
		fillSegment(wipe1, 240, 74);
	}
	else {
		fillSegment(wipe2, 240, 74);
	}

	if (++mugFillAnimation > 40) {
		mugFillAnimation = 0;
	}

	drawTips();										//Leave these to remind people what they DIDN'T get

	barRow = 0;											//This is a base vertical counter that makes it easier to draw dupe patrons, barkeep and tips
	fillSegment(score_text, 20, 5);
	decimal(score, 10, 13);								//Draw score
	decimal(tipsTotal, 252, 13);								//Draw tips

	if (levelTimer > 0) {

		if (--levelTimer == 0) {

			if (tipsTotal > 0) {
				tipsTotal--;
				score += 3;					//3 points bonus per spare tip
				levelTimer = 10;
				squareWave(100, 2000, 50);
			}
			else {
				levelTimer2 = 50;			//Next state
				squareWave(800, 1000, 50);
			}

		}


	}

	if (levelTimer2 > 0) {

		if (--levelTimer2 == 0) {
			level++;
			stageSetup();
		}
	}

}

void mainGameLoop() {	//MAIN GAME LOOP called by main while(1)---------------------------------------------------

	 frame++;

	 barRow = 0;										//This is a base vertical counter that makes it easier to draw dupe patrons, barkeep and tips

	 buttons = buttons_get();
	 updateRandomNumber();								//Seed per frame
	 uint32_t nextFrame = HAL_GetTick() + 20;			//50 FPS

	//START DRAW THE FRAME ----- "buffer" is which buffer we are drawing to this frame
	if (buffer) {										//Clear the next draw buffer
		memset(framebufferSEG1, 0x00, 320*240*2);		//Clear segment buffer 1
	}
	else {
		memset(framebufferSEG0, 0x00, 320*240*2);		//Clear segment buffer 0
	}

	gameResetControls();								//Global game controls (game, power buttons...)

	switch(gameState) {

		case attract:

			if (buttons & B_PAUSE) {
				decimal(getVolume(), 10, 13);		//Draw volume level
			}
			else {

				if (buttons & B_TIME) {
					decimal(startingLevel, 10, 13);			//Draw starting stage
					fillSegment(stage_text, 20, 39);
				}
				else {
					fillSegment(hi_text, 9, 5);
					fillSegment(score_text, 20, 5);
					decimal(highScore, 10, 13);			//Draw score
				}

			}

			break;

		case stageOpen:					//Beep and blink the stage # where score is

			decimal(level, 10, 13);		//Draw stage # where score usually goes

			if ((frame >> 3) & 0x01) {
				fillSegment(stage_text, 20, 39);
			}

			if (--stateTimer == 0) {
				stageStart();
			}

			break;

		case active:
			activeGameLoop();
			break;

		case stageClose:				//Stage won! Pause then next stage
			endofStageLoop();			//Count down tips as bonus, then a pause once done
			break;

		case death:

			break;

		case gameOver:

			if (score == highScore) {
				if ((frame >> 3) & 0x01) {
					fillSegment(hi_text, 9, 5);			//HI SCORE
					fillSegment(score_text, 20, 5);
					decimal(highScore, 10, 13);								//Draw score
				}
			}
			else {
				fillSegment(score_text, 20, 5);
				decimal(score, 10, 13);								//Draw score
			}

			fillSegment(game_over_text, 239, 39);

			if (--stateTimer == 0) {
				gameReset();			//Back to attract mode
			}

			break;



	}

//	barRow = 2;
//	decimal(patronsPerLevelToSpawn, 10, 13);
//	decimal(patronsPerLevelServed, 250, 13);


	//SWITCH BUFFERS ONCE DRAWING IS DONE------------------------------------------------------------------

	bufferSwap(&hltdc, buffer);							//Swap to new display buffer

	if (buffer) {										//Swap the buffer counter
		buffer = 0;
	}
	else {
		buffer = 1;
	}

	while (HAL_GetTick() < nextFrame) {}				//Wait for next frame (50 FPS, 20ms gap)


}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  HAL_Init();
  SystemClock_Config();
  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_LTDC_Init();
  MX_SPI2_Init();
  MX_OCTOSPI1_Init();
  MX_SAI1_Init();
  MX_RNG_Init();
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1_LOW);

  audio_set_frequency();

  //audio_init();

  /* Initialize interrupts */
  MX_NVIC_Init();
  lcd_init(&hspi2, &hltdc);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  flash_memory_map(&hospi1);

  // Sanity check, sometimes this is triggered
  uint32_t add = 0x90000000;
  uint32_t* ptr = (uint32_t*)add;

  if(*ptr == 0x88888888) {
    Error_Handler();
  }

  memset(framebufferBG, 0x00, 320*240*4);		//Clear background

  //Draw background once:
  fillRLEshadow(scoregfx, 2, 2, 0, 0);
  fillRLEshadow(stage, 121, 0, 0, 0);
  fillRLEshadow(tiptext, 233, 1, 0, 0);
  fillRLEshadow(tipgfx, 276, 9, 0, 0);
  fillRLEshadow(status_bar, 0, 37, 0, 0);
  fillRLEshadow(table, 0, 51, 0, 0);
  fillRLEshadow(table, -11, 114, 0, 0);
  fillRLEshadow(table, -22, 177, 0, 0);
  fillRLEshadow(keg, 304, 53, 0, 0);
  fillRLEshadow(keg, 294, 116, 0, 0);
  fillRLEshadow(keg, 284, 179, 0, 0);

  for (int x = 0 ; x < 76800 ; x++) {				//Random noise on BG layer
	  HAL_RNG_GenerateRandomNumber(&hrng, &rando);
	  framebufferBG[x] |= (rando & 0x0101F1F1F);
  }

  patronGFX[0] = patron0;				//Graphic points do once
  patronGFX[1] = patron1;
  patronGFX[2] = patron2;
  patronGFX[3] = patron3;
  patronGFX[4] = patron4;
  patronGFX[5] = patron5;
  patronGFX[6] = patron6;
  patronGFX[7] = patron7;
  patronGFX[8] = patron8;
  patronGFX[9] = patron9;
  patronGFX[10] = patron10;
  patronGFX[11] = patron11;
  patronGFX[12] = patronPunch;

  gameReset();			//Initial reset

  while(1) {
	  mainGameLoop();
	  //audioTick();
  }

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  /** Macro to configure the PLL clock source
  */
  __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSI);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 140;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC|RCC_PERIPHCLK_RNG
                              |RCC_PERIPHCLK_SPI2|RCC_PERIPHCLK_SAI1
                              |RCC_PERIPHCLK_OSPI|RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.PLL2.PLL2M = 25;
  PeriphClkInitStruct.PLL2.PLL2N = 192;
  PeriphClkInitStruct.PLL2.PLL2P = 5;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 5;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.PLL3.PLL3M = 4;
  PeriphClkInitStruct.PLL3.PLL3N = 9;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 24;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
  PeriphClkInitStruct.OspiClockSelection = RCC_OSPICLKSOURCE_CLKP;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_CLKP;
  PeriphClkInitStruct.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief NVIC Configuration.
  * @retval None
  */
static void MX_NVIC_Init(void)
{
  /* OCTOSPI1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(OCTOSPI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(OCTOSPI1_IRQn);
}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};
  LTDC_LayerCfgTypeDef pLayerCfg1 = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;
  hltdc.Init.HorizontalSync = 9;
  hltdc.Init.VerticalSync = 1;
  hltdc.Init.AccumulatedHBP = 60;
  hltdc.Init.AccumulatedVBP = 7;
  hltdc.Init.AccumulatedActiveW = 380;
  hltdc.Init.AccumulatedActiveH = 247;
  hltdc.Init.TotalWidth = 392;
  hltdc.Init.TotalHeigh = 255;
  hltdc.Init.Backcolor.Blue = 120;
  hltdc.Init.Backcolor.Green = 128;
  hltdc.Init.Backcolor.Red = 128;

  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }

  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 320;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 240;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888; //LTDC_PIXEL_FORMAT_ARGB1555; //LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 230;				//Make background art slightly transparent so gray background bleeds through a bit
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR1_PAxCA;
  //pLayerCfg.FBStartAdress = 0x24000000;
  pLayerCfg.ImageWidth = 320;
  pLayerCfg.ImageHeight = 240;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }

  pLayerCfg1.WindowX0 = 0;
  pLayerCfg1.WindowX1 = 320;
  pLayerCfg1.WindowY0 = 0;
  pLayerCfg1.WindowY1 = 240;
  pLayerCfg1.PixelFormat = LTDC_PIXEL_FORMAT_ARGB1555;
  pLayerCfg1.Alpha = 130;				//Make black segments slightly transparent so art shows through slightly
  pLayerCfg1.Alpha0 = 0;
  pLayerCfg1.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg1.BlendingFactor2 = LTDC_BLENDING_FACTOR1_PAxCA;
  //pLayerCfg1.FBStartAdress = 0x24000000;
  pLayerCfg1.ImageWidth = 320;
  pLayerCfg1.ImageHeight = 240;
  pLayerCfg1.Backcolor.Blue = 0;
  pLayerCfg1.Backcolor.Green = 0;
  pLayerCfg1.Backcolor.Red = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg1, 1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief OCTOSPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_OCTOSPI1_Init(void)
{

  /* USER CODE BEGIN OCTOSPI1_Init 0 */

  /* USER CODE END OCTOSPI1_Init 0 */

  OSPIM_CfgTypeDef sOspiManagerCfg = {0};

  /* USER CODE BEGIN OCTOSPI1_Init 1 */

  /* USER CODE END OCTOSPI1_Init 1 */
  /* OCTOSPI1 parameter configuration*/
  hospi1.Instance = OCTOSPI1;
  hospi1.Init.FifoThreshold = 4;
  hospi1.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
  hospi1.Init.MemoryType = HAL_OSPI_MEMTYPE_MACRONIX;
  hospi1.Init.DeviceSize = 20;
  hospi1.Init.ChipSelectHighTime = 2;
  hospi1.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
  hospi1.Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
  hospi1.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
  hospi1.Init.ClockPrescaler = 1;
  hospi1.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
  hospi1.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
  hospi1.Init.ChipSelectBoundary = 0;
  hospi1.Init.ClkChipSelectHighTime = 0;
  hospi1.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED;
  hospi1.Init.MaxTran = 0;
  hospi1.Init.Refresh = 0;
  if (HAL_OSPI_Init(&hospi1) != HAL_OK)
  {
    Error_Handler();
  }
  sOspiManagerCfg.ClkPort = 1;
  sOspiManagerCfg.NCSPort = 1;
  sOspiManagerCfg.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
  if (HAL_OSPIM_Config(&hospi1, &sOspiManagerCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN OCTOSPI1_Init 2 */

  /* USER CODE END OCTOSPI1_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief SAI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI1_Init(void)
{

  /* USER CODE BEGIN SAI1_Init 0 */

  /* USER CODE END SAI1_Init 0 */

  /* USER CODE BEGIN SAI1_Init 1 */

  /* USER CODE END SAI1_Init 1 */
  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_MONOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN SAI1_Init 2 */

  /* USER CODE END SAI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 0x0;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi2.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi2.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi2.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi2.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi2.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi2.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi2.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi2.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIO_Speaker_enable_GPIO_Port, GPIO_Speaker_enable_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8|GPIO_PIN_4, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin : GPIO_Speaker_enable_Pin */
  GPIO_InitStruct.Pin = GPIO_Speaker_enable_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIO_Speaker_enable_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_PAUSE_Pin BTN_GAME_Pin BTN_TIME_Pin */
  GPIO_InitStruct.Pin = BTN_PAUSE_Pin|BTN_GAME_Pin|BTN_TIME_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA4 PA5 PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PD8 PD1 PD4 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_1|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_A_Pin BTN_Left_Pin BTN_Down_Pin BTN_Right_Pin
                           BTN_Up_Pin BTN_B_Pin */
  GPIO_InitStruct.Pin = BTN_A_Pin|BTN_Left_Pin|BTN_Down_Pin|BTN_Right_Pin
                          |BTN_Up_Pin|BTN_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_PWR_Pin */
  GPIO_InitStruct.Pin = BTN_PWR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BTN_PWR_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1) {
    // Blink display to indicate failure
    lcd_backlight_off();
    HAL_Delay(500);
    lcd_backlight_on();
    HAL_Delay(500);
  }
  /* USER CODE END Error_Handler_Debug */
}

void GW_EnterDeepSleep(void)
{
#ifdef SALEAE_DEBUG_SIGNALS
    debug_pins_deinit();
#endif

  // Stop SAI DMA (audio)
  HAL_GPIO_WritePin(GPIO_Speaker_enable_GPIO_Port, GPIO_Speaker_enable_Pin, GPIO_PIN_RESET);		//Disable I2S DAC

  // Enable wakup by PIN1, the power button
  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1_LOW);

  //lcd_backlight_off();

  // Leave a trace in RAM that we entered standby mode
  //boot_magic = BOOT_MAGIC_STANDBY;

  // Delay 500ms to give us a chance to attach a debugger in case
  // we end up in a suspend-loop.
  for (int i = 0; i < 10; i++) {
      //wdog_refresh();
      HAL_Delay(50);
  }
  // Deinit the LCD, save power.
  lcd_deinit(&hspi2);

  HAL_PWR_EnterSTANDBYMode();

  // Execution stops here, this function will not return
  while(1) {
    // If we for some reason survive until here, let's just reboot
    HAL_NVIC_SystemReset();
  }

}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
