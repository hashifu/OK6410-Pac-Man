#ifndef LCD_APP_H
#define LCD_APP_H

/*自定义结构体用来在用户空间里管理帧缓冲的信息*/
typedef struct fd_dev
{
    int fd;//帧缓冲设备文件描述符
    void *pfb;//指向帧缓冲映射到用户空间的首地址
    int xres,yres,siz;//一帧图像的宽度、高度和大小
    int bpp;//每个像素的位数
}fb_dev_t;



/*实现缓冲设备的打开和关闭操作的接口*/
int fb_open(fb_dev_t *fbd,char *fbn)
{
    struct fb_var_screeninfo vinfo;



    if(-1 == (fbd->fd=open(fbn,O_RDWR))){
        printf("Error:cannot open framebuffer device.\n");
        _exit(EXIT_FAILURE);
    }


    /*获取LCD的可变参数*/
    ioctl(fbd->fd,FBIOGET_VSCREENINFO,&vinfo);
    //将可变参数的中的相关数据保存到自定的结构体fbd中
    fbd->xres = vinfo.xres;
    fbd->yres = vinfo.yres;
    fbd->bpp = vinfo.bits_per_pixel;

    fbd->siz = fbd->xres * fbd->yres *fbd->bpp/8;


    printf("%dx%d,%dbpp,screensize = %d\n",fbd->xres,fbd->yres,
           fbd->bpp,fbd->siz);



    /*将帧缓冲映射到内存*/
    fbd->pfb = mmap(0,fbd->siz,PROT_READ|PROT_WRITE,
                    MAP_SHARED,fbd->fd,0);


    if((int)fbd->pfb == -1){
        printf("Error:failed to map framebuffer device to memory.\n");
        _exit(EXIT_FAILURE);
    }
    return 0;
}



int fb_close(fb_dev_t *fbd)
{
    munmap(fbd->pfb,fbd->siz);//解除映射
    close(fbd->fd);//关闭设备文件

}



/*填充制定的巨型区域*/
int fb_drawrect(fb_dev_t *fbd,int x0,int y0,int w,int h,int color)
{
    int x,y;
    for(y=y0;y<y0+h;y++)
    {
        for(x = x0;x<x0+w;x++)
            *((short*)(fbd->pfb)+y*fbd->xres+x)=color;
    }

    return 0;
}

#endif