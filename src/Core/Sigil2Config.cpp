#include "Sigil2Config.hpp"

auto Sigil2Config::registerBackend(ToolName name, Backend be) -> Sigil2Config&
{
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    beFactory.add(name, be.generator);
    beFactory.add(name, be.parser);
    beFactory.add(name, be.finish);
    return *this;
}


auto Sigil2Config::registerFrontend(ToolName name, FrontendStarter start) -> Sigil2Config&
{
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    feFactory.add(name, start);
    return *this;
}

auto Sigil2Config::parseCommandLine(int argc, char* argv[]) -> Sigil2Config&
{
    Sigil2Parser parser(argc, argv);

    _threads = parser.threads();

    std::string beName;
    std::vector<std::string> beArgs;
    std::tie(beName, beArgs) = parser.backend();
    _backend = beFactory.create(beName, beArgs);

    std::string feName;
    std::vector<std::string> feArgs;
    std::tie(feName, feArgs) = parser.frontend();
    _startFrontend = feFactory.create(feName,
                                      std::make_tuple(parser.executable(), feArgs, _threads));

    return *this;
}