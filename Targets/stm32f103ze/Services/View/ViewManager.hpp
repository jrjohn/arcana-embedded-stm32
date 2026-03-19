#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_NAV_STACK

#include "IDisplay.hpp"
#include "TouchTypes.hpp"
#include "LcdView.hpp"
#include "LcdViewModel.hpp"

namespace arcana {
namespace lcd {

class ViewManager {
public:
    static const uint8_t MAX_ROOT_VIEWS = 4;
    static const uint8_t MAX_STACK_DEPTH = 4;

    ViewManager()
        : mDisplay(0), mRootCount(0), mRootIndex(0), mStackDepth(0) {}

    void init(display::IDisplay* display) { mDisplay = display; }

    /** Add a root-level view (MainView, EcgView, SettingsView) */
    void addRootView(LcdView* view) {
        if (mRootCount < MAX_ROOT_VIEWS) mRootViews[mRootCount++] = view;
    }

    // ── Root-level switching (swipe left/right) ──

    void switchRoot(uint8_t index) {
        if (index >= mRootCount || !mDisplay) return;
        if (mStackDepth > 0) currentView()->onExit(*mDisplay);
        mRootIndex = index;
        mStack[0] = mRootViews[index];
        mStackDepth = 1;
        mStack[0]->onEnter(*mDisplay);
    }

    void nextRoot() {
        if (mRootIndex + 1 < mRootCount) switchRoot(mRootIndex + 1);
    }
    void prevRoot() {
        if (mRootIndex > 0) switchRoot(mRootIndex - 1);
    }

    // ── Push/Pop navigation (sub-views) ──

    void push(LcdView* view) {
        if (!mDisplay || mStackDepth >= MAX_STACK_DEPTH) return;
        currentView()->onExit(*mDisplay);
        mStack[mStackDepth++] = view;
        view->onEnter(*mDisplay);
    }

    bool pop() {
        if (!mDisplay || mStackDepth <= 1) return false;
        currentView()->onExit(*mDisplay);
        mStackDepth--;
        currentView()->onEnter(*mDisplay);
        return true;
    }

    void popToRoot() {
        if (!mDisplay || mStackDepth <= 1) return;
        currentView()->onExit(*mDisplay);
        mStackDepth = 1;
        currentView()->onEnter(*mDisplay);
    }

    // ── Input routing ──

    bool dispatchTouch(const display::TouchEvent& event) {
        return currentView() ? currentView()->onTouch(event) : false;
    }

    bool dispatchGesture(display::Gesture gesture) {
        using G = display::Gesture;

        if (gesture == G::SwipeRight) {
            if (mStackDepth > 1) { pop(); return true; }
            if (mRootIndex > 0) { prevRoot(); return true; }
        }
        if (gesture == G::SwipeLeft) {
            if (mStackDepth == 1 && mRootIndex + 1 < mRootCount) {
                nextRoot(); return true;
            }
        }
        return currentView() ? currentView()->onGesture(gesture) : false;
    }

    bool dispatchKey(const display::KeyEvent& event) {
        using K = display::KeyEvent;
        if (event.type == K::LongPress && event.key == K::Key2 && mStackDepth > 1) {
            pop();
            return true;
        }
        return currentView() ? currentView()->onKey(event) : false;
    }

    // ── Accessors ──

    LcdView* currentView() const {
        return (mStackDepth > 0) ? mStack[mStackDepth - 1] : nullptr;
    }
    uint8_t depth() const { return mStackDepth; }
    uint8_t rootIndex() const { return mRootIndex; }
    bool isAtRoot() const { return mStackDepth <= 1; }

private:
    display::IDisplay* mDisplay;

    LcdView* mRootViews[MAX_ROOT_VIEWS];
    uint8_t mRootCount;
    uint8_t mRootIndex;

    LcdView* mStack[MAX_STACK_DEPTH];
    uint8_t mStackDepth;
};

} // namespace lcd
} // namespace arcana

#endif // DISPLAY_FEATURE_NAV_STACK
