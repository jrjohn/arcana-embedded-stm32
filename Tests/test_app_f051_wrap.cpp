/**
 * @file test_app_f051_wrap.cpp
 * @brief Renames F051 App.cpp's extern "C" entry points to F051_* via macro
 *        rewrite so test_app_entry.cpp can call them without colliding with
 *        the F103 App.cpp version linked into the same target.
 */
#define App_Init                       F051_App_Init
#define App_Run                        F051_App_Run
#define App_GetOverflowCount           F051_App_GetOverflowCount
#define App_GetQueueSpaceAvailable     F051_App_GetQueueSpaceAvailable
#define App_GetPublishCount            F051_App_GetPublishCount
#define App_GetDispatchCount           F051_App_GetDispatchCount
#define App_GetQueueHighWaterMark      F051_App_GetQueueHighWaterMark
#define App_GetHighQueueSpaceAvailable F051_App_GetHighQueueSpaceAvailable
#define App_GetHighPublishCount        F051_App_GetHighPublishCount
#define App_GetHighDispatchCount       F051_App_GetHighDispatchCount

#include "../Targets/stm32f051c8/Services/App.cpp"
