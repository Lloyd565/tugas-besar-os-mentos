#ifndef _CMOS_H
#define _CMOS_H

#include "header/cpu/portio.h"

#define CURRENT_YEAR 2025

struct Time {
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned int year;
};

void cmos_get_time(struct Time *time);

#endif