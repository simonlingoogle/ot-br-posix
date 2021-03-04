/*
 *    Copyright (c) 2017, The OpenThread Authors.
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
 *   This file implements MDNS service based on avahi.
 */

#include "mdns/mdns_avahi.hpp"

#include <algorithm>

#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/timeval.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "common/code_utils.hpp"
#include "common/logging.hpp"
#include "common/time.hpp"
#include "utils/strcpy_utils.hpp"

AvahiTimeout::AvahiTimeout(const struct timeval *aTimeout,
                           AvahiTimeoutCallback  aCallback,
                           void *                aContext,
                           void *                aPoller)
    : mCallback(aCallback)
    , mContext(aContext)
    , mPoller(aPoller)
{
    if (aTimeout)
    {
        mTimeout = otbr::GetNow() + otbr::GetTimestamp(*aTimeout);
    }
    else
    {
        mTimeout = 0;
    }
}

namespace otbr {

namespace Mdns {

Poller::Poller(void)
{
    mAvahiPoller.userdata         = this;
    mAvahiPoller.watch_new        = WatchNew;
    mAvahiPoller.watch_update     = WatchUpdate;
    mAvahiPoller.watch_get_events = WatchGetEvents;
    mAvahiPoller.watch_free       = WatchFree;

    mAvahiPoller.timeout_new    = TimeoutNew;
    mAvahiPoller.timeout_update = TimeoutUpdate;
    mAvahiPoller.timeout_free   = TimeoutFree;
}

AvahiWatch *Poller::WatchNew(const struct AvahiPoll *aPoller,
                             int                     aFd,
                             AvahiWatchEvent         aEvent,
                             AvahiWatchCallback      aCallback,
                             void *                  aContext)
{
    return reinterpret_cast<Poller *>(aPoller->userdata)->WatchNew(aFd, aEvent, aCallback, aContext);
}

AvahiWatch *Poller::WatchNew(int aFd, AvahiWatchEvent aEvent, AvahiWatchCallback aCallback, void *aContext)
{
    assert(aEvent && aCallback && aFd >= 0);

    mWatches.push_back(new AvahiWatch(aFd, aEvent, aCallback, aContext, this));

    return mWatches.back();
}

void Poller::WatchUpdate(AvahiWatch *aWatch, AvahiWatchEvent aEvent)
{
    aWatch->mEvents = aEvent;
}

AvahiWatchEvent Poller::WatchGetEvents(AvahiWatch *aWatch)
{
    return static_cast<AvahiWatchEvent>(aWatch->mHappened);
}

void Poller::WatchFree(AvahiWatch *aWatch)
{
    reinterpret_cast<Poller *>(aWatch->mPoller)->WatchFree(*aWatch);
}

void Poller::WatchFree(AvahiWatch &aWatch)
{
    for (Watches::iterator it = mWatches.begin(); it != mWatches.end(); ++it)
    {
        if (*it == &aWatch)
        {
            mWatches.erase(it);
            delete &aWatch;
            break;
        }
    }
}

AvahiTimeout *Poller::TimeoutNew(const AvahiPoll *     aPoller,
                                 const struct timeval *aTimeout,
                                 AvahiTimeoutCallback  aCallback,
                                 void *                aContext)
{
    assert(aPoller && aCallback);
    return static_cast<Poller *>(aPoller->userdata)->TimeoutNew(aTimeout, aCallback, aContext);
}

AvahiTimeout *Poller::TimeoutNew(const struct timeval *aTimeout, AvahiTimeoutCallback aCallback, void *aContext)
{
    mTimers.push_back(new AvahiTimeout(aTimeout, aCallback, aContext, this));
    return mTimers.back();
}

void Poller::TimeoutUpdate(AvahiTimeout *aTimer, const struct timeval *aTimeout)
{
    if (aTimeout == nullptr)
    {
        aTimer->mTimeout = 0;
    }
    else
    {
        aTimer->mTimeout = otbr::GetNow() + otbr::GetTimestamp(*aTimeout);
    }
}

void Poller::TimeoutFree(AvahiTimeout *aTimer)
{
    static_cast<Poller *>(aTimer->mPoller)->TimeoutFree(*aTimer);
}

void Poller::TimeoutFree(AvahiTimeout &aTimer)
{
    for (Timers::iterator it = mTimers.begin(); it != mTimers.end(); ++it)
    {
        if (*it == &aTimer)
        {
            mTimers.erase(it);
            delete &aTimer;
            break;
        }
    }
}

void Poller::UpdateFdSet(fd_set &aReadFdSet, fd_set &aWriteFdSet, fd_set &aErrorFdSet, int &aMaxFd, timeval &aTimeout)
{
    for (Watches::iterator it = mWatches.begin(); it != mWatches.end(); ++it)
    {
        int             fd     = (*it)->mFd;
        AvahiWatchEvent events = (*it)->mEvents;

        if (AVAHI_WATCH_IN & events)
        {
            FD_SET(fd, &aReadFdSet);
        }

        if (AVAHI_WATCH_OUT & events)
        {
            FD_SET(fd, &aWriteFdSet);
        }

        if (AVAHI_WATCH_ERR & events)
        {
            FD_SET(fd, &aErrorFdSet);
        }

        if (AVAHI_WATCH_HUP & events)
        {
            // TODO what do with this event type?
        }

        if (aMaxFd < fd)
        {
            aMaxFd = fd;
        }

        (*it)->mHappened = 0;
    }

    unsigned long now = GetNow();

    for (Timers::iterator it = mTimers.begin(); it != mTimers.end(); ++it)
    {
        unsigned long timeout = (*it)->mTimeout;

        if (timeout == 0)
        {
            continue;
        }

        if (static_cast<long>(timeout - now) <= 0)
        {
            aTimeout.tv_usec = 0;
            aTimeout.tv_sec  = 0;
            break;
        }
        else
        {
            time_t      sec;
            suseconds_t usec;

            timeout -= now;
            sec  = static_cast<time_t>(timeout / 1000);
            usec = static_cast<suseconds_t>((timeout % 1000) * 1000);

            if (sec < aTimeout.tv_sec)
            {
                aTimeout.tv_sec = sec;
            }
            else if (sec == aTimeout.tv_sec)
            {
                if (usec < aTimeout.tv_usec)
                {
                    aTimeout.tv_usec = usec;
                }
            }
        }
    }
}

void Poller::Process(const fd_set &aReadFdSet, const fd_set &aWriteFdSet, const fd_set &aErrorFdSet)
{
    unsigned long now = GetNow();

    for (Watches::iterator it = mWatches.begin(); it != mWatches.end(); ++it)
    {
        int             fd     = (*it)->mFd;
        AvahiWatchEvent events = (*it)->mEvents;

        (*it)->mHappened = 0;

        if ((AVAHI_WATCH_IN & events) && FD_ISSET(fd, &aReadFdSet))
        {
            (*it)->mHappened |= AVAHI_WATCH_IN;
        }

        if ((AVAHI_WATCH_OUT & events) && FD_ISSET(fd, &aWriteFdSet))
        {
            (*it)->mHappened |= AVAHI_WATCH_OUT;
        }

        if ((AVAHI_WATCH_ERR & events) && FD_ISSET(fd, &aErrorFdSet))
        {
            (*it)->mHappened |= AVAHI_WATCH_ERR;
        }

        // TODO hup events
        if ((*it)->mHappened)
        {
            (*it)->mCallback(*it, (*it)->mFd, static_cast<AvahiWatchEvent>((*it)->mHappened), (*it)->mContext);
        }
    }

    std::vector<AvahiTimeout *> expired;

    for (Timers::iterator it = mTimers.begin(); it != mTimers.end(); ++it)
    {
        if ((*it)->mTimeout == 0)
        {
            continue;
        }

        if (static_cast<long>((*it)->mTimeout - now) <= 0)
        {
            expired.push_back(*it);
        }
    }

    for (std::vector<AvahiTimeout *>::iterator it = expired.begin(); it != expired.end(); ++it)
    {
        AvahiTimeout *avahiTimeout = *it;
        avahiTimeout->mCallback(avahiTimeout, avahiTimeout->mContext);
    }
}

MdnsServiceAvahi::MdnsServiceAvahi(int aProtocol, const char *aDomain, StateHandler aHandler, void *aContext)
    : mClient(nullptr)
    , mProtocol(aProtocol == AF_INET6 ? AVAHI_PROTO_INET6
                                      : aProtocol == AF_INET ? AVAHI_PROTO_INET : AVAHI_PROTO_UNSPEC)
    , mDomain(aDomain)
    , mState(State::kIdle)
    , mStateHandler(aHandler)
    , mContext(aContext)
{
}

MdnsServiceAvahi::~MdnsServiceAvahi(void)
{
}

otbrError MdnsServiceAvahi::Start(void)
{
    otbrError error      = OTBR_ERROR_NONE;
    int       avahiError = 0;

    mClient = avahi_client_new(mPoller.GetAvahiPoll(), AVAHI_CLIENT_NO_FAIL, HandleClientState, this, &avahiError);

    if (avahiError)
    {
        otbrLog(OTBR_LOG_ERR, "Failed to create avahi client: %s!", avahi_strerror(avahiError));
        error = OTBR_ERROR_MDNS;
    }

    return error;
}

bool MdnsServiceAvahi::IsStarted(void) const
{
    return mClient != nullptr;
}

void MdnsServiceAvahi::Stop(void)
{
    FreeAllGroups();

    if (mClient)
    {
        avahi_client_free(mClient);
        mClient = nullptr;
        mState  = State::kIdle;
        mStateHandler(mContext, mState);
    }
}

void MdnsServiceAvahi::HandleClientState(AvahiClient *aClient, AvahiClientState aState, void *aContext)
{
    static_cast<MdnsServiceAvahi *>(aContext)->HandleClientState(aClient, aState);
}

void MdnsServiceAvahi::HandleGroupState(AvahiEntryGroup *aGroup, AvahiEntryGroupState aState, void *aContext)
{
    static_cast<MdnsServiceAvahi *>(aContext)->HandleGroupState(aGroup, aState);
}

void MdnsServiceAvahi::HandleGroupState(AvahiEntryGroup *aGroup, AvahiEntryGroupState aState)
{
    otbrLog(OTBR_LOG_INFO, "Avahi group change to state %d.", aState);

    /* Called whenever the entry group state changes */
    switch (aState)
    {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        /* The entry group has been established successfully */
        otbrLog(OTBR_LOG_INFO, "Group established.");
        CallHostOrServiceCallback(aGroup, OTBR_ERROR_NONE);
        break;

    case AVAHI_ENTRY_GROUP_COLLISION:
        otbrLog(OTBR_LOG_ERR, "Name collision!");
        CallHostOrServiceCallback(aGroup, OTBR_ERROR_DUPLICATED);
        break;

    case AVAHI_ENTRY_GROUP_FAILURE:
        /* Some kind of failure happened while we were registering our services */
        otbrLog(OTBR_LOG_ERR, "Group failed: %s!",
                avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(aGroup))));
        CallHostOrServiceCallback(aGroup, OTBR_ERROR_MDNS);
        break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
        otbrLog(OTBR_LOG_ERR, "Group ready.");
        break;

    default:
        assert(false);
        break;
    }
}

void MdnsServiceAvahi::CallHostOrServiceCallback(AvahiEntryGroup *aGroup, otbrError aError) const
{
    if (mHostHandler != nullptr)
    {
        const auto hostIt =
            std::find_if(mHosts.begin(), mHosts.end(), [aGroup](const Host &aHost) { return aHost.mGroup == aGroup; });

        if (hostIt != mHosts.end())
        {
            mHostHandler(hostIt->mHostName.c_str(), aError, mHostHandlerContext);
        }
    }

    if (mServiceHandler != nullptr)
    {
        const auto serviceIt = std::find_if(mServices.begin(), mServices.end(),
                                            [aGroup](const Service &aService) { return aService.mGroup == aGroup; });

        if (serviceIt != mServices.end())
        {
            mServiceHandler(serviceIt->mName.c_str(), serviceIt->mType.c_str(), aError, mServiceHandlerContext);
        }
    }
}

MdnsServiceAvahi::Hosts::iterator MdnsServiceAvahi::FindHost(const char *aHostName)
{
    assert(aHostName != nullptr);

    return std::find_if(mHosts.begin(), mHosts.end(),
                        [aHostName](const Host &aHost) { return aHost.mHostName == aHostName; });
}

otbrError MdnsServiceAvahi::CreateHost(AvahiClient &aClient, const char *aHostName, Hosts::iterator &aOutHostIt)
{
    assert(aHostName != nullptr);

    otbrError error = OTBR_ERROR_NONE;
    Host      newHost;

    newHost.mHostName = aHostName;
    SuccessOrExit(error = CreateGroup(aClient, newHost.mGroup));

    mHosts.push_back(newHost);
    aOutHostIt = mHosts.end() - 1;

exit:
    return error;
}

MdnsServiceAvahi::Services::iterator MdnsServiceAvahi::FindService(const char *aName, const char *aType)
{
    assert(aName != nullptr);
    assert(aType != nullptr);

    return std::find_if(mServices.begin(), mServices.end(), [aName, aType](const Service &aService) {
        return aService.mName == aName && aService.mType == aType;
    });
}

otbrError MdnsServiceAvahi::CreateService(AvahiClient &       aClient,
                                          const char *        aName,
                                          const char *        aType,
                                          Services::iterator &aOutServiceIt)
{
    assert(aName != nullptr);
    assert(aType != nullptr);

    otbrError error = OTBR_ERROR_NONE;
    Service   newService;

    newService.mName = aName;
    newService.mType = aType;
    SuccessOrExit(error = CreateGroup(aClient, newService.mGroup));

    mServices.push_back(newService);
    aOutServiceIt = mServices.end() - 1;

exit:
    return error;
}

otbrError MdnsServiceAvahi::CreateGroup(AvahiClient &aClient, AvahiEntryGroup *&aOutGroup)
{
    otbrError error = OTBR_ERROR_NONE;

    assert(aOutGroup == nullptr);

    aOutGroup = avahi_entry_group_new(&aClient, HandleGroupState, this);
    VerifyOrExit(aOutGroup != nullptr, error = OTBR_ERROR_MDNS);

exit:
    if (error == OTBR_ERROR_MDNS)
    {
        otbrLog(OTBR_LOG_ERR, "Failed to create entry group for avahi error: %s",
                avahi_strerror(avahi_client_errno(&aClient)));
    }

    return error;
}

otbrError MdnsServiceAvahi::ResetGroup(AvahiEntryGroup *aGroup)
{
    assert(aGroup != nullptr);

    otbrError error      = OTBR_ERROR_NONE;
    int       avahiError = avahi_entry_group_reset(aGroup);

    if (avahiError)
    {
        error = OTBR_ERROR_MDNS;
        otbrLog(OTBR_LOG_ERR, "Failed to reset entry group for avahi error: %s", avahi_strerror(avahiError));
    }

    return error;
}

otbrError MdnsServiceAvahi::FreeGroup(AvahiEntryGroup *aGroup)
{
    assert(aGroup != nullptr);

    otbrError error      = OTBR_ERROR_NONE;
    int       avahiError = avahi_entry_group_free(aGroup);

    if (avahiError)
    {
        error = OTBR_ERROR_MDNS;
        otbrLog(OTBR_LOG_ERR, "Failed to free entry group for avahi error: %s", avahi_strerror(avahiError));
    }

    return error;
}

void MdnsServiceAvahi::FreeAllGroups(void)
{
    for (Service &service : mServices)
    {
        FreeGroup(service.mGroup);
    }

    mServices.clear();

    for (Host &host : mHosts)
    {
        FreeGroup(host.mGroup);
    }

    mHosts.clear();
}

void MdnsServiceAvahi::HandleClientState(AvahiClient *aClient, AvahiClientState aState)
{
    otbrLog(OTBR_LOG_INFO, "Avahi client state changed to %d.", aState);
    switch (aState)
    {
    case AVAHI_CLIENT_S_RUNNING:
        /* The server has startup successfully and registered its host
         * name on the network, so it's time to create our services */
        otbrLog(OTBR_LOG_INFO, "Avahi client ready.");
        mState  = State::kReady;
        mClient = aClient;
        mStateHandler(mContext, mState);
        break;

    case AVAHI_CLIENT_FAILURE:
        otbrLog(OTBR_LOG_ERR, "Client failure: %s", avahi_strerror(avahi_client_errno(aClient)));
        mState = State::kIdle;
        mStateHandler(mContext, mState);
        break;

    case AVAHI_CLIENT_S_COLLISION:
        /* Let's drop our registered services. When the server is back
         * in AVAHI_SERVER_RUNNING state we will register them
         * again with the new host name. */
        otbrLog(OTBR_LOG_ERR, "Client collision: %s", avahi_strerror(avahi_client_errno(aClient)));

        // fall through

    case AVAHI_CLIENT_S_REGISTERING:
        /* The server records are now being established. This
         * might be caused by a host name change. We need to wait
         * for our own records to register until the host name is
         * properly esatblished. */
        FreeAllGroups();
        break;

    case AVAHI_CLIENT_CONNECTING:
        otbrLog(OTBR_LOG_DEBUG, "Connecting to avahi server");
        break;

    default:
        assert(false);
        break;
    }
}

void MdnsServiceAvahi::UpdateFdSet(fd_set & aReadFdSet,
                                   fd_set & aWriteFdSet,
                                   fd_set & aErrorFdSet,
                                   int &    aMaxFd,
                                   timeval &aTimeout)
{
    mPoller.UpdateFdSet(aReadFdSet, aWriteFdSet, aErrorFdSet, aMaxFd, aTimeout);
}

void MdnsServiceAvahi::Process(const fd_set &aReadFdSet, const fd_set &aWriteFdSet, const fd_set &aErrorFdSet)
{
    mPoller.Process(aReadFdSet, aWriteFdSet, aErrorFdSet);
}

otbrError MdnsServiceAvahi::PublishService(const char *   aHostName,
                                           uint16_t       aPort,
                                           const char *   aName,
                                           const char *   aType,
                                           const TxtList &aTxtList)
{
    otbrError          error        = OTBR_ERROR_NONE;
    int                avahiError   = 0;
    Services::iterator serviceIt    = mServices.end();
    const char *       safeHostName = (aHostName != nullptr) ? aHostName : "";
    const char *       logHostName  = (aHostName != nullptr) ? aHostName : "localhost";
    std::string        fullHostName;
    // aligned with AvahiStringList
    AvahiStringList  buffer[(kMaxSizeOfTxtRecord - 1) / sizeof(AvahiStringList) + 1];
    AvahiStringList *last = nullptr;
    AvahiStringList *curr = buffer;
    size_t           used = 0;

    VerifyOrExit(mState == State::kReady, errno = EAGAIN, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(mClient != nullptr, errno = EAGAIN, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(aName != nullptr, error = OTBR_ERROR_INVALID_ARGS);
    VerifyOrExit(aType != nullptr, error = OTBR_ERROR_INVALID_ARGS);

    if (aHostName != nullptr)
    {
        fullHostName = MakeFullName(aHostName);
        aHostName    = fullHostName.c_str();
    }

    for (const auto &txtEntry : aTxtList)
    {
        const char *   name        = txtEntry.mName;
        size_t         nameLength  = txtEntry.mNameLength;
        const uint8_t *value       = txtEntry.mValue;
        size_t         valueLength = txtEntry.mValueLength;
        // +1 for the size of "=", avahi doesn't need '\0' at the end of the entry
        size_t needed = sizeof(AvahiStringList) - sizeof(AvahiStringList::text) + nameLength + valueLength + 1;

        VerifyOrExit(used + needed <= sizeof(buffer), errno = EMSGSIZE, error = OTBR_ERROR_ERRNO);
        curr->next = last;
        last       = curr;
        memcpy(curr->text, name, nameLength);
        curr->text[nameLength] = '=';
        memcpy(curr->text + nameLength + 1, value, valueLength);
        curr->size = nameLength + valueLength + 1;
        {
            const uint8_t *next = curr->text + curr->size;
            curr                = OTBR_ALIGNED(next, AvahiStringList *);
        }
        used = static_cast<size_t>(reinterpret_cast<uint8_t *>(curr) - reinterpret_cast<uint8_t *>(buffer));
    }

    serviceIt = FindService(aName, aType);

    if (serviceIt == mServices.end())
    {
        SuccessOrExit(error = CreateService(*mClient, aName, aType, serviceIt));
    }
    else if (serviceIt->mHostName != safeHostName || serviceIt->mPort != aPort)
    {
        SuccessOrExit(error = ResetGroup(serviceIt->mGroup));
    }
    else
    {
        otbrLog(OTBR_LOG_INFO, "[mdns] update service %s.%s for host %s", aName, aType, logHostName);
        avahiError = avahi_entry_group_update_service_txt_strlst(serviceIt->mGroup, AVAHI_IF_UNSPEC, mProtocol,
                                                                 AvahiPublishFlags{}, aName, aType, mDomain, last);
        if (avahiError == 0 && mServiceHandler != nullptr)
        {
            // The handler should be called even if the request can be processed synchronously
            mServiceHandler(aName, aType, OTBR_ERROR_NONE, mServiceHandlerContext);
        }
        ExitNow();
    }

    otbrLog(OTBR_LOG_INFO, "[mdns] create service %s.%s for host %s", aName, aType, logHostName);
    avahiError =
        avahi_entry_group_add_service_strlst(serviceIt->mGroup, AVAHI_IF_UNSPEC, mProtocol, AvahiPublishFlags{}, aName,
                                             aType, mDomain, aHostName, aPort, last);
    SuccessOrExit(avahiError);

    otbrLog(OTBR_LOG_INFO, "[mdns] commit service %s.%s", aName, aType);
    avahiError = avahi_entry_group_commit(serviceIt->mGroup);
    SuccessOrExit(avahiError);

    serviceIt->mHostName = safeHostName;
    serviceIt->mPort     = aPort;

exit:

    if (avahiError)
    {
        error = OTBR_ERROR_MDNS;
        otbrLog(OTBR_LOG_ERR, "Failed to publish service for avahi error: %s!", avahi_strerror(avahiError));
    }
    else if (error != OTBR_ERROR_NONE)
    {
        otbrLog(OTBR_LOG_ERR, "Failed to publish service: %s!", otbrErrorString(error));
    }

    if (error != OTBR_ERROR_NONE && serviceIt != mServices.end())
    {
        FreeGroup(serviceIt->mGroup);
        mServices.erase(serviceIt);
    }

    return error;
}

otbrError MdnsServiceAvahi::UnpublishService(const char *aName, const char *aType)
{
    otbrError          error = OTBR_ERROR_NONE;
    Services::iterator serviceIt;

    VerifyOrExit(aName != nullptr, error = OTBR_ERROR_INVALID_ARGS);
    VerifyOrExit(aType != nullptr, error = OTBR_ERROR_INVALID_ARGS);

    serviceIt = FindService(aName, aType);
    VerifyOrExit(serviceIt != mServices.end());

    otbrLog(OTBR_LOG_INFO, "[mdns] unpublish service %s.%s", aName, aType);
    error = FreeGroup(serviceIt->mGroup);
    mServices.erase(serviceIt);

exit:
    return error;
}

otbrError MdnsServiceAvahi::PublishHost(const char *aName, const uint8_t *aAddress, uint8_t aAddressLength)
{
    otbrError       error      = OTBR_ERROR_NONE;
    int             avahiError = 0;
    Hosts::iterator hostIt     = mHosts.end();
    std::string     fullHostName;
    AvahiAddress    address;

    VerifyOrExit(mState == State::kReady, errno = EAGAIN, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(mClient != nullptr, errno = EAGAIN, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(aName != nullptr, error = OTBR_ERROR_INVALID_ARGS);
    VerifyOrExit(aAddress != nullptr, error = OTBR_ERROR_INVALID_ARGS);
    VerifyOrExit(aAddressLength == sizeof(address.data.ipv6.address), error = OTBR_ERROR_INVALID_ARGS);

    fullHostName = MakeFullName(aName);
    hostIt       = FindHost(aName);

    if (hostIt == mHosts.end())
    {
        SuccessOrExit(error = CreateHost(*mClient, aName, hostIt));
    }
    else if (memcmp(hostIt->mAddress.data.ipv6.address, aAddress, aAddressLength))
    {
        SuccessOrExit(error = ResetGroup(hostIt->mGroup));
    }
    else
    {
        if (mHostHandler != nullptr)
        {
            // The handler should be called even if the request can be processed synchronously
            mHostHandler(aName, OTBR_ERROR_NONE, mHostHandlerContext);
        }
        ExitNow();
    }

    address.proto = AVAHI_PROTO_INET6;
    memcpy(&address.data.ipv6.address[0], aAddress, aAddressLength);

    otbrLog(OTBR_LOG_INFO, "[mdns] create host %s", aName);
    avahiError = avahi_entry_group_add_address(hostIt->mGroup, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET6,
                                               AVAHI_PUBLISH_NO_REVERSE, fullHostName.c_str(), &address);
    SuccessOrExit(avahiError);

    otbrLog(OTBR_LOG_INFO, "[mdns] commit host %s", aName);
    avahiError = avahi_entry_group_commit(hostIt->mGroup);
    SuccessOrExit(avahiError);

    hostIt->mAddress = address;

exit:

    if (avahiError)
    {
        error = OTBR_ERROR_MDNS;
        otbrLog(OTBR_LOG_ERR, "Failed to publish host for avahi error: %s!", avahi_strerror(avahiError));
    }
    else if (error != OTBR_ERROR_NONE)
    {
        otbrLog(OTBR_LOG_ERR, "Failed to publish host: %s!", otbrErrorString(error));
    }

    if (error != OTBR_ERROR_NONE && hostIt != mHosts.end())
    {
        FreeGroup(hostIt->mGroup);
        mHosts.erase(hostIt);
    }

    return error;
}

otbrError MdnsServiceAvahi::UnpublishHost(const char *aName)
{
    otbrError       error = OTBR_ERROR_NONE;
    Hosts::iterator hostIt;

    VerifyOrExit(aName != nullptr, error = OTBR_ERROR_INVALID_ARGS);

    hostIt = FindHost(aName);
    VerifyOrExit(hostIt != mHosts.end());

    otbrLog(OTBR_LOG_INFO, "[mdns] delete host %s", aName);
    error = FreeGroup(hostIt->mGroup);
    mHosts.erase(hostIt);

exit:
    return error;
}

std::string MdnsServiceAvahi::MakeFullName(const char *aName)
{
    assert(aName != nullptr);

    std::string fullHostName(aName);

    fullHostName += '.';
    fullHostName += (mDomain == nullptr ? "local." : mDomain);

    return fullHostName;
}

MdnsService *MdnsService::Create(int aFamily, const char *aDomain, StateHandler aHandler, void *aContext)
{
    return new MdnsServiceAvahi(aFamily, aDomain, aHandler, aContext);
}

void MdnsService::Destroy(MdnsService *aMdnsService)
{
    delete static_cast<MdnsServiceAvahi *>(aMdnsService);
}

} // namespace Mdns

} // namespace otbr
