#include "pch.h"

using namespace plugin;

struct Main
{
    Main()
    {
        Events::initGameEvent += [] {
            // EyeFinder92SA init
        };
    }
} gInstance;
