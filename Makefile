VID = 1.3
PRODUCT = 1

SUPPORTED_VERSIONS = 64 67

MOD_TARGETS = v474.out

include ${PRODUCTS_INCDIR}frontend.mk

v474.out : mooc_class.o ${PRODUCTS_LIBDIR}libvwpp-2.1.a
	${make-mod}
