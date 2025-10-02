/**************************************************************************************************
* Copyright © 2024 ams-OSRAM AG                                                                   *
* All rights are reserved.                                                                        *
*                                                                                                 *
* FOR FULL LICENSE TEXT SEE LICENSES-MIT.TXT                                                      *
*                                                                                                 *
**************************************************************************************************/

/* tmf8829 arduino uno sample program */

// ---------------------------------------------- includes ----------------------------------------
#include "tmf8829_app.h"

// ---------------------------------------------- defines  ----------------------------------------
//#define UART_BAUD_RATE              115200
#define UART_BAUD_RATE              2000000
#define I2C_CLK_SPEED               400000

// ------------------------------------------------------------------------------------------------

// Arduino setup function is only called once at startup. Do all the HW initialisation stuff here.
void setup ( )
{
  setupFn( 2 /* log-level nr*/, UART_BAUD_RATE, I2C_CLK_SPEED );
}

// Arduino main loop function, is executed cyclic
void loop ( )
{
  loopFn( );
}
