/**
 * @file   kcylon.c
 * @author Benjamin James
 * @date   6 September 2015
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
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin James");
MODULE_DESCRIPTION("A cylon kernel module");
MODULE_VERSION("0.2");

#define NUM_LEDS 10

/**
 * @brief LED pin assignments
 */
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

/**
 * @brief The pin of the button used for input
 */
static unsigned int button_pin = 27;

/**
 * @brief The default sleep time in milliseconds
 */
static unsigned int sleep_time = 100;

/**
 * @brief The variable the button alters, so
 * consequently, it must have a mutex to make
 * sure the threads aren't using it at the same
 * time
 */
static volatile int button_level;
static struct mutex button_level_mutex;

/**
 * @brief The direction the cylon beam is going in
 */
static int button_direction;

/**
 * @brief The ID of the button for the IRQ interrupts
 */
static int irq_number;

/**
 * @brief The struct containing info on the worker thread
 */
static struct task_struct *task;

/**
 * @brief Benchmarking variables
 */
static struct timespec ts_current, ts_last, ts_diff;

/**
 * @brief Prototype for the irq handler
 *
 * Used as a callback for button presses
 */
static irq_handler_t kcylon_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

/**
 * @brief kthread main loop
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
		mutex_lock(&button_level_mutex);
		if (button_level > 0)
			thread_sleep_time = sleep_time * button_level;
		else if (button_level < 0)
			thread_sleep_time = sleep_time / (-1 * button_level);
		else
			thread_sleep_time = sleep_time;
		mutex_unlock(&button_level_mutex);
		set_current_state(TASK_INTERRUPTIBLE);
		msleep(thread_sleep_time);
	}
	printk(KERN_INFO "KCYLON: Thread has completed\n");
	return 0;
}

/**
 * @brief Kernel module entry point
 * Sets up all of the GPIOs and the button
 * interrupts
 *
 * @return returns 0 on success, -ENODEV if gpio isn't possible
 */
static int __init kcylon_init(void)
{
	int i, ret = 0;
	mutex_init(&button_level_mutex);
	button_level = 0;
	button_direction = -1;
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
	gpio_request(button_pin, "sysfs");
	gpio_direction_input(button_pin);
	gpio_set_debounce(button_pin, 200);
	gpio_export(button_pin, false);

	irq_number = gpio_to_irq(button_pin);
	printk(KERN_INFO "KCYLON: The button %u is mapped to IRQ %d\n", button_pin, irq_number);

	if (request_irq(irq_number, (irq_handler_t) kcylon_irq_handler, IRQF_TRIGGER_RISING, "kcylon_button", NULL)) {
		printk(KERN_INFO "KCYLON: Couldn't create an interrupt handler for irq number %d\n", irq_number);
		ret = -1;
	}

	getnstimeofday(&ts_last);
	ts_diff = timespec_sub(ts_last, ts_last); /* zero out diff */

	task = kthread_run(cylon, NULL, "KCYLON_thread");
	if (IS_ERR(task)) {
		printk(KERN_ALERT "KCYLON: Failed to create the thread\n");
		ret = PTR_ERR(task);
	}
	return ret;
}

/**
 * @brief Kernel module exit point
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
	free_irq(irq_number, NULL);
	gpio_unexport(button_pin);
	gpio_free(button_pin);
	printk(KERN_INFO "KCYLON: Goodbye!\n");
}

/**
 * @brief Kernel module interrupt handler
 *  Changes the button level when a button
 *  is pressed. Also it puts limits on the level.
 *
 * @param irq The irq number that identifies the button
 * @return returns IRQ_HANDLED which tells the kernel that this is a non-fatal interrupt
 */
static irq_handler_t kcylon_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	mutex_lock(&button_level_mutex);
	button_level += button_direction;
	if (button_level == 10 || button_level == -10)
		button_direction *= -1;
	mutex_unlock(&button_level_mutex);
	getnstimeofday(&ts_current);
	ts_diff = timespec_sub(ts_current, ts_last);
	ts_last = ts_current;
	printk(KERN_INFO "KCYLON: Interrupt received (button level %d)\n", button_level);
	return (irq_handler_t) IRQ_HANDLED;
}
#undef NUM_LEDS

module_init(kcylon_init);
module_exit(kcylon_exit);
