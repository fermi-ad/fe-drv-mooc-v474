#include <vxWorks.h>
#include <vme.h>
#include <sysLib.h>
#include <vector>
#include <vwpp-3.0.h>
#include <errlogLib-2.0.h>
#include <mooc++-4.6.h>

int v474_lock_tmo = 1500;
int v474_debug = 0;

typedef unsigned char chan_t;

// Include some macros used by the CAMAC front-ends. The V474 and V473
// cards are loosely based on the C473 (CAMAC-based) cards, and they
// share some SSDN definitions so these macros are useful.

#define OMSPDEF_TO_CHAN(a)    ((chan_t) (a).chan & 0x3)
#define	REQ_TO_CHAN(a)	      OMSPDEF_TO_CHAN(*(OMSP_DEF const*) &(a)->OMSP)

#define DATAS(req)	(*(unsigned short const*)(req)->data)

// Junk class. Some versions of GCC don't honor the
// constructor/destructor attributes unless there's a C++ global
// object needing to be created/destroyed. This small section creates
// an unused object to make sure these functions are called.

namespace V474 {
    static HLOG hLog = 0;

    class Junk {
     public:
	Junk()
	{
	    hLog = logRegister("V474", 1);
	}
    } junk;
};

namespace V474 {
    using namespace vwpp::v3_0;

    // Simple wrapper class to range-check channel indices. If a
    // channel object has been created, it is always in range.

    class Channel {
	size_t const value;

	Channel();

     public:
	static size_t const TOTAL = 4;

	Channel(size_t const v) : value(v < TOTAL ? v : throw(ERR_BADCHN)) {}

	operator size_t() const { return value; }
    };

    // This class is used to formalize the weird hardware mapping. The
    // channel number is placed in bits 4 through 7, so we can't use
    // an array of 16-bit ints to access the registers. This class
    // sets the step size to be 8 but still accesses each element as a
    // 16-bit value. ("8" is used because the element size is 16 bits
    // so 8, 16-bit values steps 16 bytes in memory.)

    template <VME::AddressSpace Space, typename T, size_t Offset>
    struct RegArray {
	typedef T Type;
	typedef T AtomicType;

	static VME::AddressSpace const space = Space;

	enum { RegOffset = Offset, RegEntries = Channel::TOTAL };

	static Type read(uint8_t volatile* const base,
			 size_t const idx) NOTHROW_IMPL
	{
	    typedef VME::ReadAPI<Type, Offset, VME::Read> API;

	    return API::readMem(base, idx * (16 / sizeof(Type)));
	}

	static void write(uint8_t volatile* const base,
			  size_t const idx, Type const& v) NOTHROW_IMPL
	{
	    typedef VME::WriteAPI<Type, Offset, VME::Write> API;

	    API::writeMem(base, idx * (16 / sizeof(Type)), v);
	}
    };

    // This class controls the underlying hardware.

    class Card : public Uncopyable {
	Mutex mutex;

     public:
	typedef Mutex::PMLock<Card, &Card::mutex> LockType;

     private:
	typedef VME::Memory<VME::A24, VME::D16, 65536, LockType> REG_BANK;

	REG_BANK const hw;
	bool zero_dac[Channel::TOTAL];
	uint16_t lastSetting[Channel::TOTAL];

	// Define the registers in the V474 card.

	typedef RegArray<VME::A24, int16_t, 0x0000> regDAC;
	typedef RegArray<VME::A24, int16_t, 0x0100> regADC;
	typedef RegArray<VME::A24, uint16_t, 0x0200> regStatus;
	typedef RegArray<VME::A24, uint16_t, 0x0300> regOnOff;
	typedef RegArray<VME::A24, uint16_t, 0x0302> regReset;
	typedef VME::Register<VME::A24, uint16_t, 0xff00, VME::Read, VME::NoWrite> regModuleId;
	typedef VME::Register<VME::A24, uint16_t, 0xff02, VME::Read, VME::NoWrite> regVersion;
	typedef VME::Register<VME::A24, uint16_t, 0xff10, VME::Read, VME::Write> regLED;

     public:
	Card(uint8_t const dip, bool const zdac[Channel::TOTAL]) :
	    hw(dip << 16)
	{
	    zero_dac[0] = zdac[0];
	    zero_dac[1] = zdac[1];
	    zero_dac[2] = zdac[2];
	    zero_dac[3] = zdac[3];

	    LockType lock(this);

	    if (hw.get<regModuleId>(lock) != 0x01da)
		throw std::runtime_error("Did not find V474 at configured "
					 "address");

	    lastSetting[0] = hw.get_element<regDAC>(lock, 0);
	    lastSetting[1] = hw.get_element<regDAC>(lock, 1);
	    lastSetting[2] = hw.get_element<regDAC>(lock, 2);
	    lastSetting[3] = hw.get_element<regDAC>(lock, 3);

	    uint16_t const version = hw.get<regVersion>(lock);

	    logInform3(V474::hLog, "V474 found (version %d.%d, DIP switch %d)",
		       version / 256, version % 256, dip);
	}

	void off(LockType const& lock, Channel const chan)
	{
	    hw.set_element<regOnOff>(lock, chan, 0);
	    if (zero_dac[chan])
		hw.set_element<regDAC>(lock, chan, 0);
	    hw.set_field<regLED>(lock, 1 << chan, 0);
	}

	void on(LockType const& lock, Channel const chan)
	{
	    if (zero_dac[chan])
		hw.set_element<regDAC>(lock, chan, lastSetting[chan]);
	    hw.set_element<regOnOff>(lock, chan, 1);
	    hw.set_field<regLED>(lock, 1 << chan, 1 << chan);
	}

	void reset(LockType const& lock, Channel const chan)
	{
	    hw.set_element<regReset>(lock, chan, 1);
	}

	bool isOff(LockType const& lock, Channel const chan)
	{ return (status(lock, chan) & 0x400) == 0; }

	uint16_t adc(LockType const& lock, Channel const chan)
	{ return hw.get_element<regADC>(lock, chan); }

	uint16_t dac(LockType const& lock, Channel const chan)
	{
	    return (zero_dac[chan] && isOff(lock, chan)) ?
		lastSetting[chan] : hw.get_element<regDAC>(lock, chan);
	}

	void dac(LockType const& lock, Channel const chan, uint16_t const val)
	{
	    lastSetting[chan] = val;
	    if (!zero_dac[chan] || !isOff(lock, chan))
		hw.set_element<regDAC>(lock, chan, val);
	}

	uint16_t status(LockType const& lock, Channel const chan)
	{
	    return 0x24ff & hw.get_element<regStatus>(lock, chan);
	}
    };
};

typedef int16_t typReading;
typedef uint16_t typStatus;

static STATUS devBasicStatus(short, RS_REQ const*, typStatus*,
			     V474::Card* const*);

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

    ALARM_GUTS* const albl = (ALARM_GUTS*) ivs - 1;

    albl->anl_chan = 0;
    albl->anl_typ = SIGNED_SHORT_TYPE;
    albl->aread = 0;
    albl->anotify = albl->dnotify = 0;
    albl->oid = oid;
    albl->ivs = ivs;
    albl->dig_chan = 4;
    albl->dig_len = 2;
    albl->dstat = (PMETHOD) devBasicStatus;

    alarm_tabloids_create(albl);
    return OK;
}

static STATUS devReading(short, RS_REQ const* const req, typReading* const rep,
			 V474::Card* const* const ivs)
{
    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;

    try {
	V474::Channel const chan = V474::Channel(REQ_TO_CHAN(req));
	V474::Card::LockType lock(*ivs);

	*rep = (*ivs)->adc(lock, chan);
	return NOERR;
    }
    catch (int16_t status) {
	return status;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "ERROR: devReading threw exception -- %s",
		  e.what());
	return ERR_DEVICEERROR;
    }
}

static STATUS devReadSetting(short, RS_REQ const* const req,
			     typReading* const rep,
			     V474::Card* const* const ivs)
{
    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;

    try {
	V474::Channel const chan = V474::Channel(REQ_TO_CHAN(req));
	V474::Card::LockType lock(*ivs);

	*rep = (*ivs)->dac(lock, chan);
	return NOERR;
    }
    catch (int16_t status) {
	return status;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "ERROR: devReadSetting threw exception -- %s",
		  e.what());
	return ERR_DEVICEERROR;
    }
}

static STATUS devSetting(short, RS_REQ* req, void*,
			 V474::Card* const* const ivs)
{
    if (req->ILEN != sizeof(typReading))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;

    try {
	V474::Channel const chan = V474::Channel(REQ_TO_CHAN(req));
	V474::Card::LockType lock(*ivs);

	(*ivs)->dac(lock, chan, DATAS(req));
	return NOERR;
    }
    catch (int16_t status) {
	return status;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "ERROR: devSetting threw exception -- %s",
		  e.what());
	return ERR_DEVICEERROR;
    }
}

static STATUS devBasicControl(short, RS_REQ const* const req, void*,
			      V474::Card* const* const obj)
{
    if (req->ILEN != sizeof(typStatus))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;

    try {
	V474::Channel const chan = V474::Channel(REQ_TO_CHAN(req));
	V474::Card::LockType lock(*obj);

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
    }
    catch (int16_t status) {
	return status;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "ERROR: devBasicControl threw exception -- "
		  "%s", e.what());
	return ERR_DEVICEERROR;
    }
}

static STATUS devBasicStatus(short, RS_REQ const* const req,
			     typStatus* const rep,
			     V474::Card* const* const obj)
{
    if (req->ILEN != sizeof(typStatus))
	return ERR_BADLEN;
    if (req->OFFSET != 0)
	return ERR_BADOFF;

    try {
	V474::Channel const chan = V474::Channel(REQ_TO_CHAN(req));
	V474::Card::LockType lock(*obj);

	*rep = (*obj)->status(lock, chan);
	return NOERR;
    }
    catch (int16_t status) {
	return status;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "ERROR: devBasicStatus threw exception -- %s",
		  e.what());
	return ERR_DEVICEERROR;
    }
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

	bool const zDac[V474::Channel::TOTAL] = { zCh0 != 0, zCh1 != 0,
						  zCh2 != 0, zCh3 != 0 };

	std::auto_ptr<V474::Card> ptr(new V474::Card(addr, zDac));

	if (ptr.get()) {
	    if (create_instance(oid, cls, ptr.get(), "V474") != NOERR)
		throw std::runtime_error("problem creating an instance");
	    instance_is_reentrant(oid);
	    logInform1(V474::hLog, "New instance of V474 created. Underlying "
		       "object @ %p.", ptr.release());
	}
	return OK;
    }
    catch (std::exception const& e) {
	logAlarm1(V474::hLog, 0, "Error creating V474 (oid: %d) instance.",
		  oid);
	return ERROR;
    }
}

// Creates the MOOC class for the V474. Instances of V474::Card
// objects can be attached to instances of this MOOC class.

STATUS v474_create_mooc_class(uint8_t const cls)
{
    if (cls < 16) {
	logAlarm0(V474::hLog, 0, "MOOC class codes need to be 16, or "
		  "greater.");
	return ERROR;
    }

    short const scl[] = { ALRCLS };

    if (NOERR != create_class(cls, NELEMENTS(scl), scl, 6, sizeof(V474::Card*))) {
	logAlarm0(V474::hLog, 0, "Error returned from create_class()!");
	return ERROR;
    }
    if (NOERR != name_class(cls, "V474")) {
	logAlarm0(V474::hLog, 0, "Error trying to name the class.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, Init, (PMETHOD) objectInit)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the Init handler.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRREAD, (PMETHOD) devReading)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the reading handler.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRSET, (PMETHOD) devReadSetting)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the "
		  "reading-of-the-setting handler.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, sPRSET, (PMETHOD) devSetting)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the setting handler.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, rPRBSTS, (PMETHOD) devBasicStatus)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the basic status "
		  "handler.");
	return ERROR;
    }
    if (NOERR != add_class_msg(cls, sPRBCTL, (PMETHOD) devBasicControl)) {
	logAlarm0(V474::hLog, 0, "Error trying to add the basic control "
		  "handler.");
	return ERROR;
    }
    return OK;
}
