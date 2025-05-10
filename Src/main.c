/**
 *===========================================================================*
 *<<<<<<<<<<<<<<<<<<<<<<<<<<    Eco_Home        >>>>>>>>>>>>>>>>>>>>>>>>>>>>>*
 **                                                                         **
 **                  Author : AZEX TEAM                                     **
 **                  Track  : Embedded System                               **
 **                  Name   : ECO_HOME                                      **
 **                  Section: The House                                     **
 **                  MCU    : ATMEGA32                                      **
 **                  Layer  : App_Layer                                     **
 **                                                                         **
 *===========================================================================*
 */
/* AVR Library Includes */
#include <util/delay.h> // Delay functions for timing
#include <avr/interrupt.h> // AVR interrupt handling

/* LIB Includes */
#include "../Inc/LIB/STD_TYPES.h" // Standard type definitions (u8, u16, etc.)
#include "../Inc/LIB/BIT_MATH.h" // Bit manipulation macros

/* MCAL Includes */
#include "../Inc/MCAL/DIO/DIO_interface.h" // Digital I/O interface
#include "../Inc/MCAL/ADC/ADC_interface.h" // ADC (Analog to Digital Converter) interface
#include "../Inc/MCAL/TIMER/TIMER_interface.h" // Timer interface
#include "../Inc/MCAL/USART/USART_interface.h" // USART (serial communication) interface

/* HAL Includes */
#include "../Inc/HAL/LCD/LCD_interface.h"   // LCD display interface
#include "../Inc/HAL/LM35/LM35_interface.h" // LM35 temperature sensor interface
#include "../Inc/HAL/LDR/LDR_interface.h" // LDR (Light Dependent Resistor) interface
#include "../Inc/HAL/MQ5/MQ5_interface.h" // MQ5 gas sensor interface

//----------------- UART Command Definitions -----------------//
// Characters received via UART to control devices
#define Out_Light_on    'a' // Turn ON outdoor light
#define Out_Light_off   'b' // Turn OFF outdoor light
#define IN_Light_on     'c' // Turn ON indoor light
#define IN_Light_off    'd' // Turn OFF indoor light
#define CMD_Fire        'e' // Fire emergency
#define CMD_Gas         'f' // Gas leak emergency
#define CMD_COOLING_on  'g' // Turn ON cooling system
#define CMD_COOLING_off 'h' // Turn OFF cooling system

//----------------- Time and Threshold Definitions -----------------//
volatile u8 seconds_counter = 0; // Seconds counter
volatile u8 minutes_counter = 45; // Minutes counter (initialized to 45)
volatile u8 hours_counter = 9;    // Hours counter (initialized to 9)
volatile u8 day = 7;              // Day counter (initialized to 7)
volatile u8 month = 5;            // Month counter (initialized to 5)

#define TEMP_NORMAL_THRESHOLD    25 // Normal temperature threshold (°C)
#define TEMP_CRITICAL_THRESHOLD  50 // Critical temperature threshold (°C)
#define LIGHT_THRESHOLD         50  // Light level threshold (%)
#define NIGHT_START_HOUR        18  // Night time start (6 PM)
#define NIGHT_END_HOUR          6   // Night time end (6 AM)

//----------------- Global Variables -----------------//
u8 gas, purity, temp, LightLevel; // Sensor readings
u8 OUT_flag =0; // Flag to control outdoor light automation (1: auto, 0: manual)
u8 IN_flag =1;  // Flag to control indoor light automation (1: auto, 0: manual)
u8 Fire_flag=0; // Fire emergency flag
u8 Gas_flag=0;  // Gas emergency flag

volatile u8 CMD = 0xff; // Last received UART command

//----------------- Function Prototypes -----------------//
// Functions for handling different system events
void vLeaked_Gas_Sys(void);   // Gas leak emergency handler
void vFire_Sys(void);         // Fire emergency handler
void vCooling_Sys_On(void);   // Turn ON cooling system
void vCooling_Sys_Off(void);  // Turn OFF cooling system
void vIn_door_on(void);       // Turn ON indoor light
void vIn_door_off(void);      // Turn OFF indoor light
void vOut_door_on(void);      // Turn ON outdoor light
void vOut_door_off(void);     // Turn OFF outdoor light

//----------------- Timer2 Overflow ISR -----------------//
// This ISR is called every timer overflow (about every second)
// It increments the time counters (seconds, minutes, hours, day, month)
void ISR_Timer2_OVF(void) {
	static u8 ovf_count = 0; // Overflow counter for 1 second timing
	ovf_count++;
	if (ovf_count >= 50) { // About every second (with prescaler 1024, 8MHz)
		ovf_count = 0;
		seconds_counter++;
		if(seconds_counter == 60) {
			seconds_counter = 0;
			minutes_counter++;
			if(minutes_counter == 60) {
				minutes_counter = 0;
				hours_counter++;
				if(hours_counter == 24) {
					hours_counter = 0;
					day++;
					if(day == 32) {
						day = 1;
						month++;
						if(month == 13) {
							month = 1;
						}
					}
				}
			}
		}
	}
}

//----------------- USART Receive Complete ISR -----------------//
// Called when a byte is received via UART
ISR (USART_RXC_vect){
	USART_u8ReceiveData(&CMD); // Read received command

	switch (CMD)
	{
	case Out_Light_on:     vOut_door_on();     OUT_flag=1;   break; // Manual ON, disable auto
	case Out_Light_off:    vOut_door_off();    OUT_flag=0;   break; // Manual OFF, disable auto
	case IN_Light_on:      vIn_door_on();      IN_flag=0;    break; // Manual indoor ON
	case IN_Light_off:     vIn_door_off();     IN_flag=0;    break; // Manual indoor OFF
	case CMD_Fire:         vFire_Sys();        Fire_flag=1;  break; // Fire emergency
	case CMD_Gas:          vLeaked_Gas_Sys();  Gas_flag=1;   break; // Gas leak emergency
	case CMD_COOLING_on:   vCooling_Sys_On();                break; // Manual cooling ON
	case CMD_COOLING_off:  vCooling_Sys_Off();               break; // Manual cooling OFF
	default:               IN_flag=1;   OUT_flag=1;          break; // Unknown command, enable auto
	}
}

//----------------- Main Function -----------------//
void main(void) {
	// Initialize all peripherals
	ADC_vInit();         // Initialize ADC
	LCD_vInit();         // Initialize LCD
	MQ5_vInit(ADC_CHANNEL2); // Initialize MQ5 gas sensor
	USART_vInit();       // Initialize USART

	TIMER2_vInit();      // Initialize Timer2
	TIMER_u8SetCallBack(ISR_Timer2_OVF, TIMER2_OVF_VECTOR_ID); // Set timer ISR
	sei();               // Enable global interrupts

	// Set pin directions for actuators
	DIO_vSetPinDirection(DIO_PORTC, DIO_PIN3, DIO_PIN_OUTPUT);  // Indoor fan
	DIO_vSetPinDirection(DIO_PORTC, DIO_PIN4, DIO_PIN_OUTPUT);  // Exhaust fan
	DIO_vSetPinDirection(DIO_PORTC, DIO_PIN5, DIO_PIN_OUTPUT);  // Main power
	DIO_vSetPinDirection(DIO_PORTD, DIO_PIN2, DIO_PIN_OUTPUT);  // Outdoor lights
	DIO_vSetPinDirection(DIO_PORTD, DIO_PIN3, DIO_PIN_OUTPUT);  // Gas valve
	DIO_vSetPinDirection(DIO_PORTD, DIO_PIN6, DIO_PIN_OUTPUT);  // BUZZER
	DIO_vSetPinDirection(DIO_PORTD, DIO_PIN7, DIO_PIN_OUTPUT);  // Bump valve

	// Sensor configuration structures
	LM35_CONFIG Temp_sensor = {
			.Copy_u8LM35Channel = ADC_CHANNEL0,
			.Copy_u8ADCVoltageReference = 5,
			.Copy_u8ADCResolution = ADC_RESOLUTION_10_BIT
	};
	LDR_Config Light_sensor = {
			.LDR_Channel = ADC_CHANNEL1,
			.LDR_VoltageRef = 5,
			.LDR_Resolution = ADC_RESOLUTION_10_BIT
	};

	DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_HIGH); // Main power ON

	while(1) {
		if (IN_flag==1){
		DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_HIGH); // Main power ON
		}
		else
		{
			DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_LOW); // Main power ON

		}
		/* 1. Sensor Reading */
		LM35_u8GetTemp(&Temp_sensor, &temp); // Read temperature
		LDR_u8GetLightLevel(&Light_sensor, &LightLevel); // Read light level
		MQ5_u8GetGasPercentage(&gas); // Read gas level
		MQ5_u8GetAirPurity(&purity); // Read air purity

		/* 3. Display Updates */
		// Display time (HH:MM:SS)
		LCD_vSetPosition(LCD_ROW_1, LCD_COL_1);
		if(hours_counter < 10) LCD_vSendString("0");
		LCD_vSendNumber(hours_counter);
		LCD_vSendString(":");
		if(minutes_counter < 10) LCD_vSendString("0");
		LCD_vSendNumber(minutes_counter);
		LCD_vSendString(":");
		if(seconds_counter < 10) LCD_vSendString("0");
		LCD_vSendNumber(seconds_counter);

		// Display date (DD/MM)
		LCD_vSetPosition(LCD_ROW_1, LCD_COL_11);
		if(day < 10) LCD_vSendString("0");
		LCD_vSendNumber(day);
		LCD_vSendString("/");
		if(month < 10) LCD_vSendString("0");
		LCD_vSendNumber(month);

		// Display temperature with leading zero if needed
		if (temp>=10){
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_1);
			LCD_vSendNumber(temp);
			LCD_vSendString("C");
		}
		else
		{
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_1);
			LCD_vSendNumber(0);
			LCD_vSendNumber(temp);
			LCD_vSendString("C");
		}

		// Display light level with leading zero if needed
		if (LightLevel>=10){
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_6);
			LCD_vSendString("L");
			LCD_vSendNumber(LightLevel);
			LCD_vSendString("%");
		}
		else
		{
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_6);
			LCD_vSendString("L");
			LCD_vSendNumber(0);
			LCD_vSendNumber(LightLevel);
			LCD_vSendString("%");
		}
		// Display air purity with leading zero if needed
		if(purity>=10){
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_12);
			LCD_vSendString("G");
			LCD_vSendNumber(purity);
			LCD_vSendString("%");
		}
		else
		{
			LCD_vSetPosition(LCD_ROW_2, LCD_COL_12);
			LCD_vSendString("G");
			LCD_vSendNumber(0);
			LCD_vSendNumber(purity);
			LCD_vSendString("%");
		}

		/* 2. Automatic Control Logic (if no UART command is received, system works automatically) */
		// Cooling system automatic control
		if (temp <= TEMP_NORMAL_THRESHOLD ) {
			vCooling_Sys_Off(); // Turn off cooling if temp is normal
		} else if(temp > TEMP_NORMAL_THRESHOLD && temp < TEMP_CRITICAL_THRESHOLD) {
			vCooling_Sys_On(); // Turn on cooling if temp is high but not critical
		} else if(temp>50 || Fire_flag==1){
			while(1){
				vFire_Sys(); // Critical temperature or fire flag triggers fire system (infinite loop)
			}
		}

		// Outdoor light automatic control (only if not overridden by UART)

		if(OUT_flag==1){
			vOut_door_on();

		}
		else{
			if (LightLevel < LIGHT_THRESHOLD) {
				vOut_door_on(); // Turn on outdoor light at night or if light is low
			} else {
				vOut_door_off(); // Otherwise, turn off outdoor light
			}
		}


		// Gas and air purity based control (always active)
		if((purity < 100 && purity > 75) || (gas < 50 && gas > 25)) {
			vCooling_Sys_On(); // Turn on cooling if air quality is not perfect
		}
		else if(gas > 50 || Gas_flag==1) {
			while(1){
				vLeaked_Gas_Sys(); // Gas leak or flag triggers gas emergency system (infinite loop)
			}
		}

		_delay_ms(100); // Main loop delay for stability
	}
}

//----------------- Emergency and Control System Functions -----------------//
// Gas leak emergency: close gas valve, start ventilation and exhaust, cut main power, turn off outdoor lights, activate buzzer
void vLeaked_Gas_Sys(void) {
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN3, DIO_PIN_HIGH);  // Close gas valve
	vCooling_Sys_On();                                    // Start ventilation and exhaust
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_LOW);   // Cut main power
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN2, DIO_PIN_LOW);   // Turn off outdoor lights
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN6, DIO_PIN_HIGH);  // Activate buzzer
}

// Fire emergency: do gas leak actions and start water pump
void vFire_Sys(void) {
	vLeaked_Gas_Sys();
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN7, DIO_PIN_HIGH);  // Start water pump
}

// Turn ON cooling system (indoor fan and exhaust)
void vCooling_Sys_On(void) {
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN3, DIO_PIN_HIGH);  // Start indoor fan
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN4, DIO_PIN_HIGH);  // Start exhaust fan
}

// Turn OFF cooling system
void vCooling_Sys_Off(void) {
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN3, DIO_PIN_LOW);   // Stop indoor fan
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN4, DIO_PIN_LOW);   // Stop exhaust fan
}

// Turn ON indoor light
void vIn_door_on(void) {
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_HIGH);  // Turn on indoor light
}

// Turn OFF indoor light
void vIn_door_off(void) {
	DIO_vSetPinValue(DIO_PORTC, DIO_PIN5, DIO_PIN_LOW);   // Turn off indoor light
}

// Turn ON outdoor light
void vOut_door_on(void) {
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN2, DIO_PIN_HIGH);  // Turn on outdoor light
}

// Turn OFF outdoor light
void vOut_door_off(void) {
	DIO_vSetPinValue(DIO_PORTD, DIO_PIN2, DIO_PIN_LOW);   // Turn off outdoor light
}
