#ifndef XEN_DUMMY_SUPPLYMENTARY
#define XEN_DUMMY_SUPPLYMENTARY

#ifndef DPRINTF
    #include <syslog.h>
	#define DPRINTF(_f, _a...)           syslog(LOG_INFO, _f, ##_a)
#endif

#endif
