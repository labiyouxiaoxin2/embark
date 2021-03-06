/*
 * senddevice.{cc,hh} -- element writes packets to network via Intel DPDK 
 * multi-queue enabled, works with multi-queue 1G and 10G NICs
 * Douglas S. J. De Couto, Eddie Kohler, John Jannotti 
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2005-2008 Regents of the University of California
 * Copyright (c) 2011 Meraki, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */
/* Created 2012-2013 by Maziar Manesh, Intel Labs */

#include <click/config.h>
#if HAVE_NET_BPF_H
# include <sys/types.h>
# include <sys/time.h>
# include <net/bpf.h>
# define PCAP_DONT_INCLUDE_PCAP_BPF_H 1
#endif
#include "senddevice.hh"
#include <click/error.hh>
#include <click/etheraddress.hh>
#include <click/args.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet_anno.hh>
#include <click/straccum.hh>
#include <click/userutils.hh>
#include <stdio.h>
#include <unistd.h>
#include <click/dpdk.h>

CLICK_DECLS

SendDevice::SendDevice()
//: _task(this), _timer(&_task), _q(0), _pulls(0)
  : _task(this), _q(0), _pulls(0), _drops(0)
{
#if SENDDEVICE_ALLOW_PCAP
    _pcap = 0;
    _my_pcap = false;
#endif
#if SENDDEVICE_ALLOW_LINUX || SENDDEVICE_ALLOW_DEVBPF || SENDDEVICE_ALLOW_PCAPFD
    _fd = -1;
    _my_fd = false;
#endif
}

SendDevice::~SendDevice()
{
}

int
SendDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String method;
    //_portid = 0;
    _queueid = 0;
    _burst = 1;
    if (Args(conf, this, errh)
	.read_mp("DEVNAME", _ifname)
	.read("DEBUG", _debug)
	//.read("METHOD", WordArg(), method)
	.read("BURST", _burst)
	//.read("PORTID", _portid)
	.read("QUEUE", _queueid)
	.complete() < 0)
	return -1;
    if (!_ifname)
	return errh->error("interface not set");
    if (_burst <= 0 || _queueid < 0)
	return errh->error("bad BURST or PORTID or QUEUE");
    if (get_portID(_ifname) < 0)
      return errh->error("DEVNAME is not a valid device and not configured for use");
    
    /*
    if (method == "") {
#if SENDDEVICE_ALLOW_PCAP && SENDDEVICE_ALLOW_LINUX
	_method = method_pcap;
	if (FromDevice *fd = find_fromdevice())
	    if (fd->linux_fd())
		_method = method_linux;
#elif SENDDEVICE_ALLOW_PCAP
	_method = method_pcap;
#elif SENDDEVICE_ALLOW_LINUX
	_method = method_linux;
#elif SENDDEVICE_ALLOW_DEVBPF
	_method = method_devbpf;
#elif SENDDEVICE_ALLOW_PCAPFD
	_method = method_pcapfd;
#else
	//return errh->error("cannot send packets on this platform");
#endif
    }
#if SENDDEVICE_ALLOW_PCAP
    else if (method == "PCAP")
	_method = method_pcap;
#endif
#if SENDDEVICE_ALLOW_LINUX
    else if (method == "LINUX")
	_method = method_linux;
#endif
#if SENDDEVICE_ALLOW_DEVBPF
    else if (method == "DEVBPF")
	_method = method_devbpf;
#endif
#if SENDDEVICE_ALLOW_PCAPFD
    else if (method == "PCAPFD")
	_method = method_pcapfd;
#endif
    else
	return errh->error("bad METHOD");

    */
    return 0;
}

PollDevice *
SendDevice::find_fromdevice() const
{
    Router *r = router();
    for (int ei = 0; ei < r->nelements(); ++ei) {
	PollDevice *fd = (PollDevice *) r->element(ei)->cast("PollDevice");
	if (fd && fd->ifname() == _ifname)
	  return fd;
    }
    return 0;
}

int
SendDevice::initialize(ErrorHandler *errh)
{
  //_timer.initialize(this);

    //setting portid form dev-name
    _portid = get_portID(_ifname);

    click_chatter("SendDevice(%s): DPDK port_id: %d, queueid:%d", _ifname.c_str(), _portid, _queueid);
#if 0
#if SENDDEVICE_ALLOW_PCAP
    if (_method == method_pcap) {
	PollDevice *fd = find_fromdevice();
	if (fd && fd->pcap())
	    _pcap = fd->pcap();
	else {
	    _pcap = PollDevice::open_pcap(_ifname, PollDevice::default_snaplen, false, errh);
	    if (!_pcap)
		return -1;
	    _my_pcap = true;
	}
	_fd = pcap_fileno(_pcap);
	/* _my_fd = false by default */
    }
#endif

#if SENDDEVICE_ALLOW_DEVBPF
    if (_method == method_devbpf) {
	/* pcap_open_live() doesn't open for writing. */
	for (int i = 0; i < 16 && _fd < 0; i++) {
	    char tmp[64];
	    sprintf(tmp, "/dev/bpf%d", i);
	    _fd = open(tmp, 1);
	}
	if (_fd < 0)
	    return(errh->error("open /dev/bpf* for write: %s", strerror(errno)));
	_my_fd = true;

	struct ifreq ifr;
	strncpy(ifr.ifr_name, _ifname.c_str(), sizeof(ifr.ifr_name));
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = 0;
	if (ioctl(_fd, BIOCSETIF, (caddr_t)&ifr) < 0)
	    return errh->error("BIOCSETIF %s failed", ifr.ifr_name);
# ifdef BIOCSHDRCMPLT
	int yes = 1;
	if (ioctl(_fd, BIOCSHDRCMPLT, (caddr_t)&yes) < 0)
	    errh->warning("BIOCSHDRCMPLT %s failed", ifr.ifr_name);
# endif
    }
#endif

#if SENDDEVICE_ALLOW_LINUX
    if (_method == method_linux) {
	PollDevice *fd = find_fromdevice();
	if (fd && fd->linux_fd() >= 0)
	    _fd = fd->linux_fd();
	else {
	    _fd = PollDevice::open_packet_socket(_ifname, errh);
	    if (_fd < 0)
		return -1;
	    _my_fd = true;
	}
    }
#endif

#if SENDDEVICE_ALLOW_PCAPFD
    if (_method == method_pcapfd) {
	PollDevice *fd = find_fromdevice();
	if (fd && fd->pcap())
	    _fd = fd->fd();
	else
	    return errh->error("initialized PollDevice required on this platform");
    }
#endif
#endif

if (input_is_pull(0)) {
  // check for duplicate writers
  void *&used = router()->force_attachment("device_writer_" + _ifname + "_queue_" + String(_queueid));
  if (used)
    return errh->error("duplicate writer for device %<%s%>", _ifname.c_str());
  used = this;
  
  //ScheduleInfo::join_scheduler(this, &_task, errh);
  ScheduleInfo::initialize_task(this, &_task, true, errh);
  _signal = Notifier::upstream_empty_signal(this, 0, &_task);
 }
 return 0;
}

void
SendDevice::cleanup(CleanupStage)
{
#if SENDDEVICE_ALLOW_PCAP
    if (_pcap && _my_pcap)
	pcap_close(_pcap);
    _pcap = 0;
#endif
#if SENDDEVICE_ALLOW_LINUX || SENDDEVICE_ALLOW_DEVBPF || SENDDEVICE_ALLOW_PCAPFD
    if (_fd >= 0 && _my_fd)
	close(_fd);
    _fd = -1;
#endif
}

bool
SendDevice::run_task(Task *)
{
  Packet *p = 0;
  int count;;
  unsigned ret;
    count = 0;
    do {
	if (!p) {
	    ++_pulls;
	    if (!(p = input(0).pull()))
		break;
	}
	//this is only for port forwarding passing thru enire rte_mbuf unchanged
	//pkts_burst[count] = (struct rte_mbuf *) p->data();
        
	//now just getting the mbuf addr from _head, mbuf is already in Packet p
	pkts_burst[count] = p->ptr_mbuf; //RTE_MBUF_FROM_BADDR(p->buffer());
        //if (_pulls < 33)
	//  click_chatter("pktlen= %d\n", rte_pktmbuf_pkt_len(p->ptr_mbuf));

	//build the rte_mbuf pkt, this is for copying packet
	/*
	if (pktmbuf_pool==NULL) {
	  click_chatter("pktmbuf_poo is null\n");
	  break;
	}
	*/
    /*
	pkts_burst[count] = rte_pktmbuf_alloc(pktmbuf_pool);
	memcpy(pkts_burst[count]->pkt.data, p->data(), p->length());
	pkts_burst[count]->pkt.nb_segs = 1;
	pkts_burst[count]->pkt.next = NULL;
	pkts_burst[count]->pkt.pkt_len = (uint16_t)p->length();
	pkts_burst[count]->pkt.data_len = (uint16_t)p->length();
	*/
	count++;
	p->kill_lite();
	p = 0;
	/*
	if ((r = send_packet(p)) >= 0) {
	    _backoff = 0;
	    checked_output_push(0, p);
	    ++count;
	} else
	    break;
	*/
    } while (count < _burst);
    //click_chatter("SendDevice(%s): queue %d", _ifname.c_str(), _queueid);
    if (likely(count > 0)) {
      ret = rte_eth_tx_burst((uint8_t) _portid, _queueid, pkts_burst, (uint16_t) count);
      if (unlikely(ret < count)) {                                
	_drops += (count - ret);
	do {
	  rte_pktmbuf_free(pkts_burst[ret]);
	} while (++ret < count);
      }
    }

    if (count || _signal)
      _task.reschedule();

    return count > 0;
}


String
SendDevice::read_param(Element *e, void *thunk)
{
    SendDevice *td = (SendDevice *)e;
    switch((uintptr_t) thunk) {
    case h_debug:
	return String(td->_debug);
    case h_signal:
	return String(td->_signal);
    case h_pulls:
	return String(td->_pulls);
    case h_q:
	return String((bool) td->_q);
    case h_drops:
        return String(td->_drops);
    default:
	return String();
    }
}

int
SendDevice::write_param(const String &in_s, Element *e, void *vparam,
		     ErrorHandler *errh)
{
    SendDevice *td = (SendDevice *)e;
    String s = cp_uncomment(in_s);
    switch ((intptr_t)vparam) {
    case h_debug: {
	bool debug;
	if (!BoolArg().parse(s, debug))
	    return errh->error("type mismatch");
	td->_debug = debug;
	break;
    }
    }
    return 0;
}

void
SendDevice::add_handlers()
{
    add_task_handlers(&_task);
    add_read_handler("debug", read_param, h_debug, Handler::CHECKBOX);
    add_read_handler("pulls", read_param, h_pulls);
    add_read_handler("signal", read_param, h_signal);
    add_read_handler("q", read_param, h_q);
    add_write_handler("debug", write_param, h_debug);
    //added for drop count
    add_read_handler("drops", read_param, h_drops);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(PollDevice userlevel)
EXPORT_ELEMENT(SendDevice)
