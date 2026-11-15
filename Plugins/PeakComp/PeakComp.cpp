// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "Common/ManualComp.h"

class PeakComp : public ltp::ManualComp<false>
{
public:
    ltp::Info getInfo() const override
    {
        auto i = ManualComp::getInfo();
        LTP_BASE_INFO (i)
        return i;
    }
};

LTP_REGISTER (PeakComp)
