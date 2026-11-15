// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "Common/VcaComp.h"

class Vca16T : public ltp::VcaComp<false>
{
public:
    ltp::Info getInfo() const override
    {
        auto i = VcaComp::getInfo();
        LTP_BASE_INFO (i)
        return i;
    }
};

LTP_REGISTER (Vca16T)
