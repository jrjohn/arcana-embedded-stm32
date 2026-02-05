/**
 * @file App.hpp
 * @brief Application entry point (C++ interface)
 */

#ifndef ARCANA_APP_HPP
#define ARCANA_APP_HPP

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

#ifdef __cplusplus
}
#endif

#endif /* ARCANA_APP_HPP */
