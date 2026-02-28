#pragma once

#include <Geode/Prelude.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>

using namespace geode::prelude;

class LevelManagerDelegateImpl : public LevelManagerDelegate {
    std::function<void(CCArray*, const char*)> loadLevelsFinishedCallback;
    std::function<void(const char*)> loadLevelsFailedCallback;

    void loadLevelsFinished(CCArray* levels, const char* key) override {
        if (loadLevelsFinishedCallback) loadLevelsFinishedCallback(levels, key);
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int) override {
        loadLevelsFinished(levels, key);
    }

    void loadLevelsFailed(const char* key) override {
        if (loadLevelsFailedCallback) loadLevelsFailedCallback(key);
    }

    void loadLevelsFailed(const char* key, int) override {
        loadLevelsFailed(key);
    }

public:
    virtual ~LevelManagerDelegateImpl() = default;

    void setFinishedCallback(const std::function<void(CCArray*, const char*)>& callback) {
        loadLevelsFinishedCallback = callback;
    }

    void setFailedCallback(const std::function<void(const char*)>& callback) {
        loadLevelsFailedCallback = callback;
    }
};