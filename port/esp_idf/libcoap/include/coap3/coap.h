/* Modify head file implementation for ESP32 platform.
 *
 * Uses libcoap software implementation for failover when concurrent
 * define operations are in use.
 *
 * coap.h -- main header file for CoAP stack of libcoap
 *
 * Copyright (C) 2010-2012,2015-2016 Olaf Bergmann <bergmann@tzi.org>
 *               2015 Carsten Schoenert <c.schoenert@t-online.de>
 *
 * Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
 *
 * This file is part of the CoAP library libcoap. Please see README for terms
 * of use.
 */

#ifndef _COAP_H_
#define _COAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "coap3/libcoap.h"

#include "coap3/coap_forward_decls.h"
#include "coap3/block.h"
#include "coap3/coap_address.h"
#include "coap3/coap_async.h"
#include "coap3/coap_cache.h"
#include "coap3/coap_debug.h"
#include "coap3/coap_dtls.h"
#include "coap3/coap_encode.h"
#include "coap3/coap_event.h"
#include "coap3/coap_io.h"
#include "coap3/coap_mem.h"
#include "coap3/coap_net.h"
#include "coap3/coap_option.h"
#include "coap3/coap_prng.h"
#include "coap3/coap_str.h"
#include "coap3/coap_subscribe.h"
#include "coap3/coap_time.h"
#include "coap3/coap_uri.h"
#include "coap3/pdu.h"
#include "coap3/resource.h"

#ifdef __cplusplus
}
#endif

#endif /* _COAP_H_ */
