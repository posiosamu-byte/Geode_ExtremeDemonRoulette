#include "Roulette.hpp"

#include <algorithm>
#include <random>

static std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// Parses a JSON array of level objects. Works for both the AREDL v2 API and
// the Pointercrate API, which both expose "level_id", "name" and "position".
static std::vector<DemonEntry> parseList(matjson::Value const& json, bool skipLegacy, int positionOffset) {
    std::vector<DemonEntry> out;
    auto arr = json.asArray();
    if (!arr) return out;

    int idx = 0;
    for (auto v : arr.unwrap()) {
        ++idx;
        int id = static_cast<int>(v["level_id"].asInt().unwrapOr(0));
        if (id <= 0) continue; // levels without a GD id can't be played
        if (skipLegacy && v["legacy"].asBool().unwrapOr(false)) continue;

        DemonEntry e;
        e.levelID = id;
        e.name = v["name"].asString().unwrapOr("Unknown");
        e.position = static_cast<int>(v["position"].asInt().unwrapOr(positionOffset + idx));
        out.push_back(std::move(e));
    }
    return out;
}

RouletteManager* RouletteManager::get() {
    static RouletteManager instance;
    return &instance;
}

// ---------------------------------------------------------------- menu

void RouletteManager::openMenu() {
    if (m_active) {
        auto body = fmt::format(
            "Run in progress.\nCleared: {}\nCurrent: #{} {}\nGoal: {}%",
            m_cleared.size(), m_current.position, m_current.name, m_required
        );
        createQuickPopup("Demon Roulette", body, "End run", "Resume", [this](FLAlertLayer*, bool resume) {
            if (resume) this->openCurrentLevel();
            else this->endRun(true);
        });
        return;
    }

    auto lo = Mod::get()->getSettingValue<int64_t>("min-rank");
    auto hi = Mod::get()->getSettingValue<int64_t>("max-rank");
    auto body = fmt::format(
        "Pick a list to roll levels from.\nRank range: {}-{} (change it in the mod settings).",
        std::min(lo, hi), std::max(lo, hi)
    );
    createQuickPopup("Extreme Demon Roulette", body, "AREDL", "Rated+", [this](FLAlertLayer*, bool ratedPlus) {
        this->startRun(ratedPlus ? ListSource::RatedPlus : ListSource::AREDL);
    });
}

// ---------------------------------------------------------------- run control

void RouletteManager::startRun(ListSource source) {
    m_source = source;
    m_pool.clear();
    m_used.clear();
    m_cleared.clear();
    m_active = false;
    m_pendingResult = false;

    m_loading = Notification::create("Loading list...", NotificationIcon::Loading, 0.f);
    m_loading->show();

    if (source == ListSource::AREDL) {
        this->fetchAredl();
    } else {
        this->fetchPointercrate(Mod::get()->getSettingValue<std::string>("rated-plus-url"), 0, 0);
    }
}

void RouletteManager::endRun(bool showSummary) {
    m_active = false;
    m_pendingResult = false;
    if (showSummary) {
        FLAlertLayer::create("Run ended", summary(), "OK")->show();
    }
}

std::string RouletteManager::summary() const {
    std::string out = fmt::format("Levels cleared: {}", m_cleared.size());
    size_t shown = 0;
    for (auto const& e : m_cleared) {
        if (shown++ >= 15) {
            out += "\n...";
            break;
        }
        out += fmt::format("\n#{} {}", e.position, e.name);
    }
    return out;
}

void RouletteManager::nextLevel() {
    auto lo = static_cast<int>(Mod::get()->getSettingValue<int64_t>("min-rank"));
    auto hi = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-rank"));
    if (lo > hi) std::swap(lo, hi);

    std::vector<size_t> candidates;
    for (size_t i = 0; i < m_pool.size(); ++i) {
        auto const& e = m_pool[i];
        if (m_used.contains(e.levelID)) continue;
        if (e.position < lo || e.position > hi) continue;
        candidates.push_back(i);
    }

    if (candidates.empty()) {
        m_active = false;
        FLAlertLayer::create(
            "Roulette complete",
            "No levels left in the selected rank range!\n\n" + summary(),
            "OK"
        )->show();
        return;
    }

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    m_current = m_pool[candidates[dist(rng())]];
    m_used.insert(m_current.levelID);
    m_best = 0.f;
    m_goalNotified = false;
    m_pendingResult = false;

    auto body = fmt::format(
        "Level {}\n#{} - {}\nReach {}% to continue.",
        m_cleared.size() + 1, m_current.position, m_current.name, m_required
    );
    createQuickPopup("Roulette", body, "End run", "Play", [this](FLAlertLayer*, bool play) {
        if (play) this->openCurrentLevel();
        else this->endRun(true);
    });
}

// ---------------------------------------------------------------- networking

void RouletteManager::fetchAredl() {
    auto url = Mod::get()->getSettingValue<std::string>("aredl-url");

    m_listener.bind([this](web::WebTask::Event* e) {
        if (auto* res = e->getValue()) {
            if (!res->ok()) {
                return this->onListFailed(fmt::format("HTTP {}", res->code()));
            }
            auto json = res->json();
            if (!json) {
                return this->onListFailed("The server returned invalid JSON.");
            }
            bool includeLegacy = Mod::get()->getSettingValue<bool>("include-legacy");
            m_pool = parseList(json.unwrap(), !includeLegacy, 0);
            this->onListLoaded();
        } else if (e->isCancelled()) {
            this->onListFailed("The request was cancelled.");
        }
    });

    auto req = web::WebRequest();
    req.userAgent("Geode ED Roulette");
    m_listener.setFilter(req.get(url));
}

void RouletteManager::fetchPointercrate(std::string baseUrl, int after, int page) {
    // Pointercrate pages hold at most 100 demons, paginated by position.
    auto url = fmt::format(
        "{}{}limit=100&after={}",
        baseUrl, baseUrl.find('?') == std::string::npos ? "?" : "&", after
    );

    m_listener.bind([this, baseUrl, after, page](web::WebTask::Event* e) {
        if (auto* res = e->getValue()) {
            if (!res->ok()) {
                return this->onListFailed(fmt::format("HTTP {}", res->code()));
            }
            auto json = res->json();
            if (!json) {
                return this->onListFailed("The server returned invalid JSON.");
            }

            auto val = json.unwrap();
            auto arr = val.asArray();
            size_t count = arr ? arr.unwrap().size() : 0;

            auto entries = parseList(val, false, after);
            int lastPos = after + static_cast<int>(count);
            for (auto const& e2 : entries) lastPos = std::max(lastPos, e2.position);
            for (auto& e2 : entries) m_pool.push_back(std::move(e2));

            if (count >= 100 && page < 5) {
                // Rebinding the listener from inside its own callback is unsafe,
                // so do the next page on the next frame.
                Loader::get()->queueInMainThread([this, baseUrl, lastPos, page]() {
                    this->fetchPointercrate(baseUrl, lastPos, page + 1);
                });
            } else {
                this->onListLoaded();
            }
        } else if (e->isCancelled()) {
            this->onListFailed("The request was cancelled.");
        }
    });

    auto req = web::WebRequest();
    req.userAgent("Geode ED Roulette");
    m_listener.setFilter(req.get(url));
}

void RouletteManager::onListLoaded() {
    if (m_loading) {
        m_loading->hide();
        m_loading = nullptr;
    }
    if (m_pool.empty()) {
        FLAlertLayer::create("Roulette", "The list came back empty.", "OK")->show();
        return;
    }
    m_required = static_cast<int>(Mod::get()->getSettingValue<int64_t>("start-percent"));
    m_active = true;
    this->nextLevel();
}

void RouletteManager::onListFailed(std::string const& reason) {
    if (m_loading) {
        m_loading->hide();
        m_loading = nullptr;
    }
    m_active = false;
    FLAlertLayer::create(
        "Roulette",
        "Couldn't load the list:\n" + reason + "\n\nCheck the API URLs in the mod settings.",
        "OK"
    )->show();
}

// ---------------------------------------------------------------- opening levels

void RouletteManager::openCurrentLevel() {
    auto* glm = GameLevelManager::get();

    if (auto* saved = glm->getSavedLevel(m_current.levelID)) {
        if (!std::string(saved->m_levelString).empty()) {
            this->showLevel(saved);
            return;
        }
    }

    glm->m_levelDownloadDelegate = this;
    glm->downloadLevel(m_current.levelID, false);

    m_loading = Notification::create("Downloading level...", NotificationIcon::Loading, 0.f);
    m_loading->show();
}

void RouletteManager::levelDownloadFinished(GJGameLevel* level) {
    if (m_loading) {
        m_loading->hide();
        m_loading = nullptr;
    }
    auto* glm = GameLevelManager::get();
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    this->showLevel(level);
}

void RouletteManager::levelDownloadFailed(int) {
    if (m_loading) {
        m_loading->hide();
        m_loading = nullptr;
    }
    auto* glm = GameLevelManager::get();
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    FLAlertLayer::create(
        "Roulette",
        "Couldn't download the level. Open the roulette menu and press Resume to retry.",
        "OK"
    )->show();
}

void RouletteManager::showLevel(GJGameLevel* level) {
    auto scene = LevelInfoLayer::scene(level, false);
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

// ---------------------------------------------------------------- play tracking

bool RouletteManager::isCurrentLevel(int levelID) const {
    return m_active && levelID == m_current.levelID;
}

void RouletteManager::onProgress(float percent) {
    if (!m_active) return;
    m_best = std::max(m_best, percent);

    if (!m_goalNotified && static_cast<int>(m_best) >= m_required) {
        m_goalNotified = true;
        Notification::create(
            fmt::format("{}% reached! Quit the level to continue.", m_required),
            NotificationIcon::Success
        )->show();
    }
}

void RouletteManager::onQuit() {
    if (!m_active) return;
    m_pendingSuccess = static_cast<int>(m_best) >= m_required;
    m_pendingResult = true;
}

void RouletteManager::showPendingResultIfReady() {
    if (!m_pendingResult) return;
    if (PlayLayer::get()) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;

    m_pendingResult = false;

    if (m_pendingSuccess) {
        m_cleared.push_back(m_current);
        auto step = static_cast<int>(Mod::get()->getSettingValue<int64_t>("percent-step"));
        m_required = std::min(100, m_required + step);

        auto body = fmt::format(
            "Cleared {} level(s) so far.\nNext goal: {}%.",
            m_cleared.size(), m_required
        );
        createQuickPopup("Level passed!", body, "End run", "Next level", [this](FLAlertLayer*, bool next) {
            if (next) this->nextLevel();
            else this->endRun(true);
        });
    } else {
        auto body = fmt::format(
            "#{} {}\nYou got {}% but needed {}%.\n\n{}",
            m_current.position, m_current.name,
            static_cast<int>(m_best), m_required, summary()
        );
        m_active = false;
        createQuickPopup("Run over", body, "Close", "New run", [this](FLAlertLayer*, bool again) {
            if (again) this->openMenu();
        });
    }
}

// ---------------------------------------------------------------- watcher

RouletteWatcher* RouletteWatcher::create() {
    auto ret = new RouletteWatcher();
    if (ret && ret->init()) {
        ret->autorelease();
        ret->scheduleUpdate();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void RouletteWatcher::update(float) {
    RouletteManager::get()->showPendingResultIfReady();
}
