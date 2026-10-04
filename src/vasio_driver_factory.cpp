// SPDX-License-Identifier: GPL-3.0-only
#include "vasio_com_driver.h"

const CLSID IID_ASIO_DRIVER = {0xa4d39126, 0x78cb, 0x4d89, {0x9e, 0x0a, 0x54, 0x49, 0x4d, 0x4f, 0x58, 0x56}};

CFactoryTemplate g_Templates[] = {
    {L"TimoxVasio", &IID_ASIO_DRIVER, VASIODriver::CreateInstance, nullptr}
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);
