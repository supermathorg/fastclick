/*
 * checkipheader.{cc,hh} -- element checks IP header for correctness
 * (checksums, lengths, source addresses)
 * Robert Morris
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "checkipheader.hh"
#include "click_ip.h"
#include "glue.hh"
#include "confparse.hh"
#include "error.hh"
#include "bitvector.hh"
#ifdef __KERNEL__
# include <net/checksum.h>
#endif

CheckIPHeader::CheckIPHeader()
  : _bad_src(0), _drops(0)
{
  add_input();
  add_output();
}

CheckIPHeader::~CheckIPHeader()
{
  delete[] _bad_src;
}

CheckIPHeader *
CheckIPHeader::clone() const
{
  return new CheckIPHeader();
}

void
CheckIPHeader::notify_noutputs(int n)
{
  set_noutputs(n < 2 ? 1 : 2);
}

int
CheckIPHeader::configure(const String &conf, ErrorHandler *errh)
{
  Vector<String> args;
  cp_argvec(conf, args);
  if (args.size() > 1)
    return errh->error("too many arguments to `CheckIPHeader([ADDRS])'");
  
  Vector<u_int> ips;
  ips.push_back(0);
  ips.push_back(0xffffffff);

  if (args.size()) {
    String s = args[0];
    while (s) {
      u_int a;
      if (!cp_ip_address(s, (unsigned char *)&a, &s))
	return errh->error("expects IPADDRESS");
      cp_eat_space(s);
      for (int j = 0; j < ips.size(); j++)
	if (ips[j] == a)
	  goto repeat;
      ips.push_back(a);
     repeat: ;
    }
  }

  delete[] _bad_src;
  _n_bad_src = ips.size();
  if (_n_bad_src) {
    _bad_src = new u_int [_n_bad_src];
    memcpy(_bad_src, &ips[0], sizeof(u_int) * ips.size());
  } else
    _bad_src = 0;

  return 0;
}

void
CheckIPHeader::drop_it(Packet *p)
{
  if (_drops == 0)
    click_chatter("IP checksum failed");
  _drops++;
  
  if (noutputs() == 2)
    output(1).push(p);
  else
    p->kill();
}

Packet *
CheckIPHeader::simple_action(Packet *p)
{
  click_ip *ip = (click_ip *) p->data();
  unsigned int src;
  unsigned hlen;
  
  if(p->length() < sizeof(click_ip))
    goto bad;
  
  if(ip->ip_v != 4)
    goto bad;
  
  hlen = ip->ip_hl << 2;
  if(hlen < sizeof(click_ip))
    goto bad;
  
  if(hlen > p->length())
    goto bad;
  
#ifndef __KERNEL__
  if(in_cksum((unsigned char *)ip, hlen) != 0)
    goto bad;
#else
  if (ip_fast_csum((unsigned char *)ip, ip->ip_hl) != 0)
    goto bad;
#endif
  
  if(ntohs(ip->ip_len) < hlen)
    goto bad;

  /*
   * RFC1812 5.3.7 and 4.2.2.11: discard illegal source addresses.
   * Configuration string should have listed all subnet
   * broadcast addresses known to this router.
   */
  src = ip->ip_src.s_addr;
  for(int i = 0; i < _n_bad_src; i++)
    if(src == _bad_src[i])
      goto bad;

  /*
   * RFC1812 4.2.3.1: discard illegal destinations.
   * We now do this in the IP routing table.
   */

  p->set_ip_header(ip);
  return(p);
  
 bad:
  drop_it(p);
  return 0;
}

static String
CheckIPHeader_read_drops(Element *xf, void *)
{
  CheckIPHeader *f = (CheckIPHeader *)xf;
  return String(f->drops()) + "\n";
}

void
CheckIPHeader::add_handlers()
{
  add_read_handler("drops", CheckIPHeader_read_drops, 0);
}

EXPORT_ELEMENT(CheckIPHeader)
