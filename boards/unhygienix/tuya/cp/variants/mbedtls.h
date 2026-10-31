/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "sdkconfig.h"
#if !CONFIG_LWIP
#undef MBEDTLS_NET_C
#endif
