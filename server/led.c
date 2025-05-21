#include<stdio.h>
#include<signal.h>
#include<pigpio.h>

const uint32_t red_led = 21;
volatile sig_atomic_t signal_received = 0;

void siginit_handler (int signal)
{
	signal_received = signal;
}

int main (int argc, char** argv)
{
	if (gpioInitialise() == PI_INIT_FAILED) {
		printf("ERROR: Failed to initialize the GPIO interface\n");
		return 1;
	}
	
	gpioSetMode(red_led, PI_OUTPUT);
	signal(SIGINT, siginit_handler);
	printf("Press CTRL-C to exit\n");
	while (!signal_received) {
		gpioWrite(red_led, PI_HIGH);
		time_sleep(1);
		gpioWrite(red_led, PI_LOW);
		time_sleep(1);
	}

	gpioSetMode(red_led, PI_INPUT);
	gpioTerminate();
	printf("\n");
	return 0;
}
