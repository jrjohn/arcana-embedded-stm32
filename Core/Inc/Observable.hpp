/**
 * @file Observable.hpp
 * @brief Lightweight Observable Pattern for STM32 with FreeRTOS (C++ version)
 *
 * Memory-efficient implementation designed for 8KB RAM MCUs.
 * Features:
 * - Static allocation only (no new/delete)
 * - Template-based type safety
 * - Fixed observer capacity
 * - Single shared dispatcher task
 */

#ifndef ARCANA_OBSERVABLE_HPP
#define ARCANA_OBSERVABLE_HPP

#include <cstdint>
#include <type_traits>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

namespace arcana {

/* Configuration */
constexpr uint8_t MAX_OBSERVERS = 4;
constexpr uint8_t DISPATCHER_QUEUE_SIZE = 8;
constexpr uint16_t DISPATCHER_STACK_SIZE = 128;  /* Reduced from 256 */

/**
 * @brief Base Model class - all models should inherit from this
 */
class Model {
public:
    uint32_t timestamp = 0;
    uint8_t type = 0;

    Model() : timestamp(xTaskGetTickCount()), type(0) {}
    explicit Model(uint8_t modelType) : timestamp(xTaskGetTickCount()), type(modelType) {}
    virtual ~Model() = default;

    void updateTimestamp() { timestamp = xTaskGetTickCount(); }
};

/**
 * @brief Observer callback type using function pointer (lightweight)
 */
template<typename T>
using ObserverCallback = void (*)(T* model, void* context);

/**
 * @brief Observable class template
 * @tparam T Model type (must inherit from Model)
 */
template<typename T>
class Observable {
    static_assert(std::is_base_of<Model, T>::value, "T must inherit from Model");

public:
    struct Observer {
        ObserverCallback<T> callback = nullptr;
        void* context = nullptr;
    };

private:
    Observer observers_[MAX_OBSERVERS];
    uint8_t count_ = 0;
    const char* name_ = nullptr;

public:
    explicit Observable(const char* name = nullptr) : observers_{}, name_(name) {
    }

    /**
     * @brief Subscribe to this observable
     * @param callback Function to call on publish
     * @param context User context (optional)
     * @return true if subscribed successfully
     */
    bool subscribe(ObserverCallback<T> callback, void* context = nullptr) {
        if (callback == nullptr) return false;

        // Check for duplicate
        for (uint8_t i = 0; i < count_; i++) {
            if (observers_[i].callback == callback) {
                observers_[i].context = context;
                return true;
            }
        }

        // Check capacity
        if (count_ >= MAX_OBSERVERS) return false;

        // Add observer
        observers_[count_].callback = callback;
        observers_[count_].context = context;
        count_++;
        return true;
    }

    /**
     * @brief Unsubscribe from this observable
     * @param callback The callback to remove
     * @return true if removed successfully
     */
    bool unsubscribe(ObserverCallback<T> callback) {
        for (uint8_t i = 0; i < count_; i++) {
            if (observers_[i].callback == callback) {
                // Shift remaining
                for (uint8_t j = i; j < count_ - 1; j++) {
                    observers_[j] = observers_[j + 1];
                }
                count_--;
                observers_[count_] = {nullptr, nullptr};
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Notify all observers directly (synchronous)
     * @param model Pointer to model
     */
    void notify(T* model) {
        for (uint8_t i = 0; i < count_; i++) {
            if (observers_[i].callback != nullptr) {
                observers_[i].callback(model, observers_[i].context);
            }
        }
    }

    /**
     * @brief Publish to dispatcher queue (asynchronous)
     * @param model Pointer to model
     * @return true if queued successfully
     */
    bool publish(T* model);

    uint8_t getObserverCount() const { return count_; }
    const char* getName() const { return name_; }
};

/**
 * @brief Dispatcher for async observable processing
 */
class ObservableDispatcher {
public:
    struct DispatchItem {
        void (*notifyFunc)(void* observable, Model* model);
        void* observable;
        Model* model;
    };

private:
    static StaticQueue_t queueBuffer_;
    static uint8_t queueStorage_[DISPATCHER_QUEUE_SIZE * sizeof(DispatchItem)];
    static QueueHandle_t queue_;

    static StaticTask_t taskBuffer_;
    static StackType_t taskStack_[DISPATCHER_STACK_SIZE];
    static TaskHandle_t taskHandle_;

    static void dispatcherTask(void* pvParameters);

public:
    static void start();
    static bool enqueue(const DispatchItem& item);
    static QueueHandle_t getQueue() { return queue_; }
};

/* Template implementation for publish */
template<typename T>
bool Observable<T>::publish(T* model) {
    if (model == nullptr || count_ == 0) return true;
    if (ObservableDispatcher::getQueue() == nullptr) return false;

    ObservableDispatcher::DispatchItem item;
    item.notifyFunc = [](void* obs, Model* m) {
        static_cast<Observable<T>*>(obs)->notify(static_cast<T*>(m));
    };
    item.observable = this;
    item.model = model;

    return ObservableDispatcher::enqueue(item);
}

} // namespace arcana

#endif /* ARCANA_OBSERVABLE_HPP */
