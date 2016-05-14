#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>


#include <cstdio>    //errorno
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

//uint8_t *buffer;
void *buffer;
static int xioctl(int fd, int request, void *arg)
{
        int r;
 
        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);
 
        return r;
}
 
int print_caps(int fd)
{
        struct v4l2_capability caps = {};
        if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps))
        {
                perror("Querying Capabilities");
                return 1;
        }
 
        printf( "Driver Caps:\n"
                "  Driver: \"%s\"\n"
                "  Card: \"%s\"\n"
                "  Bus: \"%s\"\n"
                "  Version: %d.%d\n"
                "  Capabilities: %08x\n",
                caps.driver,
                caps.card,
                caps.bus_info,
                (caps.version>>16)&&0xff,
                (caps.version>>24)&&0xff,
                caps.capabilities);
 
        if(!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
            fprintf(stderr, "The device does not handle single-planar video capture.\n");
            exit(1);
        }
//        else printf("V4L2_CAP_VIDEO_CAPTURE OK\n");
        if(!(caps.capabilities & V4L2_CAP_STREAMING)){
            fprintf(stderr, "the device can't handle frame streaming so that our queue/dequeue routine can go fluently.\n");
            exit(1);
        }
//        else printf("V4L2_CAP_STREAMING OK\n");
        
        struct v4l2_cropcap cropcap = {0};
        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == xioctl (fd, VIDIOC_CROPCAP, &cropcap))
        {
                perror("Querying Cropping Capabilities");
                return 1;
        }
        printf("\n");
        printf( "Camera Cropping:\n"
                "  Bounds: %dx%d+%d+%d\n"
                "  Default: %dx%d+%d+%d\n"
                "  Aspect: %d/%d\n",
                cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top,
                cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top,
                cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);
 
        int support_grbg10 = 0;
 
        struct v4l2_fmtdesc fmtdesc = {0};
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        char fourcc[5] = {0};
        char c, e;
        printf("\n");
        printf("  FMT : CE Desc\n--------------------\n");
        while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
        {
                strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
                if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10)
                    support_grbg10 = 1;
                c = fmtdesc.flags & 1? 'C' : ' ';
                e = fmtdesc.flags & 2? 'E' : ' ';
                printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
                fmtdesc.index++;
        }
        
//        if (!support_grbg10)
//        {
//            printf("Doesn't support GRBG10.\n");
//            return 1;
//        }
 
        struct v4l2_format fmt = {0};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
//        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // my
//        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
//        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
//        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        
        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        {
            perror("Setting Pixel Format");
            return 1;
        }
 
        strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
        printf("\n");
        printf( "Selected Camera Mode:\n"
                "--------------------\n" 
                "  Width: %d\n"
                "  Height: %d\n"
                "  PixFmt: %s\n"
                "  Field: %d\n",
                fmt.fmt.pix.width,
                fmt.fmt.pix.height,
                fourcc,
                fmt.fmt.pix.field);
        return 0;
}
 
int init_mmap(int fd)
{
    struct v4l2_requestbuffers req = {0};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 1;
 
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
        perror("Requesting Buffer");
        return 1;
    }
 
    struct v4l2_buffer bufinfo;
    memset(&bufinfo, 0, sizeof(bufinfo));
        
    bufinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufinfo.memory = V4L2_MEMORY_MMAP;
    bufinfo.index = 0;
    if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &bufinfo))
    {
        perror("Querying Buffer");
        return 1;
    }

    buffer = static_cast<uint8_t*>(mmap (
            NULL,
            bufinfo.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            bufinfo.m.offset
    ));
    
    memset(buffer, 0, bufinfo.length);
    
    printf("Length: %d\nAddress: %p\n", bufinfo.length, buffer);
    printf("Image Length: %d\n", bufinfo.bytesused);
 
    return 0;
}
 
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <cstdio>

int capture_video(int fd)
{
    bool quit = false;
    SDL_Event event;


    if (SDL_Init(SDL_INIT_VIDEO) != 0){
                    std::cout << "SDL_Init Error: " << SDL_GetError() << std::endl;
                    return 1;
            }
            SDL_Quit();

    SDL_Window *window;        
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    window = SDL_CreateWindow("Hello World!", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
    if (window == nullptr){
            std::cout << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr){
            SDL_DestroyWindow(window);
            std::cout << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
    }

    IMG_Init(IMG_INIT_JPG);
    
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YVYU, SDL_TEXTUREACCESS_STREAMING, 640, 480);

    IplImage* frame;
    cvNamedWindow("window",CV_WINDOW_AUTOSIZE);
    printf("Start Capture");
    
    struct v4l2_buffer buf = {0};
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
 
    if(-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type))
    {
        perror("Start Capture");
        return 1;
    }
 
    while(true) {
    
        if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        {
            perror("Query Buffer");
            return 1;
        }
/* //задержка для заполнения буфера 
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = {0};
        tv.tv_sec = 2;
        int r = select(fd+1, &fds, NULL, NULL, &tv);
        if(-1 == r)
        {
            perror("Waiting for Frame");
            return 1;
        }
  */    
        if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
        {
            perror("Retrieving Frame");
            return 1;
        }
    //    printf ("saving image\n");

        SDL_UpdateTexture(texture, nullptr, buffer, 640 * 2);
        SDL_WaitEvent(&event);

        switch (event.type)
        {
            case SDL_QUIT:
                quit = true;
                break;
        }

        //SDL_Rect dstrect = { 5, 5, 320, 240 };
        //SDL_RenderCopy(renderer, texture, NULL, &dstrect);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

//    CvMat cvmat = cvMat(480, 640, CV_8UC3, (void*)buffer);
//    frame = cvDecodeImage(&cvmat, 1);
//    cvNamedWindow("window",CV_WINDOW_AUTOSIZE);
//    cvShowImage("window", frame); 
//    cvWaitKey(1);  //обновление окна
        
    }
    
    if(-1 == xioctl(fd, VIDIOC_STREAMOFF, &buf.type))
    {
        perror("stop Capture");
        return 1;
    }
    
//    IplImage* frame;
//    CvMat cvmat = cvMat(960, 1280, CV_8UC3, (void*)buffer);
//    frame = cvDecodeImage(&cvmat, 1);
//    cvNamedWindow("window",CV_WINDOW_AUTOSIZE);
//    cvShowImage("window", frame);
//    cvWaitKey(0);
//    cvSaveImage("image.jpg", frame, 0);
 
    return 0;
}
 
int main()
{
        int fd;
 
        fd = open("/dev/video0", O_RDWR);
        if (fd == -1)
        {
                perror("Opening video device");
                return 1;
        }
        if(print_caps(fd))
            return 1;
        
        if(init_mmap(fd))
            return 1;
        int i;
        for(i=0; i<1; i++)
        {
            if(capture_video(fd))
                return 1;
        }
        close(fd);
        return 0;
}