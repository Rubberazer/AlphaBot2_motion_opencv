#include <iostream>	/* for standard I/O in C++ */
#include <cstdio>	/* for printf */
//#include <stdint.h>	/* for uint64 definition */
#include <cstdlib>	/* for exit() definition */
#define _BSD_SOURCE	/* this one makes time.h to work properly */
#include <sys/time.h>	/* for clock_gettime */
#include <cmath>	/* for mathematical funtions */
#include <pthread.h>    /* for threading out loud*/
#include <pigpio.h>     /* for handling the GPIO */
#include <csignal>	/* for catching exceptions e.g. control-C */
#include <unistd.h>	/* this one is to make usleep() work */
#include <opencv2/core/core.hpp>	/*this one and the ones that follow are the opencv stuff */
#include <opencv2/core/utility.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace std;
using namespace cv;

#define DR 19 /*BCM pin 16, this is an infrared diode*/
#define DL 16 /*BCM pin 19, this is an infrared diode*/
#define TRIGGER 22 /*BCM pin 22, sending ultrasound cry*/
#define ECHO 27 /*BCM pin 27, receiving ultrasound*/
#define AIN1 12 /*BCM pin 12, to spin motor backward*/
#define AIN2 13 /*BCM pin 13, to spin motor forward*/
#define BIN1 20 /*BCM pin 20, to spin motor backward*/
#define BIN2 21 /*BCM pin 21, to spin motor forward*/
#define ENA 6 /*BCM pin 6, motor PWM A*/
#define ENB 26 /*BCM pin 26, motor PWM B*/

/*The following structure defines all the inputs from th sensorial parts , it is meant to act 
as a global variable for the threads*/

typedef struct
{
  double distance; /*distance to the obstacle from the ultrasounds sensor*/
  int ultrimpct; /*near frontal impact to the wall, from the ultrasounds */ 
  int dr; /* near impact to the wall on the right side, from right infrared diode, GPIO == 0*/
  int dl; /* near impact to the wall on the left side, from left infrared diode, GPIO == 0*/ 
  int block; /* this is to avoid the robot from getting stuck in obstacles not detected by the sensors */
  int movement; /*this one indicates movement detection */
  int disablecv; /*this one disables motion detection */
  int *servo; /*this one is the handler returned by I2Copen() */
} PARAM;

/* Define globally accessible variables no mutex */
PARAM parameters;

/* This global variable is used to interrupt the infite while loops with signaction()
 this is important as if not used and tryiong to stop the program with Ctrl-c,
 the program might go full speed against the  wall (it happened)*/
static volatile int interrupt = 1;

/*
These functions below are for - in descending order- ultrasounds, infrared, 
 moving forward.. 
*/

void *Distance(void *arg)
{
gpioSetMode(ECHO, PI_INPUT); 
gpioSetMode(TRIGGER, PI_OUTPUT); 
struct timeval t1, t2, t3;
double time_interval,distance, distbuff;
int count;
char s[100];
time_t now;
struct tm *t;
FILE *f;

f = fopen("record", "w+");
if (f == NULL)
{
    printf("Error opening file!\n");
    exit(1);
}
while (interrupt)
{
time_interval = 0;
distance = 0;

gettimeofday(&t3, NULL);

gpioWrite(TRIGGER, 1);
usleep(15);
gpioWrite(TRIGGER, 0);

while (!gpioRead(ECHO))
	{
	gettimeofday(&t1, NULL);
	gettimeofday(&t2, NULL);
	time_interval = (t2.tv_sec-t3.tv_sec) * 1000000; 
        time_interval = (time_interval + (t2.tv_usec-t2.tv_usec))/1000000; 
	if (time_interval > 0.05)
       	    break;
	}

while (gpioRead(ECHO))
	{
	gettimeofday(&t2, NULL);
	if (time_interval > 0.05)
       	    break;
	}
if (time_interval < 0.05)
{
time_interval = (t2.tv_sec-t1.tv_sec) * 1000000; 
time_interval = (time_interval + (t2.tv_usec-t1.tv_usec))/1000000; 
distance = time_interval*17150;
parameters.distance = distance;
if ((abs(distance - distbuff)) < 2)
{
count++;	
}
if (count>7)
{
parameters.block = 1;
count = 0;
}
else
{
parameters.block = 0;
}
distbuff = distance;
if(distance<20) 
parameters.ultrimpct = 1;
else
parameters.ultrimpct = 0;
time(&now);
t = localtime(&now);
strftime(s, 100, "%H:%M:%S", t);
fprintf(f,"%s, %f\n",s ,distance);
}
usleep(250000);
}
fclose(f);
pthread_exit(NULL);
}
void *Infrared(void *arg)
{
gpioSetPullUpDown(DR, PI_PUD_UP); 
gpioSetPullUpDown(DL, PI_PUD_UP); 

while (interrupt)
{
parameters.dr = !gpioRead(DR);
parameters.dl = !gpioRead(DL);
usleep(1000); /* in microseconds */
}
pthread_exit(NULL);
}
void *Camera(void *arg)
{
int countmov = 0;
vector<vector<Point> > contours; /* this creates a vector object to store the contours detectec */
Mat frame; /* object frame to store the camera capture */    
Mat fgMaskMOG2; /* Foreground mask generated by MOG2 method */  
Ptr<BackgroundSubtractor> pMOG2; /* MOG2 Background subtractor */
pMOG2 = createBackgroundSubtractorMOG2(50,200,false); /* Create MOG2 Background Subtractor object */

VideoCapture cap(0); /*starts camera capture (0 means camera) */
/*cap.set(CAP_PROP_FRAME_WIDTH,1024); /*set camera resolution */
/*cap.set(CAP_PROP_FRAME_HEIGHT,768); /* set camera resolution */
/*cap.set(CAP_PROP_BUFFERSIZE, 3);*/
    /* Check if camera opened successfully */
    if(!cap.isOpened())
    {
    cout << "Error opening video stream or file" << endl;
    pthread_exit(NULL);
    }
while(interrupt)
    {
	while (!parameters.disablecv)
	{
        cap >> frame; /* Capture the current frame */
        pMOG2->apply(frame, fgMaskMOG2); /* Update the MOG2 background model based on the current frame */
        //imshow("Frame", frame); /* Show the current frame */
        //imshow("FG Mask MOG 2", fgMaskMOG2); /*Show the MOG2 foreground mask */
	findContours(fgMaskMOG2, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
	//Mat output= Mat::zeros(fgMaskMOG2.rows,fgMaskMOG2.cols, CV_8UC3);
	
	/* Check the number of objects detected */
	cout << contours.size()<< endl;
	if(contours.size() <50)
	{
		parameters.movement = 0;
		countmov = 0;
		/*cout << contours.size()<< endl;*/
		//return;
	}
	else
	{
		countmov++;
		if (countmov > 2) 
		{
			parameters.movement = 1; 
			countmov = 0;
			bool saver = imwrite("./capture.jpg", frame);
		}
		//cout << "Something is moving. Number of objects detected: " << contours.size() << endl;
	}
	usleep(10000);
	}
    }
cap.release();
pthread_exit(NULL);
}
void Forward(void)
{
gpioPWM(ENA, 100); /*Set ENA to a low number, maximum is 255*/
gpioPWM(ENB, 100); /*Set ENB to low number, maximum is 255*/
gpioWrite(AIN1, 0);
gpioWrite(AIN2, 1);
gpioWrite(BIN1, 0);
gpioWrite(BIN2, 1);
}
void Stop(void)
{
gpioWrite(AIN1, 0);
gpioWrite(AIN2, 0);
gpioWrite(BIN1, 0);
gpioWrite(BIN2, 0);
gpioPWM(ENA, 0); 
gpioPWM(ENB, 0);
}

void Backward(void) /*Backward, also used for a hard stop*/
{
gpioWrite(AIN1, 1);
gpioWrite(AIN2, 0);
gpioWrite(BIN1, 1);
gpioWrite(BIN2, 0);
gpioPWM(ENA, 70); 
gpioPWM(ENB, 70);
}
void Turnright(void) /*Turn right*/
{
gpioWrite(AIN1, 0);
gpioWrite(AIN2, 1);
gpioWrite(BIN1, 1);
gpioWrite(BIN2, 0);
gpioPWM(ENA, 30); 
gpioPWM(ENB, 30);
}
void Turnleft(void) /*Turn left*/
{
gpioWrite(AIN1, 1);
gpioWrite(AIN2, 0);
gpioWrite(BIN1, 0);
gpioWrite(BIN2, 1);
gpioPWM(ENA, 30); 
gpioPWM(ENB, 30);
}
void ServoCentre(int servo) /* This one centers both vertical and horizontal servos */
{
float pulse_1;
int pulse;
/* These ones would be for the vertical servo */
pulse_1 = 1200; /* 1500 should be the centered position, 1000 is up and 2000 is down */
pulse_1 = (pulse_1*4096)/20000;
pulse = (int) pulse_1;
/*printf("Pulse == %d\n ", pulse);*/
i2cWriteByteData(servo, 0x0A, 0x00);
i2cWriteByteData(servo, 0x0B, 0x00);
i2cWriteByteData(servo, 0x0C, pulse & 0xFF);
i2cWriteByteData(servo, 0x0D, pulse >> 8);
usleep(50000);

/* These ones would be for the horizontal servo */ 
pulse_1 = 1500; /* 1500 should be the centered position, 1000 is left and 2000 is right */
pulse_1 = (pulse_1*4096)/20000;
pulse = (int) pulse_1;
i2cWriteByteData(servo, 0x06, 0x00);
i2cWriteByteData(servo, 0x07, 0x00);
i2cWriteByteData(servo, 0x08, pulse & 0xFF);
i2cWriteByteData(servo, 0x09, pulse >> 8);
usleep(50000);
i2cWriteByteData(servo, 0xFD, 0x10); /* Shutting down all PWM channels */
i2cWriteByteData(servo, 0x00, 0x00); /* Resetting the PCA9685 last thing */
}
void ServoLeft(int servo) /* This one takes the horizontal servo to the left */
{
float pulse_1;
int pulse;
/* These ones would be for the horizontal servo */ 
pulse_1 = 1000; /* 1500 should be the centered position, 1000 is left and 2000 is right */
pulse_1 = (pulse_1*4096)/20000;
pulse = (int) pulse_1;
i2cWriteByteData(servo, 0x06, 0x00);
i2cWriteByteData(servo, 0x07, 0x00);
i2cWriteByteData(servo, 0x08, pulse & 0xFF);
i2cWriteByteData(servo, 0x09, pulse >> 8);
usleep(50000);
i2cWriteByteData(servo, 0xFD, 0x10); /* Shutting down all PWM channels */
i2cWriteByteData(servo, 0x00, 0x00); /* Resetting the PCA9685 last thing */
}
void ServoRight(int servo) /* This one takes the horizontal servo to the right */
{
float pulse_1;
int pulse;
/* These ones would be for the horizontal servo */ 
  pulse_1 = 2000; /* 1500 should be the centered position, 1000 is left and 2000 is right */
  pulse_1 = (pulse_1*4096)/20000;
  pulse = (int) pulse_1;
  i2cWriteByteData(servo, 0x06, 0x00);
  i2cWriteByteData(servo, 0x07, 0x00);
  i2cWriteByteData(servo, 0x08, pulse & 0xFF);
  i2cWriteByteData(servo, 0x09, pulse >> 8);
  usleep(50000);
  i2cWriteByteData(servo, 0xFD, 0x10); /* Shutting down all PWM channels */
  i2cWriteByteData(servo, 0x00, 0x00); /* Resetting the PCA9685 last thing */
}
void ServoMove(int x, int servo)
{
switch (x)
{
  case 1:
	ServoLeft(servo);
	break;
  case 2:
	ServoRight(servo);
	break;
  default: 
	ServoCentre(servo);
	break;
}
}
int randomizer(void) /* initializer to keep the robot moving for a number of seconds */
{
  int isecret;
  srand (time(NULL));
  isecret = rand() % 10 + 5;
  return isecret;
}

/* the next function is the signal() function handler, is critical to 
   avoid damage to the robot*/
void inthandler(int signum) 
{
   interrupt = 0;
   Stop();
   usleep(10000);
   i2cWriteByteData(*parameters.servo, 0xFD, 0x10); /* Shutting down all PWM channels */
   i2cWriteByteData(*parameters.servo, 0x00, 0x00); /* Resetting the PCA9685 last thing */
   i2cClose(*parameters.servo);
   gpioTerminate();
   
   printf("Caught signal %d, coming out...\n", signum);
   exit(1);
}
 
int main(int argc, char *argv[])
{
pthread_t callThd[3];
pthread_attr_t attr;
void *status;
int i;
int r = 0;
int pth_err;
int Init; 
int servo;
float freq;
int oldmode;
int newmode;

Init = gpioInitialise();
if (Init < 0)
{
   /* pigpio initialisation failed */
   printf("Pigpio initialisation failed. Error code:  %d\n", Init);
   exit(Init);
}
else
{
   /* pigpio initialised okay*/
   printf("Pigpio initialisation OK. Return code:  %d\n", Init);
}

gpioSetMode(AIN1, PI_OUTPUT);
gpioSetMode(AIN2, PI_OUTPUT);
gpioSetMode(BIN1, PI_OUTPUT);
gpioSetMode(BIN1, PI_OUTPUT);
gpioSetMode(ENA, PI_OUTPUT);
gpioSetMode(ENB, PI_OUTPUT);
gpioSetPWMfrequency(ENA, 50000); /*Set ENA to 50khz*/
gpioSetPWMfrequency(ENB, 50000); /*Set ENA to 50khz*/

/*Setting up the servo here */
servo = i2cOpen(1,0x40,0);
parameters.servo = &servo;
if ( servo >=  0)
{
   /* PI connection to I2C slave 40 OK*/
   printf("Open I2C to slave 0x40 OK. Return code:  %d\n", Init);
   
}
else
{
   /* No PI connection to I2C slave 40 */
   printf("Open I2C to slave 0x40 OK failed. Error code:  %d\n", Init);
   exit(servo);
}
/* Setting the PCA9685 frequency, must be 50Hz for the SG-90 servos */

i2cWriteByteData(servo, 0x00, 0x00); /* Resetting the PCA9685 first thing */
	
freq = (25000000/(4096*50)) - 0.5; /* now 25*10^6 is 25Mhz of the internal clock, 4096 is 12 bit resolution and 50hz is the wanted frequency setup) */
freq = (int) freq;
/* now there is a whole sequence to set up the frequency */

oldmode = i2cReadByteData(servo, 0x00); /* getting current mode */
newmode = (oldmode & 0x7F) | 0x10; /* sleep mode definition */
i2cWriteByteData(servo, 0x00, newmode); /* going to sleep now */
i2cWriteByteData(servo, 0xFE, freq); /* setting up the frequency now */
i2cWriteByteData(servo, 0x00, oldmode); /* coming back to the old mode */
usleep(5000);
i2cWriteByteData(servo, 0x00, oldmode | 0x80); /* final step on frequency set up */

/* control signal() here */
signal(SIGINT, inthandler);
	
/* Create threads to start seeing, will not use attributes on this occasion*/  
pthread_attr_init(&attr);
pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

pth_err = pthread_create(&callThd[0], &attr, Distance, NULL); 
if (pth_err !=0)
{
printf("Thread 1 not created, exiting the program with error: %d\n", pth_err);
exit(1);
}

pth_err = pthread_create(&callThd[1], &attr, Infrared, NULL); 
if (pth_err !=0)
{
printf("Thread 2 not created, exiting the program with error: %d\n", pth_err);
exit(1);
}

pth_err = pthread_create(&callThd[2], &attr, Camera, NULL); 
if (pth_err !=0)
{
printf("Thread 3 not created, exiting the program with error: %d\n", pth_err);
exit(1);
}

while(interrupt)
{
/*Printing parameters*/
/*printf("Parameters.dr == %d\n", parameters.dr);
printf("Parameters.dl == %d\n", parameters.dl);
printf("Parameters.distance == %lf\n ", parameters.distance);
printf("Parameters.ultrimpct == %d\n ", parameters.ultrimpct);
printf("Parameters.block == %d\n ", parameters.block);*/
int z = 0;
int next = 0;
parameters.disablecv = 1;
ServoCentre(servo);
sleep(10);

while (!parameters.movement)
{
parameters.disablecv = 0;
if(z == 60)
{
	next++;
	z = 0;
	parameters.disablecv = 1;
	ServoMove(next, servo);
	if (next == 2)
	{
	next = 0;
	} 
	sleep(10);
}
sleep(1);
z++;
}
printf("Parameters.movement == %d\n ", parameters.movement);
r = randomizer();
parameters.disablecv = 1;
parameters.movement = 0;
ServoCentre(servo);
while (r)
{
printf("counter left == %d\n ", r);
r--;

while(!parameters.ultrimpct && !parameters.dl && !parameters.dr && !parameters.block)
{
Forward();
}
while(parameters.ultrimpct || parameters.dl || parameters.dr || parameters.block)
{
int dl = parameters.dl;
int dr = parameters.dr;
int ultrimpct = parameters.ultrimpct;
int block = parameters.block;
Backward();
usleep(700000);
Stop();

if (dl || ultrimpct || block)
{
	Turnright();
	usleep(700000);
}
if (dr)
{
	Turnleft();
	usleep(700000);
}
}
}
r = 0;
sleep(10);
}

for(i=0;i<3;i++) 
	{
  	pthread_join(callThd[i], &status);
  	}
i2cWriteByteData(servo, 0xFD, 0x10); /* Shutting down all PWM channels */
i2cWriteByteData(servo, 0x00, 0x00); /* Resetting the PCA9685 last thing */
i2cClose(servo);
gpioTerminate();
exit(0);
}
