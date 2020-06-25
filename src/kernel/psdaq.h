#ifndef __PSDAQ_H__
#define __PSDAQ_H__

#include <linux/ioctl.h>
#include <linux/types.h>

struct ioctl_reg_t
{
	uint32_t offset;
	uint32_t value;
} ;
#define PSDAQ_IOCTL_READ_REGISTER _IOR('q', 1, struct ioctl_reg_t *)
#define PSDAQ_IOCTL_WRITE_REGISTER _IOW('q', 2, struct ioctl_reg_t *)

#endif