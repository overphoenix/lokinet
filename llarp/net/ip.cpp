#include <net/ip.hpp>

#include <util/buffer.hpp>
#include <util/endian.hpp>
#include <util/mem.hpp>

#ifndef _WIN32
#include <netinet/in.h>
#endif

#include <algorithm>
#include <map>

namespace llarp
{
  namespace net
  {
    huint128_t
    IPPacket::In6ToHUInt(in6_addr addr)
    {
#if __BYTE_ORDER == __BIG_ENDIAN
      return huint128_t{addr.s6_addr32[0]}
          | (huint128_t{addr.s6_addr32[1]} << 32)
          | (huint128_t{addr.s6_addr32[2]} << 64)
          | (huint128_t{addr.s6_addr32[3]} << 96);
#else
      return huint128_t{ntohl(addr.s6_addr32[3])}
          | (huint128_t{ntohl(addr.s6_addr32[2])} << 32)
          | (huint128_t{ntohl(addr.s6_addr32[1])} << 64)
          | (huint128_t{ntohl(addr.s6_addr32[0])} << 96);
#endif
    }

    in6_addr
    IPPacket::HUIntToIn6(huint128_t x)
    {
      in6_addr addr;
      auto i = ntoh128(x.h);
      memcpy(&addr, &i, 16);
      return addr;
    }

    huint128_t
    IPPacket::ExpandV4(huint32_t i)
    {
      huint128_t ff = {0xff};
      huint128_t expanded{i.h};
      return (ff << 40) | (ff << 32) | expanded;
    }

    huint32_t
    IPPacket::TruncateV6(huint128_t i)
    {
      huint32_t ret = {0};
      ret.h         = (uint32_t)(i.h & (0x00000000ffffffffUL));
      return ret;
    }

    huint128_t
    IPPacket::srcv6() const
    {
      return In6ToHUInt(HeaderV6()->srcaddr);
    }

    huint128_t
    IPPacket::dstv6() const
    {
      return In6ToHUInt(HeaderV6()->dstaddr);
    }

    bool
    IPPacket::Load(const llarp_buffer_t &pkt)
    {
      if(pkt.sz > sizeof(buf))
        return false;
      sz = pkt.sz;
      memcpy(buf, pkt.base, sz);
      return true;
    }

    llarp_buffer_t
    IPPacket::ConstBuffer() const
    {
      return {buf, sz};
    }

    llarp_buffer_t
    IPPacket::Buffer()
    {
      return {buf, sz};
    }

    huint32_t
    IPPacket::srcv4() const
    {
      return huint32_t{ntohl(Header()->saddr)};
    }

    huint32_t
    IPPacket::dstv4() const
    {
      return huint32_t{ntohl(Header()->daddr)};
    }


#if 0
    static uint32_t
    ipchksum_pseudoIPv4(nuint32_t src_ip, nuint32_t dst_ip, uint8_t proto,
                        uint16_t innerlen)
    {
#define IPCS(x) ((uint32_t)(x & 0xFFff) + (uint32_t)(x >> 16))
      uint32_t sum = IPCS(src_ip.n) + IPCS(dst_ip.n) + (uint32_t)proto
          + (uint32_t)htons(innerlen);
#undef IPCS
      return sum;
    }

    static uint16_t
    ipchksum(const byte_t *buf, size_t sz, uint32_t sum = 0)
    {
      while(sz > 1)
      {
        sum += *(const uint16_t *)buf;
        sz -= sizeof(uint16_t);
        buf += sizeof(uint16_t);
      }
      if(sz != 0)
      {
        uint16_t x = 0;

        *(byte_t *)&x = *(const byte_t *)buf;
        sum += x;
      }

      // only need to do it 2 times to be sure
      // proof: 0xFFff + 0xFFff = 0x1FFfe -> 0xFFff
      sum = (sum & 0xFFff) + (sum >> 16);
      sum += sum >> 16;

      return uint16_t((~sum) & 0xFFff);
    }
#endif


    void
    IPPacket::UpdateIPv4Address(nuint32_t nSrcIP, nuint32_t nDstIP)
    {
      llarp::LogDebug("set src=", newSrcIP, " dst=", newDstIP);

      auto hdr = Header();

      auto oSrcIP = nuint32_t{hdr->saddr};
      auto oDstIP = nuint32_t{hdr->daddr};

      // L4 checksum
      auto ihs = size_t(hdr->ihl * 4);
      if(ihs <= sz)
      {
        auto pld = buf + ihs;
        auto psz = sz - ihs;

        auto fragoff = size_t((ntohs(hdr->frag_off) & 0x1Fff) * 8);

        switch(hdr->protocol)
        {
          case 6:  // TCP
            checksumDstIPv4TCP(pld, psz, fragoff, 16, oSrcIP, oDstIP, nSrcIP,
                               nDstIP);
            break;
          case 17:   // UDP
          case 136:  // UDP-Lite - same checksum place, same 0->0xFFff condition
            checksumDstIPv4UDP(pld, psz, fragoff, oSrcIP, oDstIP, nSrcIP,
                               nDstIP);
            break;
          case 33:  // DCCP
            checksumDstIPv4TCP(pld, psz, fragoff, 6, oSrcIP, oDstIP, nSrcIP,
                               nDstIP);
            break;
        }
      }

      // IPv4 checksum
      auto v4chk = (nuint16_t *)&(hdr->check);
      *v4chk     = deltaIPv4Checksum(*v4chk, oSrcIP, oDstIP, nSrcIP, nDstIP);

      // write new IP addresses
      hdr->saddr = nSrcIP.n;
      hdr->daddr = nDstIP.n;
    }


#define ADD32CS(x) ((uint32_t)(x & 0xFFff) + (uint32_t)(x >> 16))
#define SUB32CS(x) ((uint32_t)((~x) & 0xFFff) + (uint32_t)((~x) >> 16))

    static nuint16_t
    deltaIPv4Checksum(nuint16_t old_sum, nuint32_t old_src_ip,
                      nuint32_t old_dst_ip, nuint32_t new_src_ip,
                      nuint32_t new_dst_ip)
    {

      uint32_t sum = uint32_t(old_sum.n) +
        ADD32CS(old_src_ip.n) + ADD32CS(old_dst_ip.n) +
        SUB32CS(new_src_ip.n) + SUB32CS(new_dst_ip.n);

      // only need to do it 2 times to be sure
      // proof: 0xFFff + 0xFFff = 0x1FFfe -> 0xFFff
      sum = (sum & 0xFFff) + (sum >> 16);
      sum += sum >> 16;

      return nuint16_t{uint16_t(sum & 0xFFff)};
    }

    static nuint16_t
    deltaIPv6Checksum(nuint16_t old_sum,
                      const uint32 old_src_ip[4], const uint32 old_dst_ip[4],
                      const uint32 new_src_ip[4], const uint32 new_dst_ip[4])
    {
      /* we don't actually care in what way integers are arranged in memory internally */
      /* as long as uint16 pairs are swapped in correct direction, result will be correct (assuming there are no gaps in structure) */
      /* we represent 128bit stuff there as 4 32bit ints, that should be more or less correct */
      /* we could do 64bit ints too but then we couldn't reuse 32bit macros and that'd suck for 32bit cpus */
#define ADDN128CS(x) (ADD32CS(x[0]) + ADD32CS(x[1]) + ADD32CS(x[2]) + ADD32CS(x[3]))
#define SUBN128CS(x) (SUB32CS(x[0]) + SUB32CS(x[1]) + SUB32CS(x[2]) + SUB32CS(x[3]))
      uint32_t sum = uint32_t(old_sum) +
        ADDN128CS(old_src_ip) + ADDN128CS(old_dst_ip) +
        SUBN128CS(new_src_ip) + SUBN128CS(new_dst_ip);
#undef ADDN128CS
#undef SUBN128CS

      // only need to do it 2 times to be sure
      // proof: 0xFFff + 0xFFff = 0x1FFfe -> 0xFFff
      sum = (sum & 0xFFff) + (sum >> 16);
      sum += sum >> 16;

      return nuint16_t{uint16_t(sum & 0xFFff)};
    }

#undef ADD32CS
#undef SUB32CS

    void
    IPPacket::UpdateIPv6Address(huint128_t src, huint128_t dst)
    {
      if(sz <= 40)
        return;
      auto hdr     = HeaderV6();
      auto oldSrc = hdr->srcaddr;
      auto oldDst = hdr->dstaddr;
      hdr->srcaddr = HUIntToIn6(src);
      hdr->dstaddr = HUIntToIn6(dst);
      const size_t ihs = 40;
      auto pld = buf + ihs;
      auto psz = sz - ihs;
      switch(hdr->proto)
      {
        //tcp
        case 6:
          return;
      }
    }


    static void
    deltaChecksumIPv4TCP(byte_t *pld, ABSL_ATTRIBUTE_UNUSED size_t psz,
                       size_t fragoff, size_t chksumoff, nuint32_t oSrcIP,
                       nuint32_t oDstIP, nuint32_t nSrcIP, nuint32_t nDstIP)
    {
      if(fragoff > chksumoff)
        return;

      auto check = (nuint16_t *)(pld + chksumoff - fragoff);

      *check = deltaIPv4Checksum(*check, oSrcIP, oDstIP, nSrcIP, nDstIP);
      // usually, TCP checksum field cannot be 0xFFff,
      // because one's complement addition cannot result in 0x0000,
      // and there's inversion in the end;
      // emulate that.
      if(check->n == 0xFFff)
        check->n = 0x0000;
    }

    static void
    deltaChecksumIPv4UDP(byte_t *pld, ABSL_ATTRIBUTE_UNUSED size_t psz,
                       size_t fragoff, nuint32_t oSrcIP, nuint32_t oDstIP,
                       nuint32_t nSrcIP, nuint32_t nDstIP)
    {
      if(fragoff > 6)
        return;

      auto check = (nuint16_t *)(pld + 6);
      if(check->n == 0x0000)
        return;  // 0 is used to indicate "no checksum", don't change

      *check = deltaIPv4Checksum(*check, oSrcIP, oDstIP, nSrcIP, nDstIP);
      // 0 is used to indicate "no checksum"
      // 0xFFff and 0 are equivalent in one's complement math
      // 0xFFff + 1 = 0x10000 -> 0x0001 (same as 0 + 1)
      // infact it's impossible to get 0 with such addition,
      // when starting from non-0 value.
      // inside deltachksum we don't invert so it's safe to skip check there
      // if(check->n == 0x0000)
      //   check->n = 0xFFff;
    }
  }  // namespace net
}  // namespace llarp
