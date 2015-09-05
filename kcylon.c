
/**
 * @file   kcylon.c
 * @author Benjamin James
 * @date   4 September 2015
 * @brief A simple kernel module for controlling 10 LEDs over
 * GPIO in a cylon pattern which can be stopped/started by
 * pressing a button. Sysfs mounts these GPIO ports under
 * /sys/class/gpio/ as gpio65 for gpio port 65, for example.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin James");
MODULE_DESCRIPTION("A cylon kernel module");
MODULE_VERSION(0.1);

#define NUM_LEDS 10
static unsigned int led_pins[NUM_LEDS] = {
	65,
	46,
	26,
	44,
	68,
	67,
	47,
	45,
	69,
	66
};
/*
module_param(led_pins[0], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[0], "GPIO LED 0 pin (default=65)");

module_param(led_pins[1], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[1], "GPIO LED 1 pin (default=46)");

module_param(led_pins[2], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[2], "GPIO LED 2 pin (default=26)");

module_param(led_pins[3], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[3], "GPIO LED 3 pin (default=44)");

module_param(led_pins[4], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[4], "GPIO LED 4 pin (default=68)");

module_param(led_pins[5], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[5], "GPIO LED 5 pin (default=67)");

module_param(led_pins[6], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[6], "GPIO LED 6 pin (default=47)");

module_param(led_pins[7], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[7], "GPIO LED 7 pin (default=45)")

module_param(led_pins[8], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[8], "GPIO LED 8 pin (default=69)");

module_param(led_pins[9], uint, S_IRUGO);
MODULE_PARM_DESC(led_pins[9], "GPIO LED 9 pin (default=66)");
*/

static unsigned int sleep_time = 100; /* in milliseconds */
/*
module_param(sleep_time, uint, S_IRUGO);
MODULE_PARM_DESC(sleep_time, "LED time between cylon steps (min:1 default:100 max:10000)");
*/
static char gpio_name[8] = "gpioXXX";

static unsigned int button_pin_right = 27;
static unsigned int button_pin_left = 61;

static volatile int button_level;

static int irq_number_right;
static int irq_number_left;

static struct task_struct *task;
/** @brief Prototype for the irq handler
 * Used as a callback for button presses
 */
static irq_handler_t kcylon_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);


/** @brief kthread main loop
 *
 * @param void pointer which isn't used
 * @return returns 0 upon success
 */
static int cylon(void *v)
{
	int current_led = 0;
	int last_led = 0;
	bool rising = 1;
	unsigned int thread_sleep_time = sleep_time;
	printk(KERN_INFO "KCYLON: Thread has started\n");
	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);

		if (last_led >= 0 && last_led <= 9)
			gpio_set_value(led_pins[last_led], false);
		gpio_set_value(led_pins[current_led], true);

		if (rising)
			last_led = current_led++;
		else
			last_led = current_led--;
		if (current_led > 9) {
			current_led = 9;
			rising = 0;
		}
		if (current_led < 0) {
			current_led = 0;
			rising = 1;
		}
		if (button_level > 0)
			thread_sleep_time = sleep_time * 10 * button_level;
		else if (button_level < 0)
			thread_sleep_time = sleep_time / (-10 * button_level);
		else
			thread_sleep_time = sleep_time;
		set_current_state(TASK_INTERRUPTABLE);
		msleep(thread_sleep_time);
	}
	printk(KERN_INFO "KCYLON: Thread has completed\n");
	return 0;
}

/** @brief Kernel module entry point
 * Sets up all of the GPIOs and the button
 * interrupts
 *
 * @return returns 0 on success, -ENODEV if gpio isn't possible
 */
static int __init kcylon_init(void)
{
	int i, ret = 0;
	button_level = 0;
	printk(KERN_INFO "KCYLON: Initializing kcylon module\n");
	for (i = 0; i < NUM_LEDS; i++) {
		if (!gpio_is_valid(led_pins[i])) {
			printk(KERN_INFO "KCYLON: LED pin %d (GPIO %d) is invalid\n", i + 1, led_pins[i]);
			return -ENODEV;
		}
		gpio_request(led_pins[i], "sysfs");
		gpio_direction_output(led_pins[i], false);
		gpio_export(led_pins[i], false);
	}
	gpio_request(button_pin_right, "sysfs");
	gpio_direction_input(button_pin_right);
	gpio_set_debounce(button_pin_right, 200);
	gpio_export(button_pin_right, false);

	gpio_request(button_pin_left, "sysfs");
	gpio_direction_input(button_pin_left);
	gpio_set_debounce(button_pin_left, 200);
	gpio_export(button_pin_left, false);

	printk(KERN_INFO "KCYLON: The button is: %d\n", gpio_get_value(button_pin));

	irq_number_left = gpio_to_irq(button_pin_left);
	irq_number_right = gpio_to_irq(button_pin_right);
	printk(KERN_INFO "KCYLON: The buttons are mapped to IRQ %d and %d\n", irq_number_left, irq_number_right);

	if (request_irq(irq_number_left, (irq_handler_t) kcylon_irq_handler, IRQF_TRIGGER_RISING, "kcylon_left_button", NULL)) {
		printk(KERN_INFO "KCYLON: Couldn't create an interrupt handler for irq number %d\n", irq_number_left);
		ret = -1;
	}
	if (request_irq(irq_number_right (irq_handler_t) kcylon_irq_handler, IRQF_TRIGGER_RISING, "kcylon_right_button", NULL)) {
		printk(KERN_INFO "KCYLON: Couldn't create an interrupt handler for irq number %d\n", irq_number_right);
		ret = -1;
	}
	task = kthread_run(cylon, NULL, "CYLON_thread");
	if (IS_ERR(task)) {
		printk(KERN_ALERT "KCYLON: Failed to create the thread\n");
		ret = PTR_ERR(task);
	}
	return ret;
}

/** @brief Kernel module exit point
 *  Makes sure all GPIO pins, the
 *  thread, and the interrupt handlers
 *  are deallocated.
 */
static void __exit kcylon_exit(void)
{
	int i;
	kthread_stop(task);
	for (i = 0; i < NUM_LEDS; i++) {
		gpio_set_value(led_pins[i], 0);
		gpio_unexport(led_pins[i]);
		gpio_free(led_pins[i]);
	}
	freeirq(irq_number_left, NULL);
	freeirq(irq_number_right, NULL);
	gpio_unexport(button_pin_left);
	gpio_free(button_pin_left);
	gpio_unexport(button_pin_right);
	gpio_free(button_pin_right);
	printk(KERN_INFO "KCYLON: Goodbye!\n");
}

/** @brief Kernel module interrupt handler
 *  Changes the button level when a button
 *  is pressed. Also it puts limits on the level.
 *
 *  @param irq The irq number that identifies the button
 */
static irq_handler_t kcylon_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	if (irq == irq_number_left) {
		button_level--;
		if (button_level < -10) {
			button_level = -10;
			printk(KERN_INFO "KCYLON: Minimum button level reached\n");
		}
	} else if (irq == irq_number_right) {
		button_level++;
		if (button_level > 10) {
			button_level = 10;
			printk(KERN_INFO "KCYLON: Maximum button level reached\n");
		}
	}
	printk(KERN_INFO "KCYLON: Interrupt received (button level %d)\n", button_level);
	return (irq_handler_t) IRQ_HANDLED;
}
#undef NUM_LEDS

module_init(kcylon_init);
module_exit(kcylon_exit);
