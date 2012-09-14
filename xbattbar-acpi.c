/*
 * xbattbar: yet another battery watcher for X11
*/

/*
 * Copyright (c) 2007 Matteo Marchesotti <matteo.marchesotti@fsfe.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libacpi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h> 
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <X11/Xlib.h>
#include <libgen.h>

#include <unistd.h>
#include <locale.h>
#include <xosd.h>

#define _GNU_SOURCE

#define VERSION "0.4.0"
#define DefaultFont "fixed"
#define DiagXMergin 20
#define DiagYMergin 5

#define BI_Horizontal	((position & 2) == 0)

#ifdef DEBUG
	FILE* out = NULL;
	char path[50];

	char buffer[30];
	char msg[50];
	struct timeval tv;

	time_t curtime;
	
	#define DBG(x...) {						\
		char* s = x;						\
		gettimeofday(&tv,NULL);					\
		curtime=tv.tv_sec;					\
		strftime(buffer,30,"%m-%d-%Y  %T.",localtime(&curtime));\
		sprintf(msg,"%s%ld: %s\n",buffer,tv.tv_usec,s);		\
		fprintf(out,msg); \
		fflush(out);\
	}

#else
	#define DBG(x...) 
#endif

bool lock = false; 

//The battery to check (0: default)
int battery_number = 0;

// Default value of battery bar
enum pos { bottom, top, left, right } position = bottom;

// Indicator colors
unsigned long onin, onout;
unsigned long offin, offout;

// Indicator default colors
char *ONIN_C   = "green";
char *ONOUT_C  = "olive drab";
char *OFFIN_C  = "green";
char *OFFOUT_C = "red";

int bi_size = 3;		

Display *disp;
Window winbar;                  
Window winstat = -1;            
GC gcbar;
GC gcstat;
unsigned int width, height;

xosd *osd;
char* text_color = "green";
int shadow_offset = 2;
char* osd_vertical_position = "";
char* osd_horizontal_position = "";

//--------------------------------------- DRAW FUNCTION ---------------------------------------
void draw(charge_state_t charge_state, int percentage)
{
	DBG("Start draw");
	
	int pos;
	unsigned long in_foreground, out_foreground;


	//Color selection
	if(charge_state == C_CHARGE){
		in_foreground = onin;
		out_foreground = onout;
	} else {
		in_foreground = offin;
		out_foreground = offout;
	}

	if (BI_Horizontal) {
		pos = width * percentage / 100;
		XSetForeground(disp, gcbar, in_foreground);
		XFillRectangle(disp, winbar, gcbar, 0, 0, pos, bi_size);
		XSetForeground(disp, gcbar, out_foreground);
		XFillRectangle(disp, winbar, gcbar, pos, 0, width, bi_size);
	} else {
		pos = height * percentage / 100;   
		XSetForeground(disp, gcbar, in_foreground);
		XFillRectangle(disp, winbar, gcbar, 0, height-pos, bi_size, height);
		XSetForeground(disp, gcbar, out_foreground);  
		XFillRectangle(disp, winbar, gcbar, 0, 0, bi_size, height-pos);
	}
	//XFlush(disp);
	DBG("End draw");

	return;
}

//-------------------------------------- BATTERY CHECK ---------------------------------------------
battery_t* battery_check()
{
	DBG("Start battery_check");
	battery_t* binfo;
	global_t *global = malloc (sizeof (global_t));
	int battstate;
	
	//Initialize battery and ac state
	DBG("Call init_acpi_batt");
  	battstate = init_acpi_batt(global);
	DBG("Return from init_acpi_batt");

	if(battstate == SUCCESS) {
		if(battery_number > global->batt_count)	{
			fprintf(stderr, "Battery %i doesn't exist!\n", battery_number);
			exit(EXIT_FAILURE);
		}
		
		binfo = &batteries[battery_number];
		DBG("CALL read_acpi_batt");
		read_acpi_batt(battery_number);
		DBG("Return from read_acpi_batt");
	} else {
		fprintf(stderr, "Battery information not supported by your kernel!\n");
		exit(EXIT_FAILURE);
	}

	free(global);

	DBG("End battery_check");

	return binfo;
}

void check(int signum)
{
	DBG("Start check");
	
	battery_t* binfo = NULL;
	if(!lock){
		lock = true;
		binfo = battery_check();
		lock = false;
	} else {
		return;
	}
	
	draw(binfo->charge_state,binfo->percentage);
	
	DBG("End check");	
	
	return;
}

//------------------------------------ X function --------------------------------------
// alloc_color: convert color name to pixel value
Status alloc_color(char *name, unsigned long *pixel)
{
	DBG("Start alloc_color");
	XColor color,exact;
	int status;

	status = XAllocNamedColor(disp, DefaultColormap(disp, 0), name, &color, &exact);
	*pixel = color.pixel;

	DBG("End alloc_color");
	
	return status;
}

//init_display: create small window in top, left, right or bottom
int init_display(void)
{
	DBG("Start init_display");

	Window root;
	int x,y;
	unsigned int border,depth;
	XSetWindowAttributes att;
	int bi_height = bi_size;                      /* height of Battery Indicator */
	int bi_width = width;                       /* width of Battery Indicator */
	int bi_x=0;                           /* x coordinate of upper left corner */
	int bi_y=0;                           /* y coordinate of upper left corner */

	if((disp = XOpenDisplay(NULL)) == NULL)	{
		fprintf(stderr, "xbattbar: can't open display.\n");
		return -1;
	}
	
	if(XGetGeometry(disp, DefaultRootWindow(disp), &root, &x, &y,&width, &height, &border, &depth) == 0) {
		fprintf(stderr, "xbattbar: can't get window geometry\n");
		return -1;
	}
	
	if (!alloc_color(ONIN_C,&onin) || !alloc_color(OFFOUT_C,&offout) ||
		!alloc_color(OFFIN_C,&offin) || !alloc_color(ONOUT_C,&onout)) {
		fprintf(stderr, "xbattbar: can't allocate color resources or unknow color\n");
		return -1;
	}

	switch (position) {
		case top: /* (0,0) - (width, bi_size) */
			bi_width = width;
			bi_height = bi_size;
			bi_x = 0;
			bi_y = 0;
			break;
		
		case bottom:
			bi_width = width;
			bi_height = bi_size;
			bi_x = 0;
			bi_y = height - bi_size;
			break;
		
		case left:
			bi_width = bi_size;
			bi_height = height;
			bi_x = 0;
			bi_y = 0;
			break;

		case right:
			bi_width = bi_size;
			bi_height = height;
			bi_x = width - bi_size;
			bi_y = 0;
	}

	winbar = XCreateSimpleWindow(disp, DefaultRootWindow(disp),
                              bi_x, bi_y, bi_width, bi_height,
                              0, BlackPixel(disp,0), WhitePixel(disp,0));

	// make this window without its titlebar
	att.override_redirect = True;
	XChangeWindowAttributes(disp, winbar, CWOverrideRedirect, &att);
	
	XMapWindow(disp, winbar);
	
	gcbar = XCreateGC(disp, winbar, 0, 0);

	DBG("End init_display");
	return 0;
}


void disposediagbox(void)
{
	DBG("Start disposediagbox");

	xosd_destroy (osd);

	DBG("End dispostediagbox");
}

int* get_time(int minutes_remaining)
{
	DBG("Start get_time");
	int* time = malloc(2*sizeof(int));
	int hours, minutes;

	hours = minutes_remaining / 60;

	minutes = minutes_remaining - (60 * hours);
	
	//time[0]=hours, time[1]=minutes
	time[0]=hours;
	time[1]=minutes;

	DBG("End get_time");
	return time;
}

char* generate_message(battery_t *binfo)
{
	DBG("Start generate_message");
	char* diagmsg=NULL;
	size_t len;

	int* time;
	
	if(!binfo->present) {
		//No battery
		len = strlen("no battery")*sizeof(char)+1;
		diagmsg = malloc(len);
		strncpy(diagmsg,"no battery", len);
	} else {
		//Battery
		switch(binfo->charge_state)
		{
			//Battery full
			case C_CHARGED:
				len = strlen("AC on-line. Battery level is 100%")*sizeof(char)+1;
				diagmsg= malloc(len);
				strncpy(diagmsg,"AC on-line. Battery level is 100%",len);
				break;

			//Battery charging
			case C_CHARGE:
				if (binfo->present_rate==0){
					len=strlen("charging at zero rate - will never fully charge")*sizeof(char)+1;
					diagmsg = malloc(len);
					strncpy(diagmsg,"charging at zero rate - will never fully charge",len);
				} else {
					//for acpi bug in some laptop
					if(binfo->percentage==100) {
						len = strlen("AC on-line. Battery level is 100%")*sizeof(char)+1;
						diagmsg= malloc(len);
						strncpy(diagmsg,"AC on-line. Battery level is 100%",len);
					} else {
						//Generate time
						time = get_time(binfo->charge_time);

						len = strlen("AC on-line. Battery level is 100%. "
								"Charging remain: 24 hr. 60 min. 60 sec.")*sizeof(char)+1;
						diagmsg = malloc(len);

						snprintf(diagmsg,len,"AC on-line. Battery level is %i%%. "
								"Charging remain: %2d hr. %2d min.",
								binfo->percentage, time[0], time[1]);
						free(time);
					}
				}
				break;

			//Battery discharging (no ac_line)
			case C_DISCHARGE:
				//Generate time
				time=get_time(binfo->remaining_time);

				len = strlen("AC off-line. Battery level is 99%. "
						"Battery remain: 24 hr. 60 min. 60 sec.")*sizeof(char)+1;
				diagmsg = malloc(len);
				snprintf(diagmsg,len,"AC off-line. Battery level is %i%%. "
						"Battery remain: %2d hr. %2d min.",
						binfo->percentage, time[0], time[1]);
				free(time);
				break;

			//Error
			case C_NOINFO:
			case C_ERR:
				len = strlen("Error getting battery info")*sizeof(char)*1;
				diagmsg = malloc(len);
				strncpy(diagmsg,"Error getting battery info",len);
				break;
		}
	}

	DBG("End generate_message");
	return diagmsg;
}

void showdiagbox()
{
	DBG("Start showdiagbox");
	char* diagmsg;
	battery_t* binfo = NULL;

	if(!lock){
		lock = true;
		binfo = battery_check();
		lock = false;
	} else {
		return;
	}

	// Get the right message
	diagmsg = generate_message(binfo);

	osd = xosd_create (1);
	if (osd == NULL){
		fprintf(stderr,"Could not create \"osd\"");
		return;
	}

	xosd_set_font (osd, "-adobe-helvetica-bold-r-normal-*-*-120-*-*-p-*-iso8859-1"); 
	xosd_set_colour (osd, text_color);
	xosd_set_shadow_offset (osd, shadow_offset);

	if (strcasecmp(osd_vertical_position, "top") == 0) 
		xosd_set_pos (osd, XOSD_top);
	else if (strcasecmp(osd_vertical_position, "bottom") == 0) 
		xosd_set_pos (osd, XOSD_bottom);
	else
		xosd_set_pos (osd, XOSD_middle);
	
			
	if (strcasecmp(osd_horizontal_position, "left") == 0) 
		xosd_set_align (osd, XOSD_left);
	else if (strcasecmp(osd_horizontal_position, "right") == 0) 
		xosd_set_align (osd, XOSD_right);
	else 
		xosd_set_align (osd, XOSD_center);

	xosd_display (osd, 0, XOSD_string, diagmsg);

	DBG("End showdiagbox");
	return; 
}

//--------------------------------------------- MAIN -----------------------------------------------------
void help_message(const char* execname)
{
	printf("\nusage:\t%s [-h] [-v] [-b battery] [-a] [-t sec] [-s size]\n"
		"\t\t[-I color] [-O color] [-i color] [-o color]\n"
		"\t\t[-p  top | bottom | left | right ]\n"
		"\t\t[-C osd text's color] [-S osd text shadow's offset]\n"
		"\t\t[-V top | middle | bottom] [-H left | center | right]\n\n"
		"-h:\tshow this message.\n"
		"-v:\tshow version.\n\n"
		"-b:\tset the battery to check (0: default)\n"
		"-a:\talways on top.\n"	
		"-s:\tsize of bar indicator. [default: 3 pixels]\n"
		"-t:\tpolling time. [default: 5 sec.]\n"
		"-p:\tbar's position. [default: bottom]\n" 
		"-I, -O: bar colors in AC on-line. [def: \"green\" & \"olive drab\"]\n"		
		"-i, -o: bar colors in AC off-line. [def: \"green\" and \"red\"]\n"
		"-C:\tosd text color. [def: \"green\"]\n"
		"-S:\tosd text shadow's offset. [def: 2]\n"
		"-V, -H: vertical and horizontal position of osd text.\n\n", execname);

	return; 
}
int main(int argc, char **argv)
{
#ifdef DEBUG
	sprintf(path,"%s/.xbattbar_dbg",getenv("HOME"));
	out = fopen(path,"w+");
	if(out==NULL){
		fprintf(stderr,"xbattbar -debug: can't open log file");
		return EXIT_FAILURE;
	}
#endif

	int ch;
	int polling_time = 5;		
	bool always_on_top = false;
	struct itimerval alarm;     /* APM polling interval timer */
	XEvent event;

	char* execname = basename(argv[0]);
	// command line parser
	while ((ch = getopt(argc, argv, "as:b:I:i:C:S:O:o:t:p:vhV:H:")) != -1) {
		switch (ch) {
			case 'h':
				help_message(execname);
				return EXIT_SUCCESS;

			case 'a':
				always_on_top = true;
				break;
			
			case 'b':
				battery_number = atoi(optarg);
				if(battery_number < 0) {
					printf("Invalid battery number\n");
					return EXIT_FAILURE;
				}
				break;
			case 's':
				bi_size = atoi(optarg);
				if(bi_size <= 0) {
					printf("Invalid bar size\n");
					return EXIT_FAILURE;
				}

				break;
				
			case 'I':
				ONIN_C = optarg;
				break;

			case 'i':
				OFFIN_C = optarg;
				break;    

			case 'O':
				ONOUT_C = optarg;
				break;

			case 'o':
				OFFOUT_C = optarg;
				break;

			case 'C':
				text_color = optarg;
				break;

			case 'S':
				shadow_offset = atoi(optarg);
				break;

			case 'V':
				if ( (strcasecmp(optarg, "top") == 0) || (strcasecmp(optarg, "middle") == 0) 
						|| (strcasecmp(optarg,"bottom") ==0)) {
					osd_vertical_position = optarg;
				} else {
					printf("%s: wrong vertical osd text position\n",execname);
					return EXIT_FAILURE;
				}
				break;

			case 'H':
				if ( (strcasecmp(optarg, "left") == 0) || (strcasecmp(optarg, "right") == 0) 
						|| (strcasecmp(optarg,"center") ==0)) {
					osd_horizontal_position = optarg;
				} else {
					printf("%s: wrong horizontal text position\n",execname);
					return EXIT_FAILURE;
				}

				break;

			case 't':
				polling_time = atoi(optarg);
				if(polling_time<0) {
					printf("Invalid polling time\n");
					return EXIT_FAILURE;
				}
				break;
			
			case 'p':
				if (strcasecmp(optarg, "top") == 0) {
					position = top;
				} else if (strcasecmp(optarg, "bottom") == 0) {
					position = bottom;
				} else if (strcasecmp(optarg, "left") == 0){
					position = left;
				} else if (strcasecmp(optarg, "right") == 0){
					position = right;
				} else {
					printf("%s: wrong position\n",execname);
					return EXIT_FAILURE;
				}
				break;

			case 'v':
				printf(VERSION"\n");
				return EXIT_SUCCESS;

		}
	}

	if(check_acpi_support() == -1){
		fprintf(stderr,"No acpi support for your system?\n");
		return EXIT_FAILURE;
	}
	
	//X Window main loop
	if(init_display()==-1)
		return EXIT_FAILURE;

	XSelectInput(disp, winbar, ExposureMask|EnterWindowMask|LeaveWindowMask|VisibilityChangeMask);
	
	//Init
	battery_t *binfo = battery_check();
	draw(binfo->charge_state,binfo->percentage);
	
	if(signal(SIGALRM, check)==SIG_ERR) {
		fprintf(stderr,"xbattbar: error setting signal handler\n");
		return EXIT_FAILURE;
	}

	// set polling interval timer
	alarm.it_interval.tv_sec = polling_time;
	alarm.it_interval.tv_usec = 0;
	alarm.it_value.tv_sec = 1;
	alarm.it_value.tv_usec = 0;
	if ( setitimer(ITIMER_REAL, &alarm, NULL) != 0 ) {
		fprintf(stderr,"xbattbar: can't set interval timer\n");
		return EXIT_FAILURE;
	}
	

	while (1) {
		XWindowEvent(disp, winbar, ExposureMask|EnterWindowMask|LeaveWindowMask|VisibilityChangeMask, &event);

		DBG("XWindowEvent");
		switch (event.type) {
			case Expose: // we redraw our window since our window has been exposed.
				if(!lock){
					lock = true;
					binfo = battery_check();
					lock = false;
				} else {
					break;
				}

				draw(binfo->charge_state,binfo->percentage);
				break;
				
			case EnterNotify: // create battery status message
				showdiagbox(binfo->charge_state,binfo->percentage);
				break;
				
			case LeaveNotify: // destroy status window 
				disposediagbox();
				break;
			
			case VisibilityNotify:
				if (always_on_top) 
					XRaiseWindow(disp, winbar);
				break;
    		}
	}

	return EXIT_SUCCESS;
}
