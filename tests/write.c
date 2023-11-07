/*
 ============================================================================
 Name        : TestDev.c
 Author      : Bruno
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>
#include <sys/select.h>
//#include <termios.h>

/*struct termios orig_termios;

void reset_terminal_mode()
{
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
    struct termios new_termios;


    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));


    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}


int kbhit()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

int getch()
{
    int r;
    unsigned char c = 0;
    if ((r = read(0, &c, sizeof(c))) < 0) {
        return r;
    } else {
        return c;
    }
}*/

#define DEFAULT_SERIALPORT "/dev/MyModuleNode0"

int main(int argc, char *argv[]) {
    int fd;
    char tp;
    int ret;
char text[15] = "ceciestunteste";

    //if (argc > 1)
    // fd = open(argv[1], O_WRONLY);
    //else
    fd = open(DEFAULT_SERIALPORT, O_WRONLY);
    if (fd < 0) {
        printf("Erreur d'ouverture = %d\n", fd);
        return -1;
    }

	printf("Ecrit\n");
    //while (1) {
    //printf("clique\n");
    //tp = (char) getch();
    //if (tp < 0)
    //continue;
    ret = write(fd, &text, 15);
    if (ret >= 0) {
        printf("bit écrit : %d\n", ret);
        //fflush(stdout);
        //if (tp == 'x')
        //break;
    }
//}


  // printf("\n\r");

   close(fd);

   return EXIT_SUCCESS;
}
