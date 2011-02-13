/*
  A Linux OMAP3 irq interrupt latency and gpio toggle speed test module.
  Requires that two GPIO pins be connected and that they are monitored 
  with an oscope to do the actual timings.

  The latency test involves irq enabling the IRQ_PIN then setting 
  the TEST_PIN high and when the IRQ_PIN's irq handler is
  invoked, setting the TEST_PIN low again. The time between the
  high and low of the TEST_PIN is the irq latency +/- the time it
  takes to set a gpio pin.

  The gpio toggle speed test attempts to measure how long it takes to 
  set/unset a gpio pin by running a tight loop of 1000 iterations doing
  just this. Again watch TEST_PIN with an oscope to do the measurement.

  FWIW, I get an irq latency around 8-9 microseconds and for the toggling
  test. I get a frequency of 2.5 MHz or around 400 nanoseconds for the HIGH/
  LOW cycle or 200 nanoseconds to toggle from one state to another.
 
  The first event seems to take slightly little longer, but the frequency
  stabilizes after that. Not sure what's happening there.
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


static irqreturn_t irqlat_handler(int irq, void *dev_id)
{
	gpio_set_value(TEST_PIN, 0);

	if (irqlat_dev.context) {			
		complete(irqlat_dev.context);			
		irqlat_dev.context = 0;
	}
	
	return IRQ_HANDLED;
}

static void do_latency_test(void)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int result;

	result = request_irq(irqlat_dev.irq,
				irqlat_handler,
				IRQF_TRIGGER_RISING,
				"irqlat",
				&irqlat_dev);

	if (result < 0) {
		printk(KERN_ALERT "request_irq failed: %d\n", result);
		return;
	}

	irqlat_dev.context = &done;
	gpio_set_value(TEST_PIN, 1);

	result = wait_for_completion_interruptible_timeout(&done, HZ / 2);

	if (result == 0) {
		printk(KERN_ALERT "Timed out waiting for interrupt.\n");
		printk(KERN_ALERT "Did you forget to jumper the pins?\n");
	}

	free_irq(irqlat_dev.irq, &irqlat_dev);
}

static void do_toggle_test(void)
{
	int i;

	for (i = 0; i < 1000; i++) {
		gpio_set_value(TEST_PIN, 1);
		gpio_set_value(TEST_PIN, 0);
	}
}

static ssize_t irqlat_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *f_pos)
{
	char cmd[4];
	ssize_t status;

	if (count == 0)
		return 0;

	if (down_interruptible(&irqlat_dev.sem))
		return -ERESTARTSYS;

	if (copy_from_user(cmd, buff, 1)) {
		printk(KERN_ALERT "Error copy_from_user\n");
		status = -EFAULT;
		goto irqlat_write_done;
	}

	/* 
	  Nothing fancy, '1' is latency test, anything else is
          toggle test.
	*/
	if (cmd[0] == '1')
		do_latency_test();
	else
		do_toggle_test();		
			
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

static int __init irqlat_init_pins(void)
{
	if (gpio_request(TEST_PIN, "testpin")) {
		printk(KERN_ALERT "gpio_request failed\n");
		goto init_pins_fail_1;
	}

	if (gpio_direction_output(TEST_PIN, 0)) {
		printk(KERN_ALERT "gpio_direction_output failed\n");
		goto init_pins_fail_2;
	}

	if (gpio_request(IRQ_PIN, "irqpin")) {
		printk(KERN_ALERT "gpio_request(2) failed\n");
		goto init_pins_fail_2;
	}

	if (gpio_direction_input(IRQ_PIN)) {
		printk(KERN_ALERT "gpio_direction_input failed\n");
		goto init_pins_fail_3;
	}

	irqlat_dev.irq = OMAP_GPIO_IRQ(IRQ_PIN);

	return 0;

init_pins_fail_3:
	gpio_free(IRQ_PIN);

init_pins_fail_2:
	gpio_free(TEST_PIN);

init_pins_fail_1:

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

	if (irqlat_init_pins() < 0)
		goto init_fail_3;

	return 0;

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
	gpio_free(TEST_PIN);
	gpio_free(IRQ_PIN);

	device_destroy(irqlat_dev.class, irqlat_dev.devt);
  	class_destroy(irqlat_dev.class);

	cdev_del(&irqlat_dev.cdev);
	unregister_chrdev_region(irqlat_dev.devt, 1);
}
module_exit(irqlat_exit);


MODULE_AUTHOR("Scott Ellis");
MODULE_DESCRIPTION("A module for testing irq latency");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.2");

