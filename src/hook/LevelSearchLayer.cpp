#include <Geode/Prelude.hpp>
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>

#include <UIBuilder.hpp>
#include <cue/ListNode.hpp>
#include <delegate/LevelManagerDelegateImpl.hpp>
#include <delegate/SetIDPopupDelegateImpl.hpp>
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

std::vector<std::string> splitString(const std::string& str, const char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

enum class LoadingState {
    NotLoading,
    LoadingBefore,
    LoadingAfter
};

std::unordered_set<std::string> failedRequests;

class $modify(SPLevelSearchLayer, LevelSearchLayer) {
    struct Fields {
        Ref<cue::ListNode> searchList;
        Ref<LoadingSpinner> loadingSpinner;
        Ref<GJSearchObject> searchObject;
        Ref<CCLabelBMFont> pageText;
        Ref<CCMenuItemSpriteExtra> pageButton;
        long long debounceTime = -1LL;
        long long lastQueryTime = -1LL;

        LevelManagerDelegateImpl searchDelegate;
        SetIDPopupDelegateImpl pagePopupDelegate;

        bool hasMorePages = true;
        int startPage = 0;
        int endPage = 0;
        int activePage = 0;
        int totalPages = 0;
        int perPage = 0;
        LoadingState loadingState = LoadingState::NotLoading;

        float activeCellHeight = 0.0f;
        int debounceThreshold = 350;

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

        CCNode* searchMenu = getChildByID("search-button-menu");

        auto pageSprite = Build<CCSprite>::create("GJ_button_02.png");
        m_fields->pageText = Build<CCLabelBMFont>::create("0", "bigFont.fnt")
            .pos(pageSprite->getContentSize() / 2.0f + ccp(0.0f, 1.0f))
            .limitLabelWidth(32.0f, 0.8f, 0.0f)
            .id("page-text"_spr)
            .parent(pageSprite);

        m_fields->pageButton = pageSprite.intoMenuItem([this](auto) {
            const auto popup = SetIDPopup::create(
                m_fields->activePage + 1,
                1,
                m_fields->totalPages > 0 ? m_fields->totalPages : 1,
                "Go to page",
                "Go",
                false,
                1,
                60.0f,
                false,
                false
            );

            popup->setTag(3);
            popup->m_delegate = &m_fields->pagePopupDelegate;
            popup->show();
        })
            .anchorPoint({0.5f, 0.5f})
            .visible(false)
            .id("page-button"_spr)
            .parent(searchMenu)
            .matchPos(searchMenu->getChildByID("clear-search-button"))
            .move({55.0f, 0.0f});

        m_fields->searchDelegate.setFinishedCallback([this](CCArray* levels, const char* key) {
            log::debug("finished {}", key);
            if (!m_fields->searchObject || strcmp(m_fields->searchObject->getKey(), key) != 0) return;

            populateList(levels);
        });
        m_fields->searchDelegate.setFailedCallback([this](const char* key) {
            failedRequests.insert(key);
            m_fields->loadingSpinner->setVisible(false);
        });
        m_fields->searchDelegate.setPageInfoCallback([this](const std::string& info, const char* key) {
            if (!m_fields->searchObject || strcmp(m_fields->searchObject->getKey(), key) != 0) return;

            const std::vector<std::string> split = splitString(info, ':');
            const int total = utils::numFromString<int>(split[0]).unwrapOr(0);
            m_fields->perPage = utils::numFromString<int>(split[2]).unwrapOr(10);
            m_fields->totalPages = (total + m_fields->perPage - 1) / m_fields->perPage;
        });

        m_fields->pagePopupDelegate.setClosedCallback([this](SetIDPopup*, const int page) {
            if (!m_fields->searchObject) return;

            const auto newObject = getSearchObject(m_fields->searchObject->m_searchType, m_fields->searchObject->m_searchQuery);
            newObject->m_page = page - 1;
            updateQuery(newObject);
        });

        m_fields->debounceThreshold = Mod::get()->getSettingValue<int>("debounceTime");

        schedule(schedule_selector(SPLevelSearchLayer::customUpdate), 0.0f, kCCRepeatForever, 0.0f);

        return true;
    }

    void onExit() {
        LevelSearchLayer::onExit();
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

        if (
            m_fields->searchObject &&
            m_fields->searchObject->m_searchQuery == node->getString() &&
            (m_fields->searchObject->m_searchType == SearchType::Search || m_fields->searchObject->m_searchType == SearchType::Users)
        ) {
            return;
        }

        if (std::string(node->m_textField->getString()).empty()) {
            updateQuery(nullptr);
            return;
        }

        if (m_fields->searchObject) m_fields->searchObject->m_searchType = SearchType::Search;
        m_fields->debounceTime = m_fields->debounceThreshold;
    }

    void customUpdate(const float delta) {
        if (m_fields->loadingState == LoadingState::NotLoading ) {
            CCContentLayer* cl = m_fields->searchList->getScrollLayer()->m_contentLayer;
            const float position = cl->getScaledContentHeight() + cl->getPositionY() - m_fields->searchList->getScrollLayer()->getContentSize().height;
            const float pageSize = m_fields->perPage * m_fields->activeCellHeight;
            const float offset = m_fields->startPage * pageSize;

            if (pageSize > 0.0f) {
                m_fields->activePage = (position + offset) / pageSize;
                m_fields->pageText->setString(std::to_string(m_fields->activePage + 1).c_str());
                m_fields->pageText->limitLabelWidth(32.0f, 0.8f, 0.0f);
            }
        }

        if (!m_fields->touchPrioSet) {
            const auto searchMenu = getChildByID("search-button-menu");
            searchMenu->setZOrder(12);
            forceSetTouchPrio(searchMenu, -256);
            m_fields->touchPrioSet = true;
        }

        if (m_fields->searchObject && m_fields->debounceTime < 0LL) {
            const auto currentQuery = getSearchObject(m_fields->searchObject->m_searchType, m_fields->searchObject->m_searchQuery);
            currentQuery->m_page = m_fields->searchObject->m_page;
            if (strcmp(currentQuery->getKey(), m_fields->searchObject->getKey()) != 0) {
                m_fields->debounceTime = m_fields->debounceThreshold;
            }
        }

        if (m_fields->lastQueryTime >= 0LL) m_fields->lastQueryTime -= delta * 1000.0f;

        if (m_fields->lastQueryTime < 0LL) {
            const auto firstCell = m_fields->searchList->getCell(0);
            if (firstCell && firstCell->isVisible()) {
                onScrollToTop();
            }

            const auto lastCell = m_fields->searchList->getCell(m_fields->searchList->size() - 1);
            if (lastCell && lastCell->isVisible()) {
                onScrollToBottom();
            }
        }

        if (m_fields->debounceTime < 0LL) return;
        m_fields->debounceTime -= delta * 1000.0f;

        if (m_fields->debounceTime < 0LL) {
            updateQuery(getSearchObject(
                m_fields->searchObject ? m_fields->searchObject->m_searchType : SearchType::Search,
                m_searchInput->m_textField->getString()
            ));
        }
    }

    void updateQuery(GJSearchObject* newObject) {
        log::debug("new query with page {}", newObject ? newObject->m_page : -1);
        if (newObject && newObject->m_searchQuery.empty() && newObject->m_searchType == SearchType::Search) {
            newObject = nullptr;
        }

        if (newObject == m_fields->searchObject) return;

        const char* oldQuery = m_fields->searchObject ? m_fields->searchObject->getKey() : "not a real key";
        m_fields->searchObject = newObject;

        if (!newObject || strcmp(oldQuery, newObject->getKey()) != 0) {
            m_fields->loadingState = LoadingState::NotLoading;
            m_fields->hasMorePages = true;
            if (newObject) {
                m_fields->startPage = newObject->m_page;
                m_fields->endPage = newObject->m_page;
            }
            showQuery();
        }
    }

    void showQuery() {
        m_fields->searchList->setVisible(m_fields->searchObject);
        m_fields->pageButton->setVisible(m_fields->searchObject);
        m_fields->pageText->setString(m_fields->searchObject ? std::to_string(m_fields->searchObject->m_page + 1).c_str() : "0");
        m_fields->pageText->limitLabelWidth(32.0f, 0.8f, 0.0f);
        getChildByID("quick-search-menu")->setVisible(!m_fields->searchObject);
        getChildByID("difficulty-filter-menu")->setVisible(!m_fields->searchObject);
        getChildByID("length-filter-menu")->setVisible(!m_fields->searchObject);

        m_fields->searchList->clear();
        if (!m_fields->searchObject) return;

        m_fields->loadingSpinner->setVisible(true);

        GameLevelManager* glm = GameLevelManager::get();
        glm->m_levelManagerDelegate = nullptr;
        m_fields->loadingState = LoadingState::LoadingAfter;
        const auto key = m_fields->searchObject->getKey();

        if (failedRequests.contains(key)) {
            m_fields->loadingSpinner->setVisible(false);
            return;
        }

        if (const auto cached = glm->getStoredOnlineLevels(key)) {
            populateList(cached);
            return;
        }

        glm->m_levelManagerDelegate = &m_fields->searchDelegate;
        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            glm->getUsers(m_fields->searchObject);
        } else if (m_fields->searchObject->m_searchMode == 1) {
            glm->getLevelLists(m_fields->searchObject);
        } else {
            glm->getOnlineLevels(m_fields->searchObject);
        }
    }

    void onScrollToTop() {
        if (!m_fields->searchObject || m_fields->loadingState != LoadingState::NotLoading || m_fields->startPage <= 0) {
            return;
        }

        m_fields->loadingState = LoadingState::LoadingBefore;
        m_fields->loadingSpinner->setVisible(true);

        m_fields->startPage--;
        m_fields->searchObject->m_page = m_fields->startPage;

        GameLevelManager* glm = GameLevelManager::get();
        glm->m_levelManagerDelegate = &m_fields->searchDelegate;

        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            glm->getUsers(m_fields->searchObject);
        } else if (m_fields->searchObject->m_searchMode == 1) {
            glm->getLevelLists(m_fields->searchObject);
        } else {
            glm->getOnlineLevels(m_fields->searchObject);
        }
        m_fields->lastQueryTime = 1000;
    }

    void onScrollToBottom() {
        if (!m_fields->searchObject || m_fields->loadingState != LoadingState::NotLoading || !m_fields->hasMorePages) {
            return;
        }

        m_fields->loadingState = LoadingState::LoadingAfter;
        m_fields->loadingSpinner->setVisible(true);

        m_fields->endPage++;
        m_fields->searchObject->m_page = m_fields->endPage;

        GameLevelManager* glm = GameLevelManager::get();
        glm->m_levelManagerDelegate = &m_fields->searchDelegate;

        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            glm->getUsers(m_fields->searchObject);
        } else if (m_fields->searchObject->m_searchMode == 1) {
            glm->getLevelLists(m_fields->searchObject);
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
            m_fields->loadingState = LoadingState::NotLoading;
            return;
        }

        CCContentLayer* cl = m_fields->searchList->getScrollLayer()->m_contentLayer;
        float newScrollPos = cl->getPositionY();
        if (cl->getChildrenCount() == 0) {
            newScrollPos = cl->getScaledContentHeight();
        }

        int total = 0;
        if (m_fields->searchObject->m_searchType == SearchType::Users) {
            m_fields->activeCellHeight = 60.0f;
            for (GJUserScore* score : CCArrayExt<GJUserScore*>(levels)) {
                if (!score) continue;

                const auto cell = new GJScoreCell("", 356.0f, 60.0f);
                cell->autorelease();
                cell->loadFromScore(score);
                cell->setContentSize({356.0f, 60.0f});

                switch (m_fields->loadingState) {
                    case LoadingState::LoadingBefore:
                        m_fields->searchList->insertCell(cell, total);
                        break;
                    case LoadingState::LoadingAfter:
                        m_fields->searchList->addCell(cell);
                        newScrollPos -= cell->getContentHeight();
                        break;
                    default:
                        break;
                }
                total++;
            }
        } else if (m_fields->searchObject->m_searchMode == 1) {
            m_fields->activeCellHeight = 90.0f;
            for (GJLevelList* list : CCArrayExt<GJLevelList*>(levels)) {
                if (!list) continue;

                const auto cell = new LevelListCell("", 356.0f, 90.0f);
                cell->autorelease();
                cell->loadFromList(list);
                cell->setContentSize({356.0f, 90.0f});

                switch (m_fields->loadingState) {
                    case LoadingState::LoadingBefore:
                        m_fields->searchList->insertCell(cell, total);
                        break;
                    case LoadingState::LoadingAfter:
                        m_fields->searchList->addCell(cell);
                        newScrollPos -= cell->getContentHeight();
                        break;
                    default:
                        break;
                }
                total++;
            }
        } else {
            const bool compact = Loader::get()->isModLoaded("cvolton.compact_lists") && Loader::get()->getLoadedMod("cvolton.compact_lists")->getSettingValue<bool>("enable-compact-lists");
            m_fields->activeCellHeight = compact ? 50.0f : 90.0f;
            for (GJGameLevel* level : CCArrayExt<GJGameLevel*>(levels)) {
                if (!level) continue;

                const auto cell = new LevelCell("", 356.0f, compact ? 50.0f : 90.0f);
                cell->m_compactView = compact;
                cell->autorelease();
                cell->loadFromLevel(level);
                cell->setContentSize({356.0f, compact ? 50.0f : 90.0f});

                switch (m_fields->loadingState) {
                    case LoadingState::LoadingBefore:
                        m_fields->searchList->insertCell(cell, total);
                        break;
                    case LoadingState::LoadingAfter:
                        m_fields->searchList->addCell(cell);
                        newScrollPos -= cell->getContentHeight();
                        break;
                    default:
                        break;
                }
                total++;
            }
        }

        cl->setPositionY(std::min(newScrollPos, 0.0f));

        m_fields->loadingState = LoadingState::NotLoading;
    }
};