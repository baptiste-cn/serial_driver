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

#define DEFAULT_SERIALPORT "/dev/MyModuleNode0"

int main(int argc, char *argv[]) {
    int fd;
    char tp;
    int ret;
    int i;
char buffer[15];

   // if (argc > 1)
      // fd = open(argv[1], O_RDONLY);
      // else
       fd = open(DEFAULT_SERIALPORT, O_RDONLY);
       if (fd < 0) {
           printf("Erreur d'ouverture = %d\n", fd);
           return -1;
           }

	ioctl(fd,4, 5); 

       //while (tp != 'x') {
       //while (1) {

ret = read(fd, &buffer,15);
        	//if ((ret >= 0)&&(tp != (char) 0x0a)) {
	        printf("FILE: Data from Buffer:  %s\n", buffer);
		printf("%d", ret);

           //      fflush(stdout);
        //}
       //}
    //release(DEFAULT_SERIALPORT, fd);
       close(fd);
       printf("\n");
       return EXIT_SUCCESS;
}
