#ifndef _CMOS_H
#define _CMOS_H
#include "header/cpu/portio.h"

#define CURRENT_YEAR 2025

enum {
      cmos_address = 0x70,
      cmos_data    = 0x71
};

int get_update_in_progress_flag();

unsigned char get_RTC_register(int reg);

void read_rtc();

#endif