#pragma once

#include <objbase.h>

#include "error.h"

class com_init
{
public:
    com_init()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        check_hr(hr, "CoInitializeEx");
        initialized_ = true;
    }

    ~com_init()
    {
        if (initialized_)
        {
            CoUninitialize();
        }
    }

    com_init(const com_init&) = delete;
    com_init& operator=(const com_init&) = delete;

private:
    bool initialized_{false};
};
