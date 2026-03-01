#pragma once

#include <Geode/Prelude.hpp>
#include <Geode/binding/SetIDPopupDelegate.hpp>

using namespace geode::prelude;

class SetIDPopupDelegateImpl : public SetIDPopupDelegate {
    std::function<void(SetIDPopup*, int)> closedCallback;

    void setIDPopupClosed(SetIDPopup* popup, const int value) override {
        if (closedCallback) closedCallback(popup, value);
    }

public:
    virtual ~SetIDPopupDelegateImpl() = default;

    void setClosedCallback(const std::function<void(SetIDPopup*, int)>& callback) {
        closedCallback = callback;
    }
};