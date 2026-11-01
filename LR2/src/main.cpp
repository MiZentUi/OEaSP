#include "cli.h"
#include "file_io.h"

#include <iostream>
#include <string>

namespace {

constexpr int kOk = 0;
constexpr int kError = 1;    // runtime or format error
constexpr int kArgError = 2; // argument error

int run(const cli::Parsed& p) {
    switch (p.cmd) {
    case cli::Command::Help:
        std::cout << cli::usage();
        return kOk;

    case cli::Command::Create: {
        file_io::OpResult r = file_io::create(p.file);
        if (!r.ok) {
            std::cerr << "main: " << r.message << "\n";
            return kError;
        }
        std::cout << "created database: " << p.file << "\n";
        return kOk;
    }

    case cli::Command::Add: {
        file_io::Timer t;
        file_io::OpResult r = file_io::add(p.file, p.text);
        const double ms = t.elapsedMs();
        if (!r.ok) {
            std::cerr << "main: " << r.message << "\n";
            return kError;
        }
        std::cout << "added: " << r.message << "  elapsed_ms=" << ms << "\n";
        return kOk;
    }

    case cli::Command::List: {
        file_io::OpResult r = file_io::list(p.file, std::cout);
        if (!r.ok) {
            std::cerr << "main: " << r.message << "\n";
            return kError;
        }
        return kOk;
    }

    case cli::Command::Find: {
        file_io::Timer t;
        file_io::OpResult r = file_io::find(p.file, p.uuid);
        const double ms = t.elapsedMs();
        if (!r.ok) {
            std::cerr << "main: " << r.message << "\n";
            return kError;
        }
        std::cout << r.message << "  elapsed_ms=" << ms << "\n";
        return kOk;
    }

    case cli::Command::Generate: {
        file_io::OpResult r = file_io::generate(p.file, p.count, p.maxLen);
        if (!r.ok) {
            std::cerr << "main: " << r.message << "\n";
            return kError;
        }
        std::cout << r.message << "\n";
        return kOk;
    }

    case cli::Command::None:
    default:
        return kArgError;
    }
}

} // namespace

int main(int argc, char** argv) {
    const cli::Parsed p = cli::parse(argc, argv);
    if (!p.ok) {
        std::cerr << "main: " << p.error << "\n\n" << cli::usage();
        return kArgError;
    }
    return run(p);
}