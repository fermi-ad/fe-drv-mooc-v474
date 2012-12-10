# $Id$

VID = 1.0
PRODUCT = 1

SUPPORTED_VERSIONS = 55 61 64 67

MOD_TARGETS = v474.out

include ${PRODUCTS_INCDIR}frontend-2.3.mk

v474.out : mooc_class.o ${PRODUCTS_LIBDIR}libvwpp-1.0.a
	${make-mod}
