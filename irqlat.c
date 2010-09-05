/*
  Gumstix OMAP3 irq interrupt latency test
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <mach/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>


#define IRQ_PIN 146
#define TEST_PIN 147

struct irqlat_dev {
	dev_t devt;
	struct cdev cdev;
	struct semaphore sem;
	struct class *class;
	void *context;
	int irq;
};

static struct irqlat_dev irqlat_dev;


static void irqlat_complete(void *arg)
{
	complete(arg);
}

static irqreturn_t irqlat_handler(int irq, void *dev_id)
{
	if (irqlat_dev.context) {			
		gpio_set_value(TEST_PIN, 0);
		irqlat_complete(irqlat_dev.context);			
		irqlat_dev.context = 0;
	}
	
	return IRQ_HANDLED;
}

static ssize_t irqlat_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *f_pos)
{
	DECLARE_COMPLETION_ONSTACK(done);
	ssize_t status;

	if (count == 0)
		return 0;

	if (down_interruptible(&irqlat_dev.sem))
		return -ERESTARTSYS;

	irqlat_dev.context = &done;

	status = request_irq(irqlat_dev.irq,
				irqlat_handler,
				IRQF_TRIGGER_RISING,
				"irqlat",
				&irqlat_dev);

	if (status < 0) {
		printk(KERN_ALERT "request_irq failed: %d\n", 
			status);
		goto irqlat_write_done;
	}
	
	gpio_set_value(TEST_PIN, 1);
	wait_for_completion(&done);
	free_irq(irqlat_dev.irq, &irqlat_dev);
	
	status = count;
	*f_pos += count;

irqlat_write_done:

	up(&irqlat_dev.sem);

	return status;
}

static const struct file_operations irqlat_fops = {
	.owner = THIS_MODULE,
	.write = irqlat_write,
};

static int __init irqlat_init_cdev(void)
{
	int error;

	irqlat_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&irqlat_dev.devt, 0, 1, "irqlat");
	if (error < 0) {
		printk(KERN_ALERT 
			"alloc_chrdev_region() failed: error = %d \n", 
			error);
		
		return -1;
	}

	cdev_init(&irqlat_dev.cdev, &irqlat_fops);
	irqlat_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&irqlat_dev.cdev, irqlat_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: error = %d\n", error);
		unregister_chrdev_region(irqlat_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init irqlat_init_class(void)
{
	irqlat_dev.class = class_create(THIS_MODULE, "irqlat");

	if (!irqlat_dev.class) {
		printk(KERN_ALERT "class_create(irqlat) failed\n");
		return -1;
	}

	if (!device_create(irqlat_dev.class, NULL, irqlat_dev.devt, 
				NULL, "irqlat")) {
		class_destroy(irqlat_dev.class);
		return -1;
	}

	return 0;
}

static int __init irqlat_init_irq_pin(void)
{
	if (gpio_request(IRQ_PIN, "irqpin")) {
		printk(KERN_ALERT "gpio_request failed\n");
		goto init_irq_pin_fail_1;
	}

	if (gpio_direction_input(IRQ_PIN)) {
		printk(KERN_ALERT "gpio_direction_input failed\n");
		goto init_irq_pin_fail_2;
	}

	irqlat_dev.irq = OMAP_GPIO_IRQ(IRQ_PIN);

	return 0;

init_irq_pin_fail_2:
	gpio_free(IRQ_PIN);

init_irq_pin_fail_1:
	return -1;
}

static int __init irqlat_init_test_pin(void)
{
	if (gpio_request(TEST_PIN, "testpin")) {
		printk(KERN_ALERT "gpio_request failed\n");
		goto init_test_pin_fail_1;
	}

	if (gpio_direction_output(TEST_PIN, 0)) {
		printk(KERN_ALERT "gpio_direction_input failed\n");
		goto init_test_pin_fail_2;
	}

	return 0;

init_test_pin_fail_2:
	gpio_free(TEST_PIN);

init_test_pin_fail_1:
	return -1;
}

static int __init irqlat_init(void)
{
	memset(&irqlat_dev, 0, sizeof(irqlat_dev));
	sema_init(&irqlat_dev.sem, 1);

	if (irqlat_init_cdev())
		goto init_fail_1;

	if (irqlat_init_class())
		goto init_fail_2;

	if (irqlat_init_irq_pin() < 0)
		goto init_fail_3;

	if (irqlat_init_test_pin() < 0)
		goto init_fail_4;

	return 0;

init_fail_4:
	gpio_free(IRQ_PIN);

init_fail_3:
	device_destroy(irqlat_dev.class, irqlat_dev.devt);
  	class_destroy(irqlat_dev.class);

init_fail_2:
	cdev_del(&irqlat_dev.cdev);
	unregister_chrdev_region(irqlat_dev.devt, 1);

init_fail_1:

	return -1;
}
module_init(irqlat_init);

static void __exit irqlat_exit(void)
{
	gpio_free(IRQ_PIN);
	gpio_free(TEST_PIN);

	device_destroy(irqlat_dev.class, irqlat_dev.devt);
  	class_destroy(irqlat_dev.class);

	cdev_del(&irqlat_dev.cdev);
	unregister_chrdev_region(irqlat_dev.devt, 1);
}
module_exit(irqlat_exit);


MODULE_AUTHOR("Scott Ellis");
MODULE_DESCRIPTION("A module for testing irq latency");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.1");

