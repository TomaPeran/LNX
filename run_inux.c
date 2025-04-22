#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>

void send_serial_data(int32_t serial, char *buffer) {
    if (buffer == NULL) {
        return;
    }
    write(serial, buffer, strlen(buffer));  // Send the data
    tcdrain(serial);  // Wait until all data is transmitted
    printf("Sending: %s", buffer);
    sleep(1);  // Give time for transmission
}

int32_t terminal_init(int32_t serial)
{
    if (serial < 0) { return -1; }
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

int32_t main() {
    int32_t serial = open("/dev/ttyACM0", O_RDWR);
    if (serial < 0) {
        perror("Error opening serial port");
        return -1;
    }

    if (terminal_init(serial) < 0) {
      perror("Error while initializating terminal");
      close(serial);
      return -1;
    }

    tcflush(serial, TCIOFLUSH);
    sleep(1);

    char buffer[100];
    int32_t read_size = 0;

    while (1) {
        read_size = read(serial, buffer, sizeof(buffer) - 1);  // Read incoming data
        printf("read size: %d\n", read_size);

        if (read_size > 1) {
            buffer[read_size] = '\0';  // Null-terminate the buffer
            printf("Got response: %s\n", buffer);

            if (!strcmp(buffer, "Hi Linux\n")) {
                send_serial_data(serial, "Hi Pi\n");  // Send "Hi Pi"
            }
            else if (!strcmp(buffer, "Data 1\n")) {
                send_serial_data(serial, "Do 1\n");  // Send "Do 1"
            }
            else if (!strcmp(buffer, "Data 2\n")) {
                send_serial_data(serial, "Do 2\n");  // Send "Do 2"
            }
            else if (!strcmp(buffer, "Goodbye Linux\n")) {
                send_serial_data(serial, "Goodbye Pi\n");  // Send "Goodbye Pi"
                break;
            }
            else {
                perror("Unrecognized message");
                tcflush(serial, TCIOFLUSH);
                close(serial);
                return -1;
            }
        }
        sleep(1);  // Wait before reading again
    }

    close(serial);
    return 0;
}

