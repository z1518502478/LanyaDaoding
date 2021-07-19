/******************************************************************************

@file  multi_role.c

@brief This file contains the multi_role sample application for use
with the CC2650 Bluetooth Low Energy Protocol Stack.

Group: WCS, BTS
Target Device: cc2640r2

******************************************************************************

 Copyright (c) 2013-2020, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************


*****************************************************************************/

/*********************************************************************
* INCLUDES
*/
#include <string.h>

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/display/Display.h>
#include <ti/display/lcd/LCDDogm1286.h>

#if defined( USE_FPGA ) || defined( DEBUG_SW_TRACE )
#include <driverlib/ioc.h>
#endif // USE_FPGA | DEBUG_SW_TRACE

#include <icall.h>
#include "util.h"
/* This Header file contains all BLE API and icall structure definition */
#include "icall_ble_api.h"


#include "devinfoservice.h"
#include "simple_gatt_profile.h"
#include "multi.h"
#include "ibeaconcfg.h"
#include "snv.h"
#include "hal_uart.h"

#include "board_key.h"
#include "board.h"


#include "multi_role.h"

/*********************************************************************
* CONSTANTS
*/

// Enable/Disable Unlimited Scanning Feature
#define ENABLE_UNLIMITED_SCAN_RES             FALSE

// Maximum number of scan responses
// this can only be set to 15 because that is the maximum
// amount of item actions the menu module supports
#define DEFAULT_MAX_SCAN_RES                  15

// Advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160
#define GAP_DEVICE_NAME_L_LEN                 6  
// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL

// Connection parameters
#define DEFAULT_CONN_INT                      200
#define DEFAULT_CONN_TIMEOUT                  1000
#define DEFAULT_CONN_LATENCY                  0

// Default service discovery timer delay in ms
#define DEFAULT_SVC_DISCOVERY_DELAY           1000

// 扫描参数
#define DEFAULT_SCAN_DURATION                 1000
#define DEFAULT_SCAN_WIND                     500
#define DEFAULT_SCAN_INT                      500

// Discovey mode (limited, general, all)
#define DEFAULT_DISCOVERY_MODE                DEVDISC_MODE_ALL

// TRUE 使用活动扫描
#define DEFAULT_DISCOVERY_ACTIVE_SCAN         TRUE

// Set desired policy to use during discovery (use values from GAP_Disc_Filter_Policies)
#define DEFAULT_DISCOVERY_WHITE_LIST          GAP_DISC_FILTER_POLICY_ALL

// Type of Display to open
#if !defined(Display_DISABLE_ALL)
  #if defined(BOARD_DISPLAY_USE_LCD) && (BOARD_DISPLAY_USE_LCD!=0)
    #define MR_DISPLAY_TYPE Display_Type_LCD
  #elif defined (BOARD_DISPLAY_USE_UART) && (BOARD_DISPLAY_USE_UART!=0)
    #define MR_DISPLAY_TYPE Display_Type_UART
  #else // !BOARD_DISPLAY_USE_LCD && !BOARD_DISPLAY_USE_UART
    #define MR_DISPLAY_TYPE 0 // Option not supported
  #endif // BOARD_DISPLAY_USE_LCD && BOARD_DISPLAY_USE_UART
#else // BOARD_DISPLAY_USE_LCD && BOARD_DISPLAY_USE_UART
  #define MR_DISPLAY_TYPE 0 // No Display
#endif // Display_DISABLE_ALL

// Task configuration
#define MR_TASK_PRIORITY                     1
#ifndef MR_TASK_STACK_SIZE
#define MR_TASK_STACK_SIZE                   610
#endif

// Internal Events for RTOS application
#define MR_ICALL_EVT                         ICALL_MSG_EVENT_ID // Event_Id_31
#define MR_QUEUE_EVT                         UTIL_QUEUE_EVENT_ID // Event_Id_30
#define MR_STATE_CHANGE_EVT                  Event_Id_00
#define MR_CHAR_CHANGE_EVT                   Event_Id_01
#define MR_CONN_EVT_END_EVT                  Event_Id_02
#define MR_KEY_CHANGE_EVT                    Event_Id_03
#define MR_PAIRING_STATE_EVT                 Event_Id_04
#define MR_PASSCODE_NEEDED_EVT               Event_Id_05
#define MR_PERIODIC_EVT                      Event_Id_06
#define MR_PERIODIC_EVT1                     Event_Id_07

#define MR_ALL_EVENTS                        (MR_ICALL_EVT           | \
                                             MR_QUEUE_EVT            | \
                                             MR_STATE_CHANGE_EVT     | \
                                             MR_CHAR_CHANGE_EVT      | \
                                             MR_CONN_EVT_END_EVT     | \
                                             MR_KEY_CHANGE_EVT       | \
                                             MR_PAIRING_STATE_EVT    | \
                                             MR_PERIODIC_EVT         | \
                                             MR_PERIODIC_EVT1        | \
                                             MR_PASSCODE_NEEDED_EVT)

// Discovery states
typedef enum {
  BLE_DISC_STATE_IDLE,                // Idle
  BLE_DISC_STATE_MTU,                 // Exchange ATT MTU size
  BLE_DISC_STATE_SVC,                 // Service discovery
  BLE_DISC_STATE_CHAR                 // Characteristic discovery
} discState_t;

// Row numbers
#define MR_ROW_DEV_ADDR      (TBM_ROW_APP)
#define MR_ROW_CONN_STATUS   (TBM_ROW_APP + 1)
#define MR_ROW_ADV           (TBM_ROW_APP + 2)
#define MR_ROW_SECURITY      (TBM_ROW_APP + 3)
#define MR_ROW_STATUS1       (TBM_ROW_APP + 4)
#define MR_ROW_STATUS2       (TBM_ROW_APP + 5)

// address string length is an ascii character for each digit +
// an initial 0x + an ending null character
#define B_STR_ADDR_LEN       ((B_ADDR_LEN*2) + 3)

// How often to perform periodic event (in msec)
#define MR_PERIODIC_EVT_PERIOD               5000
#define MR_PERIODIC_EVT_PERIOD1              3000

// Set the register cause to the registration bit-mask
#define CONNECTION_EVENT_REGISTER_BIT_SET(RegisterCause) (connectionEventRegisterCauseBitMap |= RegisterCause )
// Remove the register cause from the registration bit-mask
#define CONNECTION_EVENT_REGISTER_BIT_REMOVE(RegisterCause) (connectionEventRegisterCauseBitMap &= (~RegisterCause) )
// Gets whether the current App is registered to the receive connection events
#define CONNECTION_EVENT_IS_REGISTERED (connectionEventRegisterCauseBitMap > 0)
// Gets whether the RegisterCause was registered to recieve connection event
#define CONNECTION_EVENT_REGISTRATION_CAUSE(RegisterCause) (connectionEventRegisterCauseBitMap & RegisterCause )
/*********************************************************************
* TYPEDEFS
*/

// App event passed from profiles.
typedef struct
{
  uint16_t event;  // event type
  uint8_t *pData;  // event data pointer
} mrEvt_t;

// pairing callback event
typedef struct
{
  uint16_t connectionHandle; // connection Handle
  uint8_t state;             // state returned from GAPBondMgr
  uint8_t status;            // status of state
} gapPairStateEvent_t;

// discovery information
typedef struct
{
  discState_t discState;   // discovery state
  uint16_t svcStartHdl;    // service start handle
  uint16_t svcEndHdl;      // service end handle
  uint16_t charHdl;        // characteristic handle
} discInfo_t;

// device discovery information with room for string address
typedef struct
{
  uint8_t eventType;                // Indicates advertising event type used by the advertiser: @ref GAP_ADVERTISEMENT_REPORT_TYPE_DEFINES
  uint8_t addrType;                 // Address Type: @ref ADDRTYPE_DEFINES
  uint8_t addr[B_ADDR_LEN];         // Device's Address
  uint8_t strAddr[B_STR_ADDR_LEN];  // Device Address as String
} mrDevRec_t;

// entry to map index to connection handle and store address string for menu module
typedef struct
{
  uint16_t connHandle;              // connection handle of an active connection
  uint8_t strAddr[B_STR_ADDR_LEN];  // memory location for menu module to store address string
} connHandleMapEntry_t;

/*********************************************************************
* GLOBAL VARIABLES
*/

// Display Interface
Display_Handle dispHandle = NULL;
ibeaconinf_config_t ibeaconInf_Config;

/*********************************************************************
* LOCAL VARIABLES
*/

/*********************************************************************
* LOCAL VARIABLES
*/

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Event globally used to post local events and pend on system and
// local events.
static ICall_SyncHandle syncEvent;

// Clock instances for internal periodic events.
static Clock_Struct periodicClock;
static Clock_Struct periodicClock1;

// Queue object used for app messages
static Queue_Struct appMsg;
static Queue_Handle appMsgQueue;

static uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;
// Task configuration
Task_Struct mrTask;
Char mrTaskStack[MR_TASK_STACK_SIZE];

static uint8_t scanRspData[] =
    {
        // complete name
        0x03,
        0x03,
        0xF0, 0xFF,
        0x07,
        0x09,
        'S','H','B','i','k','e',
        0x09,
        0x16, 
        0x06,0x29,
        0x27,0x11,
        0x00,0x01,
        0x68,0x01
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8_t advertData[] =
    {
        // Flags; this sets the device to use limited discoverable
        // mode (advertises for 30 seconds at a time) instead of general
        // discoverable mode (advertises indefinitely)
        0x02, // length of this data
        0x01,
        0x24,
        0x1A, 0xFF,
        0x4C, 0x00,
        0x02,
        0x15,
        0xFB, 0xB5, 0x06, 0x93, 0xA4, 0xE2, 0x4F, 0xB1,                 //UUID
        0xAF, 0xCF, 0xC6, 0x20, 0x21, 0x06, 0x29,                       
        0x00,                                                           //模式
        0x01,0x00,
        0x10,0x10,                                                      //编码
        0xB5
    };

// pointer to allocate the connection handle map
static connHandleMapEntry_t *connHandleMap;

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_L_LEN] = "SHBike";

// 扫描结果个数
static uint8_t scanRes = 0;

// Globals used for ATT Response retransmission
static gattMsgEvent_t *pAttRsp = NULL;
static uint8_t rspTxRetry = 0;

// Pointer to per connection discovery info
discInfo_t *discInfo;

// Maximim PDU size (default = 27 octets)
static uint16 maxPduSize;

// 扫描开始的标志
static bool scanningStarted = FALSE;

// Connecting flag
static uint8_t connecting = FALSE;

// 扫描结果列表
static mrDevRec_t devList[DEFAULT_MAX_SCAN_RES];

// Value to write
static uint8_t charVal = 0;

// Value read/write toggle
static bool doWrite = FALSE;

// Dummy parameters to use for connection updates
gapRole_updateConnParams_t updateParams =
{
  .connHandle = INVALID_CONNHANDLE,
  .minConnInterval = 80,
  .maxConnInterval = 150,
  .slaveLatency = 0,
  .timeoutMultiplier = 200
};

// Connection index for mapping connection handles

extern uint8_t configLimit_Flg;
extern uint8_t writerAttr_Flg;

static uint16_t connIndex = INVALID_CONNHANDLE;

const uint8_t D_FRT[10] ={'2','0','2','1','-','0','1','-','2','2'};                 //固件发布日期 必须与设备信息一致
const uint8_t D_FR[14]={'F','M','V','E','R','S','I','O','N','_','0','0','0','1'};   //固件版本      必须与设备信息一致
const uint8_t D_CKey[16]={0xDE,0x48,0x2B,0x1C,0x22,0x1C,0x6C,0x30,0x3C,0xF0,0x50,0xEB,0x00,0x20,0xB0,0xBD}; //与生产软件配合使用
uint8_t hw[15] ={'H','W','V','E','R','S','I','O','N','_','0','0','0','1','\0'};
// Maximum number of connected devices
static uint8_t maxNumBleConns = MAX_NUM_BLE_CONNS;

/*********************************************************************
* LOCAL FUNCTIONS
*/
static void multi_role_init( void );
static void multi_role_taskFxn(UArg a0, UArg a1);
static uint8_t multi_role_processStackMsg(ICall_Hdr *pMsg);
static uint8_t multi_role_processGATTMsg(gattMsgEvent_t *pMsg);
static void multi_role_processAppMsg(mrEvt_t *pMsg);
static void multi_role_processCharValueChangeEvt(uint8_t paramID);
static void multi_role_processRoleEvent(gapMultiRoleEvent_t *pEvent);
static void multi_role_sendAttRsp(void);
static void multi_role_freeAttRsp(uint8_t status);
static void multi_role_charValueChangeCB(uint8_t paramID);
static uint8_t multi_role_enqueueMsg(uint16_t event, uint8_t *pData);
static void multi_role_startDiscovery(uint16_t connHandle);
static void multi_role_processGATTDiscEvent(gattMsgEvent_t *pMsg);
static uint8_t multi_role_eventCB(gapMultiRoleEvent_t *pEvent);
static void multi_role_paramUpdateDecisionCB(gapUpdateLinkParamReq_t *pReq,
                                             gapUpdateLinkParamReqReply_t *pRsp);
static void multi_role_sendAttRsp(void);
static void multi_role_freeAttRsp(uint8_t status);
static uint16_t multi_role_mapConnHandleToIndex(uint16_t connHandle);
static uint8_t multi_role_addMappingEntry(uint16_t connHandle, uint8_t *addr);
static void multi_role_passcodeCB(uint8_t *deviceAddr, uint16_t connHandle,
                                  uint8_t uiInputs, uint8_t uiOutputs, uint32_t numComparison);
static void multi_role_pairStateCB(uint16_t connHandle, uint8_t state,
                                   uint8_t status);
static void multi_role_performPeriodicTask(void);
static void multi_role_performPeriodicTask1(void);
static void multi_role_clockHandler(UArg arg);
static void multi_role_connEvtCB(Gap_ConnEventRpt_t *pReport);
static void multi_role_addDeviceInfo(uint8 *pAddr, uint8 addrType);

static void SimpleBLEPeripheral_BleParameterGet(void);

/*********************************************************************
 * EXTERN FUNCTIONS
*/
extern void AssertHandler(uint8 assertCause, uint8 assertSubcause);

/*********************************************************************
* PROFILE CALLBACKS
*/

// GAP Role Callbacks
static gapRolesCBs_t multi_role_gapRoleCBs =
{
  multi_role_eventCB,                   // Events to be handled by the app are passed through the GAP Role here
  multi_role_paramUpdateDecisionCB      // Callback for application to decide whether to accept a param update
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t multi_role_simpleProfileCBs =
{
  multi_role_charValueChangeCB // Characteristic value change callback
};

/*********************************************************************
* PUBLIC FUNCTIONS
*/

/*********************************************************************
 * The following typedef and global handle the registration to connection event
 */
typedef enum
{
   NOT_REGISTER       = 0,
   FOR_AOA_SCAN       = 1,
   FOR_ATT_RSP        = 2,
   FOR_AOA_SEND       = 4,
   FOR_TOF_SEND       = 8
}connectionEventRegisterCause_u;

// Handle the registration and un-registration for the connection event, since only one can be registered.
uint32_t       connectionEventRegisterCauseBitMap = NOT_REGISTER; //see connectionEventRegisterCause_u

/*********************************************************************
 * @fn      multi_role_RegistertToAllConnectionEvent()
 *
 * @brief   register to receive connection events for all the connection
 *
 * @param connectionEventRegister represents the reason for registration
 *
 * @return @ref SUCCESS
 *
 */
bStatus_t multi_role_RegistertToAllConnectionEvent (connectionEventRegisterCause_u connectionEventRegisterCause)
{
  bStatus_t status = SUCCESS;

  // in case  there is no registration for the connection event, make the registration
  if (!CONNECTION_EVENT_IS_REGISTERED)
  {
    status = GAP_RegisterConnEventCb(multi_role_connEvtCB, GAP_CB_REGISTER, LINKDB_CONNHANDLE_ALL);
  }
  if(status == SUCCESS)
  {
    //add the reason bit to the bitamap.
    CONNECTION_EVENT_REGISTER_BIT_SET(connectionEventRegisterCause);
  }

  return(status);
}

/*********************************************************************
 * @fn      multi_role_UnRegistertToAllConnectionEvent()
 *
 * @brief   Un register  connection events
 *
 * @param connectionEventRegisterCause represents the reason for registration
 *
 * @return @ref SUCCESS
 *
 */
bStatus_t multi_role_UnRegistertToAllConnectionEvent (connectionEventRegisterCause_u connectionEventRegisterCause)
{
  bStatus_t status = SUCCESS;

  CONNECTION_EVENT_REGISTER_BIT_REMOVE(connectionEventRegisterCause);
  // in case  there is no more registration for the connection event than unregister
  if (!CONNECTION_EVENT_IS_REGISTERED)
  {
    GAP_RegisterConnEventCb(multi_role_connEvtCB, GAP_CB_UNREGISTER, LINKDB_CONNHANDLE_ALL);
  }

  return(status);
}

/*********************************************************************
* @fn      multi_role_createTask
*
* @brief   Task creation function for multi_role.
*
* @param   None.
*
* @return  None.
*/
void multi_role_createTask(void)
{
  Task_Params taskParams;

  // Configure task
  Task_Params_init(&taskParams);
  taskParams.stack = mrTaskStack;
  taskParams.stackSize = MR_TASK_STACK_SIZE;
  taskParams.priority = MR_TASK_PRIORITY;

  Task_construct(&mrTask, multi_role_taskFxn, &taskParams, NULL);
}

/*********************************************************************
* @fn      multi_role_init
*
* @brief   Called during initialization and contains application
*          specific initialization (ie. hardware initialization/setup,
*          table initialization, power up notification, etc), and
*          profile initialization/setup.
*
* @param   None.
*
* @return  None.
*/
static void multi_role_init(void)
{
  // ******************************************************************
  // N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
  // ******************************************************************
  // Register the current thread as an ICall dispatcher application
  // so that the application can send and receive messages.
  ICall_registerApp(&selfEntity, &syncEvent);

  // 建立与RTOS的队列
  appMsgQueue = Util_constructQueue(&appMsg);

  // Create one-shot clocks for internal periodic events.
  Util_constructClock(&periodicClock, multi_role_clockHandler,
                      MR_PERIODIC_EVT_PERIOD, 0, false, MR_PERIODIC_EVT);

  Util_constructClock(&periodicClock1, multi_role_clockHandler,
                      MR_PERIODIC_EVT_PERIOD1, 1, true, MR_PERIODIC_EVT1);
  // Open Display.
  dispHandle = Display_open(MR_DISPLAY_TYPE, NULL);

  Nvram_Init();
  SimpleBLEPeripheral_BleParameterGet();

  if(ibeaconInf_Config.initFlag != 0xFF)
  {
      memcpy(&scanRspData[19], &ibeaconInf_Config.majorValue[0], sizeof(uint32_t));
      memcpy(&scanRspData[17], &ibeaconInf_Config.uuidValue[14], sizeof(uint16_t));
      memcpy(&advertData[9], &ibeaconInf_Config.uuidValue, DEFAULT_UUID_LEN);
      memcpy(&advertData[9 + DEFAULT_UUID_LEN], &ibeaconInf_Config.majorValue, sizeof(uint32_t));
  }

  uint8_t txpower = HCI_EXT_TX_POWER_0_DBM;

  if(ibeaconInf_Config.initFlag != 0xFF)
  {
    switch(ibeaconInf_Config.txInterval)
    {
      case 21:advInt = 1600;
          break;
      case 1: advInt = 1400;                            //875ms
          break;
      case 2: advInt = DEFAULT_ADVERTISING_INTERVAL*5;  //500ms
          break;
      case 3: advInt = DEFAULT_ADVERTISING_INTERVAL*3;  //300ms
          break;
      case 4: advInt = 400;                             //250ms
          break;
      case 5: advInt = DEFAULT_ADVERTISING_INTERVAL*2;  //200ms
          break;
      case 10: advInt = DEFAULT_ADVERTISING_INTERVAL;   //100
          break;
      case 20: advInt = DEFAULT_ADVERTISING_INTERVAL/2; //50ms
          break;
      case 30: advInt = 48;                             //30ms test
          break;
      case 50: advInt = 32;                             //20ms test
          break;
      default: advInt = DEFAULT_ADVERTISING_INTERVAL * 3;
          break;
    }

    switch(ibeaconInf_Config.txPower)
    {
      case 0: txpower = HCI_EXT_TX_POWER_MINUS_21_DBM;  //-21dbm
          break;
      case 1: txpower = HCI_EXT_TX_POWER_MINUS_18_DBM;  //-18dbm
          break;
      case 2: txpower = HCI_EXT_TX_POWER_MINUS_12_DBM;  //-12dbm
          break;
      case 3: txpower = HCI_EXT_TX_POWER_MINUS_9_DBM;   //-9dbm
          break;
      case 4: txpower = HCI_EXT_TX_POWER_MINUS_6_DBM;   //-6dbm
          break;
      case 5: txpower = HCI_EXT_TX_POWER_MINUS_3_DBM;   //-3dbm
          break;
      case 6: txpower = HCI_EXT_TX_POWER_0_DBM;         //0dbm
          break;
      case 7: txpower = HCI_EXT_TX_POWER_2_DBM;         //2dbm
          break;
      default: txpower = HCI_EXT_TX_POWER_0_DBM;
          break;
    }
  }

  HCI_EXT_SetTxPowerCmd(txpower);

  {
    memcpy(&hw[10], &ibeaconInf_Config.hwvr[0], sizeof(uint32_t));
    DevInfo_SetParameter(DEVINFO_HARDWARE_REV, sizeof(hw), hw);
    ibeaconInf_Config.mDate[10] = '\0';
    DevInfo_SetParameter(DEVINFO_MANUFACTUREDATE, 11, &ibeaconInf_Config.mDate[0]);
  }
  // Setup the GAP
  {
    // 将所有场景的发布间隔设置为相同
    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MAX, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MAX, advInt);

    //设置扫描时间
    GAP_SetParamValue(TGAP_GEN_DISC_SCAN, DEFAULT_SCAN_DURATION);

    // 所有场景的扫描间隔和窗口相同
    GAP_SetParamValue(TGAP_GEN_DISC_SCAN_INT, DEFAULT_SCAN_INT);
    GAP_SetParamValue(TGAP_GEN_DISC_SCAN_WIND, DEFAULT_SCAN_WIND);
    GAP_SetParamValue(TGAP_LIM_DISC_SCAN_INT, DEFAULT_SCAN_INT);
    GAP_SetParamValue(TGAP_LIM_DISC_SCAN_WIND, DEFAULT_SCAN_WIND);

    //注册接收GAP和HCI消息
    GAP_RegisterForMsgs(selfEntity);
  }



  // Setup the GAP Role Profile
  {
    /*--------PERIPHERAL-------------*/
    uint8_t initialAdvertEnable = TRUE;
    uint16_t advertOffTime = 0;

    // device starts advertising upon initialization
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                         &initialAdvertEnable, NULL);

    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    GAPRole_SetParameter(GAPROLE_ADVERT_OFF_TIME, sizeof(uint16_t),
                         &advertOffTime, NULL);

    // Set scan response data
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData),
                         scanRspData, NULL);

    // Set advertising data
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData, NULL);

    // set max amount of scan responses
    uint8_t scanRes = 0;

    // In case that the Unlimited Scanning feature is disabled
    // send the number of scan results to the GAP
    //如果“无限扫描”功能被禁用，则将扫描结果的数量发送给GAP
    if(ENABLE_UNLIMITED_SCAN_RES == FALSE)
    {
        scanRes = DEFAULT_MAX_SCAN_RES;
    }

    // 设置扫描响应的最大数量
    GAPRole_SetParameter(GAPROLE_MAX_SCAN_RES, sizeof(uint8_t),
                         &scanRes, NULL);

    // Start the GAPRole and negotiate max number of connections
    VOID GAPRole_StartDevice(&multi_role_gapRoleCBs, &maxNumBleConns);

    // Allocate memory for index to connection handle map
    if (connHandleMap = ICall_malloc(sizeof(connHandleMapEntry_t) * maxNumBleConns))
    {
      // Init index to connection handle map
      for (uint8_t i = 0; i < maxNumBleConns; i++)
      {
        connHandleMap[i].connHandle = INVALID_CONNHANDLE;
      }
    }

    // Allocate memory for per connection discovery information
    if (discInfo = ICall_malloc(sizeof(discInfo_t) * maxNumBleConns))
    {
      // Init index to connection handle map to 0's
      for (uint8_t i = 0; i < maxNumBleConns; i++)
      {
        discInfo[i].charHdl = 0;
        discInfo[i].discState = BLE_DISC_STATE_IDLE;
        discInfo[i].svcEndHdl = 0;
        discInfo[i].svcStartHdl = 0;
      }
    }
  }

  // GATT
  {
    // Set the GAP Characteristics
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_L_LEN, attDeviceName);

    // Initialize GATT Server Services
    GGS_AddService(GATT_ALL_SERVICES);           // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
    DevInfo_AddService();                        // Device Information Service
    SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile

    // Setup Profile Characteristic Values
   {
    uint8_t charValue1[] = {0x34,0x12};

    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, sizeof(uint16_t),
                               charValue1);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, DEFAULT_UUID_LEN,
                               &ibeaconInf_Config.uuidValue[0]);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, sizeof(uint8_t),
                               &ibeaconInf_Config.txPower);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, sizeof(uint16_t),
                               &"ad");
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, sizeof(uint16_t),
                               &ibeaconInf_Config.majorValue[0]);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR6, sizeof(uint16_t),
                               &ibeaconInf_Config.minorValue[0]);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR7, sizeof(uint8_t),
                               &ibeaconInf_Config.txInterval);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR60, sizeof(uint8_t),
                               &ibeaconInf_Config.Rxp);
  }

    // Register callback with SimpleGATTprofile
    SimpleProfile_RegisterAppCBs(&multi_role_simpleProfileCBs);

    /*-----------------CLIENT------------------*/
    // Initialize GATT Client
    VOID GATT_InitClient();

    // Register for GATT local events and ATT Responses pending for transmission
    GATT_RegisterForMsgs(selfEntity);

    // Register to receive incoming ATT Indications/Notifications
    GATT_RegisterForInd(selfEntity);
  }

#if !defined (USE_LL_CONN_PARAM_UPDATE)
  // Get the currently set local supported LE features
  // The HCI will generate an HCI event that will get received in the main
  // loop
  HCI_LE_ReadLocalSupportedFeaturesCmd();
#endif // !defined (USE_LL_CONN_PARAM_UPDATE)

}

/*********************************************************************
* @fn      multi_role_taskFxn
*
* @brief   Application task entry point for the multi_role.
*
* @param   a0, a1 - not used.
*
* @return  None.
*/
static void multi_role_taskFxn(UArg a0, UArg a1)
{
  // Initialize application
  multi_role_init();

  // Application main loop
  for (;;)
  {
    uint32_t events;

    // Waits for an event to be posted associated with the calling thread.
    // Note that an event associated with a thread is posted when a
    // message is queued to the message receive queue of the thread
    events = Event_pend(syncEvent, Event_Id_NONE, MR_ALL_EVENTS,
                        ICALL_TIMEOUT_FOREVER);

    if (events)
    {
      ICall_EntityID dest;
      ICall_ServiceEnum src;
      ICall_HciExtEvt *pMsg = NULL;

      if (ICall_fetchServiceMsg(&src, &dest,
                                (void **)&pMsg) == ICALL_ERRNO_SUCCESS)
      {
        uint8_t safeToDealloc = TRUE;

        if ((src == ICALL_SERVICE_CLASS_BLE) && (dest == selfEntity))
        {
          ICall_Stack_Event *pEvt = (ICall_Stack_Event *)pMsg;

          if (pEvt->signature != 0xffff)
          {
            // Process inter-task message
            safeToDealloc = multi_role_processStackMsg((ICall_Hdr *)pMsg);
          }
        }

        if (pMsg && safeToDealloc)
        {
          ICall_freeMsg(pMsg);
        }
      }

      // If RTOS queue is not empty, process app message.
      if (events & MR_QUEUE_EVT)
      {
        while (!Queue_empty(appMsgQueue))
        {
          mrEvt_t *pMsg = (mrEvt_t *)Util_dequeueMsg(appMsgQueue);
          if (pMsg)
          {
            // Process message.
            multi_role_processAppMsg(pMsg);

            // Free the space from the message.
            ICall_free(pMsg);
          }
        }
      }

      if (events & MR_PERIODIC_EVT)
      {
        Util_startClock(&periodicClock);

        // Perform periodic application task
        multi_role_performPeriodicTask();
      }

      if (events & MR_PERIODIC_EVT1)
      {
          Util_startClock(&periodicClock1);
          
          multi_role_performPeriodicTask1();
      }
    }
  }
}

/*********************************************************************
* @fn      multi_role_processStackMsg
*
* @brief   处理传入的堆栈消息
*
* @param   pMsg - message to process
*
* @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
*/
static uint8_t multi_role_processStackMsg(ICall_Hdr *pMsg)
{
  uint8_t safeToDealloc = TRUE;

  switch (pMsg->event)
  {
    case GATT_MSG_EVENT:
      // Process GATT message
      safeToDealloc = multi_role_processGATTMsg((gattMsgEvent_t *)pMsg);
      break;

    case HCI_GAP_EVENT_EVENT:
      {
        // Process HCI message
        switch(pMsg->status)
        {
          case HCI_COMMAND_COMPLETE_EVENT_CODE:
            // Process HCI Command Complete Event
            {
  #if !defined (USE_LL_CONN_PARAM_UPDATE)

              // This code will disable the use of the LL_CONNECTION_PARAM_REQ
              // control procedure (for connection parameter updates, the
              // L2CAP Connection Parameter Update procedure will be used
              // instead). To re-enable the LL_CONNECTION_PARAM_REQ control
              // procedures, define the symbol USE_LL_CONN_PARAM_UPDATE

              // Parse Command Complete Event for opcode and status
              hciEvt_CmdComplete_t* command_complete = (hciEvt_CmdComplete_t*) pMsg;
              uint8_t   pktStatus = command_complete->pReturnParam[0];

              //find which command this command complete is for
              switch (command_complete->cmdOpcode)
              {
                case HCI_LE_READ_LOCAL_SUPPORTED_FEATURES:
                  {
                    if (pktStatus == SUCCESS)
                    {
                      uint8_t featSet[8];

                      // get current feature set from received event (bits 1-9 of
                      // the returned data
                      memcpy( featSet, &command_complete->pReturnParam[1], 8 );

                      // Clear bit 1 of byte 0 of feature set to disable LL
                      // Connection Parameter Updates
                      CLR_FEATURE_FLAG( featSet[0], LL_FEATURE_CONN_PARAMS_REQ );

                      // Update controller with modified features
                      HCI_EXT_SetLocalSupportedFeaturesCmd( featSet );
                    }
                  }
                  break;

                default:
                  //do nothing
                  break;
              }
  #endif // !defined (USE_LL_CONN_PARAM_UPDATE)

            }
            break;

          case HCI_BLE_HARDWARE_ERROR_EVENT_CODE:
            AssertHandler(HAL_ASSERT_CAUSE_HARDWARE_ERROR,0);
            break;

          default:
            break;
        }
      }
      break;

    case GAP_MSG_EVENT:
      multi_role_processRoleEvent((gapMultiRoleEvent_t *)pMsg);
      break;


    default:
      // Do nothing
      break;
  }

  return (safeToDealloc);
}

/*********************************************************************
* @fn      multi_role_processGATTMsg
*
* @brief   Process GATT messages and events.
*
* @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
*/
static uint8_t multi_role_processGATTMsg(gattMsgEvent_t *pMsg)
{
  // See if GATT server was unable to transmit an ATT response
  if (pMsg->hdr.status == blePending)
  {
    // No HCI buffer was available. Let's try to retransmit the response
    // on the next connection event.
    if( multi_role_RegistertToAllConnectionEvent(FOR_ATT_RSP) == SUCCESS)
    {
      // First free any pending response
      multi_role_freeAttRsp(FAILURE);

      // Hold on to the response message for retransmission
      pAttRsp = pMsg;

      // Don't free the response message yet
      return (FALSE);
    }
  }

  // Messages from GATT server
  if (linkDB_NumActive() > 0)
  {
    // Find index from connection handle
    connIndex = multi_role_mapConnHandleToIndex(pMsg->connHandle);

     if (discInfo[connIndex].discState != BLE_DISC_STATE_IDLE)
    {
      multi_role_processGATTDiscEvent(pMsg);
    }
  } // Else - in case a GATT message came after a connection has dropped, ignore it.

  // Free message payload. Needed only for ATT Protocol messages
  GATT_bm_free(&pMsg->msg, pMsg->method);

  // It's safe to free the incoming message
  return (TRUE);
}

/*********************************************************************
* @fn      multi_role_sendAttRsp
*
* @brief   Send a pending ATT response message.
*
* @param   none
*
* @return  none
*/
static void multi_role_sendAttRsp(void)
{
  // See if there's a pending ATT Response to be transmitted
  if (pAttRsp != NULL)
  {
    uint8_t status;

    // Increment retransmission count
    rspTxRetry++;

    // Try to retransmit ATT response till either we're successful or
    // the ATT Client times out (after 30s) and drops the connection.
    status = GATT_SendRsp(pAttRsp->connHandle, pAttRsp->method, &(pAttRsp->msg));
    if ((status != blePending) && (status != MSG_BUFFER_NOT_AVAIL))
    {
      // Disable connection event end notice
      multi_role_UnRegistertToAllConnectionEvent (FOR_ATT_RSP);

      // We're done with the response message
      multi_role_freeAttRsp(status);
    }
    else
    {
      // Continue retrying
    }
  }
}

/*********************************************************************
* @fn      multi_role_freeAttRsp
*
* @brief   Free ATT response message.
*
* @param   status - response transmit status
*
* @return  none
*/
static void multi_role_freeAttRsp(uint8_t status)
{
  // See if there's a pending ATT response message
  if (pAttRsp != NULL)
  {
    // See if the response was sent out successfully
    if (status == SUCCESS)
    {
    }
    else
    {
      // Free response payload
      GATT_bm_free(&pAttRsp->msg, pAttRsp->method);

    }

    // Free response message
    ICall_freeMsg(pAttRsp);

    // Reset our globals
    pAttRsp = NULL;
    rspTxRetry = 0;
  }
}

/*********************************************************************
* @fn      multi_role_processAppMsg
*
* @brief    处理队列传入的回调
*
* @param   pMsg - message to process
*
* @return  None.
*/
static void multi_role_processAppMsg(mrEvt_t *pMsg)
{
  switch (pMsg->event)
  {
  case MR_STATE_CHANGE_EVT:
    multi_role_processStackMsg((ICall_Hdr *)pMsg->pData);
    // 释放堆栈消息
    ICall_freeMsg(pMsg->pData);
    break;

  case MR_CHAR_CHANGE_EVT:
    multi_role_processCharValueChangeEvt(*(pMsg->pData));
    // Free the app data
    ICall_free(pMsg->pData);
    break;

  case MR_CONN_EVT_END_EVT:
      {
        if( CONNECTION_EVENT_REGISTRATION_CAUSE(FOR_ATT_RSP))
        {
          // The GATT server might have returned a blePending as it was trying
          // to process an ATT Response. Now that we finished with this
          // connection event, let's try sending any remaining ATT Responses
          // on the next connection event.
          // Try to retransmit pending ATT Response (if any)
          multi_role_sendAttRsp();
        }


          ICall_free(pMsg->pData);
          break;
      }

      default:
          // Do nothing.
          break;
      }
}

/*********************************************************************
* @fn      multi_role_eventCB
*
* @brief   Multi GAPRole event callback function.
*
* @param   pEvent - pointer to event structure
*
* @return  TRUE if safe to deallocate event message, FALSE otherwise.
*/
static uint8_t multi_role_eventCB(gapMultiRoleEvent_t *pEvent)
{
  // Forward the role event to the application
  if (multi_role_enqueueMsg(MR_STATE_CHANGE_EVT, (uint8_t *)pEvent))
  {
    // App will process and free the event
    return FALSE;
  }

  // Caller should free the event
  return TRUE;
}

/*********************************************************************
* @fn      multi_role_paramUpdateDecisionCB
*
* @brief   回调来决定是否接受参数更新请求，如果接受，则使用什么参数
*
* @param   pReq - pointer to param update request
* @param   pRsp - pointer to param update response
*
* @return  none
*/
static void multi_role_paramUpdateDecisionCB(gapUpdateLinkParamReq_t *pReq,
                                             gapUpdateLinkParamReqReply_t *pRsp)
{
  // Make some decision based on desired parameters. Here is an example
  // where only parameter update requests with 0 slave latency are accepted
  if (pReq->connLatency == 0)
  {
    // Accept and respond with remote's desired parameters
    pRsp->accepted = TRUE;
    pRsp->connLatency = pReq->connLatency;
    pRsp->connTimeout = pReq->connTimeout;
    pRsp->intervalMax = pReq->intervalMax;
    pRsp->intervalMin = pReq->intervalMin;
  }

  // Don't accept param update requests with slave latency other than 0
  else
  {
    pRsp->accepted = FALSE;
  }
}

/*********************************************************************
* @fn      multi_role_processRoleEvent
*
* @brief   Multi role event processing function.
*
* @param   pEvent - pointer to event structure
*
* @return  none
*/
static void multi_role_processRoleEvent(gapMultiRoleEvent_t *pEvent)
{
  switch (pEvent->gap.opcode)
  {
    // GAPRole started
    case GAP_DEVICE_INIT_DONE_EVENT:
    {
      // Store max pdu size
      maxPduSize = pEvent->initDone.dataPktLen;

      // Set device info characteristic
      DevInfo_SetParameter(DEVINFO_BTADDRESS, DEVINFO_BTADDRESS_LEN, pEvent->initDone.devAddr);
      //mr_doScan(1); 
    }
    break;

    //已发现设备报告
    case GAP_DEVICE_INFO_EVENT:
    {
      if(ENABLE_UNLIMITED_SCAN_RES == TRUE)
      {
        multi_role_addDeviceInfo(pEvent->deviceInfo.addr,
                                        pEvent->deviceInfo.addrType);
      }
    }
    break;

    // End of discovery report
    //发现报告结束
    case GAP_DEVICE_DISCOVERY_EVENT:
    {
        if(pEvent->gap.hdr.status == SUCCESS)
        {
          uint8_t i;

          // Discovery complete
          scanningStarted = FALSE;

          if(ENABLE_UNLIMITED_SCAN_RES == FALSE)
          {
              // If devices were found
              if (pEvent->discCmpl.numDevs > 0)
              {
                // Loop through discovered devices to store in static device list
                for (i = 0; i < pEvent->discCmpl.numDevs; i++)
                {

                  // Store address type
                  devList[i].addrType = pEvent->discCmpl.pDevList[i].addrType;

                  // Store event type (adv / scan response)
                  devList[i].eventType = pEvent->discCmpl.pDevList[i].eventType;

                  // Store address
                  memcpy(devList[i].addr, pEvent->discCmpl.pDevList[i].addr, B_ADDR_LEN);

                  scanRes = pEvent->discCmpl.numDevs;
                }
              }
          }


                for(i = 0; i < scanRes; i++)
                {
                  // Convert address to string
                  uint8_t *pAddr = (uint8_t*)Util_convertBdAddr2Str(devList[i].addr);

                  // Copy converted string to static device list
                  memcpy(devList[i].strAddr, pAddr, B_STR_ADDR_LEN);

                }

        }
        //mr_doScan(1);
    }
    break;

    /*----------------------- 建立链接完成事件 -----------------------*/
    case GAP_LINK_ESTABLISHED_EVENT:
    {
      // If succesfully established
      if (pEvent->gap.hdr.status == SUCCESS)
      {

        // Clear connecting flag
        connecting = FALSE;

        // Add index-to-connHandle mapping entry and update menus
        uint8_t index = multi_role_addMappingEntry(pEvent->linkCmpl.connectionHandle, pEvent->linkCmpl.devAddr);

        //turn off advertising if no available links
        if (linkDB_NumActive() >= maxNumBleConns)
        {
          uint8_t advertEnabled = FALSE;
          GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertEnabled, NULL);
        }

        // Start service discovery
        multi_role_startDiscovery(pEvent->linkCmpl.connectionHandle);

        // Start periodic clock if this is the first connection
        if (linkDB_NumActive() == 1)
        {
          Util_startClock(&periodicClock);
        }
      }
    }
    break;

    // Connection has been terminated
    case GAP_LINK_TERMINATED_EVENT:
    {
      // read current num active so that this doesn't change before this event is processed
      uint8_t currentNumActive = linkDB_NumActive();

      // Find index from connection handle
      connIndex = multi_role_mapConnHandleToIndex(pEvent->linkTerminate.connectionHandle);

      // Check to prevent buffer overrun
      if (connIndex < maxNumBleConns)
      {
        // Clear screen, reset discovery info, and return to main menu
        connHandleMap[connIndex].connHandle = INVALID_CONNHANDLE;

        // Reset discovery info
        discInfo[connIndex].discState = BLE_DISC_STATE_IDLE;
        discInfo[connIndex].charHdl = 0;


        // If there aren't any active connections
        if (currentNumActive == 0)
        {
          // Stop periodic clock
          Util_stopClock(&periodicClock);
        }

        configLimit_Flg = FALSE;
        if (writerAttr_Flg == TRUE)
        {
            Ble_WriteNv_Inf(BLE_NVID_CUST_START, &ibeaconInf_Config.txPower);
            HCI_EXT_ResetSystemCmd(HCI_EXT_RESET_SYSTEM_HARD);
        }
      }
    }
    break;

  default:
    break;
  }
}

/*********************************************************************
* @fn      multi_role_charValueChangeCB
*
* @brief   Callback from Simple Profile indicating a characteristic
*          value change.
*
* @param   paramID - parameter ID of the value that was changed.
*
* @return  None.
*/
static void multi_role_charValueChangeCB(uint8_t paramID)
{
  uint8_t *pData;

  // Allocate space for the event data.
  if ((pData = ICall_malloc(sizeof(uint8_t))))
  {
    *pData = paramID;

    // Queue the event.
    multi_role_enqueueMsg(MR_CHAR_CHANGE_EVT, pData);
  }
}

/*********************************************************************
* @fn      multi_role_processCharValueChangeEvt
*
* @brief   处理一个挂起的简单概要特征值更改事件
*
* @param   paramID - parameter ID of the value that was changed.
*
* @return  None.
*/
static void multi_role_processCharValueChangeEvt(uint8_t paramID)
{
  uint8_t newValue;

  // Print new value depending on which characteristic was updated
  switch(paramID)
  {
  case SIMPLEPROFILE_CHAR1:
    // Get new value
    SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR1, &newValue);
    break;

  case SIMPLEPROFILE_CHAR3:
    // Get new value
    SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &newValue);
    break;

  default:
    // Should not reach here!
    break;
  }
}

/*********************************************************************
* @fn      multi_role_enqueueMsg
*
* @brief   创建消息并将消息放入RTOS队列中。
*
* @param   event - message event.
* @param   pData - pointer to data to be queued
*
* @return  None.
*/
static uint8_t multi_role_enqueueMsg(uint16_t event, uint8_t *pData)
{
  // Allocate space for the message
  mrEvt_t *pMsg = ICall_malloc(sizeof(mrEvt_t));

  // If sucessfully allocated
  if (pMsg)
  {
    // Fill up message
    pMsg->event = event;
    pMsg->pData = pData;

    // Enqueue the message.
    return Util_enqueueMsg(appMsgQueue, syncEvent, (uint8_t *)pMsg);
  }

  return FALSE;
}

/*********************************************************************
* @fn      multi_role_startDiscovery
*
* @brief   开始发现服务.
*
* @param   connHandle - connection handle
*
* @return  none
*/
static void multi_role_startDiscovery(uint16_t connHandle)
{
  // Exchange MTU request
  attExchangeMTUReq_t req;

  // Map connection handle to index
  connIndex = multi_role_mapConnHandleToIndex(connHandle);

  // Check to prevent buffer overrun
  if (connIndex < maxNumBleConns)
  {
    // Update discovery state of this connection
    discInfo[connIndex].discState= BLE_DISC_STATE_MTU;

    // Initialize cached handles
    discInfo[connIndex].svcStartHdl = discInfo[connIndex].svcEndHdl = 0;
  }

  // Discover GATT Server's Rx MTU size
  req.clientRxMTU = maxPduSize - L2CAP_HDR_SIZE;

  // ATT MTU size should be set to the minimum of the Client Rx MTU
  // and Server Rx MTU values
  VOID GATT_ExchangeMTU(connHandle, &req, selfEntity);
}

/*********************************************************************
* @fn      multi_role_processGATTDiscEvent
*
* @brief   Process GATT discovery event
*
* @param   pMsg - pointer to discovery event stack message
*
* @return  none
*/
static void multi_role_processGATTDiscEvent(gattMsgEvent_t *pMsg)
{
  // Map connection handle to index
  connIndex = multi_role_mapConnHandleToIndex(pMsg->connHandle);
  // Check to prevent buffer overrun
  if (connIndex < maxNumBleConns)
  {
    //MTU update
    if (pMsg->method == ATT_MTU_UPDATED_EVENT)
    {
      // MTU size updated
    }
    // If we've updated the MTU size
    else if (discInfo[connIndex].discState == BLE_DISC_STATE_MTU)
    {
      // MTU size response received, discover simple service
      if (pMsg->method == ATT_EXCHANGE_MTU_RSP)
      {
        uint8_t uuid[ATT_BT_UUID_SIZE] = { LO_UINT16(SIMPLEPROFILE_SERV_UUID),
        HI_UINT16(SIMPLEPROFILE_SERV_UUID) };

        // Advance state
        discInfo[connIndex].discState= BLE_DISC_STATE_SVC;

        // Discovery of simple service
        VOID GATT_DiscPrimaryServiceByUUID(pMsg->connHandle, uuid, ATT_BT_UUID_SIZE,
                                           selfEntity);
      }
    }
    // If we're performing service discovery
    else if (discInfo[connIndex].discState == BLE_DISC_STATE_SVC)
    {
      // Service found, store handles
      if (pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
          pMsg->msg.findByTypeValueRsp.numInfo > 0)
      {
        discInfo[connIndex].svcStartHdl = ATT_ATTR_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
        discInfo[connIndex].svcEndHdl = ATT_GRP_END_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
      }

      // If procedure is complete
      if (((pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP) &&
           (pMsg->hdr.status == bleProcedureComplete))  ||
          (pMsg->method == ATT_ERROR_RSP))
      {
        // If we've discovered the service
        if (discInfo[connIndex].svcStartHdl != 0)
        {
          attReadByTypeReq_t req;

          // Discover characteristic
          discInfo[connIndex].discState = BLE_DISC_STATE_CHAR;
          req.startHandle = discInfo[connIndex].svcStartHdl;
          req.endHandle = discInfo[connIndex].svcEndHdl;
          req.type.len = ATT_BT_UUID_SIZE;
          req.type.uuid[0] = LO_UINT16(SIMPLEPROFILE_CHAR1_UUID);
          req.type.uuid[1] = HI_UINT16(SIMPLEPROFILE_CHAR1_UUID);

          // Send characteristic discovery request
          VOID GATT_DiscCharsByUUID(pMsg->connHandle, &req, selfEntity);
        }
      }
    }
    // If we're discovering characteristics
    else if (discInfo[connIndex].discState == BLE_DISC_STATE_CHAR)
    {
      // Characteristic found
      if ((pMsg->method == ATT_READ_BY_TYPE_RSP) &&
          (pMsg->msg.readByTypeRsp.numPairs > 0))
      {
        // Store handle
        discInfo[connIndex].charHdl = BUILD_UINT16(pMsg->msg.readByTypeRsp.pDataList[3],
                                                   pMsg->msg.readByTypeRsp.pDataList[4]);
      }
    }
  }
}

/*********************************************************************
* @fn      multi_role_mapConnHandleToIndex
*
* @brief   Translates connection handle to index
*
* @param   connHandle - the connection handle
*
* @return  index or INVALID_CONNHANDLE if connHandle isn't found
*/
static uint16_t multi_role_mapConnHandleToIndex(uint16_t connHandle)
{
  uint16_t index;
  // Loop through connection
  for (index = 0; index < maxNumBleConns; index ++)
  {
    // If matching connection handle found
    if (connHandleMap[index].connHandle == connHandle)
    {
      return index;
    }
  }
  // Not found if we got here
  return INVALID_CONNHANDLE;
}

/************************************************************************
* @fn      multi_role_pairStateCB
*
* @param   connHandle - the connection handle
*
* @param   state - pairing state
*
* @param   status - status of pairing state
*
* @return  none
*/
static void multi_role_pairStateCB(uint16_t connHandle, uint8_t state,
                                   uint8_t status)
{
  gapPairStateEvent_t *pData;

  // Allocate space for the passcode event.
  if ((pData = ICall_malloc(sizeof(gapPairStateEvent_t))))
  {
    pData->connectionHandle = connHandle;
    pData->state = state;
    pData->status = status;

    // Enqueue the event.
    multi_role_enqueueMsg(MR_PAIRING_STATE_EVT, (uint8_t *) pData);
  }
}

/*********************************************************************
* @fn      multi_role_passcodeCB
*
* @brief   Passcode callback.
*
* @param   deviceAddr - pointer to device address
*
* @param   connHandle - the connection handle
*
* @param   uiInputs - pairing User Interface Inputs
*
* @param   uiOutputs - pairing User Interface Outputs
*
* @param   numComparison - numeric Comparison 20 bits
*
* @return  none
*/
static void multi_role_passcodeCB(uint8_t *deviceAddr, uint16_t connHandle,
                                  uint8_t uiInputs, uint8_t uiOutputs, uint32_t numComparison)
{
  gapPasskeyNeededEvent_t *pData;

  // Allocate space for the passcode event.
  if ((pData = ICall_malloc(sizeof(gapPasskeyNeededEvent_t))))
  {
    memcpy(pData->deviceAddr, deviceAddr, B_ADDR_LEN);
    pData->connectionHandle = connHandle;
    pData->uiInputs = uiInputs;
    pData->uiOutputs = uiOutputs;
    pData->numComparison = numComparison;

    // Enqueue the event.
    multi_role_enqueueMsg(MR_PASSCODE_NEEDED_EVT, (uint8_t *) pData);
  }
}

/*********************************************************************
 * @fn      multi_role_clockHandler
 *
 * @brief   Handler function for clock timeouts.
 *
 * @param   arg - event type
 */
static void multi_role_clockHandler(UArg arg)
{
  // Wake up the application.
  Event_post(syncEvent, arg);
}


/*********************************************************************
 * @fn      multi_role_connEvtCB
 *
 * @brief   Connection event callback.
 *
 * @param pReport pointer to connection event report
 */
static void multi_role_connEvtCB(Gap_ConnEventRpt_t *pReport)
{
  // Enqueue the event for processing in the app context.
  if( multi_role_enqueueMsg(MR_CONN_EVT_END_EVT, (uint8_t *)pReport) == FALSE)
  {
    ICall_free(pReport);
  }

}

/*********************************************************************
 * @brief   Perform a periodic application task to demonstrate notification
 *          capabilities of simpleGATTProfile. This function gets called
 *          every five seconds (MR_PERIODIC_EVT_PERIOD). In this example,
 *          the value of the third characteristic in the SimpleGATTProfile
 *          service is retrieved from the profile, and then copied into the
 *          value of the the fourth characteristic.
 *
 */
static void multi_role_performPeriodicTask(void)
{
  uint8_t valueToCopy;

  // Call to retrieve the value of the third characteristic in the profile
  if (SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &valueToCopy) == SUCCESS)
  {
    // Call to set that value of the fourth characteristic in the profile.
    // Note that if notifications of the fourth characteristic have been
    // enabled by a GATT client device, then a notification will be sent
    // every time this function is called. Also note that this will
    // send a notification to each connected device.
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, sizeof(uint8_t),
                               &valueToCopy);
  }
}

static void multi_role_performPeriodicTask1(void)
{
    mr_doScan(1);
}

/*********************************************************************
* @fn      multi_role_addMappingEntry
*
* @brief   add a new connection to the index-to-connHandle map
*
* @param   connHandle - the connection handle
*
* @param   addr - pointer to device address
*
* @return  index of connection handle
*/
static uint8_t multi_role_addMappingEntry(uint16_t connHandle, uint8_t *addr)
{
  uint16_t index;
  // Loop though connections
  for (index = 0; index < maxNumBleConns; index++)
  {
    // If there is an open connection
    if (connHandleMap[index].connHandle == INVALID_CONNHANDLE)
    {
      // Store mapping
      connHandleMap[index].connHandle = connHandle;

      // Convert address to string
      uint8_t *pAddr = (uint8_t *) Util_convertBdAddr2Str(addr);

      // Copy converted string to persistent connection handle list
      memcpy(connHandleMap[index].strAddr, pAddr, B_STR_ADDR_LEN);

      return index;
    }
  }
  // No room if we get here
  return bleNoResources;
}

/*********************************************************************
 * @fn      multi_role_addDeviceInfo
 *
 * @brief   添加设备到设备发现结果列表中
 *          
 *
 * @return  none
 */
static void multi_role_addDeviceInfo(uint8 *pAddr, uint8 addrType)
{
  uint8 i;

  // If result count not at max
  if ( scanRes < DEFAULT_MAX_SCAN_RES )
  {
    // Check if device is already in scan results
    for ( i = 0; i < scanRes; i++ )
    {
      if (memcmp(pAddr, devList[i].addr, B_ADDR_LEN) == 0)
      {
        return;
      }
    }

    // Add addr to scan result list
    memcpy(devList[scanRes].addr, pAddr, B_ADDR_LEN );
    devList[scanRes].addrType = addrType;

    // Increment scan result count
    scanRes++;
  }
}


/*********************************************************************
* @fn      mr_doScan
*
* @brief   响应用户输入开始扫描
*
* @param   index - not used
*
* @return  TRUE since there is no callback to use this value
*/
bool mr_doScan(uint8_t index)
{
  (void) index;

  // If we can connect to another device
//   if (linkDB_NumActive() < maxNumBleConns)
//   {
    // If we're not already scanning
    if (!scanningStarted)
    {
      // Set scannin started flag
      scanningStarted = TRUE;

      // Start scanning
      GAPRole_StartDiscovery(DEFAULT_DISCOVERY_MODE,
                             DEFAULT_DISCOVERY_ACTIVE_SCAN,
                             DEFAULT_DISCOVERY_WHITE_LIST);
    }
    // We're already scanning...so do nothing    
 // }
  return TRUE;
}

/*********************************************************************
* @fn      mr_doGattRw
*
* @brief   响应用户输入进行GATT读或写
*
* @param   index - index as selected from the mrMenuGattRw
*
* @return  TRUE since there is no callback to use this value
*/
bool mr_doGattRw(uint8_t index)
{
  bStatus_t status = FAILURE;
  // If characteristic has been discovered
  if (discInfo[index].charHdl != 0)
  {
    // Do a read / write as long as no other read or write is in progress
    if (doWrite)
    {
      // Do a write
      attWriteReq_t req;

      // Allocate GATT write request
      req.pValue = GATT_bm_alloc(connHandleMap[index].connHandle, ATT_WRITE_REQ, 1, NULL);
      // If successfully allocated
      if (req.pValue != NULL)
      {
        // Fill up request
        req.handle = discInfo[index].charHdl;
        req.len = 1;
        req.pValue[0] = charVal;
        req.sig = 0;
        req.cmd = 0;

        // Send GATT write to controller
        status = GATT_WriteCharValue(connHandleMap[index].connHandle, &req, selfEntity);

        // If not sucessfully sent
        if ( status != SUCCESS )
        {
          // Free write request as the controller will not
          GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
        }
      }
    }
    // Do a read
    else
    {
      // Create read request...place in CSTACK
      attReadReq_t req;

      // Fill up read request
      req.handle = discInfo[index].charHdl;

      // Send read request. no need to free if unsuccessful since the request
      // is only placed in CSTACK; not allocated
      status = GATT_ReadCharValue(connHandleMap[index].connHandle, &req, selfEntity);
    }

    // If succesfully queued in controller
    if (status == SUCCESS)
    {
      // Toggle read / write
      doWrite = !doWrite;
    }
  }

  return TRUE;
}

/*********************************************************************
* @fn      mr_doConnUpdate
*
* @brief   响应用户输入进行连接更新
*
* @param   index - index as selected from the mrMenuConnUpdate
*
* @return  TRUE since there is no callback to use this value
*/
bool mr_doConnUpdate(uint8_t index)
{
  bStatus_t status = FAILURE;
  // Fill in connection handle in dummy params
  updateParams.connHandle = connHandleMap[index].connHandle;

  // Send connection parameter update
  status = gapRole_connUpdate( GAPROLE_NO_ACTION, &updateParams);

  // If successfully sent to controller
  if (status == SUCCESS)
  {
  }
  // If there is already an ongoing update
  else if (status == blePending)
  {
  }

  return TRUE;
}

/*********************************************************************
* @fn      mr_doDisconnect
*
* @brief   响应用户输入以终止连接
*
* @param   index - index as selected from the mrMenuConnUpdate
*
* @return  TRUE since there is no callback to use this value
*/
bool mr_doDisconnect(uint8_t index)
{
  // Disconnect
  GAPRole_TerminateConnection(connHandleMap[index].connHandle);

  return TRUE;
}

/* Actions for Menu: Init - Advertise */
bool mr_doAdvertise(uint8_t index)
{
  (void) index;
  uint8_t adv;
  uint8_t adv_status;

  // Get current advertising status
  GAPRole_GetParameter(GAPROLE_ADVERT_ENABLED, &adv_status, NULL);

  // If we're currently advertising
  if (adv_status)
  {
    // Turn off advertising
    adv = FALSE;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &adv, NULL);
  }
  // If we're not currently advertising
  else
  {
    // Turn on advertising
    adv = TRUE;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &adv, NULL);
  }

  return TRUE;
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_BleParameterGet
 *
 * @brief    Get Ble Parameter.
 *
 * @param   none
 *
 * @return  none
 */
static void SimpleBLEPeripheral_BleParameterGet(void)
{
    uint32_t crc;

    snvinf_t *ptr = (snvinf_t *)rxbuff; //reuse

    if( Ble_ReadNv_Inf( BLE_NVID_DEVINF_START, (uint8_t *)ptr) == 0 )
    {
        crc = crc32(0, &ptr->ibeaconinf_config.txPower, sizeof(ibeaconinf_config_t));
        if( crc == ptr->crc32)
          memcpy(&ibeaconInf_Config.txPower, &ptr->ibeaconinf_config.txPower, sizeof(ibeaconinf_config_t));
        else
          ibeaconInf_Config.initFlag = 0xFF;
    }
    else
        ibeaconInf_Config.initFlag = 0xFF;

    memset( (void *)rxbuff, 0, sizeof(rxbuff));
}


/*********************************************************************
*********************************************************************/
