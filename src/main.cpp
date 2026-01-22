/*
 * clap-cpp-validator: A re-implementation of the RUST clap validator
 * in c++
 *
 * Copyright 2026, various authors, as described in the GitHub
 * transaction log.
 *
 * This code is licensed under the MIT software licensed. It is
 * initiated by using Claude Sonnet to port the equivalent but
 * no longer actively developed RUST validator.
 *
 * All source in sst-filters available at
 * https://github.com/baconpaul/clap-cpp-validator
 */
#include "commands/list.h"
#include "commands/validate.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace clap_validator;

void printUsage(const char *programName)
{
    std::cout << "CLAP Plugin Validator\n\n";
    std::cout << "Usage: " << programName << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  validate <path>...   Validate one or more CLAP plugins\n";
    std::cout << "  list plugins         List all installed CLAP plugins\n";
    std::cout << "  list tests           List all available test cases\n";
    std::cout << "  help                 Show this help message\n\n";
    std::cout << "Validate options:\n";
    std::cout << "  --plugin-id <id>     Only test the plugin with the specified ID\n";
    std::cout << "  --test <pattern>     Only run tests matching the pattern (regex)\n";
    std::cout << "  --invert-filter      Invert the test filter\n";
    std::cout << "  --json               Output results as JSON\n";
    std::cout << "  --only-failed        Only show failed tests\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " validate /path/to/plugin.clap\n";
    std::cout << "  " << programName << " validate /path/to/plugin.clap --json\n";
    std::cout << "  " << programName << " list plugins\n";
    std::cout << "  " << programName << " list tests\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h")
    {
        printUsage(argv[0]);
        return 0;
    }

    if (command == "list")
    {
        if (argc < 3)
        {
            std::cerr << "Error: 'list' requires a subcommand (plugins, tests, presets)\n";
            return 1;
        }

        std::string subcommand = argv[2];
        bool json = false;

        // Check for --json flag
        for (int i = 3; i < argc; ++i)
        {
            if (strcmp(argv[i], "--json") == 0)
            {
                json = true;
            }
        }

        if (subcommand == "plugins")
        {
            return commands::listPlugins(json);
        }
        else if (subcommand == "tests")
        {
            return commands::listTests(json);
        }
        else if (subcommand == "presets")
        {
            return commands::listPresets(json, {});
        }
        else
        {
            std::cerr << "Error: Unknown list subcommand '" << subcommand << "'\n";
            return 1;
        }
    }

    if (command == "validate")
    {
        ValidatorSettings settings;

        // Parse arguments
        for (int i = 2; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--plugin-id" && i + 1 < argc)
            {
                settings.pluginId = argv[++i];
            }
            else if (arg == "--test" && i + 1 < argc)
            {
                settings.testFilter = argv[++i];
            }
            else if (arg == "--invert-filter")
            {
                settings.invertFilter = true;
            }
            else if (arg == "--json")
            {
                settings.json = true;
            }
            else if (arg == "--only-failed")
            {
                settings.onlyFailed = true;
            }
            else if (arg == "--in-process")
            {
                settings.inProcess = true;
            }
            else if (arg[0] != '-')
            {
                settings.paths.push_back(arg);
            }
            else
            {
                std::cerr << "Warning: Unknown option '" << arg << "'\n";
            }
        }

        if (settings.paths.empty())
        {
            std::cerr << "Error: No plugin paths specified\n";
            std::cerr << "Usage: " << argv[0] << " validate <path>...\n";
            return 1;
        }

        return commands::validate(settings);
    }

    std::cerr << "Error: Unknown command '" << command << "'\n";
    printUsage(argv[0]);
    return 1;
}
