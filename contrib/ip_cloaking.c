/* MODULE CONFIGURATION FOLLOWS -- please read!! */

/* Change these numbers to something else. Please
 * make sure that they match on ALL servers
 * to avoid problems!
 */
#define KEY  23857
#define KEY2 38447
#define KEY3 64709

/* END OF MODULE CONFIGURATION */

/*
 * ircd-hybrid: an advanced Internet Relay Chat Daemon (ircd).
 * ip_cloaking.c: Provides hostname (partial) cloaking for clients.
 *
 * Copyright (c) 2005 by the past and present ircd coders, and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at you option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILILTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have recieved a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * $Id$
 */

/*
 * Originally re-written from AustNet's VirtualWorld Code.
 * Virtual World written by Roger 'RogerY' Yerramesetti <rogery@austnet.org>
 *
 * Re-written using the crc32 encryption implementation by //ylo based on the original
 * implementation by Gary S. Brown.
 *
 * HiddenHost implementation based on the code from Unreal IRCd by
 * RaYmAn, NiQuiL, narf, Griever (great thinking) eternal, bball, JK.@ roxnet.org
 *
 * Additional ideas and code by ShadowMaster, [-Th3Dud3-] (RageIRCd), Solaris @ Demonkarma.net,
 * Alan 'knight-' LeVee of the ChatJunkies IRC Network and doughk_ff7.
 */

#include "stdinc.h"
#include "whowas.h"
#include "channel_mode.h"
#include "client.h"
#include "hash.h"
#include "hook.h"
#include "irc_string.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "conf.h"
#include "modules.h"
#include "memory.h"
#include "log.h"
#include "sprintf_irc.h"

static unsigned int umode_vhost = 0;
static int vhost_ipv6_err;
static dlink_node *prev_enter_umode;
static dlink_node *prev_umode;
static void *reset_ipv6err_flag(va_list);
static void *h_set_user_mode(va_list);

/*
 * The implementation originally comes from Gary S. Brown. The tables
 * are a direct transplant of his original concept and have been
 * changed for randomization purposes. Minor changes were also made
 * to the crc32-function by //ylo.
 *
 * knight-
 */

/* ============================================================= 
 * COPYRIGHT (C) 1986 Gary S. Brown. You may use this program, or
 * code or tables extracted from it, as desired without restriction.
 *
 * First, the polynomial itself and its stable of feedback terms. The
 * polynomial is:
 * 
 * X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X^1+X^0
 * 
 * Note that we take it "backwards" and put the highest-order term in
 * the lowest-order bit. The X^32 term is "implied"; the LSB is the
 * X^31 term, etc. The X^0 term (usually shown as "+1") results in
 * the MSB being 1.
 *
 * Note that the usual hardware shift register implementation, which
 * is what we're using (we're merely optimizing it by doing eight-bit
 * chunks at a time) shifts bits into the lowest-order term. In our
 * implementation, that means shifting towards the right. Why do we
 * do it this way? Because the calculated CRC must be transmitted in
 * order from highest-order term to lowest-order term. UARTs transmit
 * characters in order from LSB to MSB. By storing the CRC this way,
 * we and it to the UART in the order low-byte to high-byte; the UART
 * sends each low-bit to high-bit; and the result is transmission bit
 * by bit from highest-order to lowest-order term without requiring any
 * bit shuffling on our part. Reception works similarly.
 *
 * The feedback term table consists of 256, 32-bit entries. Notes:
 *
 * The table can be generated at runtime if desired; code to do so
 * is shown later. It might not be obvious, but the feedback terms
 * simply represent the results of eight shift/xor operations for 
 * all combinations of data and CRC register values.
 *
 * The values must be right shifted by eight bits by the "updcrc"
 * logic; the shift must be unsigned (bring in zeroes). On some hardware
 * you could probably optimize the shift in assembler by using byte-swap
 * instructions.
 *
 * polynomial $edb88320
 *
 * --------------------------------------------------------------------
 */

static unsigned long crc32_tab[] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

static unsigned long
crc32(const char *s, unsigned int len)
{
  unsigned int i;
  unsigned long crc32val = 0;

  for (i = 0; i < len; i++)
    crc32val = crc32_tab[(crc32val ^ s[i]) & 0xff] ^ (crc32val >> 8);

  return crc32val;
}

/*
 * str2arr()
 *
 * converts IPv4 address segments into arrays
 * for encryption
 *
 * inputs - address parvs, strings and deliminator
 * outputs - array
 * side effects - not IPv6 friendly
 */
static int
str2arr(char **pparv, char *string, const char *delim)
{
  char *tok;
  int pparc = 0;

  /* Diane had suggested to use this method rather than while() -- knight */
  for (tok = strtok(string, delim); tok != NULL; tok = strtok(NULL, delim))
  {
    pparv[pparc++] = tok;
  }
    
  return pparc;
}

/*
 * make_virthost()
 *
 * curr = current hostname/ip
 * host = current host socket
 * new = encrypted hostname/ip
 */
static void
make_virthost(char *curr, char *host, char *new)
{
  char mask[HOSTLEN + 1];
  char *parv[HOSTLEN + 1], *parv2[HOSTLEN + 1], s[HOSTLEN + 1], s2[HOSTLEN + 1];
  int parc = 0, parc2 = 0, len = 0;
  unsigned long hash[8];

  strlcpy(s2, curr, HOSTLEN + 1);
  strlcpy(s, host, HOSTLEN + 1);

  parc  = str2arr(parv, s, ".");
  parc2 = str2arr(parv2, s2, ".");

  if (!parc2)
    return;

  hash[0] = ((crc32 (parv[3], strlen (parv[3])) + KEY) ^ KEY2) ^ KEY3;
  hash[1] = ((KEY2 ^ crc32 (parv[2], strlen (parv[2]))) + KEY3) ^ KEY;
  hash[2] = ((crc32 (parv[1], strlen (parv[1])) + KEY3) ^ KEY) ^ KEY2;
  hash[3] = ((KEY3 ^ crc32 (parv[0], strlen (parv[0]))) + KEY2) ^ KEY;
  
  hash[0] <<= 2;
  hash[0] >>= 2;
  hash[0] &= 0x3FFFFFFF;
  hash[0] &= 0xFFFFFFFF;
  hash[1] <<= 2;
  hash[1] >>= 2;
  hash[1] &= 0x7FFFFFFF;
  hash[1] &= 0x3FFFFFFF;
  hash[2] <<= 2;
  hash[2] >>= 2;
  hash[2] &= 0x7FFFFFFF;
  hash[2] &= 0xFFFFFFFF;
  hash[3] <<= 2;
  hash[3] >>= 2;
  hash[3] &= 0x3FFFFFFF;
  hash[3] &= 0x7FFFFFFF;
  
  /* IPv4 */
  if (parc2 == 4 || parc2 < 2)
  {
    len = strlen (parv2[3]);

    if (strchr("0123456789", parv2[3][len - 1]) || parc2 < 2)
    {
      ircsprintf(mask, "%s.%s.%s.%lx",
                 parv2[parc2 - 4], parv2[parc2 - 3],
                 parv2[parc2 - 2], hash[3]);
    }
    else
    {
      /* isp.tld */
      ircsprintf(mask, "%lx-%lx.%s.%s",
                 hash[0], hash[3], parv2[parc2 - 2], parv2[parc2 - 1]);
    }
  }
  else
  {
    if (parc2 >= 4)
    {
      /* isp.sub.tld or district.isp.tld */
      ircsprintf(mask, "%lx-%lx.%s.%s.%s",
            hash[3], hash[1], parv2[parc2 - 3], parv2[parc2 - 2],
            parv2[parc2 - 1]);
    }
    else
    {
      /* isp.tld */
      ircsprintf(mask, "%lx-%lx.%s.%s",
            hash[0], hash[3], parv2[parc2 - 2], parv2[parc2 - 1]);
    }
    
    if (parc2 >= 5)
    {
      /* zone.district.isp.tld or district.isp.sub.tld */
      ircsprintf(mask, "%lx-%lx.%s.%s.%s.%s",
            hash[1], hash[0], parv2[parc2 - 4], parv2[parc2 - 3],
            parv2[parc2 - 2], parv2[parc2 - 1]);
    }
    else
    { 
      /* isp.tld */
      ircsprintf(mask, "%lx-%lx.%s.%s",
            hash[0], hash[3], parv2[parc2 - 2], parv2[parc2 - 1]);
    }
  }

  strlcpy(new, mask, HOSTLEN + 1);
}

/*
 * set_vhost()
 * 
 * inputs - pointer to given client to set IP cloak.
 * outputs - NONE
 * side effects - NONE
 */
static void
set_vhost(struct Client *client_p, struct Client *source_p,
          struct Client *target_p)
{
  target_p->umodes |= umode_vhost;
  SetIPSpoof(target_p);
  make_virthost(target_p->host, target_p->sockhost, target_p->host);

  if (IsClient(target_p))
    sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
                  ":%s ENCAP * CHGHOST %s %s",
                  me.name, target_p->name, target_p->host);

  sendto_one(target_p, form_str(RPL_HOSTHIDDEN),
             me.name, target_p->name, target_p->host);
}

static void *
reset_ipv6err_flag(va_list args)
{
  struct Client *client_p = va_arg(args, struct Client *);
  struct Client *source_p = va_arg(args, struct Client *);

  vhost_ipv6_err = 0;

  return pass_callback(prev_enter_umode, client_p, source_p);
}

static void *
h_set_user_mode(va_list args)
{
  struct Client *client_p = va_arg(args, struct Client *);
  struct Client *target_p = va_arg(args, struct Client *);
  int what = va_arg(args, int);
  unsigned int flag = va_arg(args, unsigned int);

  if (flag == umode_vhost)
  {
    if (what == MODE_ADD)
    {
      /* Automatically break if any of these conditions are met. -- knight- */
      if (!MyConnect(target_p))
        return NULL;

      if (IsIPSpoof(target_p))
        return NULL;

      /*
       * IPv6 could potentially core the server if a user connected via IPv6 sets +h
       * so we need to check and break before that happens. -- knight-
       */
#ifdef IPV6
      if (target_p->localClient->aftype == AF_INET6)
      {
        if (!vhost_ipv6_err)
        {
          sendto_one(target_p, ":%s NOTICE %s :*** Sorry, IP cloaking "
                     "does not support IPv6 users!", me.name, target_p->name);
          vhost_ipv6_err = 1;
        }
      }
      else
#endif
        set_vhost(client_p, target_p, target_p);
    }

    return NULL;
  }

  return pass_callback(prev_umode, client_p, target_p, what, flag);
}

static void
module_init(void)
{
  if (!user_modes['h'])
  {
    unsigned int all_umodes = 0, i;

    for (i = 0; i < 128; i++)
      all_umodes |= user_modes[i];

    for (umode_vhost = 1; umode_vhost && (all_umodes & umode_vhost);
         umode_vhost <<= 1);

    if (!umode_vhost)
    {
      ilog(LOG_TYPE_IRCD, "You have more than 32 usermodes, "
           "IP cloaking not installed");
      sendto_realops_flags(UMODE_ALL, L_ALL, "You have more than "
                           "32 usermodes, IP cloaking not installed");
      return;
    }

    user_modes['h'] = umode_vhost;
    assemble_umode_buffer();
  }
  else
  {
    ilog(LOG_TYPE_IRCD, "Usermode +h already in use, IP cloaking not installed");
    sendto_realops_flags(UMODE_ALL, L_ALL, "Usermode +h already in use, "
                         "IP cloaking not installed");
    return;
  }

  prev_enter_umode = install_hook(entering_umode_cb, reset_ipv6err_flag);
  prev_umode = install_hook(umode_cb, h_set_user_mode);
}

static void
module_exit(void)
{
  if (umode_vhost)
  {
    dlink_node *ptr;

    DLINK_FOREACH(ptr, local_client_list.head)
    {
      struct Client *cptr = ptr->data;
      cptr->umodes &= ~umode_vhost;
    }

    user_modes['h'] = 0;
    assemble_umode_buffer();

    uninstall_hook(entering_umode_cb, reset_ipv6err_flag);
    uninstall_hook(umode_cb, h_set_user_mode);
  }
}

struct module module_entry = {
  .node    = { NULL, NULL, NULL },
  .name    = NULL,
  .version = "$Revision$",
  .handle  = NULL,
  .modinit = module_init,
  .modexit = module_exit,
  .flags   = 0
};
