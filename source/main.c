#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <fat.h>
#include <aesndlib.h>
#include <asndlib.h>
#include <mp3player.h>
#include <jpeg/jpgogc.h>
#include "macabre_mp3.h"

#define STATE_PLAYING 1
#define STATE_PAUSED 2
#define STATE_SETUP 3
#define STATE_ADVENTURE 4

static GXRModeObj *rmode=NULL;

extern char redmandata[];
extern int redmanlength;
extern char bluemandata[];
extern int bluemanlength;
extern char greenmandata[];
extern int greenmanlength;
extern char orangemandata[];
extern int orangemanlength;

int doReload=0, doOff=0;
u32 wpadDown[4], wpadHeld[4], padDown[4], padHeld[4];

typedef enum {
	UP,
	DOWN,
	LEFT,
	RIGHT,
	JUMP,
	FALL,
	PAUSE,
	QUIT,
	A,
	B
} control;

void reload(void) {
	doReload=1;
}

void shutdown(void) {
	doOff=1;
}

u32 rgb(int r, int g, int b) {
	double fr = r/255.0;
	double fg = g/255.0;
	double fb = b/255.0;
	double wr = 0.299;
	double wb = 0.114;
	double wg = 0.587;
	double umax = 127.5;
	double vmax = 127.5;
	double fy = wr*fr + wg*fg + wb*fb;
	double fu = umax*(fb-fy)/(1-wb);
	double fv = vmax*(fr-fy)/(1-wr);
	int y = 255*fy;
	int u = umax+fu;
	int v = vmax+fv;
	return (((y) << 24)|((u) << 16)|((y) << 8)|(v));
}

void drawdot(void *xfb, GXRModeObj *rmode, float w, float h, float fx, float fy, u32 color) {
	u32 *fb;
	int px,py;
	int x,y;
	fb = (u32*)xfb;
 
	y = fy * rmode->xfbHeight / h;
	x = fx * rmode->fbWidth / w / 2;
 
	for(py=y-4; py<=(y+4); py++) {
		if(py < 0 || py >= rmode->xfbHeight)
				continue;
		for(px=x-2; px<=(x+2); px++) {
			if(px < 0 || px >= rmode->fbWidth/2)
				continue;
			fb[rmode->fbWidth/VI_DISPLAY_PIX_SZ*py + px] = color;
		}
	}
 
}

void DrawHLine (void *xfb, GXRModeObj *rmode, float w, float h, float fx1, float fx2, float fy, u32 color) {
	u32 *fb;
    int i;
	fb = (u32*)xfb;
	
    int y = fy * rmode->xfbHeight/h;
    int x1 = fx1 * rmode->fbWidth/w/2;
    int x2 = fx2 * rmode->fbWidth/w/2;
    for (i = x1; i <= x2; i++) {
        fb[rmode->fbWidth/VI_DISPLAY_PIX_SZ*y+i] = color;
    }
}

void DrawVLine (void *xfb, GXRModeObj *rmode, float w, float h, float fx, float fy1, float fy2, u32 color) {
	u32 *fb;
    int i;
	fb = (u32*)xfb;
	
    int x = fx * rmode->fbWidth/w/2;
    int y1 = fy1 * rmode->xfbHeight/h;
    int y2 = fy2 * rmode->xfbHeight/h;
    for (i = y1; i <= y2; i++) {
        fb[rmode->fbWidth/VI_DISPLAY_PIX_SZ*i+x] = color;
    }
}

void DrawRect(void *xfb, GXRModeObj *rmode, float w, float h, float fx1, float fx2, float fy1, float fy2, u32 color) {
	DrawHLine(xfb, rmode, w, h, fx1, fx2, fy1, color);
	DrawHLine(xfb, rmode, w, h, fx1, fx2, fy2, color);
	DrawVLine(xfb, rmode, w, h, fx1, fx1, fy2, color);
	DrawVLine(xfb, rmode, w, h, fx2, fx1, fy2, color);
}

void FillRect(void *xfb, GXRModeObj *rmode, float w, float h, float fx1, float fx2, float fy1, float fy2, u32 color) {
	float i;
	for(i=fy1;i<=fy2; i++)
		DrawHLine(xfb, rmode, w, h, fx1, fx2, i, color);
}

void display_jpeg(void *xfb, GXRModeObj *rmode, float w, float h, float fx, float fy, JPEGIMG jpeg) {
	
	unsigned int *jpegout = (unsigned int *) jpeg.outbuffer;
	u32 *fb;
	int px,py;
	int x,y;
	fb = (u32*)xfb;
	
	y = fy * rmode->xfbHeight / h;
	x = fx * rmode->fbWidth / w / 2;
	
	int height = jpeg.height;
	int width = jpeg.width/2;
	for(py=0;py<=height-2;py++) {
		if(py+y < 0 || py+y >= rmode->xfbHeight)
			continue;
		for(px=0;px<width;px++) {
			if(px+x < 0 || px+x >= rmode->fbWidth/2)
				continue;
			fb[rmode->fbWidth/VI_DISPLAY_PIX_SZ*(py+y) + (px+x)]=jpegout[width*py + px];
		}
	}
}

int evctr = 0;

void countevs(int chan, const WPADData *data) {
	evctr++;
}

int numberOfAttachedControllers() {
	int i, numAttached = 0;
	u32 type;
 
	for(i=0; i<WPAD_MAX_WIIMOTES; i++) {
		if (WPAD_Probe(i, &type) == WPAD_ERR_NONE) {
			numAttached++;
		}
	}
	return numAttached;
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
}

void collide(int *pos, int *prevpos, int *yvel, int *onGround, int *airJumped, int *objpos, int objvel) {
	if(pos[0] + 26 > objpos[0] && pos[0] - 4 < objpos[2] && pos[1] + 26 > objpos[1] && pos[1] - 2 < objpos[3]) {
		if(prevpos[1] + 26 <= objpos[1] && *yvel <= 0) {
			*onGround = 1;
			*yvel = 0;
			pos[1] = objpos[1] - 26;
			*airJumped = 0;
			pos[0] += 2*objvel;
		}
		else if(prevpos[1] - 2 > objpos[3]) {
			*yvel = 0;
			pos[1] = objpos[3] + 2;
		}
		
		if(prevpos[0] + 26 <= objpos[0]) {
			pos[0] = objpos[0] - 26;
		}
		else if(prevpos[0] - 4 >= objpos[2]) {
			pos[0] = objpos[2] + 4;
		}
	}
}

int controlUsed(int i, control ctrl) {
	
	switch(ctrl) {
		case UP:
			return wpadDown[i] & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT) || PAD_StickY(i) > 18;
		case DOWN:
			return wpadDown[i] & (WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT) || PAD_StickY(i) < -18;
		case LEFT:
			return wpadHeld[i] & WPAD_BUTTON_UP || PAD_StickX(i) < -18;
		case RIGHT:
			return wpadHeld[i] & WPAD_BUTTON_DOWN || PAD_StickX(i) > 18;
		case JUMP:
			return wpadDown[i] & (WPAD_BUTTON_2 | WPAD_BUTTON_A) || padDown[i] & (PAD_BUTTON_A | PAD_BUTTON_Y | PAD_BUTTON_X);
		case FALL:
			return wpadDown[i] & (WPAD_BUTTON_1 | WPAD_BUTTON_B) || padDown[i] & PAD_BUTTON_B;
		case PAUSE:
			return wpadDown[i] & WPAD_BUTTON_PLUS || padDown[i] & PAD_BUTTON_START;
		case QUIT:
			return wpadDown[i] & WPAD_BUTTON_HOME;
		case A:
			return wpadHeld[i] & WPAD_BUTTON_A || padHeld[i] & PAD_BUTTON_A;
		case B:
			return wpadHeld[i] & WPAD_BUTTON_B || padHeld[i] & PAD_BUTTON_B;
	}
	return 0;	
}

int main() {
	int res;
 
	void *xfb[2];
	u32 type;
	int i, j;
	int fbi = 0;
	WPADData *wd;
 
	VIDEO_Init();
	PAD_Init();
	WPAD_Init();
 
	rmode = VIDEO_GetPreferredMode(NULL);
 
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
 
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
 
	SYS_SetResetCallback(reload);
	SYS_SetPowerCallback(shutdown);
	
	AUDIO_Init(NULL);
	ASND_Init();
	ASND_Pause(0);
	MP3Player_Init();
	
	JPEGIMG man[4];
	for(i=0; i<4; i++)
		memset(&man[i], 0, sizeof(JPEGIMG));
	man[0].inbuffer = redmandata, man[0].inbufferlength = redmanlength;
	man[1].inbuffer = bluemandata, man[1].inbufferlength = bluemanlength;
	man[2].inbuffer = greenmandata, man[2].inbufferlength = greenmanlength;
	man[3].inbuffer = orangemandata, man[3].inbufferlength = orangemanlength;
	for(i=0; i<4; i++)
		JPEG_Decompress(&man[i]);

	struct expansion_t exp;
	
	int state = STATE_SETUP;
	
	
	int players = 1;
	
	int pausedPlayer;
	
	int onGround[4], jumpsTaken[4], doubleJumpsTaken[4], jumpedOn[4], jumpsMade[4], doubleJumped[4], score[4];
	int channel[4] = {WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2, WPAD_CHAN_3}, rumble[4];
	
	int pos[4][2], prevpos[4][2], yvel[4];
	
	int objPos[7][4] = { {210, 0, 230, 430}, {390, 80, 410, 470}, {230, 0, 410, 40}, {230, 410, 350, 430}, {270, 310, 390, 340}, {230, 210, 350, 240}, {270, 80, 390, 110} };
	int obstacles = 7;//sizeof(obstacles) / (4 * sizeof(int));
	int plane[4], planeDir;
	
	for(i=0; i<4; i++) {
		WPAD_SetDataFormat(channel[i], WPAD_FMT_BTNS_ACC_IR);
		WPAD_SetVRes(i, rmode->fbWidth, rmode->xfbHeight);
	}
	
	
	while(!doReload && ! doOff) {
		
		CON_Init(xfb[fbi],0,0,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
		switch(state) {
		case STATE_PLAYING:
		case STATE_ADVENTURE:
			VIDEO_ClearFrameBuffer(rmode,xfb[fbi],COLOR_WHITE);
			break;
		default:
			VIDEO_ClearFrameBuffer(rmode,xfb[fbi],COLOR_BLACK);
		}
		WPAD_ReadPending(WPAD_CHAN_ALL, countevs);
		PAD_ScanPads();
		for(i = 0; i < 4; i++) {
			res = WPAD_Probe(i, &type);
			if(res == WPAD_ERR_NONE) {
				wd = WPAD_Data(i);
				wpadHeld[i] = WPAD_ButtonsHeld(channel[i]);
				wpadDown[i] = WPAD_ButtonsDown(channel[i]);
			}
			padHeld[i] = PAD_ButtonsHeld(channel[i]);
			padDown[i] = PAD_ButtonsDown(channel[i]);
		}
			
		
		MP3Player_PlayBuffer(macabre_mp3,macabre_mp3_size,NULL);
		
		switch(state) {
		case STATE_ADVENTURE:
			for(j=0; j<obstacles; j++) {
				FillRect(xfb[fbi], rmode, rmode->fbWidth, rmode->xfbHeight, objPos[j][0], objPos[j][2], objPos[j][1], objPos[j][3], COLOR_BLACK);
			}
		case STATE_PLAYING:
			if(state != STATE_ADVENTURE) {
				plane[0] += planeDir;
				plane[2] += planeDir;
				if(plane[0] == 0) {
					planeDir = 1;
				}
				else if(plane[2] == 640) {
					planeDir = -1;
				}
				FillRect(xfb[fbi], rmode, rmode->fbWidth, rmode->xfbHeight, plane[0], plane[2], plane[1], plane[3], COLOR_BLACK);
			}
			for(i=0; i<players; i++) {
				display_jpeg(xfb[fbi], rmode, rmode->fbWidth, rmode->xfbHeight, pos[i][0]-4, pos[i][1]-4, man[i]);
				
				if(pos[i][1] - yvel[i] >= 444) {
					onGround[i] = 1;
					pos[i][1] = 444;
					yvel[i] = 0;
					doubleJumped[i] = 0;
				}
				else {
					pos[i][1] -= yvel[i];
					yvel[i]--;
				}
				
				if(controlUsed(i, QUIT)) reload();
				if(controlUsed(i, JUMP) && !doubleJumped[i]) {
					if(onGround[i]) {
						jumpsTaken[i]++;
					}
					else {
						doubleJumpsTaken[i]++;
						doubleJumped[i] = 1;
					}
					yvel[i] = 20;
				}
				if(controlUsed(i, LEFT)) {
					pos[i][0] -= 5;
				}	
				if(controlUsed(i, RIGHT)) {
					pos[i][0] += 5;
				}
				if(controlUsed(i, FALL)) {
					yvel[i] -= 20;
				}
				if(controlUsed(i, PAUSE)) {
					state = STATE_PAUSED;
					pausedPlayer = i;
				}
				
				onGround[i] = 0;
				
				for(j=0; j<players; j++) {
					if(i == j) continue;
					if(abs(pos[i][0] - pos[j][0]) < 32) {
						if(abs(pos[j][1] - pos[i][1]) < 32) {
							if((pos[j][1] + yvel[j]) - (pos[i][1] + yvel[i]) > 32) {
								display_jpeg(xfb[fbi], rmode, rmode->fbWidth, rmode->xfbHeight, pos[i][0]-4, pos[j][1]-36, man[i]);
								yvel[i] = 20;
								yvel[j] -= 20;
								rumble[j] = 20;
								jumpsMade[i]++;
								jumpedOn[j]++;
							}
							pos[i][0] = prevpos[i][0];
						}
					}
				}
				
				if(state == STATE_PLAYING) {
					collide(pos[i], prevpos[i], &yvel[i], &onGround[i], &doubleJumped[i], plane, planeDir);
				}
				else if(state == STATE_ADVENTURE) {
					for(j=0; j<obstacles; j++) {
						collide(pos[i], prevpos[i], &yvel[i], &onGround[i], &doubleJumped[i], objPos[j], 0);
					}
				}
				
				pos[i][0] = fmin(fmax(pos[i][0], 10), 610);
				
				
				prevpos[i][0] = pos[i][0], prevpos[i][1] = pos[i][1];
				WPAD_Rumble(channel[i], fmax(fmin(rumble[i], 1), 0));
				PAD_ControlMotor(channel[i], fmax(fmin(rumble[i], 1), 0));
				rumble[i]--;
			}
			break;
		case STATE_PAUSED:
			for(i=0; i<players; i++){
				printf("\x1b[%d;%dHPlayer %d", 2 + 6*i, 36, i+1);
				printf("\x1b[%d;%dHNumber of Jumps: %d", 4 + 6*i, 16, jumpsTaken[i] + doubleJumpsTaken[i]);
				printf("\x1b[%d;%dHScore: %d", 4 + 6*i, 45, jumpsMade[i] - jumpedOn[i]);
				printf("\x1b[%d;%dHTimes Jumped on: %d", 5 + 6*i, 16, jumpedOn[i]);
				printf("\x1b[%d;%dHNumber of Succesful Jumps: %d", 5 + 6*i, 45, jumpsMade[i]);
				
				if(controlUsed(i, QUIT)) {
					state = STATE_SETUP;
				}
				WPAD_Rumble(channel[i], 0);
				PAD_ControlMotor(channel[i], 0);
			}
			if(controlUsed(pausedPlayer, PAUSE)) {
				if(players == 1) {
					state = STATE_ADVENTURE;
				}
				else {
					state = STATE_PLAYING;
				}
				pausedPlayer = -1;
			}
			break;
		case STATE_SETUP:
			printf("\x1b[%d;%dHPlayers: %d\nPress A + B to start", 2, 1, players);
			for(i=0; i<4; i++){
				res = WPAD_Probe(i, &type);
				if(res == WPAD_ERR_NONE) {
					if(controlUsed(i, QUIT)) reload();
					if(controlUsed(i, A) && controlUsed(i, B)) {
						pausedPlayer = -1;
						
						for(i=0; i<4; i++) {
							onGround[i] = 1;
							jumpsTaken[i] = 0;
							doubleJumpsTaken[i] = 0;
							jumpedOn[i] = 0;
							jumpsMade[i] = 0;
							doubleJumped[i] = 1;
							score[i] = 0;
							rumble[i] = 0;
							
							pos[i][0] = -999;
							pos[i][1] = 0;
							prevpos[i][0] = -999;
							prevpos[i][1] = -999;
							yvel[i] = 0;
						
							WPAD_SetDataFormat(channel[i], WPAD_FMT_BTNS_ACC_IR);
							WPAD_SetVRes(i, rmode->fbWidth, rmode->xfbHeight);
						}
						
						state = STATE_ADVENTURE;
						pos[0][0] = 10;
						for(i=1; i<players; i++) {
							pos[i][0] = 10 + 600 / (players - 1) * i;
							state = STATE_PLAYING;
						}
						
						plane[0] = 0, plane[1] = 230, plane[2] = 20, plane[3] = 250;
						planeDir = 1;
					}
					if(controlUsed(i, UP)) {
						players = fmin(4, players+1);
					}	
					if(controlUsed(i, DOWN)) {
						players = fmax(1, players-1);
					}
				}
			}
			break;
		}
	
		VIDEO_SetNextFramebuffer(xfb[fbi]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		fbi ^= 1;
	}
	for(i=0; i<players; i++)
		WPAD_Rumble(channel[i], 0);
		PAD_ControlMotor(channel[i], 0);
	
	if(doReload) {
		return 0;
	}
	if(doOff) SYS_ResetSystem(SYS_SHUTDOWN,0,0);
 
	return 0;
}
