/**
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Bare minimum empty user application template
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# "Insert system clock initialization code here" comment
 * -# Minimal main function that starts with a call to board_init()
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
#include <asf.h>
#include "ethernet.h"

#define TIMER_FREQ	1000
#define APPLI_CPU_SPEED	cpu_speed

uint32_t cpu_speed;	
volatile uint32_t time_of_day;
volatile uint32_t CPU_counts;

int main (void)
{
	// Insert system clock initialization code here (sysclk_init()).
	sysclk_init();

	board_init();
	
	cpu_speed = sysclk_get_cpu_hz();

	// Insert application code here, after the board has been initialized.
	EthernetInit();
	
	uint32_t last_blink_time = 0;
	
	for (;;)
	{
		U32 delta_time = Get_sys_count() - CPU_counts;
		delta_time = ((U64)delta_time*TIMER_FREQ)/APPLI_CPU_SPEED;
		CPU_counts += ((U64)delta_time*APPLI_CPU_SPEED)/TIMER_FREQ;
		time_of_day += delta_time;
		
		if ((time_of_day - last_blink_time) >= 500)
		{
			last_blink_time = time_of_day;
			LED_Toggle(LED0);
		}
	
		EthernetTask(time_of_day);
	}
}
