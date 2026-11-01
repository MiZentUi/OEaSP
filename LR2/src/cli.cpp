#include "cli.h"

#include <regex>
#include <stdexcept>

namespace cli
{

namespace
{

// Canonical UUID form: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, hex with any
// letter case. An invalid UUID is an argument error (exit code 2).
bool isUuid(const std::string &s)
{
    static const std::regex re(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
        "[0-9a-fA-F]{12}$");
    return std::regex_match(s, re);
}

Parsed fail(Parsed p, std::string msg)
{
    p.ok = false;
    p.error = std::move(msg);
    return p;
}

}  // namespace

Parsed parse(int argc, char **argv)
{
    Parsed p;
    if (argc < 2)
    {
        return fail(p, "no command given");
    }

    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h")
    {
        p.cmd = Command::Help;
        return p;
    }

    if (cmd == "create")
    {
        if (argc != 3)
        {
            return fail(p, "create: a path is required: create <file>");
        }
        p.cmd = Command::Create;
        p.file = argv[2];
    }
    else if (cmd == "add")
    {
        if (argc != 4)
        {
            return fail(p,
                        "add: a path and text are required: add <file> <text>");
        }
        p.cmd = Command::Add;
        p.file = argv[2];
        p.text = argv[3];
    }
    else if (cmd == "list")
    {
        if (argc != 3)
        {
            return fail(p, "list: a path is required: list <file>");
        }
        p.cmd = Command::List;
        p.file = argv[2];
    }
    else if (cmd == "find")
    {
        if (argc != 4)
        {
            return fail(
                p, "find: a path and uuid are required: find <file> <uuid>");
        }
        if (!isUuid(argv[3]))
        {
            return fail(p, "find: invalid UUID: " + std::string(argv[3]));
        }
        p.cmd = Command::Find;
        p.file = argv[2];
        p.uuid = argv[3];
    }
    else if (cmd == "generate")
    {
        if (argc != 5)
        {
            return fail(p,
                        "generate: a path, count and maxLen are required: "
                        "generate <file> <count> <maxLen>");
        }
        std::uint64_t count = 0;
        std::uint64_t maxLen = 0;
        try
        {
            count = std::stoull(argv[3]);
            maxLen = std::stoull(argv[4]);
        }
        catch (const std::exception &)
        {
            return fail(p, "generate: count and maxLen must be numbers");
        }
        p.cmd = Command::Generate;
        p.file = argv[2];
        p.count = count;
        p.maxLen = maxLen;
    }
    else
    {
        return fail(p, "unknown command: " + cmd);
    }

    return p;
}

std::string usage()
{
    return "Usage:\n"
           "  main create <file>\n"
           "  main add <file> \"<text>\"\n"
           "  main list <file>\n"
           "  main find <file> <uuid>\n"
           "  main generate <file> <count> <maxLen>\n"
           "  main --help\n"
           "\n"
           "Commands:\n"
           "  create   create a new empty database (fails if the file exists)\n"
           "  add      append a record with a generated UUID and text\n"
           "  list     print all records\n"
           "  find     find a record by UUID\n"
           "  generate create a test database of count records\n"
           "\n"
           "Exit codes: 0 – success/not found, 1 – runtime or format error,\n"
           "2 – argument error.\n";
}

}  // namespace cli