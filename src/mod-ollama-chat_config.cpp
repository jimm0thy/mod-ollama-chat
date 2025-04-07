#include "mod-ollama-chat_config.h"
#include "Config.h"
#include "Log.h"
#include "mod-ollama-chat_api.h"
#include <fmt/core.h>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Global configuration variable definitions...
float      g_SayDistance       = 30.0f;
float      g_YellDistance      = 100.0f;
float      g_GeneralDistance   = 600.0f;
uint32_t   g_PlayerReplyChance = 90;
uint32_t   g_BotReplyChance    = 10;
uint32_t   g_MaxBotsToPick     = 2;
std::string g_OllamaUrl        = "http://localhost:11434/api/generate";
std::string g_OllamaModel      = "llama3.2:1b";
std::unordered_map<uint64_t, uint32> botPersonalityList;

// New configuration option: API max concurrent queries (0 means no limit)
uint32_t   g_MaxConcurrentQueries = 0;

bool       g_EnableRandomChatter             = true;
uint32_t   g_MinRandomInterval               = 45;
uint32_t   g_MaxRandomInterval               = 180;
float      g_RandomChatterRealPlayerDistance = 40.0f;
uint32_t   g_RandomChatterBotCommentChance   = 25;

bool       g_EnableRPPersonalities           = false;

// Default blacklist commands; these are prefixes that indicate the message is a command.
std::vector<std::string> g_BlacklistCommands = {
    ".playerbots",
    "playerbot",
    "autogear",
    "talents",
    "reset botAI",
    "summon",
    "release",
    "revive",
    "leave",
    "attack",
    "follow",
    "flee",
    "stay",
    "runaway",
    "grind",
    "disperse",
    "give leader",
    "spells",
    "cast",
    "quests",
    "accept",
    "drop",
    "talk",
    "talents",
    "reset",
    "autogear",
    "ss",
    "trainer",
    "rti",
    "rtsc",
    "do",
    "ll",
    "e",
    "ue",
    "nc",
    "open",
    "destroy"
    "s",
    "b",
    "bank",
    "gb",
    "u",
    "co",
};

static std::vector<std::string> SplitString(const std::string& str, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        // Trim whitespace from token
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
    }
    return tokens;
}

// Load Bot Personalities from Database
static void LoadBotPersonalityList()
{
    // Let's make sure our user has sourced the required sql file to add the new table
    QueryResult tableExists = CharacterDatabase.Query("SELECT * FROM information_schema.tables WHERE table_schema = 'acore_characters' AND table_name = 'mod_ollama_chat_personality' LIMIT 1");
    if (!tableExists)
    {
        LOG_ERROR("server.loading", "[Ollama Chat] Please source the required database table first");
        return;
    }

    QueryResult result = CharacterDatabase.Query("SELECT guid,personality FROM mod_ollama_chat_personality");

    if (!result)
    {
        return;
    }
    if (result->GetRowCount() == 0)
    {
        return;
    }    

    LOG_INFO("server.loading", "[Ollama Chat] Fetching Bot Personality List into array");

    do
    {
        uint32 personalityBotGUID = result->Fetch()[0].Get<uint64_t>();
        uint32 personalityBotType = result->Fetch()[1].Get<uint32>();
        botPersonalityList[personalityBotGUID] = personalityBotType;
    } while (result->NextRow());
}

// Callback for cURL write function.
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    std::string* responseBuffer = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    responseBuffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Pre-Load Ollama Model, Set Session and KeepAlive
static void PreLoadOllamaModel()
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        LOG_INFO("server.loading", "Failed to initialize cURL.");       
    }

    std::string url = g_OllamaUrl;
    std::string model = g_OllamaModel;
    std::string sessionCookie = "session=AzerothCoreSession"; // Replace with your actual session cookie value

    nlohmann::json requestData = {
       {"model",  model},
        {"keep_alive", -1}
    };
    std::string requestDataStr = requestData.dump();
    std::string responseBuffer;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIESESSION, 1L);
    curl_easy_setopt(curl, CURLOPT_COOKIE, sessionCookie.c_str()); 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestDataStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(requestDataStr.length()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK)
    {
        LOG_INFO("server.loading", "[Ollama Chat] Pre-Loaded Ollama Model: {}", g_OllamaModel);
    }
    else
    {
        LOG_INFO("server.loading", "[Ollama Chat] Unabled to Pre-Load Ollama Model or Set Cookie / TimeOut");
    }
}

void LoadOllamaChatConfig()
{
    g_SayDistance       = sConfigMgr->GetOption<float>("OllamaChat.SayDistance", 30.0f);
    g_YellDistance      = sConfigMgr->GetOption<float>("OllamaChat.YellDistance", 100.0f);
    g_GeneralDistance   = sConfigMgr->GetOption<float>("OllamaChat.GeneralDistance", 600.0f);
    g_PlayerReplyChance = sConfigMgr->GetOption<uint32_t>("OllamaChat.PlayerReplyChance", 90);
    g_BotReplyChance    = sConfigMgr->GetOption<uint32_t>("OllamaChat.BotReplyChance", 10);
    g_MaxBotsToPick     = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxBotsToPick", 2);
    g_OllamaUrl         = sConfigMgr->GetOption<std::string>("OllamaChat.Url", "http://localhost:11434/api/generate");
    g_OllamaModel       = sConfigMgr->GetOption<std::string>("OllamaChat.Model", "llama3.2:1b");

    // Load new configuration option for max concurrent queries.
    g_MaxConcurrentQueries = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxConcurrentQueries", 0);

    g_EnableRandomChatter             = sConfigMgr->GetOption<bool>("OllamaChat.EnableRandomChatter", true);
    g_MinRandomInterval               = sConfigMgr->GetOption<uint32_t>("OllamaChat.MinRandomInterval", 45);
    g_MaxRandomInterval               = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxRandomInterval", 180);
    g_RandomChatterRealPlayerDistance = sConfigMgr->GetOption<float>("OllamaChat.RandomChatterRealPlayerDistance", 40.0f);
    g_RandomChatterBotCommentChance   = sConfigMgr->GetOption<uint32_t>("OllamaChat.RandomChatterBotCommentChance", 25);
    
    g_MaxConcurrentQueries = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxConcurrentQueries", 0);

    g_EnableRPPersonalities = sConfigMgr->GetOption<bool>("OllamaChat.EnableRPPersonalities", false);

    // Load extra blacklist commands from config (comma-separated list)
    std::string extraBlacklist = sConfigMgr->GetOption<std::string>("OllamaChat.BlacklistCommands", "");
    if (!extraBlacklist.empty())
    {
        std::vector<std::string> extraList = SplitString(extraBlacklist, ',');
        for (const auto& cmd : extraList)
        {
            g_BlacklistCommands.push_back(cmd);
        }
    }

    g_queryManager.setMaxConcurrentQueries(g_MaxConcurrentQueries);

    LOG_INFO("server.loading",
             "[mod-ollama-chat] Config loaded: SayDistance = {}, YellDistance = {}, "
             "GeneralDistance = {}, PlayerReplyChance = {}%, BotReplyChance = {}%, MaxBotsToPick = {}, "
             "Url = {}, Model = {}, MaxConcurrentQueries = {}, EnableRandomChatter = {}, MinRandInt = {}, MaxRandInt = {}, RandomChatterRealPlayerDistance = {}, "
             "RandomChatterBotCommentChance = {}. MaxConcurrentQueries = {}. Extra blacklist commands: {}",
             g_SayDistance, g_YellDistance, g_GeneralDistance,
             g_PlayerReplyChance, g_BotReplyChance, g_MaxBotsToPick,
             g_OllamaUrl, g_OllamaModel, g_MaxConcurrentQueries,
             g_EnableRandomChatter, g_MinRandomInterval, g_MaxRandomInterval, g_RandomChatterRealPlayerDistance,
             g_RandomChatterBotCommentChance, g_MaxConcurrentQueries, extraBlacklist);
}

// Definition of the configuration WorldScript.
OllamaChatConfigWorldScript::OllamaChatConfigWorldScript() : WorldScript("OllamaChatConfigWorldScript") { }

void OllamaChatConfigWorldScript::OnStartup()
{
    curl_global_init(CURL_GLOBAL_ALL);
    LoadOllamaChatConfig();
    LoadBotPersonalityList();
    PreLoadOllamaModel();
}

// Unload Ollama model to free up memory use
void OllamaChatConfigWorldScript::OnShutdown()
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        LOG_INFO("server.loading", "Failed to initialize cURL.");
    }

    std::string url = g_OllamaUrl;
    std::string model = g_OllamaModel;
    std::string sessionCookie = "session=AzerothCoreSession"; // Replace with your actual session cookie value

    nlohmann::json requestData = {
       {"model",  model},
        {"keep_alive", 0} // setting keep_alive to 0 will unload the model immediately
    };
    std::string requestDataStr = requestData.dump();
    std::string responseBuffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());    
    curl_easy_setopt(curl, CURLOPT_COOKIE, sessionCookie.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestDataStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(requestDataStr.length()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK)
    {
        LOG_INFO("server.loading", "[Ollama Chat] UnLoaded Ollama Model: {}", g_OllamaModel);
    }
    else
    {
        LOG_INFO("server.loading", "[Ollama Chat] Unabled to UnLoad Ollama Model");
    }
}
