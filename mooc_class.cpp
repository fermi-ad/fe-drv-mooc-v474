#include <vxWorks.h>
#include <vme.h>
#include <sysLib.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <memory>
#include <vwpp-2.1.h>
#include <mooc++-4.6.h>

int v474_lock_tmo = 1500;
int v474_debug = 0;

typedef unsigned char chan_t;

// Include some macros used by the CAMAC front-ends. This the V474 and
// V473 cards are loosely based on the C473 (CAMAC-based) cards, they
// shared some SSDN definitions. So these macros are useful.

#define OMSPDEF_TO_CHAN(a)    ((chan_t) (a).chan & 0x3)
#define	REQ_TO_CHAN(a)	      OMSPDEF_TO_CHAN(*(OMSP_DEF const*) &(a)->OMSP)

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

#define N_CHAN	4

// Junk class. Some versions of GCC don't honor the
// constructor/destructor attributes unless there's a C++ global
// object needing to be created/destroyed. This small section creates
// an unused object to make sure these functions are called.

namespace V474 { class Junk {} junk; };

namespace V474 {

    class Card : public vwpp::Uncopyable {
	uint16_t* const baseAddr;
	vwpp::Mutex mutex;

	typedef vwpp::Mutex::PMLock<Card, &Card::mutex> ObjLock;

	std::vector<bool> const zero_dac;
	uint16_t lastSetting[N_CHAN];

	static uint16_t* computeBaseAddr(uint8_t);

     public:
	class Lock : public vwpp::NoHeap {
	    ObjLock lock;
	    
	 public:
	    explicit Lock(Card& card) : lock(&card) {}
	    operator ObjLock const& () { return lock; }
	};

	Card(uint8_t const dip, bool const zero_dac[N_CHAN]) :
	    baseAddr(computeBaseAddr(dip)),
	    zero_dac(zero_dac, zero_dac + N_CHAN)
	{
	    if (baseAddr[MODID_OFFSET] != 0x01da)
		throw std::runtime_error("Did not find V474 at configured "
					 "address");

	    lastSetting[0] = baseAddr[DAC_OFFSET(0)];
	    lastSetting[1] = baseAddr[DAC_OFFSET(1)];
	    lastSetting[2] = baseAddr[DAC_OFFSET(2)];
	    lastSetting[3] = baseAddr[DAC_OFFSET(3)];
	}

	void off(ObjLock const&, int const chan)
	{
	    baseAddr[ONOFF_OFFSET(chan)] = 0;
	    if (zero_dac.at(chan))
		baseAddr[DAC_OFFSET(chan)] = 0;
	}

	void on(ObjLock const&, int const chan)
	{
	    baseAddr[ONOFF_OFFSET(chan)] = 1;
	    if (zero_dac.at(chan))
		baseAddr[DAC_OFFSET(chan)] = lastSetting[chan];
	}

	void reset(ObjLock const&, int const chan)
	{
	    baseAddr[RESET_OFFSET(chan)] = 1;
	}

	bool isOff(ObjLock const& lock, int const chan)
	{ return (status(lock, chan) & 0x400) == 0; }

	uint16_t adc(ObjLock const&, int const chan)
	{ return baseAddr[ADC_OFFSET(chan)]; }

	uint16_t dac(ObjLock const& lock, int const chan)
	{
	    return (zero_dac.at(chan) && isOff(lock, chan)) ?
		lastSetting[chan] : baseAddr[DAC_OFFSET(chan)];
	}

	void dac(ObjLock const& lock, int const chan, uint16_t const val)
	{
	    lastSetting[chan] = val;
	    if (!zero_dac.at(chan) || !isOff(lock, chan))
		baseAddr[DAC_OFFSET(chan)] = val;
	}

	uint16_t status(ObjLock const&, int const chan)
	{
	    return 0x24ff & baseAddr[STS_OFFSET(chan)];
	}
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
    STATUS v474_create_mooc_instance(unsigned short, uint8_t, int, int, int,
				     int);
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

static STATUS objectInit(short const oid, V474::Card* const ptr, void const*,
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
    if (chan >= N_CHAN)
	return ERR_BADCHN;

    V474::Card::Lock lock(**ivs);

    *rep = (*ivs)->adc(lock, chan);
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
    if (chan >= N_CHAN)
	return ERR_BADCHN;

    V474::Card::Lock lock(**ivs);

    *rep = (*ivs)->dac(lock, chan);
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
    if (chan >= N_CHAN)
	return ERR_BADCHN;

    V474::Card::Lock lock(**ivs);

    (*ivs)->dac(lock, chan, DATAS(req));
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
    if (chan >= N_CHAN)
	return ERR_BADCHN;

    V474::Card::Lock lock(**obj);

    switch (DATAS(req)) {
     case 1:
	(*obj)->off(lock, chan);
	return NOERR;

     case 2:
	(*obj)->on(lock, chan);
	return NOERR;

     case 3:
	(*obj)->reset(lock, chan);
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
    if (chan >= N_CHAN)
	return ERR_BADCHN;

    V474::Card::Lock lock(**obj);

    *rep = (*obj)->status(lock, chan);
    return NOERR;
}

// Creates an instance of the MOOC V474 class.

STATUS v474_create_mooc_instance(unsigned short const oid, uint8_t const addr,
				 int const zCh0, int const zCh1,
				 int const zCh2, int const zCh3)
{
    try {
	short const cls = find_class("V474");

	if (cls == -1)
	    throw std::runtime_error("V474 class is not registered with MOOC");

	bool const zDac[N_CHAN] = { zCh0 != 0, zCh1 != 0,
				    zCh2 != 0, zCh3 != 0 };
	
	std::auto_ptr<V474::Card> ptr(new V474::Card(addr, zDac));

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
