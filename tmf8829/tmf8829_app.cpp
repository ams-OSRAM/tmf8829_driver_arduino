/**************************************************************************************************
* Copyright © 2024 ams-OSRAM AG                                                                   *
* All rights are reserved.                                                                        *
*                                                                                                 *
* FOR FULL LICENSE TEXT SEE LICENSES-MIT.TXT                                                      *
*                                                                                                 *
**************************************************************************************************/

/* TMF8829 Arduino Uno sample program */

// ---------------------------------------------- includes ----------------------------------------
#include "tmf8829_shim.h"
#include "tmf8829.h"
#include "tmf8829_app.h"
#include "tmf8829_image.h"

// ---------------------------------------------- defines -----------------------------------------
/** Application version number
 * x .. not used until DRIVER version 1.2
 * 1 .. update to clock correction feature
 * 2 .. new image file
 * 3 .. new image file 1.2.194
     .. more documentation added to application 
*/
#define TMF8829_APPLICATION_MINOR_VERSION    3


#define NR_OF_MEAS_CFGS 9 /**< number of preconfiguration commands that are available, see TMF8829_CMD_STAT */

/* tmf application states */
#define TMF8829_STATE_DISABLED      0 /**< application status, device is disabled */
#define TMF8829_STATE_STANDBY       1 /**< application status, device in standby */
#define TMF8829_STATE_STOPPED       2 /**< application status, device in active mode, but no measurement ongoing */
#define TMF8829_STATE_MEASURE       3 /**< application status, device is doing measurements */
#define TMF8829_STATE_ERROR         4 /**< application status in error mode, device in unexpected behaviour */

#define NR_LOG_LEVELS               9 /**< number of log-levels in application */

#define NR_REGS_PER_LINE            8 /**< number of registers that are printed in the dump on one line */

#define TMF8829_BINARY_BUF_SIZE     ( TMF8829_CFG_PAGE_SIZE + 5 ) /**< maximum binary command payload size with 5 spare bytes */

/* binary command identifiers */
#define TMF8829_BINARY_CMD_CONFIGURE      0x31  /**< sets arbitrary configuration */
#define TMF8829_BINARY_CMD_PRE_CONFIGURE  0x32  /**< sets pre-configuration */
#define TMF8829_BINARY_CMD_CHAR_MODE      0x00  /**< not a valid command identifier, indicates that the application is in character input mode */
#define TMF8829_BINARY_CMD_PENDING        0xFF  /**< not a valid command identifier, indicates that the application is in binary input mode awaiting a command identifier */

// ---------------------------------------------- constants -----------------------------------------
/** @brief logLevels to increase/decrease logging
 */
const uint8_t logLevels[ NR_LOG_LEVELS ] = 
{ TMF8829_LOG_LEVEL_NONE
, TMF8829_LOG_LEVEL_ERROR
, TMF8829_LOG_LEVEL_RESULTS_HEADER
, TMF8829_LOG_LEVEL_RESULTS
, TMF8829_LOG_LEVEL_CLK_CORRECTION
, TMF8829_LOG_LEVEL_INFO
, TMF8829_LOG_LEVEL_VERBOSE
, TMF8829_LOG_LEVEL_I2C
, TMF8829_LOG_LEVEL_DEBUG
};

/** @brief measCfg holds the supported pre-configurations by the TMF8829 device.
 */
const int measCfg[NR_OF_MEAS_CFGS] = 
{
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8, 
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_LONG_RANGE,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32_HIGH_ACCURACY
};

// ---------------------------------------------- variables -----------------------------------------

tmf8829Driver tmf8829;            /**< instances of tmf8829 driver */
uint8_t logLevel;                 /**< current log level of the application */
int8_t stateTmf8829;              /**< current state of the device */
int8_t configNr;                  /**< this sample application has only a few configurations it will loop through, the variable keeps track of that */
int8_t clkCorrectionOn;           /**< if non-zero clock correction is on */
volatile uint8_t irqTriggered;    /**< interrupt is triggered or not */
uint8_t binaryCmd;                /**< currently active binary command identifier (if any) */
uint8_t binaryBufFill;            /**< fill level of the binary command payload buffer */
uint8_t binaryBuf[TMF8829_BINARY_BUF_SIZE]; /**< binary command payload buffer */

// ---------------------------------------------- function declaration ------------------------------
void static printDeviceInfo();
void static printHelp();
void static printRegisters( uint8_t regAddr, uint16_t len, char seperator );
void static resetAppState();

// ---------------------------------------------- functions  ----------------------------------------
/******************************************************************************/
/* TMF8829 Device Functions                                                   */
/******************************************************************************/

/** @brief  Function will do a pre-configuration.
 * @param cfgNr ... preconfiguration  eq.: TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8
 */
void preconfigure ( int8_t cfgNr )
{
  int8_t stat = tmf8829Command(&tmf8829, cfgNr);
  
  if ( stat == APP_SUCCESS_OK ) 
  {
    PRINT_CONST_STR( F( "Preconfig " ) );
    PRINT_INT( cfgNr );
    PRINT_CHAR( SEPARATOR );
  }
  else
  {
    PRINT_CONST_STR( F(  "#Err" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "Config" ) );
  }
  PRINT_LN( );
}

/** @brief  Function to get the configuration from the Tmf8829 device.
 *  After that the configuration is printed.
 */
void getConfiguration ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829GetConfiguration( &tmf8829 );
    PRINT_CONST_STR( F(  "#Config " ) );
    for ( int i = 0; i < TMF8829_CFG_PAGE_SIZE; i++ )
    {
      PRINT_UINT_HEX(tmf8829.config[i]);
      PRINT_CHAR( SEPARATOR );
    }
    PRINT_LN( );
  }
}

/** @brief  Function to enable the Tmf8829 device with a firmware download and Ram application start.
  * The configuration and device specific information is read and printed.
  * This function does the initialize steps for the application too.
  * @param imageStartAddress ... tmf8829 memory start address 
  * @param image ... pointer to the image
  * @param imageSizeInBytes ... image size
 */
void enable ( uint32_t imageStartAddress, const unsigned char * image, int32_t imageSizeInBytes )
{
  int8_t status;

  if ( stateTmf8829 == TMF8829_STATE_DISABLED )
  {
    tmf8829Enable( &tmf8829 );
    delayInMicroseconds( ENABLE_TIME_MS * 1000);
    tmf8829ClkCorrection( &tmf8829, clkCorrectionOn ); 
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
    tmf8829PowerUp( &tmf8829 );
    if ( tmf8829IsCpuReady( &tmf8829, CPU_READY_TIME_MS) )
    {
      PRINT_CONST_STR( F( " CPU ready" ) );

      tmf8829BootloaderCmdSpiOff(&tmf8829); //i2c is used

      PRINT_CONST_STR( F( " DWNL FW and start App" ) );
      status = tmf8829DownloadFirmware( &tmf8829, imageStartAddress, image, imageSizeInBytes, 1 /* over fifo */ );
      PRINT_LN( );
      
      if ( status == BL_SUCCESS_OK ) 
      {
        resetAppState();
        tmf8829GetConfiguration(&tmf8829);
        stateTmf8829 = TMF8829_STATE_STOPPED;
        tmf8829ReadDeviceInfo( &tmf8829 );
        printDeviceInfo( );
      }
      else
      {
        stateTmf8829 = TMF8829_STATE_ERROR;
      }
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
    }
  } // else device is already enabled
  else
  {
    tmf8829ReadDeviceInfo( &tmf8829 );
    printDeviceInfo( );
  }
}

/** @brief  Function to set the histogram dumping configuration on the Tmf8829 device.
*/
void histogramDumping ( ) 
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    int8_t stat = tmf8829GetConfiguration(&tmf8829);
    if ( stat == APP_SUCCESS_OK)
    {
      if (tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB])
      {
        tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] = 0;
      }
      else
      {
        tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] = 1;
      }
      stat = tmf8829SetConfiguration(&tmf8829);
    }
    if ( stat == APP_SUCCESS_OK)
    {
    PRINT_CONST_STR( F(  "Dump Histogram Frames is " ) );
    PRINT_INT( tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] );
    PRINT_LN( );
    }
    else
    {
      PRINT_CONST_STR( F(  "#Err" ) );
      PRINT_CHAR( SEPARATOR );
      PRINT_CONST_STR( F(  "Config" ) );
    }
  }
  else
  {
    PRINT_CONST_STR( F(  "could not change to histogram dumping state" ) );
    PRINT_LN( );
  }
}

/** @brief  Function will start a measurement.
 */
void measure ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829ClrAndEnableInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );
    tmf8829StartMeasurement( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_MEASURE;
  }
  else
  {
    PRINT_CONST_STR( F(  "no start of measurement, wrong state" ) );
    PRINT_LN( );
  }
}

/** @brief Function will stop a measurement.
 */
void stop ( )
{
  if ( stateTmf8829 == TMF8829_STATE_MEASURE || stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829StopMeasurement( &tmf8829 );
    tmf8829DisableInterrupts( &tmf8829, 0xFF );               // just disable all
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
}

/** @brief Function will power down the device by setting POFF=1 bit.
 */
void powerDown ( )
{
  if ( stateTmf8829 == TMF8829_STATE_MEASURE )      // stop a measurement first
  {
    tmf8829StopMeasurement( &tmf8829 );
    tmf8829DisableInterrupts( &tmf8829, 0xFF );     // just disable all
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829Standby( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_STANDBY;
  }
}

/** @brief Function to perform a soft reset.
 */
void reset ( )
{
  if ( stateTmf8829 != TMF8829_STATE_DISABLED )
  {
    tmf8829Reset( &tmf8829 );
    PRINT_CONST_STR( F(  "Reset TMF8829" ) );
    PRINT_LN( );
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
}

/** @brief Function to perform a wakeup.
 */
void wakeup ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STANDBY )
  {
    tmf8829Wakeup( &tmf8829 );
    if ( tmf8829IsCpuReady( &tmf8829, CPU_READY_TIME_MS ) )
    {
      stateTmf8829 = TMF8829_STATE_STOPPED;
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
    }
  }
}

/** @brief  Function to read registers and print the content.
  * @param regAddr ... first register address
  * @param len ... len of registers to be read and printed
  * @param seperator ... seperator must be either " " or "," 
 */
void printRegisters ( uint8_t regAddr, uint16_t len, char seperator )
{
  if ( stateTmf8829 != TMF8829_STATE_DISABLED )
  {
    uint8_t buf[NR_REGS_PER_LINE];
    uint16_t i;
    uint8_t j;

    for ( i = 0; i < len; i += NR_REGS_PER_LINE ) // if len is not a multiple of 8, we will print a bit more registers ....
    {
      uint8_t * ptr = buf;    
      i2cRxReg( &tmf8829, tmf8829.i2cSlaveAddress, regAddr, NR_REGS_PER_LINE, buf );
      if ( seperator == ' ' )
      {
        PRINT_CONST_STR( F(  "0x" ) );
        PRINT_UINT_HEX( regAddr );
        PRINT_CONST_STR( F(  ": " ) );
      }
      for ( j = 0; j < NR_REGS_PER_LINE; j++ )
      {
        PRINT_CONST_STR( F(  " 0x" ) ); PRINT_UINT_HEX( *ptr++ ); PRINT_CHAR( seperator ); 
      }
      PRINT_LN( );
      regAddr = regAddr + 8;
    }
    if ( seperator == ',' )
    {
      PRINT_CONST_STR( F(  "};" ) );
      PRINT_LN( );
    }
  }
}

/******************************************************************************/
/* Application Functions                                                      */
/******************************************************************************/

/** @brief Function will change the pre-configuration of the Tmf8829 device.
    Next itemfrom measCfg is used.
 */
void nextConfiguration ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    configNr = configNr + 1;
    if ( configNr >= NR_OF_MEAS_CFGS )
    {
      configNr = 0;     // wrap around
    }
    preconfigure( measCfg[configNr] );
  }
  else
  {
    PRINT_CONST_STR( F(  "No change of configuration, wrong state" ) );
    PRINT_LN( );
  }
}

/** @brief Function will enable/disable clock correction.
 */

void clockCorrection ( )
{
  clkCorrectionOn = !clkCorrectionOn;       // toggle clock correction on/off  
  tmf8829ClkCorrection( &(tmf8829), clkCorrectionOn );
  PRINT_CONST_STR( F(  "Clk corr is " ) );
  PRINT_INT( clkCorrectionOn );
  PRINT_LN( );
}

/** @brief Function will decrease the log Level.
 */
void logLevelDec ( )
{
  if ( logLevel > 0 )
  {
    logLevel--;
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
  }
  PRINT_CONST_STR( F(  "Log=" ) );
  PRINT_INT( logLevels[ logLevel ] );
  PRINT_LN( );
}

/** @brief Function will increase the log Level.
 */

void logLevelInc ( )
{
  if ( logLevel < NR_LOG_LEVELS - 1 )
  {
    logLevel++;
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
  }
  PRINT_CONST_STR( F(  "Log=" ) );
  PRINT_INT( logLevels[ logLevel ] );
  PRINT_LN( );
}

/** @brief Function will print the Arduino version,
 * firmware version, chip version and serial number.
 */
void printDeviceInfo ( )
{
  PRINT_CONST_STR( F(  "TMF8829 Arduino Driver Version " ) );
  PRINT_INT( tmf8829.info.version[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.info.version[1] ); PRINT_CHAR( '.' );
  PRINT_INT( TMF8829_APPLICATION_MINOR_VERSION );
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Firmware Application Version " ) );
  PRINT_INT( tmf8829.device.appVersion[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[1] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[2] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[3] ); PRINT_CHAR( '.' );
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Chip Version " ) );
  PRINT_INT( tmf8829.device.chipVersion[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.chipVersion[1] ); 
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Serial Number 0x" ) );
  PRINT_UINT_HEX( tmf8829.device.deviceSerialNumber );
  PRINT_LN( );
}

/** @brief Function prints the current state (stateTmf8829) in a readable format
 */
void printState ( )
{
  PRINT_CONST_STR( F(  " state=" ) );
  switch ( stateTmf8829 )
  {
    case TMF8829_STATE_DISABLED: PRINT_CONST_STR( F(  "disabled" ) ); break;
    case TMF8829_STATE_STANDBY: PRINT_CONST_STR( F(  "standby" ) ); break;
    case TMF8829_STATE_STOPPED: PRINT_CONST_STR( F(  "stopped" ) ); break;
    case TMF8829_STATE_MEASURE: PRINT_CONST_STR( F(  "measure" ) ); break;
    case TMF8829_STATE_ERROR: PRINT_CONST_STR( F(  "error" ) ); break;   
    default: PRINT_CONST_STR( F(  "???" ) ); break;
  }
  PRINT_LN( );
}

/******************************************************************************/
/* Binary Input Functions                                                     */
/******************************************************************************/
/* For communication with more than one character the Binary Input Mode must be used instead of the Character Input Mode.
  
  The supported binary commands are TMF8829_BINARY_CMD_CONFIGURE and TMF8829_BINARY_CMD_PRE_CONFIGURE.

  Note:
  The payload for these commands must exactly fit! Too long or too short commands will end in unexpected behaviour of the application.
  (If a command is too short, the application will not exit the binary mode.)
  An expected commands payload size is known with the function binaryCmdPayloadSize().

  To enter the mode the character 'b' must be sent first and the function enterBinaryInputMode() is called.
  As long as data for the command is received, the binary mode is active. The function isInBinaryInputMode() is used to know which mode is active.
  The incoming data is processed with the function handleBinaryInput(). If the right amount of data is received,
  the binary command is executed in the function handleCompleteBinaryCmd.
  The mode is left with the function exitBinaryInputMode().
 */

/** @brief This function enters the binary input mode
 */
void enterBinaryInputMode ( )
{
  binaryCmd = TMF8829_BINARY_CMD_PENDING;
  binaryBufFill = 0;
  PRINT_CONST_STR( F( "Binary input mode active" ) );
  PRINT_LN( );
}

/** @brief This function leaves the binary input mode.
 */
void exitBinaryInputMode ( )
{
  binaryCmd = TMF8829_BINARY_CMD_CHAR_MODE;
  binaryBufFill = 0;
  PRINT_CONST_STR( F( "Binary input mode inactive" ) );
  PRINT_LN( );
  printState( );
}

/** @brief This function returns the expected binary command payload size.
 *  @param cmd ... the binary command.
 * \return  expected payload size if a valid identifier is passed, -1 if the identifier is invalid
 */
int16_t binaryCmdPayloadSize( uint8_t cmd ) {
  if ( cmd == TMF8829_BINARY_CMD_CONFIGURE )
  {
    return (TMF8829_CFG_PAGE_SIZE);
  }
  else if  ( cmd == TMF8829_BINARY_CMD_PRE_CONFIGURE )
  {
    return sizeof(uint8_t);
  }
  else
  {
    PRINT_CONST_STR( F( "#Err,NoCmdPayload" ) );
    PRINT_LN( );
    return -1;
  }
}
/** @brief Function checks for binary input mode.
 * \return 1 if in binary input mode, 0 if in character input mode
*/
int8_t isInBinaryInputMode ( )
{
  if ( binaryCmd == TMF8829_BINARY_CMD_CHAR_MODE ) {
    return 0;
  } else {
    return 1;
  }
}

/** @brief This function handles a received binary command with payload of the expected size.
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t handleCompleteBinaryCmd ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED && binaryCmd == TMF8829_BINARY_CMD_CONFIGURE)
  {
    int8_t stat = tmf8829GetConfiguration(&tmf8829);
     
    if (stat == APP_SUCCESS_OK )
    {
      memcpy(tmf8829.config, binaryBuf, TMF8829_CFG_PAGE_SIZE);
      stat = tmf8829SetConfiguration(&tmf8829);
    }
    if (stat == APP_SUCCESS_OK )
    {
      PRINT_CONST_STR( F( "Config changed" ) );
      PRINT_CHAR( SEPARATOR );
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
      PRINT_CONST_STR( F(  "#Err" ) );
      PRINT_CHAR( SEPARATOR );
      PRINT_CONST_STR( F(  "Config" ) );
    }
    PRINT_LN( );
  }
  else if ( stateTmf8829 == TMF8829_STATE_STOPPED && binaryCmd == TMF8829_BINARY_CMD_PRE_CONFIGURE )
  {
    uint8_t precmd = *binaryBuf;
    preconfigure(  precmd );
  }
  else
  {
    PRINT_CONST_STR( F( "Unknown Command" ) );
    PRINT_UINT_HEX( binaryCmd); 
    PRINT_CONST_STR( F( "State" ) );
    PRINT_UINT_HEX( stateTmf8829); 
    PRINT_LN( );
  }

  return 0;
}

/** @brief This function handles a single incoming byte in binary input mode.
 *  @param byte ... incoming byte
 * \return 1 if program termination is requested, otherwise 0
 */
int8_t handleBinaryInput ( uint8_t byte )
{
  if ( binaryCmd == TMF8829_BINARY_CMD_PENDING )
  {
    if ( binaryCmdPayloadSize( byte ) == -1 ) // function returns -1 if command identifier is invalid
    {
      PRINT_CONST_STR( F( "#Err,BinaryCmd," ) );
      PRINT_UINT_HEX( byte );
      PRINT_LN( );
      exitBinaryInputMode( );
    }
    else
    {
      binaryCmd = byte;
    }
  }
  else
  {
    if ( binaryBufFill >= TMF8829_BINARY_BUF_SIZE ) // prevent buffer overflow (this check is not neccessary when TMF8829_BINARY_BUF_SIZE is correctly set to the maximum payload size of all binary commands)
    {
      PRINT_CONST_STR( F( "#Err,BinaryBuf" ) );
      PRINT_LN( );
      exitBinaryInputMode( );
    }
    else
    {
      binaryBuf[binaryBufFill++] = byte; //fill the Buffer 

      if ( binaryBufFill == binaryCmdPayloadSize( binaryCmd ) )
      {
        int8_t res = handleCompleteBinaryCmd( ); // handle binary command when expected payload size has been reached
        exitBinaryInputMode( );
        return res;
      }
    }
  }

  return 0;
}

/******************************************************************************/
/* Character Input Functions                                                  */
/******************************************************************************/
#define DELAY_PRINT_HELP  10000 /**< delay between the prints */

/** @brief  Function prints a help screen.
 */
void printHelp ( )
{
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_CONST_STR( F(  "TMF8829" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "a ... dump registers" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "c ... next configuration" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "d ... disable device" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "e ... enable device and download TMF8829 FW" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "h ... help " ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "m ... measure" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "p ... power down" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "s ... stop measure" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "u ... get configuration" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "w ... wakeup" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "x ... clock corr on/off" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "z ... histogram" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "+ ... log+" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "- ... log-" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "# ... reset" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); 
}

/** @brief  This function handles a single incoming character in character input mode.
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t handleCharInput ( char key )
{
  if ( key < 33 || key >= 126 ) // skip all control characters and DEL  
  {
    return 0; // nothing to do here
  }

  if ( key == 'h' )
  {
    printHelp(); 
  }
  else if ( key == 'c' ) // show and use next configuration
  {
    nextConfiguration( );
  }
  else if ( key == 'e' ) // enable and download FW
  {  
    enable( tmf8829_image_start, tmf8829_image, tmf8829_image_length );
  }
  else if ( key == 'd' )       // disable
  {  
    tmf8829Disable( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_DISABLED;
  }
  else if ( key == 'u' )       // get configuration
  {
    getConfiguration( );
  }
  else if ( key == 'w' )       // wakeup
  {
    wakeup( );
  }
  else if ( key == 'p' )       // power down
  {
    powerDown( );
  }
  else if ( key == 'm' )
  {  
    measure( );
  }
  else if ( key == 's' )
  {
    stop( );
  }
  else if ( key == 'z' )
  {
    histogramDumping( );
  }
  else if ( key == 'a' )
  {  
    if ( stateTmf8829 != TMF8829_STATE_DISABLED )
    {
      printRegisters( 0x00, 256, ' ' );  
    }
  }
  else if ( key == 'x' )
  {
    clockCorrection( );
  }
  else if ( key == '+' ) // increase logging
  {
    logLevelInc( );
  }
  else if ( key == '-' ) // decrease logging
  {
    logLevelDec( );
  }
  else if ( key == '#' ) // reset chip to test the reset function itself
  {
    reset( );
  }
  else if ( key == 'q' ) // terminate on device where this can be done
  {
    return 0; // terminate if possible
  }
  else if ( key == 'b' )       // binary mode
  {  
    enterBinaryInputMode( );
  }
  else 
  {
    PRINT_CONST_STR( F(  "#Err" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "Cmd " ) );
    PRINT_CHAR( key );
    PRINT_LN( );
  }
  
  printState();
  return 0;
}

/******************************************************************************/
/* Arduino helper functions                                                   */
/******************************************************************************/

/** @brief  Function checks the UART for received characters and interprets them
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t serialInput ( )
{
  char rx;
  int8_t read = inputGetKey( &rx );
  while ( read ) {
    int8_t res;
    if ( isInBinaryInputMode( ) )
    {
      res = handleBinaryInput( ( uint8_t ) rx );
    }
    else
    {
      res = handleCharInput( rx );
    }
    if ( res != 0 ) {
      return res;
    }

    read = inputGetKey( &rx );
  }
  return 0;     // rx must be 0 to leave while loop
}

/** @brief This function resets the status of the application
 */
void resetAppState ( )
{
  stateTmf8829 = TMF8829_STATE_DISABLED;
  configNr = NR_OF_MEAS_CFGS;        // reset of preconfigure Nr.
  clkCorrectionOn = 1;
  irqTriggered = 0;
}

/** @brief Interrupt handler is called when INT pin goes low.
 */
void interruptHandler ( void )
{
  irqTriggered = 1;
}

/******************************************************************************/
/* Arduino specific functions                                                 */
/******************************************************************************/

void setupFn( uint8_t logLevelIdx, uint32_t baudrate, uint32_t i2cClockSpeedInHz )
{
  logLevel = logLevelIdx;

  configurePins( &tmf8829 );

  // start serial and i2c
  inputOpen( baudrate );
  i2cOpen( &tmf8829, i2cClockSpeedInHz );
  resetAppState( );
  tmf8829Initialise( &tmf8829 );
  tmf8829SetLogLevel( &tmf8829, logLevels[ logLevelIdx ] );
  setInterruptHandler( interruptHandler );
  tmf8829Disable( &tmf8829 );                                     // this resets the I2C address in the device
  delayInMicroseconds(CAP_DISCHARGE_TIME_MS * 1000); // wait for a proper discharge of the cap
  printHelp();
}

int8_t loopFn ( )
{
  int8_t res = APP_SUCCESS_OK;
  uint8_t intStatus = 0;
  int8_t exit = serialInput();   // handle any keystrokes from UART

#if ( defined( USE_INTERRUPT_TO_TRIGGER_READ ) && (USE_INTERRUPT_TO_TRIGGER_READ != 0) )
  if ( irqTriggered )
  {
    disableInterrupts( );
    irqTriggered = 0;
    enableInterrupts( );

#else
  if ( stateTmf8829 == TMF8829_STATE_MEASURE )
  { 
#endif

    intStatus = tmf8829GetAndClrInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );
    if ( intStatus & TMF8829_APP_INT_RESULTS )   // check if a result is available
    {
      res = tmf8829ReadResults( &tmf8829 );
    }
    if ( intStatus & TMF8829_APP_INT_HISTOGRAMS )
    {
      res = tmf8829ReadHistogram( &tmf8829);
    }

  }

  if ( res != APP_SUCCESS_OK ) // in case that fails there is some error in programming or on the device, this should not happen
  {
    tmf8829DisableInterrupts( &tmf8829, 0xFF );
    tmf8829StopMeasurement( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_STOPPED;
    PRINT_CONST_STR( F(  "#Err" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "inter" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_INT( intStatus );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "but no data" ) );
    PRINT_LN( );
  }
  return !exit;    // 1 == loop again, 0 == exit
}

void terminateFn ( )
{
  tmf8829Disable( &tmf8829 );
  clrInterruptHandler( );

  i2cClose( &tmf8829 );
  inputClose( );
}
