/**
 * @file App.hpp
 * @brief Application entry point (C++ interface)
 */

#ifndef ARCANA_APP_HPP
#define ARCANA_APP_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize application (called from StartDefaultTask)
 */
void App_Init(void);

/**
 * @brief Main application loop iteration
 */
void App_Run(void);

/**
 * @brief Get queue overflow count from error callback
 * @return Number of overflow events
 */
uint32_t App_GetOverflowCount(void);

/**
 * @brief Get available queue space
 * @return Number of free slots (0-8)
 */
uint8_t App_GetQueueSpaceAvailable(void);

/**
 * @brief Get dispatcher publish count
 * @return Total publish attempts
 */
uint32_t App_GetPublishCount(void);

/**
 * @brief Get dispatcher dispatch count
 * @return Successfully dispatched events
 */
uint32_t App_GetDispatchCount(void);

/**
 * @brief Get queue high water mark (normal priority)
 * @return Peak queue usage
 */
uint8_t App_GetQueueHighWaterMark(void);

/**
 * @brief Get high priority queue space available
 * @return Number of free slots (0-4)
 */
uint8_t App_GetHighQueueSpaceAvailable(void);

/**
 * @brief Get high priority publish count
 * @return Total high priority publish attempts
 */
uint32_t App_GetHighPublishCount(void);

/**
 * @brief Get high priority dispatch count
 * @return Successfully dispatched high priority events
 */
uint32_t App_GetHighDispatchCount(void);

#ifdef __cplusplus
}
#endif

#endif /* ARCANA_APP_HPP */
