#ifndef BMPLIB_H
#define BMPLIB_H



typedef struct
{
    /*位图头文件*/
    char type[2];//文件类型，必须为"BM"(0x4D42)
    char siz[4];//文件的大小
    char reserved[4];//保留，必须为0
    char off[4];//位图阵列相对于文件头的偏移字节
}bmp_file_header_t;



typedef struct{
    /*位图信息头*/
    char siz[4];
    char width[4];//位图的宽度
    char height[4];//位图的高度
    char planes[2];//目标设备的位平面数，必须设置为1
    char bitcount[2];//每个像素的位数,1,4,8,或24
    char compress[3];//位图阵列的压缩方法，0=不压缩
    char img_siz[4];//图像大小(字节)
    char xpel[4];//目标设备水平每米像素的个数
    char ypel[4];//目标像素垂直每米像素的个数
    char clr_used[4];//位图实际使用的颜色表的颜色数
    char clr_important[4];//重要颜色索引的个数

}bmp_info_header_t;




/*bitmap格式的图像文件会带有54字节的信息头，其中包含了图像和文件的基本信息，
紧接在文件头之后的就是实际的图像数据*/
typedef struct{
    char blue;
    char green;
    char red;
    char reserved;
}rgb_32_t;




/*对位图进行操作*/
typedef struct{
    rgb_32_t *curp;//指向当前像素点
    int width,height,bitcount,siz;//图形长宽、大小等信息
    int fd;//图像文件描述符
    void *data;//图像有效数据
}bmp_t;


//字符形到整形的转换
int char_to_int(char *ch)
{
    return *((int*)ch);
}



/*打开位图操作*/
int bmp_open(bmp_t *bmp,char *bmpn)
{
    bmp_file_header_t fhr;//位图文件头
    bmp_info_header_t ihr;//位图信息头



    if(-1 == (bmp->fd=open(bmpn,O_RDONLY))){
        printf("Error:cannot open framebuffer device.\n");
        _exit(EXIT_FAILURE);
    }


    read(bmp->fd,&fhr,sizeof(bmp_file_header_t));//读取文件头
    read(bmp->fd,&ihr,sizeof(bmp_info_header_t));//读取信息头


    bmp->width = char_to_int(ihr.width);
    bmp->height = char_to_int(ihr.height);
    bmp->bitcount = char_to_int(ihr.bitcount);//像素位数
    bmp->siz = (bmp->width *bmp->height*bmp->bitcount)/8;



    printf("bmp->width = %d\n",bmp->width);
    printf("bmp->height = %d\n",bmp->height);
    printf("bmp->bitcount = %d\n",bmp->bitcount);
    printf("bmp->siz = %d\n",bmp->siz);


    bmp->data = malloc(bmp->siz);//为位图数据分配存储空间
    read(bmp->fd,bmp->data,bmp->siz);//读取数据
    bmp->curp = (rgb_32_t *)bmp->data;//获取当前像素点


    return 0;

}


int bmp_close(bmp_t *bmp)
{
    close(bmp->fd);
    free(bmp->data);
    return 1;
}





/*
	因为开发板的帧缓冲区在驱动中被设置为16位数据表示一个像素点，因此需要对24或
	32位的位图进行转换，使用下面的函数
*/
static inline short transfer_to_16bit(char r,char g,char b)
{
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}



static inline short bmp_get_pixel_16bit(bmp_t *bmp)
{
    //将当前位图转化为16位位图信息
    return transfer_to_16bit(bmp->curp->red,bmp->curp->green,bmp->curp->blue);
}



//移动到下一像素点
static inline void bmp_next_pixel(bmp_t *bmp)
{
    if(24==bmp->bitcount)//如果是24位位图
        bmp->curp = (rgb_32_t*)((int)bmp->curp+3);
    else if(32 == bmp->bitcount)//如果是32位位图
        bmp->curp = (rgb_32_t*)((int)bmp->curp+4);
}







/*绘制位图*/
int fb_drawbmp(fb_dev_t *fbd,int x0,int y0,char *bmpn)
{
    int x,y,x1,y1;
    bmp_t bmp;


    bmp_open(&bmp,bmpn);
    x1 = x0+bmp.width;
    y1 = y0+bmp.height;



    for(y = y1;y>y0;y--){
        for(x = x0;x<x1;x++){

            //如果超出LCD屏幕坐标范围
            if(x>fbd->xres||y>fbd->yres){
                bmp_next_pixel(&bmp);//移动到下一个像素点
                continue;
            }
            *((short*)(fbd->pfb)+y*fbd->xres+x) = bmp_get_pixel_16bit(&bmp);
            bmp_next_pixel(&bmp);//移动到下一像素点
        }
    }
    bmp_close(&bmp);
    return 0;
}








#endif