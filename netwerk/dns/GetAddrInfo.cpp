/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GetAddrInfo.h"

#ifdef DNSQUERY_AVAILABLE
// There is a bug in windns.h where the type of parameter ppQueryResultsSet for
// DnsQuery_A is dependent on UNICODE being set. It should *always* be
// PDNS_RECORDA, but if UNICODE is set it is PDNS_RECORDW. To get around this
// we make sure that UNICODE is unset.
#  undef UNICODE
#  include <ws2tcpip.h>
#  undef GetAddrInfo
#  include <windns.h>
#endif  // DNSQUERY_AVAILABLE

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/net/DNS.h"
#include "NativeDNSResolverOverrideParent.h"
#include "prnetdb.h"
#include "nsIOService.h"
#include "nsHostResolver.h"
#include "nsError.h"
#include "mozilla/net/DNS.h"
#include <algorithm>
#include "prerror.h"

#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"
#include "nsINetworkLinkService.h"

namespace mozilla::net {

static StaticRefPtr<NativeDNSResolverOverride> gOverrideService;

LazyLogModule gGetAddrInfoLog("GetAddrInfo");
#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))
#define LOG_WARNING(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Warning, ("[DNS]: " msg, ##__VA_ARGS__))

#ifdef DNSQUERY_AVAILABLE

#  define COMPUTER_NAME_BUFFER_SIZE 100
static char sDNSComputerName[COMPUTER_NAME_BUFFER_SIZE];
static char sNETBIOSComputerName[MAX_COMPUTERNAME_LENGTH + 1];

////////////////////////////
// WINDOWS IMPLEMENTATION //
////////////////////////////

// Ensure consistency of PR_* and AF_* constants to allow for legacy usage of
// PR_* constants with this API.
static_assert(PR_AF_INET == AF_INET && PR_AF_INET6 == AF_INET6 &&
                  PR_AF_UNSPEC == AF_UNSPEC,
              "PR_AF_* must match AF_*");

// If successful, returns in aResult a TTL value that is smaller or
// equal with the one already there. Gets the TTL value by calling
// to DnsQuery_A and iterating through the returned
// records to find the one with the smallest TTL value.
static MOZ_ALWAYS_INLINE nsresult _CallDnsQuery_A_Windows(
    const nsACString& aHost, uint16_t aAddressFamily, DWORD aFlags,
    std::function<void(PDNS_RECORDA)> aCallback) {
  NS_ConvertASCIItoUTF16 name(aHost);

  auto callDnsQuery_A = [&](uint16_t reqFamily) {
    PDNS_RECORDA dnsData = nullptr;
    DNS_STATUS status = DnsQuery_A(aHost.BeginReading(), reqFamily, aFlags,
                                   nullptr, &dnsData, nullptr);
    if (status == DNS_INFO_NO_RECORDS || status == DNS_ERROR_RCODE_NAME_ERROR ||
        !dnsData) {
      LOG("No DNS records found for %s. status=%lX. reqFamily = %X\n",
          aHost.BeginReading(), status, reqFamily);
      return NS_ERROR_FAILURE;
    } else if (status != NOERROR) {
      LOG_WARNING("DnsQuery_A failed with status %lX.\n", status);
      return NS_ERROR_UNEXPECTED;
    }

    for (PDNS_RECORDA curRecord = dnsData; curRecord;
         curRecord = curRecord->pNext) {
      // Only records in the answer section are important
      if (curRecord->Flags.S.Section != DnsSectionAnswer) {
        continue;
      }
      if (curRecord->wType != reqFamily) {
        continue;
      }

      aCallback(curRecord);
    }

    DnsFree(dnsData, DNS_FREE_TYPE::DnsFreeRecordList);
    return NS_OK;
  };

  if (aAddressFamily == PR_AF_UNSPEC || aAddressFamily == PR_AF_INET) {
    callDnsQuery_A(DNS_TYPE_A);
  }

  if (aAddressFamily == PR_AF_UNSPEC || aAddressFamily == PR_AF_INET6) {
    callDnsQuery_A(DNS_TYPE_AAAA);
  }
  return NS_OK;
}

bool recordTypeMatchesRequest(uint16_t wType, uint16_t aAddressFamily) {
  if (aAddressFamily == PR_AF_UNSPEC) {
    return wType == DNS_TYPE_A || wType == DNS_TYPE_AAAA;
  }
  if (aAddressFamily == PR_AF_INET) {
    return wType == DNS_TYPE_A;
  }
  if (aAddressFamily == PR_AF_INET6) {
    return wType == DNS_TYPE_AAAA;
  }
  return false;
}

static MOZ_ALWAYS_INLINE nsresult _GetTTLData_Windows(const nsACString& aHost,
                                                      uint32_t* aResult,
                                                      uint16_t aAddressFamily) {
  MOZ_ASSERT(!aHost.IsEmpty());
  MOZ_ASSERT(aResult);
  if (aAddressFamily != PR_AF_UNSPEC && aAddressFamily != PR_AF_INET &&
      aAddressFamily != PR_AF_INET6) {
    return NS_ERROR_UNEXPECTED;
  }

  // In order to avoid using ANY records which are not always implemented as a
  // "Gimme what you have" request in hostname resolvers, we should send A
  // and/or AAAA requests, based on the address family requested.
  const DWORD ttlFlags =
      (DNS_QUERY_STANDARD | DNS_QUERY_NO_NETBT | DNS_QUERY_NO_HOSTS_FILE |
       DNS_QUERY_NO_MULTICAST | DNS_QUERY_ACCEPT_TRUNCATED_RESPONSE |
       DNS_QUERY_DONT_RESET_TTL_VALUES);
  unsigned int ttl = (unsigned int)-1;
  _CallDnsQuery_A_Windows(
      aHost, aAddressFamily, ttlFlags,
      [&ttl, &aHost, aAddressFamily](PDNS_RECORDA curRecord) {
        if (recordTypeMatchesRequest(curRecord->wType, aAddressFamily)) {
          ttl = std::min<unsigned int>(ttl, curRecord->dwTtl);
        } else {
          LOG("Received unexpected record type %u in response for %s.\n",
              curRecord->wType, aHost.BeginReading());
        }
      });

  if (ttl == (unsigned int)-1) {
    LOG("No useable TTL found.");
    return NS_ERROR_FAILURE;
  }

  *aResult = ttl;
  return NS_OK;
}

static MOZ_ALWAYS_INLINE nsresult
_DNSQuery_A_SingleLabel(const nsACString& aCanonHost, uint16_t aAddressFamily,
                        uint16_t aFlags, AddrInfo** aAddrInfo) {
  bool setCanonName = aFlags & nsIDNSService::RESOLVE_CANONICAL_NAME;
  nsAutoCString canonName;
  const DWORD flags = (DNS_QUERY_STANDARD | DNS_QUERY_NO_MULTICAST |
                       DNS_QUERY_ACCEPT_TRUNCATED_RESPONSE);
  nsTArray<NetAddr> addresses;

  _CallDnsQuery_A_Windows(
      aCanonHost, aAddressFamily, flags, [&](PDNS_RECORDA curRecord) {
        MOZ_DIAGNOSTIC_ASSERT(curRecord->wType == DNS_TYPE_A ||
                              curRecord->wType == DNS_TYPE_AAAA);
        if (setCanonName) {
          canonName.Assign(curRecord->pName);
        }
        NetAddr addr{};
        addr.inet.family = AF_INET;
        addr.inet.ip = curRecord->Data.A.IpAddress;
        addresses.AppendElement(addr);
      });

  LOG("Query for: %s has %zu results", aCanonHost.BeginReading(),
      addresses.Length());
  if (addresses.IsEmpty()) {
    return NS_ERROR_UNKNOWN_HOST;
  }
  RefPtr<AddrInfo> ai(new AddrInfo(
      aCanonHost, canonName, DNSResolverType::Native, 0, std::move(addresses)));
  ai.forget(aAddrInfo);

  return NS_OK;
}

#endif

////////////////////////////////////
// PORTABLE RUNTIME IMPLEMENTATION//
////////////////////////////////////

static bool SkipIPv6DNSLookup() {
#if defined(XP_WIN) || defined(XP_LINUX) || defined(XP_MACOSX)
  return StaticPrefs::network_dns_skip_ipv6_when_no_addresses() &&
         !nsINetworkLinkService::HasNonLocalIPv6Address();
#else
  return false;
#endif
}

static MOZ_ALWAYS_INLINE nsresult
_GetAddrInfo_Portable(const nsACString& aCanonHost, uint16_t aAddressFamily,
                      nsIDNSService::DNSFlags aFlags, AddrInfo** aAddrInfo) {
  MOZ_ASSERT(!aCanonHost.IsEmpty());
  MOZ_ASSERT(aAddrInfo);

  // We accept the same aFlags that nsHostResolver::ResolveHost accepts, but we
  // need to translate the aFlags into a form that PR_GetAddrInfoByName
  // accepts.
  int prFlags = PR_AI_ADDRCONFIG;
  if (!(aFlags & nsIDNSService::RESOLVE_CANONICAL_NAME)) {
    prFlags |= PR_AI_NOCANONNAME;
  }

  // We need to remove IPv4 records manually because PR_GetAddrInfoByName
  // doesn't support PR_AF_INET6.
  bool disableIPv4 = aAddressFamily == PR_AF_INET6;
  if (disableIPv4) {
    aAddressFamily = PR_AF_UNSPEC;
  }

  if (SkipIPv6DNSLookup()) {
    // If the family was AF_UNSPEC initially, make it AF_INET
    // when there are no IPv6 addresses.
    // If the DNS request specified IPv6 specifically, let it
    // go through.
    if (aAddressFamily == PR_AF_UNSPEC && !disableIPv4) {
      aAddressFamily = PR_AF_INET;
    }
  }

#if defined(DNSQUERY_AVAILABLE)
  if (StaticPrefs::network_dns_dns_query_single_label() &&
      !aCanonHost.Contains('.') && aCanonHost != "localhost"_ns) {
    // For some reason we can't use DnsQuery_A to get the computer's IP.
    if (!aCanonHost.Equals(nsDependentCString(sDNSComputerName),
                           nsCaseInsensitiveCStringComparator) &&
        !aCanonHost.Equals(nsDependentCString(sNETBIOSComputerName),
                           nsCaseInsensitiveCStringComparator)) {
      // This is a single label name resolve without a dot.
      // We use DNSQuery_A for these.
      LOG("Resolving %s using DnsQuery_A (computername: %s)\n",
          aCanonHost.BeginReading(), sDNSComputerName);
      return _DNSQuery_A_SingleLabel(aCanonHost, aAddressFamily, aFlags,
                                     aAddrInfo);
    }
  }
#endif

  LOG("Resolving %s using PR_GetAddrInfoByName", aCanonHost.BeginReading());
  PRAddrInfo* prai =
      PR_GetAddrInfoByName(aCanonHost.BeginReading(), aAddressFamily, prFlags);

  if (!prai) {
    LOG("PR_GetAddrInfoByName returned null PR_GetError:%d PR_GetOSErrpr:%d",
        PR_GetError(), PR_GetOSError());
    return NS_ERROR_UNKNOWN_HOST;
  }

  nsAutoCString canonName;
  if (aFlags & nsIDNSService::RESOLVE_CANONICAL_NAME) {
    canonName.Assign(PR_GetCanonNameFromAddrInfo(prai));
  }

  bool filterNameCollision =
      !(aFlags & nsIDNSService::RESOLVE_ALLOW_NAME_COLLISION);
  RefPtr<AddrInfo> ai(new AddrInfo(aCanonHost, prai, disableIPv4,
                                   filterNameCollision, canonName));
  PR_FreeAddrInfo(prai);
  if (ai->Addresses().IsEmpty()) {
    LOG("PR_GetAddrInfoByName returned empty address list");
    return NS_ERROR_UNKNOWN_HOST;
  }

  ai.forget(aAddrInfo);

  LOG("PR_GetAddrInfoByName resolved successfully");
  return NS_OK;
}

//////////////////////////////////////
// COMMON/PLATFORM INDEPENDENT CODE //
//////////////////////////////////////
nsresult GetAddrInfoInit() {
  LOG("Initializing GetAddrInfo.\n");

#ifdef DNSQUERY_AVAILABLE
  DWORD namesize = COMPUTER_NAME_BUFFER_SIZE;
  if (!GetComputerNameExA(ComputerNameDnsHostname, sDNSComputerName,
                          &namesize)) {
    sDNSComputerName[0] = 0;
  }
  namesize = MAX_COMPUTERNAME_LENGTH + 1;
  if (!GetComputerNameExA(ComputerNameNetBIOS, sNETBIOSComputerName,
                          &namesize)) {
    sNETBIOSComputerName[0] = 0;
  }
#endif
  return NS_OK;
}

nsresult GetAddrInfoShutdown() {
  LOG("Shutting down GetAddrInfo.\n");
  return NS_OK;
}

bool FindAddrOverride(const nsACString& aHost, uint16_t aAddressFamily,
                      nsIDNSService::DNSFlags aFlags, AddrInfo** aAddrInfo) {
  RefPtr<NativeDNSResolverOverride> overrideService = gOverrideService;
  if (!overrideService) {
    return false;
  }
  AutoReadLock lock(overrideService->mLock);
  auto overrides = overrideService->mOverrides.Lookup(aHost);
  if (!overrides) {
    return false;
  }
  nsCString* cname = nullptr;
  if (aFlags & nsIDNSService::RESOLVE_CANONICAL_NAME) {
    cname = overrideService->mCnames.Lookup(aHost).DataPtrOrNull();
  }

  RefPtr<AddrInfo> ai;

  nsTArray<NetAddr> addresses;
  for (const auto& ip : *overrides) {
    if (aAddressFamily != AF_UNSPEC && ip.raw.family != aAddressFamily) {
      continue;
    }
    addresses.AppendElement(ip);
  }

  if (!cname) {
    ai = new AddrInfo(aHost, DNSResolverType::Native, 0, std::move(addresses));
  } else {
    ai = new AddrInfo(aHost, *cname, DNSResolverType::Native, 0,
                      std::move(addresses));
  }

  ai.forget(aAddrInfo);
  return true;
}

nsresult GetAddrInfo(const nsACString& aHost, uint16_t aAddressFamily,
                     nsIDNSService::DNSFlags aFlags, AddrInfo** aAddrInfo,
                     bool aGetTtl) {
  if (NS_WARN_IF(aHost.IsEmpty()) || NS_WARN_IF(!aAddrInfo)) {
    return NS_ERROR_NULL_POINTER;
  }
  *aAddrInfo = nullptr;

  if (StaticPrefs::network_dns_disabled()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

#ifdef DNSQUERY_AVAILABLE
  // The GetTTLData needs the canonical name to function properly
  if (aGetTtl) {
    aFlags |= nsIDNSService::RESOLVE_CANONICAL_NAME;
  }
#endif

  // If there is an override for this host, then we synthetize a result.
  if (gOverrideService &&
      FindAddrOverride(aHost, aAddressFamily, aFlags, aAddrInfo)) {
    LOG("Returning IP address from NativeDNSResolverOverride");
    return (*aAddrInfo)->Addresses().Length() ? NS_OK : NS_ERROR_UNKNOWN_HOST;
  }

  nsAutoCString host;
  if (StaticPrefs::network_dns_copy_string_before_call()) {
    host = Substring(aHost.BeginReading(), aHost.Length());
    MOZ_ASSERT(aHost.BeginReading() != host.BeginReading());
  } else {
    host = aHost;
  }

  if (StaticPrefs::network_dns_native_is_localhost()) {
    // pretend we use the given host but use IPv4 localhost instead!
    host = "localhost"_ns;
    aAddressFamily = PR_AF_INET;
  }

  RefPtr<AddrInfo> info;
  nsresult rv =
      _GetAddrInfo_Portable(host, aAddressFamily, aFlags, getter_AddRefs(info));

#ifdef DNSQUERY_AVAILABLE
  if (aGetTtl && NS_SUCCEEDED(rv)) {
    // Figure out the canonical name, or if that fails, just use the host name
    // we have.
    nsAutoCString name;
    if (info && !info->CanonicalHostname().IsEmpty()) {
      name = info->CanonicalHostname();
    } else {
      name = host;
    }

    LOG("Getting TTL for %s (cname = %s).", host.get(), name.get());
    uint32_t ttl = 0;
    nsresult ttlRv = _GetTTLData_Windows(name, &ttl, aAddressFamily);
    if (NS_SUCCEEDED(ttlRv)) {
      auto builder = info->Build();
      builder.SetTTL(ttl);
      info = builder.Finish();
      LOG("Got TTL %u for %s (name = %s).", ttl, host.get(), name.get());
    } else {
      LOG_WARNING("Could not get TTL for %s (cname = %s).", host.get(),
                  name.get());
    }
  }
#endif

  info.forget(aAddrInfo);
  return rv;
}

bool FindHTTPSRecordOverride(const nsACString& aHost,
                             TypeRecordResultType& aResult) {
  LOG("FindHTTPSRecordOverride aHost=%s", nsCString(aHost).get());
  RefPtr<NativeDNSResolverOverride> overrideService = gOverrideService;
  if (!overrideService) {
    return false;
  }

  AutoReadLock lock(overrideService->mLock);
  auto overrides = overrideService->mHTTPSRecordOverrides.Lookup(aHost);
  if (!overrides) {
    return false;
  }

  DNSPacket packet;
  nsAutoCString host(aHost);

  LOG("resolving %s\n", host.get());
  // Perform the query
  nsresult rv = packet.FillBuffer(
      [&](unsigned char response[DNSPacket::MAX_SIZE]) -> int {
        if (overrides->Length() > DNSPacket::MAX_SIZE) {
          return -1;
        }
        memcpy(response, overrides->Elements(), overrides->Length());
        return overrides->Length();
      });
  if (NS_FAILED(rv)) {
    return false;
  }

  uint32_t ttl = 0;
  rv = ParseHTTPSRecord(host, packet, aResult, ttl);

  return NS_SUCCEEDED(rv);
}

nsresult ParseHTTPSRecord(nsCString& aHost, DNSPacket& aDNSPacket,
                          TypeRecordResultType& aResult, uint32_t& aTTL) {
  nsAutoCString cname;
  nsresult rv;

  aDNSPacket.SetNativePacket(true);

  int32_t loopCount = 64;
  while (loopCount > 0 && aResult.is<Nothing>()) {
    loopCount--;
    DOHresp resp;
    nsClassHashtable<nsCStringHashKey, DOHresp> additionalRecords;
    rv = aDNSPacket.Decode(aHost, TRRTYPE_HTTPSSVC, cname, true, resp, aResult,
                           additionalRecords, aTTL);
    if (NS_FAILED(rv)) {
      LOG("Decode failed %x", static_cast<uint32_t>(rv));
      return rv;
    }
    if (!cname.IsEmpty() && aResult.is<Nothing>()) {
      aHost = cname;
      cname.Truncate();
      continue;
    }
  }

  if (aResult.is<Nothing>()) {
    LOG("Result is nothing");
    // The call succeeded, but no HTTPS records were found.
    return NS_ERROR_UNKNOWN_HOST;
  }

  return NS_OK;
}

nsresult ResolveHTTPSRecord(const nsACString& aHost,
                            nsIDNSService::DNSFlags aFlags,
                            TypeRecordResultType& aResult, uint32_t& aTTL) {
  if (gOverrideService) {
    return FindHTTPSRecordOverride(aHost, aResult) ? NS_OK
                                                   : NS_ERROR_UNKNOWN_HOST;
  }

  return ResolveHTTPSRecordImpl(aHost, aFlags, aResult, aTTL);
}

nsresult CreateAndResolveMockHTTPSRecord(const nsACString& aHost,
                                         nsIDNSService::DNSFlags aFlags,
                                         TypeRecordResultType& aResult,
                                         uint32_t& aTTL) {
  nsCString buffer;
  buffer += '\0';
  buffer += '\0';  // 16 bit id
  buffer += 0x80;
  buffer += '\0';  // Flags
  buffer += '\0';
  buffer += '\0';  // Question count
  buffer += '\0';
  buffer += 0x1;  // Answer count
  buffer += '\0';
  buffer += '\0';
  buffer += '\0';
  buffer += '\0';

  nsresult rv = DNSPacket::EncodeHost(buffer, aHost);
  if (NS_FAILED(rv)) {
    return rv;
  }

  buffer += '\0';
  buffer += 0x41;  // TYPE 65

  buffer += '\0';
  buffer += 0x1;  // Class

  buffer += '\0';
  buffer += '\0';
  buffer += '\0';
  buffer += 0xFF;  // TTL
  buffer += '\0';
  buffer += 0x03;  // RDLENGTH
  buffer += '\0';
  buffer += 0x01;  // SvcPriority
  buffer += '\0';

  DNSPacket packet;
  nsAutoCString host(aHost);

  LOG("resolving %s\n", host.get());
  // Perform the query
  rv = packet.FillBuffer(
      [&](unsigned char response[DNSPacket::MAX_SIZE]) -> int {
        if (buffer.Length() > DNSPacket::MAX_SIZE) {
          return -1;
        }
        memcpy(response, buffer.BeginReading(), buffer.Length());
        return buffer.Length();
      });
  if (NS_FAILED(rv)) {
    return rv;
  }

  return ParseHTTPSRecord(host, packet, aResult, aTTL);
}

// static
already_AddRefed<nsINativeDNSResolverOverride>
NativeDNSResolverOverride::GetSingleton() {
  if (nsIOService::UseSocketProcess() && XRE_IsParentProcess()) {
    return NativeDNSResolverOverrideParent::GetSingleton();
  }

  if (gOverrideService) {
    return do_AddRef(gOverrideService);
  }

  gOverrideService = new NativeDNSResolverOverride();
  ClearOnShutdown(&gOverrideService);
  return do_AddRef(gOverrideService);
}

NS_IMPL_ISUPPORTS(NativeDNSResolverOverride, nsINativeDNSResolverOverride)

NS_IMETHODIMP NativeDNSResolverOverride::AddIPOverride(
    const nsACString& aHost, const nsACString& aIPLiteral) {
  NetAddr tempAddr;

  if (aIPLiteral.Equals("N/A"_ns)) {
    AutoWriteLock lock(mLock);
    auto& overrides = mOverrides.LookupOrInsert(aHost);
    overrides.Clear();
    return NS_OK;
  }

  if (NS_FAILED(tempAddr.InitFromString(aIPLiteral))) {
    return NS_ERROR_UNEXPECTED;
  }

  AutoWriteLock lock(mLock);
  auto& overrides = mOverrides.LookupOrInsert(aHost);
  overrides.AppendElement(tempAddr);

  return NS_OK;
}

NS_IMETHODIMP NativeDNSResolverOverride::AddHTTPSRecordOverride(
    const nsACString& aHost, const uint8_t* aData, uint32_t aLength) {
  AutoWriteLock lock(mLock);
  nsTArray<uint8_t> data(aData, aLength);
  mHTTPSRecordOverrides.InsertOrUpdate(aHost, std::move(data));

  return NS_OK;
}

NS_IMETHODIMP NativeDNSResolverOverride::SetCnameOverride(
    const nsACString& aHost, const nsACString& aCNAME) {
  if (aCNAME.IsEmpty()) {
    return NS_ERROR_UNEXPECTED;
  }

  AutoWriteLock lock(mLock);
  mCnames.InsertOrUpdate(aHost, nsCString(aCNAME));

  return NS_OK;
}

NS_IMETHODIMP NativeDNSResolverOverride::ClearHostOverride(
    const nsACString& aHost) {
  AutoWriteLock lock(mLock);
  mCnames.Remove(aHost);
  auto overrides = mOverrides.Extract(aHost);
  if (!overrides) {
    return NS_OK;
  }

  overrides->Clear();
  return NS_OK;
}

NS_IMETHODIMP NativeDNSResolverOverride::ClearOverrides() {
  AutoWriteLock lock(mLock);
  mOverrides.Clear();
  mCnames.Clear();
  return NS_OK;
}

#ifdef MOZ_NO_HTTPS_IMPL

// If there is no platform specific implementation of ResolveHTTPSRecordImpl
// we link a dummy implementation here.
// Otherwise this is implemented in PlatformDNSWin/Linux/etc
nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void DNSThreadShutdown() {}

#endif  // MOZ_NO_HTTPS_IMPL

}  // namespace mozilla::net
