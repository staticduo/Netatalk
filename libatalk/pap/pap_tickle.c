/*
 * $Id: pap_tickle.c,v 1.4 2002-01-04 04:45:48 sibaz Exp $
 *
 * send a tickle
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

void pap_tickle(PAP pap, const u_int8_t connid, struct sockaddr_at *sat)
{
  struct atp_block atpb; 
  u_int8_t buf[PAP_HDRSIZ];

  buf[ 0 ] = connid;
  buf[ 1 ] = PAP_TICKLE;
  buf[ 2 ] = buf[ 3 ] = 0;

  atpb.atp_saddr = sat;
  atpb.atp_sreqdata = buf;
  atpb.atp_sreqdlen = sizeof(buf);	/* bytes in Tickle request */
  atpb.atp_sreqto = 0;		/* retry timer */
  atpb.atp_sreqtries = 1;	/* retry count */
  if ( atp_sreq( pap->pap_atp, &atpb, 0, 0 ) < 0 ) {
    LOG(log_error, logtype_default, "atp_sreq: %m");
  }
}