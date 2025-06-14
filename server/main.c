#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>
#include <signal.h>
#include <pigpio.h>

const uint32_t red_led = 21;
volatile sig_atomic_t signal_received = 0;


int32_t SERIAL_ID = -1;

void sigint_handler(int signal)
{
    signal_received = signal;
    gpioSetMode(red_led, PI_INPUT);
    gpioTerminate();
    close(SERIAL_ID);
}

int32_t terminal_init(int32_t serial)
{
    if (serial< 0) { return -1; }
    struct termios options;

    if(tcgetattr(serial, &options) < 0) { return -1; }

    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);

    options.c_iflag &= ~BRKINT;
    options.c_iflag &= ~IMAXBEL;
    options.c_lflag &= ~ECHO;

    if (tcsetattr(serial, TCSANOW, &options) < 0) { return -1; }
    return 0;
}

void print_entry()
{
    printf("################################################\n");
    printf("#                                              #\n");
    printf("# Successfully initialized GPIO interface..... #\n");
    printf("# Successfully opened serial port............. #\n");
    printf("# Successfully configured terminal port....... #\n");
    printf("# Server is ready............................. #\n");
    printf("#                                              #\n");
    printf("# Press CTRL+C for shutting down.............. #\n");
    printf("################################################\n");

}

int32_t main() {
    if(gpioInitialise() == PI_INIT_FAILED) {
        perror("Failed to initialize the GPIO interface\n");
	return -1;
    }

    SERIAL_ID = open("/dev/ttyGS0", O_RDWR);
    if (SERIAL_ID < 0) {
        perror("Error opening serial port");
        return -1;
    }

    if(terminal_init(SERIAL_ID)) {
        perror("Failed to setup terminal connection!\n");
	return -1;
    }

    print_entry();

    tcflush(SERIAL_ID, TCIOFLUSH);
    sleep(1);
    char buffer[100];
    int32_t read_size = 0;
    gpioSetMode(red_led, PI_OUTPUT);
    signal(SIGINT, sigint_handler);

    while (!signal_received) {
        read_size = read(SERIAL_ID, buffer, sizeof(buffer) - 1);  // Read response
        if (read_size > 1) {
            buffer[read_size] = '\0';
            printf("Command: %s\n", buffer);
            if (!strcmp(buffer, "Turn on\n")) {
                gpioWrite(red_led, PI_HIGH);
                time_sleep(1);
                tcflush(SERIAL_ID, TCIOFLUSH);
                write(SERIAL_ID, "Done\n", 5);
                printf("Turn on -> Done\n");
                tcdrain(SERIAL_ID);
                sleep(1);
            }
            else if (!strcmp(buffer, "Turn off\n")) {
                gpioWrite(red_led, PI_LOW);
                time_sleep(1);
                tcflush(SERIAL_ID, TCIOFLUSH);
                write(SERIAL_ID, "Done\n", 5);
                printf("Turn off -> Done\n");
                tcdrain(SERIAL_ID);
                sleep(1);
            }
            else {
                printf("Unhandled message: %s\n", buffer);
            }
        }
        sleep(1);  // Wait for next data
    }

    gpioTerminate();
    close(SERIAL_ID);
    return 0;
}
