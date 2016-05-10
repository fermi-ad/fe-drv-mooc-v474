// $Id$

#include <vxWorks.h>
#include <vme.h>
#include <sysLib.h>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <vwpp-1.0.h>
#include <mooc++-4.3.h>

int v474_lock_tmo = 1500;
int v474_debug = 0;

typedef unsigned char chan_t;

// Include some macros used by the CAMAC front-ends. This the V474 and
// V473 cards are loosely based on the C473 (CAMAC-based) cards, they
// shared some SSDN definitions. So these macros are useful.

#define OMSPDEF_TO_CHAN(a)    ((chan_t) (a).chan & 0x3)
#define	REQ_TO_CHAN(a)	      OMSPDEF_TO_CHAN(*(OMSP_DEF const*) &(a)->OMSP)
#define OMSPDEF_TO_ZERODAC(a) (((chan_t) (a).chan & 0x4) != 0)
#define	REQ_TO_ZERODAC(a)     OMSPDEF_TO_ZERODAC(*(OMSP_DEF const*) &(a)->OMSP)

#define DATAS(req)	(*(unsigned short const*)(req)->data)

// Offsets into the memory map.

#define BASE_OFFSET(o)	((o) / sizeof(uint16_t))

#define DAC_OFFSET(c)	BASE_OFFSET((c) << 4)
#define ADC_OFFSET(c)	BASE_OFFSET(0x100 + ((c) << 4))
#define STS_OFFSET(c)	BASE_OFFSET(0x200 + ((c) << 4))
#define ONOFF_OFFSET(c)	BASE_OFFSET(0x300 + ((c) << 4))
#define RESET_OFFSET(c)	BASE_OFFSET(0x302 + ((c) << 4))
#define MODID_OFFSET	BASE_OFFSET(0xff00)
#define VER_OFFSET	BASE_OFFSET(0xff02)
#define RESET474_OFFSET	BASE_OFFSET(0xfffe)

// Junk class. Some versions of GCC don't honor the
// constructor/destructor attributes unless there's a C++ global
// object needing to be created/destroyed. This small section creates
// an unused object to make sure these functions are called.

namespace V474 { class Junk {} junk; };

namespace V474 {

    struct Card {
	uint16_t* baseAddr;
	vwpp::Mutex mutex;

	uint16_t lastSetting[4];

	static uint16_t* computeBaseAddr(uint8_t);

	Card(uint8_t const dip) : baseAddr(computeBaseAddr(dip))
	{
	    if (baseAddr[0xff00 / 2] != 0x01da)
		throw std::runtime_error("Did not find V474 at configured "
					 "address");

	    lastSetting[0] = baseAddr[DAC_OFFSET(0)];
	    lastSetting[1] = baseAddr[DAC_OFFSET(1)];
	    lastSetting[2] = baseAddr[DAC_OFFSET(2)];
	    lastSetting[3] = baseAddr[DAC_OFFSET(3)];
	}

	bool isOff(int const chan) const
	{ return baseAddr[ONOFF_OFFSET(chan)] == 0; }

     private:
	Card();
	Card(Card const&);
	Card& operator=(Card const&);
    };

    uint16_t* Card::computeBaseAddr(uint8_t const dip)
    {
	char* tmp;
	uint32_t const addr = (uint32_t) dip << 16;

	if (ERROR == sysBusToLocalAdrs(VME_AM_STD_SUP_DATA,
				       reinterpret_cast<char*>(addr), &tmp))
	    throw std::runtime_error("illegal A	24 VME address");
	return reinterpret_cast<uint16_t*>(tmp);
    }

};

extern "C" {
    STATUS objectInit(short, V474::Card*, void const*, V474::Card**);
    STATUS v474_create_mooc_instance(unsigned short, uint8_t);
    STATUS v474_create_mooc_class(uint8_t);
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
typedef uint16_t typStatus;

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

    *rep = ((*ivs)->baseAddr)[ADC_OFFSET(chan)];
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

    *rep = ((*ivs)->baseAddr)[DAC_OFFSET(chan)];
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

    (*ivs)->lastSetting[chan] = DATAS(req);
    ((*ivs)->baseAddr)[DAC_OFFSET(chan)] =
	(REQ_TO_ZERODAC(req) && (*ivs)->isOff(chan)) ? 0 : DATAS(req);
    return NOERR;
}

static STATUS devBasicControl(short, RS_REQ const* const req, void*,
			      V474::Card* const* const obj)
{
    chan_t const chan = REQ_TO_CHAN(req);

    if (req->ILEN != sizeof(typStatus))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;
    if (chan > 3)
	return ERR_BADCHN;

    switch (DATAS(req)) {
     case 1:
	((*obj)->baseAddr)[ONOFF_OFFSET(chan)] = 0;
	if (REQ_TO_ZERODAC(req))
	    ((*obj)->baseAddr)[DAC_OFFSET(chan)] = 0;
	return NOERR;

     case 2:
	((*obj)->baseAddr)[ONOFF_OFFSET(chan)] = 1;
	if (REQ_TO_ZERODAC(req))
	    ((*obj)->baseAddr)[DAC_OFFSET(chan)] = (*obj)->lastSetting[chan];
	return NOERR;

     case 3:
	((*obj)->baseAddr)[RESET_OFFSET(chan)] = 1;
	return NOERR;

     default:
	return ERR_WRBASCON;
    }
    return NOERR;
}

static STATUS devBasicStatus(short, RS_REQ const* const req,
			     typStatus* const rep,
			     V474::Card* const* const obj)
{
    chan_t const chan = REQ_TO_CHAN(req);

    if (req->ILEN != sizeof(typStatus))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;
    if (chan > 3)
	return ERR_BADCHN;

    *rep = 0x24ff & ((*obj)->baseAddr)[STS_OFFSET(chan)];
    return NOERR;
}

// Creates an instance of the MOOC V474 class.

STATUS v474_create_mooc_instance(unsigned short const oid,
				 uint8_t const addr)
{
    try {
	short const cls = find_class("V474");

	if (cls == -1)
	    throw std::runtime_error("V474 class is not registered with MOOC");

	std::auto_ptr<V474::Card> ptr(new V474::Card(addr));

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

STATUS v474_create_mooc_class(uint8_t const cls)
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
