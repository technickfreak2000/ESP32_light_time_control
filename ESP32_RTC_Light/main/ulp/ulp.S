#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

//Portdefinitionen
#define PORT 8	//RTC-GPIO 8 => GPIO 33

//Macro-Definitionen ------------------------
//Port-Mode => Output
.macro SET_MODE_OUTPUT rtc_port
	WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S + \rtc_port, 1, 1)
.endm

//Set Port => 1
.macro SET_PIN_HIGH rtc_port
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + \rtc_port, 1, 1)
.endm

//Set Port => 0
.macro SET_PIN_LOW rtc_port
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + \rtc_port, 1, 1)
.endm

//-------------------------------------------
//Daten
	.data
mytable:
	.long 1, 2, 3, 4, 5	//locales array
	
	.bss
	.global var1
var1:
	.long 0
	
//Programmcode	
	.text
	.global entry
entry:
	SET_MODE_OUTPUT PORT
	move R1, var1		
	ld   R0, R1, 0
	add  R0, R0, 1
	st   R0, R1, 0
	and  R0, R0, 1
	// SET_PIN_LOW PORT
	jump exit, EQ
	// SET_PIN_HIGH PORT
exit:	
	halt

