/*
 *    Copyright (c) 2020, The OpenThread Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *    POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file includes definitions for Thread Backbone helper utilities.
 */

#ifndef BACKBONE_HELPER_HPP_
#define BACKBONE_HELPER_HPP_

#include <openthread/backbone_router_ftd.h>

#include "agent/ncp_openthread.hpp"

namespace otbr {

namespace Backbone {

/**
 * @addtogroup border-router-backbone
 *
 * @brief
 *   This module includes definitions for Thread Backbone helper utilities.
 *
 * @{
 */

/**
 * This class implements Thread Backbone helper utilities.
 *
 */
class BackboneHelper
{
public:
    enum
    {
        kSystemCommandMaxLength = 1024, ///< Max length of a system call command.
        kMaxLogLine             = 1024, ///< Max lenght of a log line.
    };

    /**
     * This method formats and outputs a log line within the "Backbone" region.
     *
     * @param[in] aLevel      The logging level.
     * @param[in] aSubRegion  The sub region within the "Backbone" region.
     * @param[in] aFormat     A pointer to the format string.
     * @param[in] ...         Arguments for the format specification.
     *
     */
    static void Log(int aLevel, const char *aSubRegion, const char *aFormat, ...);

    /**
     * This method formats and outputs a log line within the "Backbone" region.
     *
     * @param[in] aLevel      The logging level.
     * @param[in] aSubRegion  The sub region within the "Backbone" region.
     * @param[in] aFormat     A pointer to the format string.
     * @param[in] aArgs       A variable list of arguments for format.
     *
     */
    static void Logv(int aLevel, const char *aSubRegion, const char *aFormat, va_list aArgs);

    /**
     * This method formats a system command to execute.
     *
     * @param[in] aFormat  A pointer to the format string.
     * @param[in] ...      Arguments for the format specification.
     *
     * @returns The command exit code.
     *
     */
    static int SystemCommand(const char *aFormat, ...);

    /**
     * This method converts a Backbone Router State into a string.
     *
     * @param[in] aState  The Backbone Router State.
     *
     * @returns  A string representation of the Backbone Router State.
     *
     */
    static const char *BackboneRouterStateToString(otBackboneRouterState aState);
};

/**
 * @}
 */

} // namespace Backbone

} // namespace otbr

#endif // BACKBONE_HELPER_HPP_
