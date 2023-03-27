#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "lcd_app.h"
#include "bmplib.h"





#define FB_DEV_NAME "/dev/fb0"


#define RED_COLOR565 0x0F100
#define GREEN_COLOR565 0x007E0
#define BLUE_COLOR565  0x0001F




int main(int argc,char *argv[])
{
    fb_dev_t *fbd;


    fbd = (fb_dev_t*)malloc(sizeof(fb_dev_t));


    fb_open(fbd,FB_DEV_NAME);





    if(fbd->bpp == 16){
        printf("Red/Green/Blue Screen\n");
        printf("%d,%d",fbd->xres,fbd->yres);
        fb_drawrect(fbd,0,0,fbd->xres,fbd->yres/3,RED_COLOR565);
        fb_drawrect(fbd,0,fbd->yres/3,fbd->xres,fbd->yres*2/3,GREEN_COLOR565);
        fb_drawrect(fbd,0,fbd->yres*2/3,fbd->xres,fbd->yres/3,BLUE_COLOR565);
    }else
        printf("16 bits only!\n");


    //fb_drawbmp(fbd,0,0,argv[1]);
    fb_close(fbd);
    return 0;
}
