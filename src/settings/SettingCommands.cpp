#include <core/sdk.hpp>
#include <core/cvwrapper.hpp>
#include <settings/Manager.hpp>
#include <init.hpp>
#include <settings/SettingsIO.hpp>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <MiscTemporary.hpp>
/*
  Created on 29.07.18.
*/

namespace settings::commands {

static void getAndSortAllConfigs();

static CatCommand cat("cat", "", [](const CCommand &args) {
    if (args.ArgC() < 3)
    {
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "Usage: cat <set/get> <variable> [value]\n");
        return;
    }

    auto variable = settings::Manager::instance().lookup(args.Arg(2));
    if (variable == nullptr)
    {
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "Variable not found: %s\n", args.Arg(2));
        return;
    }

    if (!strcmp(args.Arg(1), "set"))
    {
        if (args.ArgC() < 4)
        {
            g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "Usage: cat <set> <variable> <value>\n");
            return;
        }
        variable->fromString(args.Arg(3));
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "%s = \"%s\"\n", args.Arg(2), variable->toString().c_str());
        return;
    }
    else if (!strcmp(args.Arg(1), "get"))
    {
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "%s = \"%s\"\n", args.Arg(2), variable->toString().c_str());
        return;
    }
    else
    {
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "Usage: cat <set/get> <variable> <value>\n");
        return;
    }
});

static CatCommand save("save", "", [](const CCommand &args) {
    settings::SettingsWriter writer{ settings::Manager::instance() };

    DIR *config_directory = opendir(DATA_PATH "/configs");
    if (!config_directory)
    {
        logging::Info("Configs directory doesn't exist, creating one!");
        mkdir(DATA_PATH "/configs", S_IRWXU | S_IRWXG);
    }

    if (args.ArgC() == 1)
    {
        writer.saveTo(DATA_PATH "/configs/default.conf");
    }
    else
    {
        writer.saveTo(std::string(DATA_PATH "/configs/") + args.ArgS() + ".conf");
    }
    logging::Info("cat_save: Sorting configs...");
    getAndSortAllConfigs();
    logging::Info("cat_save: Closing dir...");
    closedir(config_directory);
});

static CatCommand load("load", "", [](const CCommand &args) {
    settings::SettingsReader loader{ settings::Manager::instance() };
    if (args.ArgC() == 1)
    {
        loader.loadFrom(DATA_PATH "/configs/default.conf");
    }
    else
    {
        std::string backup = args.ArgS();
        std::string ArgS   = backup;
        ArgS.erase(std::remove(ArgS.begin(), ArgS.end(), '\n'), ArgS.end());
        ArgS.erase(std::remove(ArgS.begin(), ArgS.end(), '\r'), ArgS.end());
        loader.loadFrom(std::string(DATA_PATH "/configs/") + ArgS + ".conf");
    }
});

static std::vector<std::string> sortedVariables{};

static void getAndSortAllVariables()
{
    for (auto &v : settings::Manager::instance().registered)
    {
        sortedVariables.push_back(v.first);
    }

    std::sort(sortedVariables.begin(), sortedVariables.end());

    logging::Info("Sorted %u variables\n", sortedVariables.size());
}

static std::vector<std::string> sortedConfigs{};

static void getAndSortAllConfigs()
{
    DIR *config_directory = opendir(DATA_PATH "/configs");
    if (!config_directory)
    {
        logging::Info("Config directoy does not exist.");
        closedir(config_directory);
        return;
    }
    sortedConfigs.clear();

    struct dirent *ent;
    while ((ent = readdir(config_directory)))
    {
        std::string s(ent->d_name);
        s = s.substr(0, s.find_last_of("."));
        sortedConfigs.push_back(s);
    }
    std::sort(sortedConfigs.begin(), sortedConfigs.end());
    sortedConfigs.erase(sortedConfigs.begin());
    sortedConfigs.erase(sortedConfigs.begin());

    closedir(config_directory);
    logging::Info("Sorted %u config files\n", sortedConfigs.size());
}

static CatCommand cat_find("find", "Find a command by name", [](const CCommand &args) {
    // We need arguments
    if (args.ArgC() < 2)
        return logging::Info("Usage: cat_find (name)");
    // Store all found rvars
    std::vector<std::string> found_rvars;
    for (const auto &s : sortedVariables)
    {
        // Store std::tolower'd rvar
        std::string lowered_str;
        for (auto &i : s)
            lowered_str += std::tolower(i);
        std::string to_find = args.Arg(1);
        // store rvar to find in lowercase too
        std::string to_find_lower;
        for (auto &s : to_find)
            to_find_lower += std::tolower(s);
        // If it matches then add to vector
        if (lowered_str.find(to_find_lower) != lowered_str.npos)
            found_rvars.push_back(s);
    }
    // Yes
    g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "Found rvars:\n");
    // Nothing found :C
    if (found_rvars.empty())
        g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "No rvars found.\n");
    // Found rvars
    else
        for (auto &s : found_rvars)
            g_ICvar->ConsoleColorPrintf(Color(*print_r, *print_g, *print_b, 255), "%s\n", s.c_str());
});

static int cat_completionCallback(const char *c_partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH])
{
    std::string partial = c_partial;
    std::array<std::string, 2> parts{};
    auto j    = 0u;
    auto f    = false;
    int count = 0;

    for (auto i = 0u; i < partial.size() && j < 3; ++i)
    {
        auto space = (bool) isspace(partial.at(i));
        if (!space)
        {
            if (j)
                parts.at(j - 1).push_back(partial[i]);
            f = true;
        }

        if (i == partial.size() - 1 || (f && space))
        {
            if (space)
                ++j;
            f = false;
        }
    }

    // "" -> cat [get, set]
    // "g" -> cat get
    // "get " -> cat get <variable>

    // logging::Info("%s|%s", parts.at(0).c_str(), parts.at(1).c_str());

    if (parts.at(0).empty() || parts.at(1).empty() && (!parts.at(0).empty() && partial.back() != ' '))
    {
        if (std::string("get").find(parts.at(0)) != std::string::npos)
            snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH, "cat get ");
        if (std::string("set").find(parts[0]) != std::string::npos)
            snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH, "cat set ");
        return count;
    }

    for (const auto &s : sortedVariables)
    {
        if (s.find(parts.at(1)) == 0)
        {
            auto variable = settings::Manager::instance().lookup(s);
            if (variable)
            {
                if (s.compare(parts.at(1)))
                    snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH - 1, "cat %s %s", parts.at(0).c_str(), s.c_str());
                else
                    snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH - 1, "cat %s %s %s", parts.at(0).c_str(), s.c_str(), variable->toString().c_str());
                if (count == COMMAND_COMPLETION_MAXITEMS)
                    break;
            }
        }
    }
    return count;
}

static int load_CompletionCallback(const char *c_partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH])
{
    std::string partial = c_partial;
    std::array<std::string, 2> parts{};
    auto j    = 0u;
    auto f    = false;
    int count = 0;

    for (auto i = 0u; i < partial.size() && j < 3; ++i)
    {
        auto space = (bool) isspace(partial.at(i));
        if (!space)
        {
            if (j)
                parts.at(j - 1).push_back(partial[i]);
            f = true;
        }

        if (i == partial.size() - 1 || (f && space))
        {
            if (space)
                ++j;
            f = false;
        }
    }

    for (const auto &s : sortedConfigs)
    {
        if (s.find(parts.at(0)) == 0)
        {
            snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH - 1, "cat_load %s", s.c_str());
            if (count == COMMAND_COMPLETION_MAXITEMS)
                break;
        }
    }
    return count;
}

static int save_CompletionCallback(const char *c_partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH])
{
    std::string partial = c_partial;
    std::array<std::string, 2> parts{};
    auto j    = 0u;
    auto f    = false;
    int count = 0;

    for (auto i = 0u; i < partial.size() && j < 3; ++i)
    {
        auto space = (bool) isspace(partial.at(i));
        if (!space)
        {
            if (j)
                parts.at(j - 1).push_back(partial[i]);
            f = true;
        }

        if (i == partial.size() - 1 || (f && space))
        {
            if (space)
                ++j;
            f = false;
        }
    }

    for (const auto &s : sortedConfigs)
    {
        if (s.find(parts.at(0)) == 0)
        {
            snprintf(commands[count++], COMMAND_COMPLETION_ITEM_LENGTH - 1, "cat_save %s", s.c_str());
            if (count == COMMAND_COMPLETION_MAXITEMS)
                break;
        }
    }
    return count;
}

static InitRoutine init([]() {
    getAndSortAllVariables();
    getAndSortAllConfigs();
    cat.cmd->m_bHasCompletionCallback  = true;
    cat.cmd->m_fnCompletionCallback    = cat_completionCallback;
    load.cmd->m_bHasCompletionCallback = true;
    load.cmd->m_fnCompletionCallback   = load_CompletionCallback;
    save.cmd->m_bHasCompletionCallback = true;
    save.cmd->m_fnCompletionCallback   = save_CompletionCallback;
});
}