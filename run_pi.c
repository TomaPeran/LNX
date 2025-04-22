#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>

int32_t terminal_init(int32_t serial)
{
    if (serial < 0) { return -1; }
    struct termios options;

    if (tcgetattr(serial, &options) < 0 ) { return -1; }

    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);

    options.c_iflag &= ~BRKINT;
    options.c_iflag &= ~IMAXBEL;
    options.c_lflag &= ~ECHO;

    if (tcsetattr(serial, TCSANOW, &options) < 0 ) { return -1; }
    return 0;
}

int32_t main() {
    int32_t serial = open("/dev/ttyGS0", O_RDWR);
    if (serial < 0) {
        perror("Error opening serial port");
        return -1;
    }

    tcflush(serial, TCIOFLUSH);
    sleep(1);

    char buffer[100];
    int32_t read_size = 0;

    if (terminal_init(serial) < 0) {
	perror("Error while initializating terminal!\n");
	return -1;
    }

    write(serial, "Hi Linux\n", 9);  // Send initial message to Linux
    tcdrain(serial);  // Ensure all data has been transmitted
    sleep(1);  // Give time for the transmission

    while (1) {
        read_size = read(serial, buffer, sizeof(buffer) - 1);  // Read response
	printf("%d\n", read_size);
        if (read_size > 1) {
            buffer[read_size] = '\0';  // Null-terminate the buffer
            printf("Got response: %s\n", buffer);
            if (!strcmp(buffer, "Hi Pi\n")) {
		tcflush(serial, TCIOFLUSH);
                write(serial, "Data 1\n", 7);  // Send response to "Hi Pi"
                tcdrain(serial);  // Wait until data is transmitted
                sleep(1);
            }
            else if (!strcmp(buffer, "Do 1\n")) {
                write(serial, "Data 2\n", 7);
                tcdrain(serial);  // Wait until data is transmitted
                sleep(1);
            }
            else if (!strcmp(buffer, "Do 2\n")) {
                write(serial, "Goodbye Linux\n", 14);
                tcdrain(serial);  // Wait until data is transmitted
                sleep(1);
            }
            else if (!strcmp(buffer, "Goodbye Pi\n")) {
                printf("The end\n");
                break;
            }
            else {
                printf("Unhandled message: %s\n", buffer);
                close(serial);
                return -1;
            }
        }
        sleep(1);  // Wait for next data
    }

    close(serial);
    return 0;
}

