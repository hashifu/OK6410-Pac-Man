# 开发板吃豆人小游戏

姓名：陈建宇

学号：PB20030767

## 实验内容

- 在 Linux 环境下编写一个吃豆人小游戏，包含
  - 吃豆人小游戏的具体内容
  - 控制吃豆人移动的按键驱动
  - 控制显示内容的显示驱动
- 将程序利用交叉编译器编译成可执行文件，加载到开发板上运行。



## 游戏规则

吃豆人游戏的规则如下：

- 游戏有类似于迷宫的围墙，吃豆人和鬼魂均不能穿越围墙。
- 吃豆人在游戏界面的最下端出生，并可以沿着线路进行上下左右的移动。吃豆人要尽可能多的吃掉豆子，吃掉豆子会获得一定的分数。
- 吃豆人会被四个鬼魂追击，刚开始鬼魂都在中间的鬼屋，游戏开始时开始行动。若吃豆人被前来追击的鬼魂吃掉则游戏立即结束并开始结算分数。鬼魂在非拐弯时只能直行，且他们的行动轨迹互不影响。
- 场景中分布着数枚"大力丸"，在大力丸的药效作用内，鬼魂四散奔逃，吃豆人可以追击吃掉鬼魂并且获得一定的分数。



## 实验过程

#### 显示驱动的实现

​		由于本次实验我们需要在开发板上显示出地图和吃豆人、鬼魂、大力丸等元素，因此我们需要编写一个在开发板上显示内容的驱动，而在本实验中，我们无需重新编写一个驱动，而是使用了开发板自带的驱动，通过对 `“/dev/fb0”` 的读取从而在开发板的 LCD 屏幕上显示图像。

​		我们定义一个结构体用来在用户空间里管理帧缓冲的信息：

```c
typedef struct fd_dev
{
    int fd;					//文件描述符
    void *pfb;				//帧缓冲映射到用户空间的首地址
    int xres,yres,siz;		//图像的宽度、高度和大小
    int bpp;				//像素位数
}fb_dev_t;
```

​		打开接口的函数：

```c
int fb_open(fb_dev_t *fbd,char *fbn)
{
    struct fb_var_screeninfo vinfo;
	if(-1 == (fbd->fd=open(fbn,O_RDWR))) //打开失败的处理
    {
        printf("Error:cannot open framebuffer device.\n");
        _exit(EXIT_FAILURE);
    }
    
    ioctl(fbd->fd,FBIOGET_VSCREENINFO,&vinfo); //获取LCD的可变参数
    
    fbd->xres = vinfo.xres;
    fbd->yres = vinfo.yres;
    fbd->bpp = vinfo.bits_per_pixel;
	fbd->siz = fbd->xres * fbd->yres *fbd->bpp/8; //保存相关数据

    //printf("%dx%d,%dbpp,screensize = %d\n",fbd->xres,fbd->yres,fbd->bpp,fbd->siz);
    
    fbd->pfb = mmap(0,fbd->siz,PROT_READ|PROT_WRITE,MAP_SHARED,fbd->fd,0); //映射帧缓冲到内存
    
	if((int)fbd->pfb == -1)
    {
        printf("Error:failed to map framebuffer device to memory.\n");
        _exit(EXIT_FAILURE);
    }
    return 0;
}
```

​		关闭接口的函数：

```c
int fb_close(fb_dev_t *fbd)
{
    munmap(fbd->pfb,fbd->siz);	//解除映射
    close(fbd->fd);				//关闭文件
}
```

​		我们在该实验中主要使用的函数为 `fb_drawrect` 函数，它使用了我们上文中定义的结构体通过逐个像素输出的方式对区域进行颜色填充：

```c
int fb_drawrect(fb_dev_t *fbd,int x0,int y0,int w,int h,int color) //这里的color我们使用RGB565颜色表表示
{
    int x,y;
    for(y = y0; y < y0 + h; y++)	//对纵轴进行填充
    {
        for(x = x0; x < x0 + w; x++) //对横轴进行填充
            *( (short*) (fbd -> pfb) + y * fbd -> xres + x) = color;
    }
	return 0;
}
```



#### 按键驱动的实现

​		在显示驱动的设计部分，我们使用了开发板自带的驱动，而在按键驱动的设计部分，我们选择设计一个全新的按键驱动，该驱动使用了等待队列的方式对按键信号进行处理，同时将 0~5 6个按键转换为数组中的六个量，1代表按下，0代表未按下，其代码部分如下：

```c
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <mach/irqs.h>

#define DEVICE_NAME "keyint"
#define KEYNUM 6
dev_t devid;

//定义一个信号量
struct semaphore key_lock;
static struct fasync_struct *key_async;
static struct timer_list key_timer;
//定义一个等待队列
static DECLARE_WAIT_QUEUE_HEAD(key_waitq);
//定义一个按键标志位
static volatile int ev_press = 0;

static volatile int press_cnt[KEYNUM] = {0,0,0,0,0,0};

struct key_irq_desc {
    //定义中断描述符的三个部分
 	int irq;
 	unsigned long flags;
 	char *name;
};

static struct key_irq_desc key_irqs[] = {
 	//下降沿产生中断
 	{IRQ_EINT(0), IRQF_TRIGGER_FALLING, "KEY1"},
 	{IRQ_EINT(1), IRQF_TRIGGER_FALLING, "KEY2"},
 	{IRQ_EINT(2), IRQF_TRIGGER_FALLING, "KEY3"},
 	{IRQ_EINT(3), IRQF_TRIGGER_FALLING, "KEY4"},
 	{IRQ_EINT(4), IRQF_TRIGGER_FALLING, "KEY5"},
 	{IRQ_EINT(5), IRQF_TRIGGER_FALLING, "KEY6"},
};

//中断处理
static irqreturn_t keys_interrupt(int irq, void *dev_id)
{
 	volatile int *press_cnt = (volatile int *) dev_id;
    
 	*press_cnt = *press_cnt + 1;
	//延时10ms后执行定时器处理函数
 	mod_timer(&key_timer,jiffies+HZ/100);

 	return IRQ_RETVAL(IRQ_HANDLED);
}


//定时器处理函数
static void key_timer_func(unsigned long data)
{
 	ev_press = 1;//置位
 	//唤醒等待队列
 	wake_up_interruptible(&key_waitq);
 	kill_fasync(&key_async, SIGIO, POLL_IN);
}


static int key_fasync(int fd, struct file *filp, int on)
{
    //进行异步通知
 	printk("Function key_fasync\n");
 	return fasync_helper(fd,filp,on,&key_async);
}



static unsigned key_poll(struct file *file, poll_table *wait)
{
 	unsigned int mask=0;
 	//指明要使用的等待队列
 	poll_wait(file,&key_waitq,wait);

    //返回掩码
 	if(ev_press)
 		mask |= POLL_IN | POLLRDNORM;

 	printk("poll wait\n");

 	return mask;
}

static int key_open(struct inode *inode, struct file *file)
{
 	int num;
 	//在实验当中我们使用的是非阻塞
 	if(file->f_flags & O_NONBLOCK) {
   		if(down_trylock(&key_lock)) return -EBUSY;
 	} else {
   		down(&key_lock);
 	}

 	//为每个按键注册中断处理程序
 	for(num=0;num<KEYNUM;num++) {
   		request_irq(key_irqs[num].irq, keys_interrupt, key_irqs[num].flags，key_irqs[num].name, (void *)&press_cnt[num]);
 	}

    return 0;
}

static int key_close(struct inode *inode, struct file *file)
{
 	int num;
 	//释放中断号
 	for(num=0;num<6;num++) {
   		free_irq(key_irqs[num].irq, (void *)&press_cnt[num]);
 	}
 	up(&key_lock);

 	printk("key_close free irqs\n");

 	return 0;
}

static int key_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	//判断是阻塞读还是非阻塞读
 	if(filp->f_flags & O_NONBLOCK) {
   		if(!ev_press)  return -EAGAIN;
 	} else {
   		//当有按键按下时被唤醒
   		wait_event_interruptible(key_waitq,ev_press);
 	}
 	//阻塞结束，有键按下了
    
	ev_press = 0;

 	//拷贝数据到用户空间
 	copy_to_user(buff,(const void *)press_cnt,min(sizeof(press_cnt),count));
 	memset((void *)press_cnt,0,sizeof(press_cnt));

 	return 1;
}

static struct file_operations key_ops = {
 .owner     = THIS_MODULE,
 .open     = key_open,
 .release = key_close,
 .read     = key_read,
 .poll     = key_poll,
 .fasync     = key_fasync,
};

static struct cdev *cdev_keyint;
static struct class *keyint_class;



//模块初始化
static int __init keyint_init(void) {
 	int val;

 	//初始化timer
 	init_timer(&key_timer);
 	key_timer.function = key_timer_func;
 	add_timer(&key_timer);

    /*初始化信号量*/
    init_MUTEX(&key_lock);
 	/*register device*/
	val = alloc_chrdev_region(&devid,0,1,DEVICE_NAME);
 	if(val) {
   		return -1;
   		printk("register keyint error\n");
 	}

 	cdev_keyint = cdev_alloc();
 	cdev_init(cdev_keyint, &key_ops);
 	cdev_keyint->owner = THIS_MODULE;
 	cdev_keyint->ops   = &key_ops;

 	val = cdev_add(cdev_keyint,devid,1);
 	if(val) {
   		return -1;
   		printk("add device error\n");
 	}

 	keyint_class = class_create(THIS_MODULE,DEVICE_NAME);
 	device_create(keyint_class,NULL,devid,NULL,"%s",DEVICE_NAME);

 	printk("KEY initialezed\n");
 	return 0;
}

//退出模块
static void __exit keyint_exit(void)
{
  	cdev_del(cdev_keyint);
 	device_destroy(keyint_class,devid);
 	class_destroy(keyint_class);
 	unregister_chrdev_region(devid,1);
}

module_init(keyint_init);
module_exit(keyint_exit);

MODULE_LICENSE("GPL");
```

​		其 Makefile 文件编写如下：

```makefile
ifneq ($(KERNELRELEASE),)
	obj-m := key.o
else
 
KDIR := /home/hashifu/forlinx/linux-3.0.1
all:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=arm-linux-
clean:
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers
endif
```

​		其中， `/home/hashifu/forlinx/linux-3.0.1` 为 linux 内核所在目录。	

​		注意到，系统本身其实是有一个按键驱动的，因此我们在给开发板安装我们自己编写的按键驱动时，应当注销掉原有的按键驱动，注销步骤如下：

- 首先在开发板官方文件中下载内核源码 `‘FORLINX_linux-3.0.1.tar.gz’` 并将其拷贝到你的工作目录下，解压缩。
- 在命令行输入 make menuconfig，进入内核配置图形界面，依次进入 Device Drivers、input device support、Key Boards、GPIO Buttons。然后去掉前面的*。
- 使用 `make zImmage` 命令编译内核镜像文件，编译结束后将在内核源码目录的 arch/arm/boot 中得到 Linux 内核映像文件：zImage，在烧写开发板系统的时候使用该 zImmage 作为镜像文件。



#### 游戏内容的实现

​		我们使用一个文件来实现游戏的所有内容，使用上文中提到的按键驱动和显示驱动，实现游戏内容在开发板上的 LED 屏幕显示、控制吃豆人移动等，其中地图上的迷宫墙壁和豆子使用数组进行存储。我们将主程序分为几个控制函数：

- bit 置位和 bit 检查：

  ```c
  void setbit(unsigned char x, unsigned char y, unsigned char *list, unsigned char what)
  {
      if (what)
          list[16 * y + x / 8] |= (1 << (7 - x % 8));
      else
          list[16 * y + x / 8] &= ~(1 << (7 - x % 8));
  }
  unsigned char checkbit(unsigned char x, unsigned char y, unsigned char *list)
  {
      return ((list[16 * y + x / 8] & (1 << (7 - x % 8))) == 0 ? 0 : 1);
  }
  ```

- 位置的更新：

  ```c
  void update(void)
  {
      static unsigned char ii = 0;
      int i;
      unsigned char j, k;
      for (i = 0; i < 1024; i++)	//对物品进行更新
      {
          ShowData[i] = douData[i] | MapData[i];
      }
      for (j = 0; j < 5; j++)	//对于每个人物（吃豆人、鬼）进行更新
      {
          if (dead[j] == 1)
              continue;
          else
          {
              ii = 1 - ii;
              setbit(x[j], y[j], ShowData, 1);
              setbit(x[j] + 1, y[j] + 1, ShowData, 1);
              setbit(x[j] - 1, y[j] - 1, ShowData, 1);
              setbit(x[j] + 1, y[j] - 1, ShowData, 1);
              setbit(x[j] - 1, y[j] + 1, ShowData, 1);
              if (dalitime == 0 || (dalitime < 50 && ii == 0) || j == 0)
              {
                  setbit(x[j], y[j] + 1, ShowData, 1);
                  setbit(x[j] - 1, y[j], ShowData, 1);
                  setbit(x[j] + 1, y[j], ShowData, 1);
                  setbit(x[j], y[j] - 1, ShowData, 1);
              }
          }
      }
      for (k = 0; k < 4; k++)	//对大力丸进行更新
      {
          if (daliwan[k][3] == 0)
              continue;
          setbit(daliwan[k][0], daliwan[k][1], ShowData, 1);
          if (daliwan[k][2])
          {
              setbit(daliwan[k][0] - 1, daliwan[k][1], ShowData, 1);
              setbit(daliwan[k][0] + 1, daliwan[k][1], ShowData, 1);
          }
          else
          {
              setbit(daliwan[k][0], daliwan[k][1] - 1, ShowData, 1);
              setbit(daliwan[k][0], daliwan[k][1] + 1, ShowData, 1);
          }
      }
  }
  ```

- 判断某个地方某个地方存在的物品是什么（0：空白、1：鬼、2：豆子、3：墙、4：大力丸）：

  ```c
  unsigned char whats(unsigned char m, unsigned char n)
  {
      unsigned char i, j;
      for (i = 1; i <= 4; i++)
      {
          if (m == x[i] && n == y[i] && !dead[i])
              return 1;
      }
      if (checkbit(m, n, douData))
          return 2;
      if (checkbit(m, n, MapData))
          return 3;
      for (j = 0; j < 4; j++)
      {
          if (m == daliwan[j][0] && n == daliwan[j][1] && daliwan[j][3] != 0)
          {
              return 4;
          }
      }
      return 0;
  }
  ```

- 判断吃豆人是否撞到了墙，若撞到了墙则不能再向对应方向移动：

  ```c
  unsigned char crackwall(int x, int y)
  {
      int i, j;
      for (i = -2; i <= 2; i++)
      {
          for (j = -2; j <= 2; j++)
          {
              if (whats(x + i, y + j) == 3)
                  return 1;
              if (x + i < 0 || x + i > 127)
                  return 1;
              if (y + j < 0 || y + j > 63)
                  return 1;
          }
      }
      return 0;
  }
  ```

- 根据输入的按键计算位置偏移量的函数：

  ```c
  int deltax(unsigned char direction)
  {
      switch (direction)
      {
      case 0:
          return 0;
      case 1:
          return 0;
      case 2:
          return -1;
      case 3:
          return 1;
      }
  }
  int deltay(unsigned char direction)
  {
      switch (direction)
      {
      case 0:
          return -1;
      case 1:
          return 1;
      case 2:
          return 0;
      case 3:
          return 0;
      }
  }
  ```

- 吃豆人移动的函数：

  ```c
  unsigned char zoulu(unsigned char keydown, unsigned char newdirection)
  {
      unsigned char what = 5;
      unsigned char newx = x[0] + deltax(newdirection), newy = y[0] + deltay(newdirection);	//根据输入的newdirection的值计算新位置，其中0代表向上、1代表向下、2代表向左、3代表向右
      if (keydown)
      {
          if (!crackwall(newx, newy))
          {
              what = whats(newx, newy);
          }
      }
      unsigned char i, j, k;
      switch (what)	//判断新位置上是什么
      {
      case 4:
          for (i = 0; i < 4; i++)
          {
              if (daliwan[i][0] == newx && daliwan[i][1] == newy)
              {
                  daliwan[i][3] = 0;
                  break;
              }
          }
          for (j = 1; j <= 4; j++)
          {
              direction[j] = fanxiang(direction[j]);
          }
  
          x[0] = newx;
          y[0] = newy;
          direction[0] = newdirection;
          dalitime = 100;
          break;
      case 2:
          setbit(newx, newy, douData, 0);
          score++;
          x[0] = newx;
          y[0] = newy;
          direction[0] = newdirection;
          break;
      case 0:
          x[0] = newx;
          y[0] = newy;
          direction[0] = newdirection;
          break;
      case 1:
          if (dalitime > 0)	//在大力丸时间内，吃豆人吃掉鬼并获得加分
          {
              for (k = 1; k <= 4; k++)
              {
                  if (x[k] == newx && y[k] == newy)
                  {
                      dead[k] = 1;
                      score += 20;
                      break;
                  }
              }
          }
          else	//吃豆人死亡，游戏结束，计算分数
          {
              char str[30];
              printf("Your score=%d\n", score);
              printf("You died.");
              exit(0);
          }
          break;
      }
      return 0;
  }
  ```

- 对鬼进行自动移动的函数：

  ```c
  unsigned char autoi()
  {
      int targetx[5], targety[5];
      unsigned char i, j, k;
      if (fensantime > 0)	//此时四个鬼均处于分散状态，设置他们的运动目标
      {
          targetx[1] = 127;
          targety[1] = 0;
          targetx[2] = 127;
          targety[2] = 0;
          targetx[3] = 0;
          targety[3] = 0;
          targetx[4] = 0;
          targety[4] = 0;
      }
      else //此时四个鬼均处于追踪状态，设置他们的运动目标
      {
          targetx[1] = x[0];
          targety[1] = y[0];
          targetx[2] = 2 * x[0] - x[1];
          targety[2] = 2 * y[0] - y[1];
          targetx[3] = x[0] + 8 * deltax(direction[0]);
          targety[3] = y[0] + 8 * deltay(direction[0]);
          if (abs(x[4] - x[0]) + abs(y[4] - y[0]) > 32)	//距离大于32格子时会对吃豆人进行追踪
          {
              targetx[4] = x[0];
              targety[4] = y[0];
          }
          else	//距离<32格子会放弃追踪
          {
              targetx[4] = 0;
              targety[4] = 0;
          }
      }
      for (i = 1; i <= 4; i++)	//设置行动轨迹
      {
          if (dead[i])
              continue;
          int bestdirection;
          int bestdist = dalitime > 0 ? 0 : 255;
          for (j = 0; j < 4; j++)	//鬼遇到路口时选择与其target曼哈顿距离最近的岔路
          {
              int newx = x[i] + deltax(j);
              int newy = y[i] + deltay(j);
              if (crackwall(newx, newy))
                  continue;
  
              if (dalitime == 0)
              {
                  int thisdist = abs(newx - targetx[i]) + abs(newy - targety[i]);
                  if (fanxiang(j) == direction[i])
                      thisdist += 30;
                  if (thisdist < bestdist)
                  {
                      bestdist = thisdist;
                      bestdirection = j;
                  }
              }
              else
              {
                  int thisdist = rand();
                  if (fanxiang(j) == direction[i])
                      thisdist = 1;
                  if (thisdist > bestdist)
                  {
                      bestdist = thisdist;
                      bestdirection = j;
                  }
              }
          }
          x[i] += deltax(bestdirection);
          y[i] += deltay(bestdirection);
          direction[i] = bestdirection;
          if (x[i] == x[0] && y[i] == y[0])	//鬼遇到吃豆人时的处理
          {
              if (dalitime > 0)
              {
                  dead[i] = 1;
              }
              else
              {
                  char str[30];
                  printf("Your score=%d\n", score);
                  printf("You died.");
                  exit(0);
              }
          }
      }
      if (fensantime == 1)
      {
          fensantime = 0;
          for (k = 0; k < 5; k++)
          {
              setbit(61 + k, 21, MapData, 1);
          }
      }
      else if (fensantime > 0)
          fensantime--;
      if (dalitime > 0)
          dalitime--;
      return 0;
  }
  ```

- 程序的主函数如下：

  ```c
  int main(int argc, char *argv[])
  {
      fb_dev_t *fbd;
      int fd;
      int val;
      int key_value[4];
  
      fd = open("/dev/keyint", O_RDWR | O_NONBLOCK);	//首先打开按键驱动生成的驱动文件
      if (fd < 0)
      {
          printf("open devie error\n");
          return -1;
      }
      fbd = (fb_dev_t *)malloc(sizeof(fb_dev_t));
      fb_open(fbd, FB_DEV_NAME);	//打开显示驱动文件
      if (fbd->bpp != 16)
      {
          exit(-1);
      }
      fb_drawrect(fbd, 0, 0, fbd->xres, fbd->yres, GREEN_COLOR565);	//显示底色
      while (1)
      {
          val = read(fd, key_value, sizeof(key_value));	//读取按键信息
          int status = 0;
          if (val >= 0)	//对吃豆人进行移动操作
          {
              if (key_value[0])
              {
                  status = zoulu(1, 0);
              }
              else if (key_value[1])
              {
                  status = zoulu(1, 1);
              }
              else if (key_value[2])
              {
                  status = zoulu(1, 2);
              }
              else if (key_value[3])
              {
                  status = zoulu(1, 3);
              }
          }
          else
          {
              status = zoulu(0,1);
          }
          autoi();	//更新鬼的移动
          update();	//更新位置信息
          if (status == 0)
          {
              LCD_Display_Picture(ShowData, fbd);	//输出新的位置信息
          }
          usleep(500 * 1000);	//使用usleep来控制更新时间从而控制吃豆人和怪物的移动速度
      }
      fb_close(fbd);	//游戏结束
  }
  ```



## 实验结果

- 在 linux 环境下使用交叉编译器对 main 函数进行编译得到可执行文件 `test.out` 

  ![image-20230306001631921](C:\Users\Hashifu\AppData\Roaming\Typora\typora-user-images\image-20230306001631921.png)

- 在按键驱动目录下使用 `make` 指令，生成文件 `key.ko`

  ![image-20230306001844194](C:\Users\Hashifu\AppData\Roaming\Typora\typora-user-images\image-20230306001844194.png)

- 使用开发板官方文件中的 uboot 和 yaffs2 文件系统，使用我们上面制作的 zImmage 作为镜像文件，使用 SD 卡重新给开发板烧写系统。

- 使用串口线连接开发板和电脑，并在电脑上打开 DNW 软件，待开发板开机完成之后首先进入到存有 `key.ko` 和 `test.out` 的文件夹中，然后使用 `insmod key.ko` 指令加载驱动。

- 驱动加载成功后使用 `./test.out` 运行程序，即可游玩该吃豆人小游戏。

  ![image-20230306002228251](C:\Users\Hashifu\AppData\Roaming\Typora\typora-user-images\image-20230306002228251.png)

- 游戏结束后， DNW 软件上会弹出提示。

  ![image-20230306002316843](C:\Users\Hashifu\AppData\Roaming\Typora\typora-user-images\image-20230306002316843.png)



## 实验反思

​		由于本人是第一次接触到开发板，也是第一次接触到嵌入式开发，因此此次大作业我选择了开发板小游戏的制作，且制作过程中也仍然遇到了许多的困难，也因此花费了比较多的时间，首先就是对于显示驱动和按键驱动的设计方面，对显示驱动而言，由于我事先对 Linux 较为了解，因此就直接使用了 `/dev/fb0` 进行 LCD 的显示，因此省去了一部分时间，也规避了驱动设计这一复杂的操作，但是对于按键驱动的设计来说，由于我不知道开发板自带的按键驱动的源码，因此只能自行设计，这是非常有挑战性的，因此我阅读了大量的文章，了解了驱动的设计，也找了大量的代码，最终在互联网的帮助下完成了这一工作。对于游戏的设计方面，在显示驱动和按键驱动的基础上，剩下最难的部分莫过于对鬼和吃豆人移动的设计，在程序的设计过程中也无数次出现了段错误等问题，不过最终都通过 STFW 和向同学询问等方式得到了解决，虽然实验的过程非常艰难，但是看到实验结果时仍然是非常开心的，而且相信基于此次大作业的基础，我在今后面对开发板设计时也会更加得心应手。

