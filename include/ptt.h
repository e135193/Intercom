/* ptt.h - PTT butonu (active-low, debounce) ve durum LED'i. */
#pragma once
#include <stdbool.h>

void ptt_init(void);
bool ptt_pressed(void);     /* debounce'lu, ISR'dan okunabilir */
void ptt_task(void *arg);   /* debounce + LED durum desenleri */