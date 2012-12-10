// $Id$

#include <vxWorks.h>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <vwpp-1.0.h>
#include <mooc++-4.2.h>

int v474_lock_tmo = 1500;
int v474_debug = 0;

typedef unsigned char chan_t;
typedef unsigned char type_t;

#define OMSPDEF_TO_CHAN(a)	((chan_t) (a).chan)
#define OMSPDEF_TO_TYPE(a)	((type_t) (a).typ)

#define	REQ_TO_CHAN(a)		OMSPDEF_TO_CHAN(*(OMSP_DEF const*) &(a)->OMSP)
#define	REQ_TO_TYPE(a)		OMSPDEF_TO_TYPE(*(OMSP_DEF const*) &(a)->OMSP)
#define REQ_TO_SUBCODE(req)	((((OMSP_DEF const*)&(req)->OMSP)->chan & 0xf0) >> 4)

#define DATAS(req) (*(unsigned short const*)(req)->data)

// Junk class. Some versions of GCC don't honor the
// constructor/destructor attributes unless there's a C++ global
// object needing to be created/destroyed. This small section creates
// an unused object to make sure these functions are called.

namespace V474 { class Junk {} junk; };

namespace V474 {

    struct Card {
	uint16_t* baseAddr;
	vwpp::Mutex mutex;
    };

};

// Local prototypes...

static void term(void) __attribute__((destructor));

// This function gets called when trying to unload the module under
// VxWorks. Since we don't support it, let the user know she screwed
// up the machine.

static void term(void)
{
    printf("ERROR: the V474 module is not designed to be unloaded! This\n"
	   "       system is now unstable -- a reboot is recommended!");
}

// This function initializes an instance of the MOOC V474 class.

STATUS objectInit(short const oid, V474::Card* const ptr, void const*,
		  V474::Card** const ivs)
{
    *ivs = ptr;
    return OK;
}

typedef int16_t typReading;

static STATUS devReading(short, RS_REQ const* const req, typReading* const rep,
			 V474::Card* const* const ivs)
{
    chan_t const chan = REQ_TO_CHAN(req);

    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;
    if (chan > 3)
	return ERR_BADCHN;

    *rep = ((*ivs)->baseAddr)[0x80 + 8 * chan];
    return NOERR;
}

static STATUS devReadSetting(short, RS_REQ const* const req,
			     typReading* const rep,
			     V474::Card* const* const ivs)
{
    chan_t const chan = REQ_TO_CHAN(req);

    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;
    if (chan > 3)
	return ERR_BADCHN;

    *rep = ((*ivs)->baseAddr)[8 * chan];
    return NOERR;
}

static STATUS devSetting(short, RS_REQ* req, void*,
			 V474::Card* const* const ivs)
{
    chan_t const chan = REQ_TO_CHAN(req);

    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;
    if (chan > 3)
	return ERR_BADCHN;

    ((*ivs)->baseAddr)[8 * chan] = DATAS(req);
    return NOERR;
}

static STATUS devBasicControl(short, RS_REQ const* const req, void*,
			      V474::Card* const* const obj)
{
}

static STATUS devBasicStatus(short, RS_REQ const* const req, void* const rep,
			     V474::Card* const* const obj)
{
}

// Creates an instance of the MOOC V474 class.

STATUS v473_create_mooc_instance(unsigned short const oid,
				 uint8_t const addr)
{
    try {
	short const cls = find_class("V474");

	if (cls == -1)
	    throw std::runtime_error("V474 class is not registered with MOOC");

	std::auto_ptr<V474::Card> ptr(new V474::Card());

	if (ptr.get()) {
	    if (create_instance(oid, cls, ptr.get(), "V474") != NOERR)
		throw std::runtime_error("problem creating an instance");
	    instance_is_reentrant(oid);
	    printf("New instance of V474 created. Underlying object @ %p.\n",
		   ptr.release());
	}
	return OK;
    }
    catch (std::exception const& e) {
	printf("ERROR: %s\n", e.what());
	return ERROR;
    }
}

// Creates the MOOC class for the V474. Instances of V474::Card
// objects can be attached to instances of this MOOC class.

STATUS v474_create_mooc_class(uint8_t cls)
{
    if (cls < 16) {
	printf("MOOC class codes need to be 16, or greater.\n");
	return ERROR;
    }

    if (NOERR != create_class(cls, 0, 0, 6, sizeof(V474::Card*))) {
	printf("Error returned from create_class()!\n");
	return ERROR;
    }
    if (NOERR != name_class(cls, "V474")) {
	printf("Error trying to name the class.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, Init, (PMETHOD) objectInit)) {
	printf("Error trying to add the Init handler.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRREAD, (PMETHOD) devReading)) {
	printf("Error trying to add the reading handler.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRSET, (PMETHOD) devReadSetting)) {
	printf("Error trying to add the reading-of-the-setting handler.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, sPRSET, (PMETHOD) devSetting)) {
	printf("Error trying to add the setting handler.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRBSTS, (PMETHOD) devBasicStatus)) {
	printf("Error trying to add the basic status handler.\n");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, sPRBCTL, (PMETHOD) devBasicControl)) {
	printf("Error trying to add the basic control handler.\n");
	return ERROR;
    }

    printf("V474 class successfully registered with MOOC.\n");
    return OK;
}
