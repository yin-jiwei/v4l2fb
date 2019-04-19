//
// Created by yjw on 18-3-13.
//

#ifndef SWC_SCREEN_H
#define SWC_SCREEN_H

struct screen
{
    int fd;
    char *buffer;
    int width;
    int height;
    int screensize;
};

void screen_init();

void screen_quit();

void screen_mainloop();

#endif //SWC_SCREEN_H
