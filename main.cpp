#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

// Global variables
inline std::vector<int> g_illLevels;
inline bool g_isILLSearch = false;
inline int g_illPage = 0; // Tracks our custom page internally (0-indexed)

// Helper function to extract EXACTLY 10 level IDs for the current page
std::string getChunkedIDString(const std::vector<int>& ids, int pageIndex) {
    size_t start = static_cast<size_t>(pageIndex) * 10;
    if (start >= ids.size()) return "";

    std::stringstream ss;
    // Only grab 10 IDs so it perfectly matches one Geometry Dash page
    for (size_t i = start; i < start + 10 && i < ids.size(); ++i) {
        if (i > start) ss << ",";
        ss << ids[i];
    }
    return ss.str();
}

// Function to load the JSON data on startup
inline void loadILLData() {
    g_illLevels.clear();
    auto path = Mod::get()->getResourcesDir() / "ill_data.json";
    
    auto res = utils::file::readString(path);
    if (!res) {
        log::error("ILL Mod: ill_data.json not found!");
        return;
    }
    
    auto parsed = matjson::parse(res.unwrap());
    if (!parsed) {
        log::error("ILL Mod: Failed to parse JSON!");
        return;
    }
    
    auto arrRes = parsed.unwrap().asArray();
    if (!arrRes) return;
    
    auto arrayData = arrRes.unwrap();
    
    for (auto& entry : arrayData) {
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
        
        if (extractedID > 0) {
            g_illLevels.push_back(extractedID);
        }
    }
    log::info("ILL Mod: Successfully loaded {} valid level IDs", g_illLevels.size());
}

// Run JSON loader on mod load
$on_mod(Loaded) {
    loadILLData();
}

// Function to open the ILL viewer on the actively tracked page
void openILLBrowser(bool isPush) {
    if (g_illLevels.empty()) {
        FLAlertLayer::create("Error", "No levels found in JSON.", "OK")->show();
        return;
    }

    g_isILLSearch = true;
    std::string searchString = getChunkedIDString(g_illLevels, g_illPage);
    auto searchObj = GJSearchObject::create(SearchType::Type19, searchString);
    
    // IMPORTANT: Lock the native search page at 0 so the GD server doesn't 
    // skip any of our exact 10 IDs!
    searchObj->m_page = 0;

    auto scene = LevelBrowserLayer::scene(searchObj);
    
    if (isPush) {
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.5f, scene));
    } else {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
}

// Hook LevelBrowserLayer to handle pagination completely manually
class $modify(ILLBrowserLayer, LevelBrowserLayer) {
    bool init(GJSearchObject* searchObj) {
        if (!LevelBrowserLayer::init(searchObj)) return false;
        
        if (g_isILLSearch) {
            if (auto title = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("title-label"))) {
                title->setString("IMPOSSIBLE LEVEL LIST");
                title->setScale(0.7f); 
            }
        }
        return true;
    }

    void setupPageInfo(gd::string info, const char* key) {
        // Let the base class do its thing first
        LevelBrowserLayer::setupPageInfo(info, key);

        if (g_isILLSearch && !g_illLevels.empty()) {
            int maxPages = (g_illLevels.size() + 9) / 10;
            
            // Format accurate totals (e.g., "1 to 10 of 1981")
            int start = (g_illPage * 10) + 1;
            int end = std::min(static_cast<int>((g_illPage + 1) * 10), static_cast<int>(g_illLevels.size()));
            std::string pageText = fmt::format("{} to {} of {}", start, end, g_illLevels.size());
            
            if (m_countText) {
                m_countText->setString(pageText.c_str());
            }

            // Manually manage arrow visibility because native m_page is locked at 0
            if (m_leftArrow) m_leftArrow->setVisible(g_illPage > 0);
            if (m_rightArrow) m_rightArrow->setVisible(g_illPage < maxPages - 1);
        }
    }

    void onNextPage(cocos2d::CCObject* sender) {
        if (g_isILLSearch) {
            int maxPages = (g_illLevels.size() + 9) / 10;
            if (g_illPage + 1 < maxPages) {
                g_illPage++; // Increase our custom page tracker
                openILLBrowser(false); // Reload the layer with the next 10 IDs
            }
            return;
        }
        LevelBrowserLayer::onNextPage(sender);
    }

    void onPrevPage(cocos2d::CCObject* sender) {
        if (g_isILLSearch) {
            if (g_illPage > 0) {
                g_illPage--; // Decrease our custom page tracker
                openILLBrowser(false); // Reload the layer with the previous 10 IDs
            }
            return;
        }
        LevelBrowserLayer::onPrevPage(sender);
    }

    void onBack(CCObject* sender) {
        if (g_isILLSearch) {
            g_isILLSearch = false; // Safely disable our mod's hooks when exiting
        }
        LevelBrowserLayer::onBack(sender);
    }
};

// Hook LevelSearchLayer to add the blue-tinted Extreme Demon button
class $modify(ILLLevelSearchLayer, LevelSearchLayer) {
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;
            
        auto menu = this->getChildByIDRecursive("other-filter-menu");
        if (!menu) menu = this->getChildByIDRecursive("other-filters-menu");
        
        if (menu) {
            auto ccMenu = typeinfo_cast<CCMenu*>(menu);
            if (ccMenu) {
                // Load the built-in Extreme Demon sprite and tint it blue
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
        g_illPage = 0; // Reset tracker to page 1
        openILLBrowser(true);
    }
};
