#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
inline std::vector<int> g_illLevels;        // every level on the list, in rank order
inline std::vector<int> g_illFiltered;      // levels matching the current search
inline std::string g_illSearchQuery;        // current search text (empty = no filter)
inline bool g_isILLSearch = false;
inline int g_illPage = 0;                   // our own page tracker (0-indexed)
inline bool g_illDataLoading = false;
inline bool g_illDataLoadFailed = false;

constexpr const char* ILL_API_LEVELS_URL = "https://api.impossiblelevels.com/api/Levels/details";
constexpr const char* ILL_API_TAGS_URL = "https://api.impossiblelevels.com/api/Tags";
constexpr const char* ILL_API_TIERS_URL = "https://api.impossiblelevels.com/api/Tiers";
constexpr int ILL_API_PAGE_SIZE = 100;

// Badge settings -- tweak these freely.
constexpr size_t ILL_MAX_BADGES = 5;            // tier + up to 4 more
constexpr float ILL_BADGE_MAX_ROW_WIDTH = 150.f; // badges wrap to a new row past this width
constexpr float ILL_BADGE_HEIGHT = 11.f;
constexpr float ILL_BADGE_GAP = 3.f;
constexpr float ILL_BADGE_TEXT_SCALE = 0.25f;

// Extra per-level info, looked up by level ID.
struct ILLLevelInfo {
    std::string name;
    std::string uploader;
    std::string tier;                // raw tier name from /api/Tiers
    int rank = 0;
    std::vector<std::string> tags;   // tag names from /api/Tags, in API order
    bool isNoNG = false;
};
inline std::unordered_map<int, ILLLevelInfo> g_illInfoByLevelId;

// The list the browser should currently show (full list, or search results).
inline std::vector<int> const& activeILLList() {
    return g_illSearchQuery.empty() ? g_illLevels : g_illFiltered;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// Extract EXACTLY 10 level IDs for the given page.
std::string getChunkedIDString(const std::vector<int>& ids, int pageIndex) {
    size_t start = static_cast<size_t>(pageIndex) * 10;
    if (start >= ids.size()) return "";

    std::stringstream ss;
    for (size_t i = start; i < start + 10 && i < ids.size(); ++i) {
        if (i > start) ss << ",";
        ss << ids[i];
    }
    return ss.str();
}

inline std::string illLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// "The Top Ten" -> "Top 10"; any other tier just loses a leading "The ".
inline std::string illTierLabel(std::string const& name) {
    std::string out = name;
    if (out.rfind("The ", 0) == 0) out = out.substr(4);
    if (out == "Top Ten") return "Top 10";
    return out;
}

inline int extractLevelIdFromEntry(matjson::Value const& entry) {
    int extractedID = 0;

    if (entry.contains("level")) {
        auto levelData = entry["level"];
        if (levelData.contains("levelId") && !levelData["levelId"].isNull()) {
            auto idField = levelData["levelId"];

            if (idField.isString()) {
                std::string idStr = idField.asString().unwrapOr("0");
                if (!idStr.empty() && idStr != "0") {
                    try {
                        extractedID = std::stoi(idStr);
                    } catch (...) {
                        extractedID = 0;
                    }
                }
            } else if (idField.isNumber()) {
                extractedID = idField.asInt().unwrapOr(0);
            }
        }
    }

    return extractedID;
}

// Applies a search. Returns false (and changes nothing) if nothing matches.
// An empty/blank query clears the filter and returns true.
// Every word typed must match somewhere in: name, uploader, tier, or tags.
inline bool applyILLSearch(std::string const& rawQuery) {
    auto first = rawQuery.find_first_not_of(" \t");
    if (first == std::string::npos) {
        g_illSearchQuery.clear();
        g_illFiltered.clear();
        return true;
    }
    auto last = rawQuery.find_last_not_of(" \t");
    std::string trimmed = rawQuery.substr(first, last - first + 1);

    std::vector<std::string> words;
    {
        std::stringstream ss(illLower(trimmed));
        std::string w;
        while (ss >> w) words.push_back(w);
    }

    std::vector<int> results;
    for (int id : g_illLevels) {
        auto it = g_illInfoByLevelId.find(id);
        if (it == g_illInfoByLevelId.end()) continue;
        auto& info = it->second;

        std::string hay = info.name + " " + info.uploader + " " + illTierLabel(info.tier);
        for (auto& t : info.tags) hay += " " + t;
        if (info.isNoNG) hay += " no ng";
        hay = illLower(hay);

        bool all = true;
        for (auto& word : words) {
            if (hay.find(word) == std::string::npos) {
                all = false;
                break;
            }
        }
        if (all) results.push_back(id);
    }

    if (results.empty()) return false;

    g_illSearchQuery = trimmed;
    g_illFiltered = std::move(results);
    return true;
}

// ---------------------------------------------------------------------------
// Badge colors
// ---------------------------------------------------------------------------
inline ccColor3B illHsv(float h, float s, float v) {
    float c = v * s;
    float hp = std::fmod(h / 60.f, 6.f);
    float x = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
    float r = 0.f, g = 0.f, b = 0.f;
    if (hp < 1.f)      { r = c; g = x; }
    else if (hp < 2.f) { r = x; g = c; }
    else if (hp < 3.f) { g = c; b = x; }
    else if (hp < 4.f) { g = x; b = c; }
    else if (hp < 5.f) { r = x; b = c; }
    else               { r = c; b = x; }
    float m = v - c;
    return ccColor3B{
        static_cast<GLubyte>((r + m) * 255.f),
        static_cast<GLubyte>((g + m) * 255.f),
        static_cast<GLubyte>((b + m) * 255.f)
    };
}

// Tier badge color depends on the level's rank.
inline ccColor3B illTierColor(int rank) {
    if (rank <= 0)    return ccColor3B{170, 170, 170};
    if (rank <= 10)   return ccColor3B{255, 200, 40};   // gold
    if (rank <= 50)   return ccColor3B{255, 140, 40};   // orange
    if (rank <= 100)  return ccColor3B{240, 70, 70};    // red
    if (rank <= 250)  return ccColor3B{220, 80, 200};   // magenta
    if (rank <= 500)  return ccColor3B{150, 100, 240};  // purple
    if (rank <= 1000) return ccColor3B{80, 140, 240};   // blue
    return ccColor3B{60, 190, 170};                     // teal
}

// Tag badge color: a few fixed colors, everything else gets a stable
// color derived from its name.
inline ccColor3B illTagColor(std::string const& tag) {
    if (tag == "2 Player") return ccColor3B{70, 160, 255};
    if (tag == "No NG") return ccColor3B{200, 60, 60};
    // Version-style tags like "2.1" / "2.2"
    if (tag.size() >= 3 && std::isdigit(static_cast<unsigned char>(tag[0])) && tag[1] == '.') {
        return ccColor3B{90, 200, 110};
    }
    unsigned int hash = 5381;
    for (unsigned char c : tag) hash = hash * 33u + c;
    return illHsv(static_cast<float>(hash % 360u), 0.55f, 0.95f);
}

inline ccColor3B illTextColorFor(ccColor3B bg) {
    float lum = 0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b;
    return lum > 140.f ? ccColor3B{0, 0, 0} : ccColor3B{255, 255, 255};
}

// One small colored box with centered text.
inline CCLayerColor* makeILLBadge(std::string const& text, ccColor3B bg) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(ILL_BADGE_TEXT_SCALE);
    float w = label->getContentSize().width * ILL_BADGE_TEXT_SCALE + 8.f;
    float h = ILL_BADGE_HEIGHT;

    auto box = CCLayerColor::create(ccColor4B{bg.r, bg.g, bg.b, 235}, w, h);
    label->setColor(illTextColorFor(bg));
    label->setPosition({w / 2.f, h / 2.f});
    box->addChild(label);
    return box;
}

// ---------------------------------------------------------------------------
// Live fetch
// ---------------------------------------------------------------------------
struct ILLTierRange {
    int start = 0;
    int end = 0;
    std::string name;
};

inline std::string tierForRank(int rank, std::vector<ILLTierRange> const& tiers) {
    for (auto& t : tiers) {
        if (rank >= t.start && rank <= t.end) return t.name;
    }
    return "";
}

// Fetches tags + tiers (nice-to-have: if they fail, levels still load, just
// without badges), then every page of the level list (required). Only
// replaces the in-memory data once the whole level fetch has succeeded.
arc::Future<> fetchAllILLLevelsAsync() {
    g_illDataLoading = true;
    g_illDataLoadFailed = false;

    // 1. Tag id -> name map
    std::unordered_map<int, std::string> tagNames;
    {
        auto req = web::WebRequest();
        req.userAgent("Mozilla/5.0 (compatible; ILL-Viewer-GeodeMod/1.0)");
        auto res = co_await req.get(ILL_API_TAGS_URL);

        bool ok = false;
        if (res.code() >= 200 && res.code() < 300) {
            auto jsonRes = res.json();
            if (jsonRes) {
                auto arrRes = jsonRes.unwrap().asArray();
                if (arrRes) {
                    auto tagArray = arrRes.unwrap();
                    for (auto& tag : tagArray) {
                        if (tag.contains("id") && tag.contains("name")) {
                            tagNames[static_cast<int>(tag["id"].asInt().unwrapOr(0))] = tag["name"].asString().unwrapOr("");
                        }
                    }
                    ok = true;
                }
            }
        }
        if (!ok) {
            log::warn("ILL Mod: Couldn't load tags (HTTP {}), badges will be missing tag names.", res.code());
        }
    }

    // 2. Tier rank brackets
    std::vector<ILLTierRange> tiers;
    {
        auto req = web::WebRequest();
        req.userAgent("Mozilla/5.0 (compatible; ILL-Viewer-GeodeMod/1.0)");
        auto res = co_await req.get(ILL_API_TIERS_URL);

        bool ok = false;
        if (res.code() >= 200 && res.code() < 300) {
            auto jsonRes = res.json();
            if (jsonRes) {
                auto arrRes = jsonRes.unwrap().asArray();
                if (arrRes) {
                    auto tierArray = arrRes.unwrap();
                    for (auto& t : tierArray) {
                        ILLTierRange range;
                        range.start = t.contains("start") ? static_cast<int>(t["start"].asInt().unwrapOr(0)) : 0;
                        range.end = t.contains("end") ? static_cast<int>(t["end"].asInt().unwrapOr(999999)) : 999999;
                        range.name = t.contains("name") ? t["name"].asString().unwrapOr("") : "";
                        tiers.push_back(range);
                    }
                    ok = true;
                }
            }
        }
        if (!ok) {
            log::warn("ILL Mod: Couldn't load tiers (HTTP {}), tier badges will be missing.", res.code());
        }
    }

    // 3. The level list itself, paginated
    std::vector<int> fetchedLevels;
    std::unordered_map<int, ILLLevelInfo> fetchedInfo;

    int page = 1;
    while (true) {
        auto req = web::WebRequest();
        req.userAgent("Mozilla/5.0 (compatible; ILL-Viewer-GeodeMod/1.0)");
        req.param("page", page);
        req.param("pageSize", ILL_API_PAGE_SIZE);

        auto res = co_await req.get(ILL_API_LEVELS_URL);

        if (res.code() < 200 || res.code() >= 300) {
            log::error("ILL Mod: API returned HTTP {} on page {}", res.code(), page);
            g_illDataLoadFailed = true;
            break;
        }

        auto jsonRes = res.json();
        if (!jsonRes) {
            log::error("ILL Mod: Failed to parse API response as JSON on page {}", page);
            g_illDataLoadFailed = true;
            break;
        }

        auto root = jsonRes.unwrap();
        if (!root.contains("data")) {
            log::error("ILL Mod: API response missing 'data' field on page {}", page);
            g_illDataLoadFailed = true;
            break;
        }

        auto arrRes = root["data"].asArray();
        if (!arrRes) {
            log::error("ILL Mod: 'data' field was not an array on page {}", page);
            g_illDataLoadFailed = true;
            break;
        }

        auto entries = arrRes.unwrap();
        int countThisPage = static_cast<int>(entries.size());

        for (auto& entry : entries) {
            int extractedID = extractLevelIdFromEntry(entry);
            if (extractedID <= 0) continue;

            fetchedLevels.push_back(extractedID);

            ILLLevelInfo info;
            if (entry.contains("level")) {
                auto lv = entry["level"];
                info.rank = lv.contains("rank") ? static_cast<int>(lv["rank"].asInt().unwrapOr(0)) : 0;
                info.tier = tierForRank(info.rank, tiers);
                if (lv.contains("name") && lv["name"].isString()) {
                    info.name = lv["name"].asString().unwrapOr("");
                }
                if (lv.contains("uploader") && lv["uploader"].isString()) {
                    info.uploader = lv["uploader"].asString().unwrapOr("");
                }
                info.isNoNG = lv.contains("isNoNG") && lv["isNoNG"].isBool() && lv["isNoNG"].asBool().unwrapOr(false);
            }
            if (entry.contains("tagIds")) {
                auto tagIdsRes = entry["tagIds"].asArray();
                if (tagIdsRes) {
                    auto tagIds = tagIdsRes.unwrap();
                    for (auto& tagIdVal : tagIds) {
                        int tagId = static_cast<int>(tagIdVal.asInt().unwrapOr(-1));
                        auto it = tagNames.find(tagId);
                        if (it != tagNames.end()) {
                            info.tags.push_back(it->second);
                        }
                    }
                }
            }
            fetchedInfo[extractedID] = std::move(info);
        }

        log::info("ILL Mod: Fetched page {} ({} entries, {} valid IDs so far)", page, countThisPage, fetchedLevels.size());

        if (countThisPage < ILL_API_PAGE_SIZE) {
            break; // partial page = end of the list
        }
        page++;
    }

    if (!g_illDataLoadFailed) {
        g_illLevels = std::move(fetchedLevels);
        g_illInfoByLevelId = std::move(fetchedInfo);
        log::info("ILL Mod: Loaded {} levels, {} tags, {} tiers, {} info entries.",
            g_illLevels.size(), tagNames.size(), tiers.size(), g_illInfoByLevelId.size());
    } else {
        log::error("ILL Mod: Live fetch failed, keeping previously loaded data ({} entries).", g_illLevels.size());
    }

    g_illDataLoading = false;
}

void startILLFetch() {
    if (g_illDataLoading) return;
    async::spawn(fetchAllILLLevelsAsync(), [] {});
}

// Kick off the live fetch on mod load.
$on_mod(Loaded) {
    startILLFetch();
}

// ---------------------------------------------------------------------------
// Opening the browser
// ---------------------------------------------------------------------------
void openILLBrowser(bool isPush) {
    if (g_illLevels.empty()) {
        if (g_illDataLoading) {
            FLAlertLayer::create("Please Wait", "Still loading the ILL list from the server, try again in a moment.", "OK")->show();
        } else if (g_illDataLoadFailed) {
            FLAlertLayer::create("Error", "Couldn't load the ILL list (check your internet connection), retrying now.", "OK")->show();
            startILLFetch();
        } else {
            FLAlertLayer::create("Error", "No levels found.", "OK")->show();
        }
        return;
    }

    g_isILLSearch = true;
    std::string searchString = getChunkedIDString(activeILLList(), g_illPage);
    auto searchObj = GJSearchObject::create(SearchType::Type19, searchString);

    // Lock the native search page at 0 so the GD server doesn't skip any
    // of our exact 10 IDs!
    searchObj->m_page = 0;

    auto scene = LevelBrowserLayer::scene(searchObj);

    if (isPush) {
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.5f, scene));
    } else {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
}

// ---------------------------------------------------------------------------
// LevelBrowserLayer: manual pagination + search bar
// ---------------------------------------------------------------------------
class $modify(ILLBrowserLayer, LevelBrowserLayer) {
    struct Fields {
        TextInput* illInput = nullptr;
    };

    bool init(GJSearchObject* searchObj) {
        if (!LevelBrowserLayer::init(searchObj)) return false;

        if (g_isILLSearch) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();

            // Title stays at the top, just a bit smaller, with the search bar right below it.
            if (auto title = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("title-label"))) {
                title->setString("IMPOSSIBLE LEVEL LIST");
                title->setScale(0.45f);
                title->setPositionY(winSize.height - 9.f);
            }

            // Where the level list sits (falls back to the standard centered 356x220 list).
            CCPoint listPos = CCPoint{winSize.width / 2.f - 178.f, winSize.height / 2.f - 110.f};
            CCSize listSize = CCSize{356.f, 220.f};
            if (m_list) {
                listPos = m_list->getPosition();
                listSize = m_list->getContentSize();
            }

            // Search bar: a brown 356x30 strip sitting on top of the list, same look
            // as the Integrated Demonlist one (brown bar, dark input, green magnifier).
            auto searchMenu = CCMenu::create();
            searchMenu->setID("ill-search-bar"_spr);
            searchMenu->setContentSize({356.f, 30.f});
            searchMenu->setPosition({
                listPos.x + (listSize.width - 356.f) / 2.f,
                listPos.y + listSize.height + 2.f
            });

            auto barBackground = CCLayerColor::create({194, 114, 62, 255}, 356.f, 30.f);
            barBackground->setID("ill-search-background"_spr);
            searchMenu->addChild(barBackground);

            CCNode* btnSpr = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
            if (!btnSpr) btnSpr = ButtonSprite::create("Go");
            btnSpr->setScale(0.7f);
            auto searchBtn = CCMenuItemSpriteExtra::create(
                btnSpr,
                this,
                menu_selector(ILLBrowserLayer::onILLSearch)
            );
            searchBtn->setID("ill-search-button"_spr);
            searchBtn->setPosition({337.f, 15.f});
            searchMenu->addChild(searchBtn);

            auto input = TextInput::create(310.f, "Search ILL Levels...");
            input->setID("ill-search-input"_spr);
            input->setPosition({165.f, 15.f});
            input->setTextAlign(TextInputAlign::Left);
            input->setMaxCharCount(40);
            auto inputNode = input->getInputNode();
            inputNode->setLabelPlaceholderScale(0.4f);
            inputNode->setMaxLabelScale(0.4f);
            auto bgSprite = input->getBGSprite();
            bgSprite->setContentSize({620.f, 40.f});
            bgSprite->setScale(0.5f);
            if (!g_illSearchQuery.empty()) {
                input->setString(g_illSearchQuery);
            }
            searchMenu->addChild(input);
            m_fields->illInput = input;

            this->addChild(searchMenu, 20);
        }
        return true;
    }

    void onILLSearch(CCObject*) {
        if (!g_isILLSearch) return;

        std::string text = m_fields->illInput ? m_fields->illInput->getString() : std::string();
        if (!applyILLSearch(text)) {
            std::string msg = fmt::format("No ILL levels match \"{}\".", text);
            FLAlertLayer::create("No Results", msg.c_str(), "OK")->show();
            return;
        }

        g_illPage = 0;
        openILLBrowser(false);
    }

    void setupPageInfo(gd::string info, const char* key) {
        LevelBrowserLayer::setupPageInfo(info, key);

        auto& list = activeILLList();
        if (g_isILLSearch && !list.empty()) {
            int maxPages = (static_cast<int>(list.size()) + 9) / 10;

            int start = (g_illPage * 10) + 1;
            int end = std::min((g_illPage + 1) * 10, static_cast<int>(list.size()));
            std::string pageText = fmt::format("{} to {} of {}", start, end, list.size());

            if (m_countText) {
                m_countText->setString(pageText.c_str());
            }

            // Native m_page is locked at 0, so manage the arrows ourselves.
            if (m_leftArrow) m_leftArrow->setVisible(g_illPage > 0);
            if (m_rightArrow) m_rightArrow->setVisible(g_illPage < maxPages - 1);
        }
    }

    void onNextPage(cocos2d::CCObject* sender) {
        if (g_isILLSearch) {
            int maxPages = (static_cast<int>(activeILLList().size()) + 9) / 10;
            if (g_illPage + 1 < maxPages) {
                g_illPage++;
                openILLBrowser(false);
            }
            return;
        }
        LevelBrowserLayer::onNextPage(sender);
    }

    void onPrevPage(cocos2d::CCObject* sender) {
        if (g_isILLSearch) {
            if (g_illPage > 0) {
                g_illPage--;
                openILLBrowser(false);
            }
            return;
        }
        LevelBrowserLayer::onPrevPage(sender);
    }

    void onBack(CCObject* sender) {
        if (g_isILLSearch) {
            g_isILLSearch = false;
            g_illSearchQuery.clear();
            g_illFiltered.clear();
        }
        LevelBrowserLayer::onBack(sender);
    }
};

// ---------------------------------------------------------------------------
// LevelSearchLayer: the blue ILL button
// ---------------------------------------------------------------------------
class $modify(ILLLevelSearchLayer, LevelSearchLayer) {
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;

        auto menu = this->getChildByIDRecursive("other-filter-menu");
        if (!menu) menu = this->getChildByIDRecursive("other-filters-menu");

        if (menu) {
            auto ccMenu = typeinfo_cast<CCMenu*>(menu);
            if (ccMenu) {
                auto spr = CCSprite::createWithSpriteFrameName("difficulty_10_btn2_001.png");
                spr->setColor({50, 150, 255});
                spr->setScale(0.85f);

                auto btn = CCMenuItemSpriteExtra::create(
                    spr,
                    this,
                    menu_selector(ILLLevelSearchLayer::onILLButton)
                );

                btn->setID("ill-button"_spr);
                ccMenu->addChild(btn);
                ccMenu->updateLayout();
            }
        }
        return true;
    }

    void onILLButton(CCObject*) {
        g_illPage = 0;               // start on page 1
        g_illSearchQuery.clear();    // and with no search filter
        g_illFiltered.clear();
        openILLBrowser(true);
    }
};

// ---------------------------------------------------------------------------
// LevelCell: colored tier/tag badges above the View button
// ---------------------------------------------------------------------------
class $modify(ILLLevelCell, LevelCell) {
    void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);

        if (!g_isILLSearch || !level) return;

        // Cells can be reloaded; never stack a second set of badges.
        if (auto old = this->getChildByID("ill-badges"_spr)) {
            old->removeFromParent();
        }

        int levelId = static_cast<int>(level->m_levelID);
        auto it = g_illInfoByLevelId.find(levelId);

        static int s_logged = 0;
        bool shouldLog = s_logged < 3;
        if (shouldLog) {
            ++s_logged;
            log::info("ILL Mod: LevelCell hook ran for level {} (info found: {}, info map size: {})",
                levelId, it != g_illInfoByLevelId.end(), g_illInfoByLevelId.size());
        }
        if (it == g_illInfoByLevelId.end()) return;
        auto& info = it->second;

        // Decide which badges to show: tier first, then No NG, then tags in API order.
        std::vector<std::pair<std::string, ccColor3B>> badges;
        if (!info.tier.empty()) {
            badges.push_back({illTierLabel(info.tier), illTierColor(info.rank)});
        }
        if (info.isNoNG) {
            badges.push_back({"No NG", illTagColor("No NG")});
        }
        for (auto& tag : info.tags) {
            if (badges.size() >= ILL_MAX_BADGES) break;
            badges.push_back({tag, illTagColor(tag)});
        }
        if (badges.empty()) return;

        // Build the boxes and wrap them into rows.
        std::vector<CCLayerColor*> boxes;
        for (auto& b : badges) {
            boxes.push_back(makeILLBadge(b.first, b.second));
        }

        std::vector<std::vector<CCLayerColor*>> rows(1);
        float curW = 0.f;
        for (auto* box : boxes) {
            float w = box->getContentSize().width;
            if (!rows.back().empty() && curW + ILL_BADGE_GAP + w > ILL_BADGE_MAX_ROW_WIDTH) {
                rows.emplace_back();
                curW = 0.f;
            }
            curW += (rows.back().empty() ? 0.f : ILL_BADGE_GAP) + w;
            rows.back().push_back(box);
        }

        std::vector<float> rowWidths;
        float containerW = 0.f;
        for (auto& row : rows) {
            float rw = 0.f;
            for (size_t i = 0; i < row.size(); ++i) {
                rw += row[i]->getContentSize().width + (i > 0 ? ILL_BADGE_GAP : 0.f);
            }
            rowWidths.push_back(rw);
            containerW = std::max(containerW, rw);
        }
        float rowCount = static_cast<float>(rows.size());
        float containerH = rowCount * ILL_BADGE_HEIGHT + (rowCount - 1.f) * ILL_BADGE_GAP;

        // Container: right-aligned rows, first badges (the tier) on the top row.
        auto container = CCNode::create();
        container->setID("ill-badges"_spr);
        container->setContentSize({containerW, containerH});
        container->setAnchorPoint({1.f, 0.f});
        for (size_t r = 0; r < rows.size(); ++r) {
            float y = static_cast<float>(rows.size() - 1 - r) * (ILL_BADGE_HEIGHT + ILL_BADGE_GAP);
            float x = containerW - rowWidths[r];
            for (auto* box : rows[r]) {
                box->setPosition({x, y});
                container->addChild(box);
                x += box->getContentSize().width + ILL_BADGE_GAP;
            }
        }

        // Anchor above the cell's View button (found via its node ID). If the
        // ID isn't there, fall back to the top-right of a standard-size cell.
        CCSize cellSize = this->getContentSize();
        if (cellSize.width <= 0.f || cellSize.height <= 0.f) {
            cellSize = CCSize{356.f, 90.f};
        }
        CCPoint rightBottom = CCPoint{cellSize.width - 8.f, cellSize.height * 0.5f + 22.f};
        bool foundButton = false;
        if (auto viewBtn = this->getChildByIDRecursive("view-button")) {
            if (auto parent = viewBtn->getParent()) {
                CCPoint local = this->convertToNodeSpace(parent->convertToWorldSpace(viewBtn->getPosition()));
                CCSize btnSize = viewBtn->getContentSize();
                float btnScale = viewBtn->getScale();
                rightBottom = CCPoint{
                    local.x + (btnSize.width * btnScale) / 2.f,
                    local.y + (btnSize.height * btnScale) / 2.f + 4.f
                };
                foundButton = true;
            }
        }

        // If the badges would poke out of the top of the cell, shrink them to fit.
        float availH = cellSize.height - 2.f - rightBottom.y;
        if (availH > 4.f && containerH > availH) {
            container->setScale(availH / containerH);
        }

        container->setPosition(rightBottom);
        this->addChild(container, 100);

        if (shouldLog) {
            log::info("ILL Mod: Added {} badges in {} row(s) (view-button found: {}, cell size {}x{})",
                badges.size(), rows.size(), foundButton, cellSize.width, cellSize.height);
        }
    }
};
