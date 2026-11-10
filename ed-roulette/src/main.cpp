#include "Roulette.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

class $modify(EDMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        // One persistent node that shows the result popup once the player
        // is back out of the level.
        static bool watcherAdded = false;
        if (!watcherAdded) {
            watcherAdded = true;
            if (auto* watcher = RouletteWatcher::create()) {
                SceneManager::get()->keepAcrossScenes(watcher);
            }
        }

        auto* spr = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
        spr->setScale(0.45f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(EDMenuLayer::onRoulette));
        btn->setID("roulette-button"_spr);

        if (auto* menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }

    void onRoulette(CCObject*) {
        RouletteManager::get()->openMenu();
    }
};

class $modify(EDPlayLayer, PlayLayer) {
    bool isRouletteLevel() {
        return m_level && RouletteManager::get()->isCurrentLevel(m_level->m_levelID.value());
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        // Practice and test runs don't count.
        if (m_isPracticeMode || m_isTestMode) return;
        if (!this->isRouletteLevel()) return;
        RouletteManager::get()->onProgress(this->getCurrentPercent());
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (m_isPracticeMode || m_isTestMode) return;
        if (!this->isRouletteLevel()) return;
        RouletteManager::get()->onProgress(100.f);
    }

    void onQuit() {
        if (this->isRouletteLevel()) {
            RouletteManager::get()->onQuit();
        }
        PlayLayer::onQuit();
    }
};
