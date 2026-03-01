#pragma once

#include <Geode/Prelude.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>

using namespace geode::prelude;

class LevelManagerDelegateImpl : public LevelManagerDelegate {
    std::function<void(CCArray*, const char*)> finishedCallback;
    std::function<void(const char*)> failedCallback;
    std::function<void(std::string, const char*)> pageInfoCallback;

    void loadLevelsFinished(CCArray* levels, const char* key) override {
        if (finishedCallback) finishedCallback(levels, key);
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int) override {
        loadLevelsFinished(levels, key);
    }

    void loadLevelsFailed(const char* key) override {
        if (failedCallback) failedCallback(key);
    }

    void loadLevelsFailed(const char* key, int) override {
        loadLevelsFailed(key);
    }

    void setupPageInfo(const gd::string info, const char* key) override {
        if (pageInfoCallback) pageInfoCallback(info, key);
    }

public:
    virtual ~LevelManagerDelegateImpl() = default;

    void setFinishedCallback(const std::function<void(CCArray*, const char*)>& callback) {
        finishedCallback = callback;
    }

    void setFailedCallback(const std::function<void(const char*)>& callback) {
        failedCallback = callback;
    }

    void setPageInfoCallback(const std::function<void(std::string, const char*)>& callback) {
        pageInfoCallback = callback;
    }
};