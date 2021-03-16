/*
 *    Copyright (c) 2021, The OpenThread Authors.
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
 *   The file implements the DNS-SD Discovery Proxy.
 */

#include "agent/discovery_proxy.hpp"

#include <algorithm>
#include <string>

#include <assert.h>

#include <openthread/dns.h>

#include "common/code_utils.hpp"
#include "common/dns_utils.hpp"
#include "common/logging.hpp"

#if OTBR_ENABLE_DNSSD_DISCOVERY_PROXY

namespace otbr {
namespace Dnssd {

DiscoveryProxy::DiscoveryProxy(Ncp::ControllerOpenThread &aNcp, Mdns::Publisher &aPublisher)
    : mNcp(aNcp)
    , mMdnsPublisher(aPublisher)
{
}

void DiscoveryProxy::Start(void)
{
    otbrLog(OTBR_LOG_INFO, "DiscoveryProxy::Start");

    otDnssdQuerySetCallbacks(mNcp.GetInstance(), this, &DiscoveryProxy::OnDiscoveryProxySubscribe,
                             &DiscoveryProxy::OnDiscoveryProxyUnsubscribe);
    mMdnsPublisher.SetDiscoveredServiceInstanceCallback(
        [this](const std::string &aType, const Mdns::Publisher::DiscoveredInstanceInfo &aInstanceInfo) {
            OnServiceDiscovered(aType, aInstanceInfo);
        });
}

void DiscoveryProxy::Stop(void)
{
    otbrLog(OTBR_LOG_INFO, "DiscoveryProxy::Stop");

    otDnssdQuerySetCallbacks(mNcp.GetInstance(), nullptr, nullptr, nullptr);
    mMdnsPublisher.SetDiscoveredServiceInstanceCallback(nullptr);
}

void DiscoveryProxy::OnDiscoveryProxySubscribe(void *aContext, const char *aFullName)
{
    reinterpret_cast<DiscoveryProxy *>(aContext)->OnDiscoveryProxySubscribe(aFullName);
}

void DiscoveryProxy::OnDiscoveryProxySubscribe(const char *aFullName)
{
    std::string                       fullName(aFullName);
    std::string                       instanceName, serviceName, domain;
    otbrError                         error = OTBR_ERROR_NONE;
    ServiceSubscriptionList::iterator it;
    DnsNameType                       nameType = GetDnsNameType(fullName);

    otbrLog(OTBR_LOG_INFO, "[dnssd discovery proxy] subscribe: %s", fullName.c_str());

    switch (nameType)
    {
    case kDnsNameTypeService:
        SuccessOrExit(error = SplitFullServiceName(fullName, serviceName, domain));
        break;
    case kDnsNameTypeInstance:
        SuccessOrExit(error = SplitFullServiceInstanceName(fullName, instanceName, serviceName, domain));
        break;
    default:
        ExitNow(error = OTBR_ERROR_NOT_IMPLEMENTED);
    }

    it = std::find_if(mSubscribedServices.begin(), mSubscribedServices.end(), [&](const ServiceSubscription &aService) {
        return aService.mInstanceName == instanceName && aService.mServiceName == serviceName &&
               aService.mDomain == domain;
    });

    VerifyOrExit(it == mSubscribedServices.end(), it->mSubscriptionCount++);

    mSubscribedServices.emplace_back(instanceName, serviceName, domain);

    otbrLog(OTBR_LOG_DEBUG, "[dnssd discovery proxy] service subscriptions: %sx%d",
            mSubscribedServices.back().ToString().c_str(), mSubscribedServices.back().mSubscriptionCount);

    if (GetServiceSubscriptionCount(instanceName, serviceName) == 1)
    {
        mMdnsPublisher.SubscribeService(serviceName, instanceName);
    }

exit:
    if (error != OTBR_ERROR_NONE)
    {
        otbrLog(OTBR_LOG_WARNING, "[dnssd discovery proxy] failed to subscribe %s: %s", fullName.c_str(),
                otbrErrorString(error));
    }
}

void DiscoveryProxy::OnDiscoveryProxyUnsubscribe(void *aContext, const char *aFullName)
{
    reinterpret_cast<DiscoveryProxy *>(aContext)->OnDiscoveryProxyUnsubscribe(aFullName);
}

void DiscoveryProxy::OnDiscoveryProxyUnsubscribe(const char *aFullName)
{
    std::string                       fullName(aFullName);
    std::string                       instanceName, serviceName, hostName, domain;
    otbrError                         error = OTBR_ERROR_NONE;
    ServiceSubscriptionList::iterator it;
    DnsNameType                       nameType = GetDnsNameType(fullName);

    otbrLog(OTBR_LOG_INFO, "[dnssd discovery proxy] unsubscribe: %s", fullName.c_str());

    switch (nameType)
    {
    case kDnsNameTypeService:
        SuccessOrExit(error = SplitFullServiceName(fullName, serviceName, domain));
        break;
    case kDnsNameTypeInstance:
        SuccessOrExit(error = SplitFullServiceInstanceName(fullName, instanceName, serviceName, domain));
        break;
    case kDnsNameTypeHost:
        SuccessOrExit(error = SplitFullHostName(fullName, hostName, domain));
        break;
    default:
        ExitNow(error = OTBR_ERROR_NOT_IMPLEMENTED);
    }

    it = std::find_if(mSubscribedServices.begin(), mSubscribedServices.end(), [&](const ServiceSubscription &aService) {
        return aService.mInstanceName == instanceName && aService.mServiceName == serviceName &&
               aService.mDomain == domain;
    });

    VerifyOrExit(it != mSubscribedServices.end(), error = OTBR_ERROR_NOT_FOUND);

    it->mSubscriptionCount--;
    assert(it->mSubscriptionCount >= 0);

    if (it->mSubscriptionCount == 0)
    {
        mSubscribedServices.erase(it);
    }

    otbrLog(OTBR_LOG_DEBUG, "[dnssd discovery proxy] service subscriptions: %sx%d", it->ToString().c_str(),
            it->mSubscriptionCount);

    if (GetServiceSubscriptionCount(instanceName, serviceName) == 0)
    {
        mMdnsPublisher.UnsubscribeService(serviceName, instanceName);
    }

exit:
    if (error != OTBR_ERROR_NONE)
    {
        otbrLog(OTBR_LOG_WARNING, "[dnssd discovery proxy] failed to unsubscribe service %s.%s: %s",
                instanceName.empty() ? "ANY" : instanceName.c_str(), aFullName, otbrErrorString(error));
    }
}

void DiscoveryProxy::OnServiceDiscovered(const std::string &                            aType,
                                         const Mdns::Publisher::DiscoveredInstanceInfo &aInstanceInfo)
{
    otbrLog(OTBR_LOG_INFO,
            "[dnssd discovery proxy] service discovered: %s, instance %s hostname %s address %s:%d priority %d "
            "weight %d",
            aType.c_str(), aInstanceInfo.mName.c_str(), aInstanceInfo.mHostName.c_str(),
            aInstanceInfo.mAddress.ToString().c_str(), aInstanceInfo.mPort, aInstanceInfo.mPriority,
            aInstanceInfo.mWeight);

    otDnssdServiceInstanceInfo instanceInfo;

    CheckServiceNameSanity(aType);
    CheckHostnameSanity(aInstanceInfo.mHostName);

    instanceInfo.mAddress   = *reinterpret_cast<const otIp6Address *>(&aInstanceInfo.mAddress);
    instanceInfo.mPort      = aInstanceInfo.mPort;
    instanceInfo.mPriority  = aInstanceInfo.mPriority;
    instanceInfo.mWeight    = aInstanceInfo.mWeight;
    instanceInfo.mTxtLength = static_cast<uint16_t>(aInstanceInfo.mTxtData.size());
    instanceInfo.mTxtData   = aInstanceInfo.mTxtData.data();
    instanceInfo.mTtl       = std::min(aInstanceInfo.mTtl, static_cast<uint32_t>(kServiceTtlCapLimit));

    std::for_each(mSubscribedServices.begin(), mSubscribedServices.end(), [&](const ServiceSubscription &aService) {
        std::string serviceFullName  = aType + "." + aService.mDomain;
        std::string hostName         = TranslateDomain(aInstanceInfo.mHostName, aService.mDomain);
        std::string instanceFullName = aInstanceInfo.mName + "." + serviceFullName;

        instanceInfo.mFullName = instanceFullName.c_str();
        instanceInfo.mHostName = hostName.c_str();

        otDnssdQueryNotifyServiceInstance(mNcp.GetInstance(), serviceFullName.c_str(), &instanceInfo);
    });
}

std::string DiscoveryProxy::TranslateDomain(const std::string &aName, const std::string &aTargetDomain)
{
    std::string targetName;
    std::string hostName, domain;

    VerifyOrExit(OTBR_ERROR_NONE == SplitFullHostName(aName, hostName, domain), targetName = aName);
    VerifyOrExit(domain == "local.", targetName = aName);

    targetName = hostName + "." + aTargetDomain;

exit:
    otbrLog(OTBR_LOG_DEBUG, "Translate domain: %s => %s", aName.c_str(), targetName.c_str());
    return targetName;
}

int DiscoveryProxy::GetServiceSubscriptionCount(const std::string &aInstanceName, const std::string &aServiceName)
{
    return std::accumulate(mSubscribedServices.begin(), mSubscribedServices.end(), 0,
                           [&](int aAccum, const ServiceSubscription &aService) {
                               return aAccum +
                                      (aService.mInstanceName == aInstanceName && aService.mServiceName == aServiceName
                                           ? aService.mSubscriptionCount
                                           : 0);
                           });
}

void DiscoveryProxy::CheckServiceNameSanity(const std::string &aType)
{
    size_t dotpos;

    OTBR_UNUSED_VARIABLE(aType);
    OTBR_UNUSED_VARIABLE(dotpos);

    assert(aType.length() > 0);
    assert(aType[aType.length() - 1] != '.');
    dotpos = aType.find_first_of('.');
    assert(dotpos != std::string::npos);
    assert(dotpos == aType.find_last_of('.'));
}

void DiscoveryProxy::CheckHostnameSanity(const std::string &aHostName)
{
    OTBR_UNUSED_VARIABLE(aHostName);

    assert(aHostName.length() > 0);
    assert(aHostName[aHostName.length() - 1] == '.');
}

} // namespace Dnssd
} // namespace otbr

#endif // OTBR_ENABLE_DNSSD_DISCOVERY_PROXY
