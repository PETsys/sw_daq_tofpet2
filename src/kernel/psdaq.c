/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the         */
/* GNU General Public License for more details.                         */
/*                                                                      */
/************************************************************************/
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#include "psdaq.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ricardo Bugalho rbugalho at petsyselectronics dot com");
MODULE_DESCRIPTION("PETsys DAQ driver");


const static struct
pci_device_id psdaq_pci_id_tbl[] =
{
	// { VENDOR_ID, DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_DEVICE(0x10EE, 0x7024) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, psdaq_pci_id_tbl);

static int psdaq_dev_probe( struct pci_dev *, const struct pci_device_id *);
static void psdaq_dev_remove( struct pci_dev *);

static struct
pci_driver psdaq_driver =
{
	.name = "psdaq",
	.id_table = psdaq_pci_id_tbl,
	.probe = psdaq_dev_probe,
	.remove= psdaq_dev_remove,
};

static int psdaq_file_open (struct inode *, struct file *);
static int psdaq_file_close (struct inode *, struct file *);
static ssize_t psdaq_file_read (struct file *, char *, size_t, loff_t *);
static ssize_t psdaq_file_write (struct file *, __user const char *, size_t, loff_t *);
static long psdaq_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations psdaq_fops =
{
	.owner = THIS_MODULE,
	.open = psdaq_file_open,
	.release = psdaq_file_close,
	.read = psdaq_file_read,
	.write = psdaq_file_write,
	.unlocked_ioctl = psdaq_ioctl

};


static int __init psdaq_init(void);
static void __exit psdaq_exit(void);

module_init(psdaq_init);
module_exit(psdaq_exit);


/* Base Address register */
struct bar_t {
	resource_size_t len;
	void __iomem *addr;
};

#define MAX_TLP_SIZE 256
#define BUF_SIZE 262144

/* Private structure */
struct psdaq_dev_t {
	struct bar_t bar[2];
	dev_t dev;
	struct cdev cdev;
	unsigned dev_index;
	size_t tlp_count;
	size_t tlp_size;
	char *dma_buf;
	dma_addr_t dma_hwaddr;
	size_t dma_fill;
	size_t dma_used;
};
#define READ_BAR0_REG(reg) readl(psdaq_dev->bar[0].addr + 4*reg)
#define WRITE_BAR0_REG(reg, val) writel(val, psdaq_dev->bar[0].addr + 4*reg)


static struct class *psdaq_dev_class;
static unsigned device_counter = 0;

static int psdaq_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEV_NAME=psdaq%d", device_counter);
	return 0;
}

static int __init psdaq_init(void) 
{
	int err;

	psdaq_dev_class = class_create(THIS_MODULE, "psdaq");
	if (IS_ERR(psdaq_dev_class)) {
		err = PTR_ERR(psdaq_dev_class);
		return err;
	}
	psdaq_dev_class->dev_uevent = psdaq_dev_uevent;

	err = pci_register_driver(&psdaq_driver);
	if (err)
		goto failure_register_pci_driver;
	return 0;

failure_register_pci_driver:
	class_destroy(psdaq_dev_class);
	return err;
}

static void __exit psdaq_exit(void)
{
	pci_unregister_driver(&psdaq_driver);
	class_destroy(psdaq_dev_class);
}


#define WRITE_BAR0_REG(reg, val) writel(val, psdaq_dev->bar[0].addr + 4*reg)
static void psdaq_initcard(struct pci_dev *pdev, struct psdaq_dev_t *psdaq_dev)
{ 
	psdaq_dev->tlp_size = pcie_get_mps(pdev);
	// Cap TLP size to tested values
	if(psdaq_dev->tlp_size > MAX_TLP_SIZE) psdaq_dev->tlp_size = MAX_TLP_SIZE;	

	psdaq_dev->tlp_count = BUF_SIZE / psdaq_dev->tlp_size;

	WRITE_BAR0_REG(0, 1);                   // Write: DCSR (offset 0) with value of 1 (Reset Device)
	WRITE_BAR0_REG(0, 0);                   // Write: DCSR (offset 0) with value of 0 (Make Active)

	WRITE_BAR0_REG(1, 0x00800080);	// Disable DMA and interrupts

	WRITE_BAR0_REG(2, psdaq_dev->dma_hwaddr);        // Write: Write DMA TLP Address register with starting address
	WRITE_BAR0_REG(3, psdaq_dev->tlp_size/4);          // Write: Write DMA TLP Size register with defined default value
	WRITE_BAR0_REG(4, psdaq_dev->tlp_count);           // Write: Write DMA TLP Count register with defined default value
	WRITE_BAR0_REG(5, 0x00000000);          // Write: Write DMA TLP Pattern register with default value (0x0)
}


static int psdaq_dev_probe( struct pci_dev *pdev,  const struct pci_device_id *id)
{
	int err = 0, i;
	int mem_bars;
	struct psdaq_dev_t *psdaq_dev;
	struct device *dev;

	printk(KERN_INFO"psdaq_dev_probe called\n");

	psdaq_dev = kmalloc(sizeof(struct psdaq_dev_t), GFP_KERNEL);
	if (!psdaq_dev) {
		err = -ENOMEM;
		goto failure_kmalloc;
	}
	
	// Initialize to sensible values
	for (i = 0; i < 2; i++) {
		psdaq_dev->bar[i].addr = NULL;
		psdaq_dev->bar[i].len = 0;
	}
	
	// Add device number
	psdaq_dev->dev_index = device_counter;

	err = pci_enable_device_mem(pdev);
	if (err)
		goto failure_pci_enable;
	
	
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if(err) goto failure_dma_mask;
	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if(err) goto failure_dma_mask;

	/* Request only the BARs that contain memory regions */
	mem_bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions(pdev, mem_bars, "psdaq");
	if (err)
		goto failure_pci_regions;
	
	
	for (i = 0; i < 2; i++) {
		void *addr = pci_ioremap_bar(pdev, i);
		if(IS_ERR(addr)) {
			err = PTR_ERR(addr);
			goto failure_ioremap;
		}
		psdaq_dev->bar[i].addr = addr;
		psdaq_dev->bar[i].len = pci_resource_len(pdev, i);
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	psdaq_dev->dma_buf = pci_alloc_consistent(pdev, BUF_SIZE, &psdaq_dev->dma_hwaddr);
	if(psdaq_dev->dma_buf == NULL) {
		err = -ENOMEM;
		goto failure_dma_allocation;
	}
	psdaq_dev->dma_fill = 0;
	psdaq_dev->dma_used = 0;

	psdaq_initcard(pdev, psdaq_dev);

	/* Get device number range */
	err = alloc_chrdev_region(&psdaq_dev->dev, psdaq_dev->dev_index, 1, "psdaq");
	if (err)
		goto failure_alloc_chrdev_region;


	/* connect cdev with file operations */
	cdev_init(&psdaq_dev->cdev, &psdaq_fops);
	psdaq_dev->cdev.owner = THIS_MODULE;

	/* add major/min range to cdev */
	err = cdev_add(&psdaq_dev->cdev, psdaq_dev->dev, 1);
	if (err)
		goto failure_cdev_add;

	dev = device_create(psdaq_dev_class, &pdev->dev,
				psdaq_dev->dev,
				NULL,
				"psdaq%d",
				psdaq_dev->dev_index);

	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto failure_device_create;
	}

	device_counter += 1;
	pci_set_drvdata(pdev, psdaq_dev);
	dev_info(&pdev->dev, "claimed by psdaq\n");

	return 0;

failure_device_create:
	cdev_del(&psdaq_dev->cdev);

failure_cdev_add:
	unregister_chrdev_region(psdaq_dev->dev, 1);

failure_alloc_chrdev_region:
	pci_free_consistent(pdev, BUF_SIZE, psdaq_dev->dma_buf, psdaq_dev->dma_hwaddr);

failure_dma_allocation:
	for (i = 0; i < 2; i++)
		if (psdaq_dev->bar[i].len)
			iounmap(psdaq_dev->bar[i].addr);

failure_ioremap:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

failure_pci_regions:
failure_dma_mask:
	pci_disable_device(pdev);

failure_pci_enable:
	kfree(psdaq_dev);

failure_kmalloc:
	return err;
}

static void psdaq_dev_remove( struct pci_dev *pdev)
{
	int i;
	struct psdaq_dev_t *psdaq_dev = pci_get_drvdata(pdev);

	device_destroy(psdaq_dev_class, psdaq_dev->dev);
	cdev_del(&psdaq_dev->cdev);
	unregister_chrdev_region(psdaq_dev->dev, 1);

	pci_free_consistent(pdev, BUF_SIZE, psdaq_dev->dma_buf, psdaq_dev->dma_hwaddr);

	for (i = 0; i < 2; i++)
		if (psdaq_dev->bar[i].len)
			iounmap(psdaq_dev->bar[i].addr);

	pci_release_selected_regions(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	pci_disable_device(pdev);
	kfree(psdaq_dev);
}

static int psdaq_file_open (struct inode *inode, struct file *file)
{
	struct psdaq_dev_t *psdaq_dev = container_of(inode->i_cdev, struct psdaq_dev_t, cdev);
	file->private_data = psdaq_dev;
	return 0;
}


static int psdaq_file_close (struct inode *inode, struct file *file)
{
	return 0;
}


static ssize_t psdaq_file_read (struct file *file, char *buf, size_t count, loff_t *off)
{
	struct psdaq_dev_t *psdaq_dev = file->private_data;

	int err;
	size_t available, n;
	if (psdaq_dev->dma_used == psdaq_dev->dma_fill) {
		WRITE_BAR0_REG(0, 1);            // Write: DCSR (offset 0) with value of 1 (Reset Device)
		WRITE_BAR0_REG(0, 0);            // Write: DCSR (offset 0) with value of 0 (Make Active)
		WRITE_BAR0_REG(1, 0x00000001);   // Start DMA
	
		do {
			udelay(2);
		} while((READ_BAR0_REG(1) & 0x100) == 0);

		psdaq_dev->dma_used = 0;
		psdaq_dev->dma_fill = psdaq_dev->tlp_count * psdaq_dev->tlp_size;
	}



	available = psdaq_dev->dma_fill - psdaq_dev->dma_used;
	n = (count  < available) ? count : available;
	err = copy_to_user(buf, (psdaq_dev->dma_buf)+(psdaq_dev->dma_used), n);
	if(err) return -EFAULT;
	psdaq_dev->dma_used += n;
	return n;
}

static ssize_t psdaq_file_write (struct file *file, __user const char *buf, size_t count, loff_t *off)
{
	return -EINVAL;
}

static long  psdaq_ioctl(struct file *file, unsigned int request, unsigned long argp)
{
	struct ioctl_reg_t ioctl_reg;
	
	struct psdaq_dev_t *psdaq_dev = file->private_data;
	
	int err;
	if(request == PSDAQ_IOCTL_READ_REGISTER) {
		err = copy_from_user(&ioctl_reg, (struct ioctl_reg_t *)argp, sizeof(struct ioctl_reg_t));
		if(err > 0) return -EFAULT;
		
		ioctl_reg.value = readl(psdaq_dev->bar[1].addr + ioctl_reg.offset);
		
		err = copy_to_user((struct ioctl_reg_t *)argp, &ioctl_reg, sizeof(struct ioctl_reg_t));
		if(err > 0) return -EFAULT;
		
		return 0;
		
	}
	else if(request == PSDAQ_IOCTL_WRITE_REGISTER) {
		err = copy_from_user(&ioctl_reg, (struct ioctl_reg_t *)argp, sizeof(struct ioctl_reg_t));
		if(err > 0) return -EFAULT;

		writel(ioctl_reg.value, psdaq_dev->bar[1].addr + ioctl_reg.offset);
		return 0;
	}

	return -EINVAL;
}




