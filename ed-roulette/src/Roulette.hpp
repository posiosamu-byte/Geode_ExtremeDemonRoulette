#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <set>
#include <string>
#include <vector>

using namespace geode::prelude;

enum class ListSource { AREDL, RatedPlus };

struct DemonEntry {
    int levelID = 0;
    int position = 0;
    std::string name;
};

class RouletteManager : public LevelDownloadDelegate {
public:
    static RouletteManager* get();

    // Entry point from the main menu button.
    void openMenu();

    // PlayLayer hooks.
    bool isCurrentLevel(int levelID) const;
    void onProgress(float percent);
    void onQuit();

    // Called every frame by the watcher once the player is out of the level.
    void showPendingResultIfReady();

private:
    RouletteManager() = default;

    // Run control.
    void startRun(ListSource source);
    void endRun(bool showSummary);
    void nextLevel();
    void openCurrentLevel();

    // Networking.
    void fetchAredl();
    void fetchPointercrate(std::string baseUrl, int after, int page);
    void onListLoaded();
    void onListFailed(std::string const& reason);

    // LevelDownloadDelegate.
    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int id) override;

    void showLevel(GJGameLevel* level);
    std::string summary() const;

    EventListener<web::WebTask> m_listener;
    Ref<Notification> m_loading;

    ListSource m_source = ListSource::AREDL;
    std::vector<DemonEntry> m_pool;
    std::set<int> m_used;
    std::vector<DemonEntry> m_cleared;

    bool m_active = false;
    DemonEntry m_current;
    int m_required = 1;
    float m_best = 0.f;
    bool m_goalNotified = false;

    bool m_pendingResult = false;
    bool m_pendingSuccess = false;
};

// Persistent node that waits until the player is back on a normal screen
// before showing the result popup.
class RouletteWatcher : public CCNode {
public:
    static RouletteWatcher* create();
    void update(float dt) override;
};
