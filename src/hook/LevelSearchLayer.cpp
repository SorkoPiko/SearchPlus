#include <Geode/Prelude.hpp>
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>

#include <UIBuilder.hpp>
#include <cue/ListNode.hpp>
#include <delegate/LevelManagerDelegateImpl.hpp>
#include <Geode/utils/VMTHookManager.hpp>

using namespace geode::prelude;

void forceSetTouchPrio(CCNode* node, const int prio) {
    if (const auto delegate = typeinfo_cast<CCTouchDelegate*>(node)) {
        if (const auto handler = CCTouchDispatcher::get()->findHandler(delegate)) {
            CCTouchDispatcher::get()->setPriority(prio, handler->getDelegate());
        } else {
            log::warn("Failed to set touch priority for node {}: no handler found", node->getID().c_str());
        }
    } else {
        log::warn("Failed to set touch priority for node {}: not a touch delegate", node->getID().c_str());
    }
}

class $modify(SPLevelSearchLayer, LevelSearchLayer) {
    struct Fields {
        Ref<cue::ListNode> searchList;
        Ref<LoadingSpinner> loadingSpinner;
        Ref<GJSearchObject> searchObject;
        LevelManagerDelegateImpl searchDelegate;
        long long debounceTime = -1LL;
        long long lastQueryTime = -1LL;

        bool hasMorePages = true;
        bool isLoadingMore = false;

        bool touchPrioSet = false;
    };

    bool init(const int type) {
        if (!LevelSearchLayer::init(type)) return false;

        (void)VMTHookManager::get().addHook<ResolveC<SPLevelSearchLayer>::func(&SPLevelSearchLayer::onExit)>(this, "LevelSearchLayer::onExit");

        const auto winSize = CCDirector::sharedDirector()->getWinSize();

        m_fields->searchList = Build(cue::ListNode::create(
            {358.0f, 220.0f},
            cue::Brown,
            cue::ListBorderStyle::SlimLevels
        ))
            .anchorPoint({0.5f, 0.5f})
            .pos(winSize / 2.0f - CCPoint{0.0f, 20.0f})
            .visible(false)
            .id("search-list"_spr)
            .zOrder(10)
            .parent(this);

        m_fields->loadingSpinner = Build(LoadingSpinner::create(50.0f))
            .parent(this)
            .matchPos(m_fields->searchList)
            .visible(false)
            .id("search-loading-spinner"_spr)
            .zOrder(11);

        m_fields->searchDelegate.setFinishedCallback([this](CCArray* levels, const char* key) {
            loadLevelsFinished(levels, key);
        });
        m_fields->searchDelegate.setFailedCallback([this](const char* key) {
            m_fields->loadingSpinner->setVisible(false);
        });

        schedule(schedule_selector(SPLevelSearchLayer::customUpdate), 0.0f, kCCRepeatForever, 0.0f);

        return true;
    }

    void onExit() {
        LevelSearchLayer::onExit();
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
        m_searchInput->onClickTrackNode(false);
    }

    void keyBackClicked() {
        if (m_fields->searchObject) {
            updateQuery(nullptr);
            return;
        }

        LevelSearchLayer::keyBackClicked();
    }

    void onSearch(CCObject*) {
        updateQuery(getSearchObject(SearchType::Search, m_searchInput->m_textField->getString()));
    }

    void onSearchUser(CCObject*) {
        updateQuery(getSearchObject(SearchType::Users, m_searchInput->m_textField->getString()));
    }

    void onMostDownloaded(CCObject*) {
        updateQuery(getSearchObject(SearchType::Downloaded, ""));
    }

    void onMostLikes(CCObject*) {
        updateQuery(getSearchObject(SearchType::MostLiked, ""));
    }

    void onSuggested(CCObject*) {
        updateQuery(getSearchObject(SearchType::Sent, ""));
    }

    void onTrending(CCObject*) {
        updateQuery(getSearchObject(SearchType::Trending, ""));
    }

    void onMagic(CCObject*) {
        updateQuery(getSearchObject(SearchType::Magic, ""));
    }

    void onMostRecent(CCObject*) {
        updateQuery(getSearchObject(SearchType::Recent, ""));
    }

    void onLatestStars(CCObject*) {
        updateQuery(getSearchObject(SearchType::Awarded, ""));
    }

    void onFriends(CCObject*) {
        updateQuery(getSearchObject(SearchType::Friends, ""));
    }

    void onFollowed(CCObject*) {
        updateQuery(getSearchObject(SearchType::Followed, ""));
    }

    void onStarAward(CCObject*) {
        updateQuery(getSearchObject(SearchType::StarAward, ""));
    }

    void textChanged(CCTextInputNode* node) {
        LevelSearchLayer::textChanged(node);

        if (m_fields->searchObject != nullptr && m_fields->searchObject->m_searchQuery == node->getString() && m_fields->searchObject->m_searchType == SearchType::Users) return;

        if (std::string(node->m_textField->getString()).empty()) {
            updateQuery(nullptr);
            return;
        }

        m_fields->debounceTime = 350;
    }

    void customUpdate(const float delta) {
        if (!m_fields->touchPrioSet) {
            const auto searchMenu = getChildByID("search-button-menu");
            searchMenu->setZOrder(12);
            forceSetTouchPrio(searchMenu, -256);
            m_fields->touchPrioSet = true;
        }

        if (m_fields->searchObject) {
            const auto currentQuery = getSearchObject(m_fields->searchObject->m_searchType, m_fields->searchObject->m_searchQuery);
            currentQuery->m_page = m_fields->searchObject->m_page;
            if (strcmp(currentQuery->getKey(), m_fields->searchObject->getKey()) != 0) {
                updateQuery(getSearchObject(m_fields->searchObject->m_searchType, m_searchInput->m_textField->getString()));
            }
        }

        if (m_fields->lastQueryTime >= 0LL) m_fields->lastQueryTime -= delta * 1000.0f;

        if (m_fields->lastQueryTime < 0LL) {
            const auto lastCell = m_fields->searchList->getCell(m_fields->searchList->size() - 1);
            if (lastCell && lastCell->isVisible()) {
                onScrollToBottom();
            }
        }

        if (m_fields->debounceTime < 0LL) return;
        m_fields->debounceTime -= delta * 1000.0f;

        if (m_fields->debounceTime < 0LL) {
            updateQuery(getSearchObject(SearchType::Search, m_searchInput->m_textField->getString()));
        }
    }

    void updateQuery(GJSearchObject* newObject) {
        if (newObject && newObject->m_searchQuery.empty() && (newObject->m_searchType == SearchType::Search || newObject->m_searchType == SearchType::Users)) {
            newObject = nullptr;
        }

        if (newObject == m_fields->searchObject) return;

        const char* oldQuery = m_fields->searchObject ? m_fields->searchObject->getKey() : "not a real key";
        m_fields->searchObject = newObject;

        if (!newObject || strcmp(oldQuery, newObject->getKey()) != 0) {
            m_fields->isLoadingMore = false;
            m_fields->hasMorePages = true;
            showQuery();
        }
    }

    void showQuery() {
        m_fields->searchList->setVisible(m_fields->searchObject);
        getChildByID("quick-search-menu")->setVisible(!m_fields->searchObject);
        getChildByID("difficulty-filter-menu")->setVisible(!m_fields->searchObject);
        getChildByID("length-filter-menu")->setVisible(!m_fields->searchObject);

        m_fields->searchList->clear();
        if (!m_fields->searchObject) return;

        m_fields->loadingSpinner->setVisible(true);

        GameLevelManager* glm = GameLevelManager::get();
        glm->m_levelManagerDelegate = nullptr;
        const auto key = m_fields->searchObject->getKey();
        if (const auto cached = glm->getStoredOnlineLevels(key)) {
            populateList(cached);
            return;
        }

        glm->m_levelManagerDelegate = &m_fields->searchDelegate;
        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            glm->getUsers(m_fields->searchObject);
        } else {
            glm->getOnlineLevels(m_fields->searchObject);
        }
    }

    void onScrollToBottom() {
        if (!m_fields->searchObject || m_fields->isLoadingMore || !m_fields->hasMorePages) {
            return;
        }

        m_fields->isLoadingMore = true;
        m_fields->loadingSpinner->setVisible(true);

        m_fields->searchObject->m_page++;

        GameLevelManager* glm = GameLevelManager::get();
        glm->m_levelManagerDelegate = &m_fields->searchDelegate;

        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            glm->getUsers(m_fields->searchObject);
        } else {
            glm->getOnlineLevels(m_fields->searchObject);
        }
        m_fields->lastQueryTime = 1000;
    }

    void populateList(CCArray* levels) {
        m_fields->loadingSpinner->setVisible(false);
        if (m_fields->searchObject == nullptr) return;

        if (levels->count() == 0) {
            m_fields->hasMorePages = false;
            m_fields->isLoadingMore = false;
            return;
        }

        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            for (GJUserScore* score : CCArrayExt<GJUserScore*>(levels)) {
                if (!score) continue;

                const auto cell = new GJScoreCell("", 356.0f, 60.0f);
                cell->autorelease();
                cell->loadFromScore(score);
                cell->setContentSize({356.0f, 60.0f});

                m_fields->searchList->addCell(cell);
            }
        } else {
            for (GJGameLevel* level : CCArrayExt<GJGameLevel*>(levels)) {
                if (!level) continue;

                bool compact = Loader::get()->isModLoaded("cvolton.compact_lists") && Loader::get()->getLoadedMod("cvolton.compact_lists")->getSettingValue<bool>("enable-compact-lists");
                const auto cell = new LevelCell("", 356.0f, compact ? 50.0f : 90.0f);
                cell->m_compactView = compact;
                cell->autorelease();
                cell->loadFromLevel(level);
                cell->setContentSize({356.0f, compact ? 50.0f : 90.0f});

                m_fields->searchList->addCell(cell);
            }
        }

        m_fields->isLoadingMore = false;
    }

    void loadLevelsFinished(CCArray* levels, const char* key) {
        if (!m_fields->searchObject || strcmp(m_fields->searchObject->getKey(), key) != 0) return;

        populateList(levels);
    }
};