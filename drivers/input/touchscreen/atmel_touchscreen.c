/*
 *  Copyright (C) 2010, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#define TSP_DEBUG 0

#define RESERVED_T0                               0u
#define RESERVED_T1                               1u
#define DEBUG_DELTAS_T2                           2u
#define DEBUG_REFERENCES_T3                       3u
#define DEBUG_SIGNALS_T4                          4u
#define GEN_MESSAGEPROCESSOR_T5                   5u
#define GEN_COMMANDPROCESSOR_T6                   6u
#define GEN_POWERCONFIG_T7                        7u
#define GEN_ACQUISITIONCONFIG_T8                  8u
#define TOUCH_MULTITOUCHSCREEN_T9                 9u
#define TOUCH_SINGLETOUCHSCREEN_T10               10u
#define TOUCH_XSLIDER_T11                         11u
#define TOUCH_YSLIDER_T12                         12u
#define TOUCH_XWHEEL_T13                          13u
#define TOUCH_YWHEEL_T14                          14u
#define TOUCH_KEYARRAY_T15                        15u
#define PROCG_SIGNALFILTER_T16                    16u
#define PROCI_LINEARIZATIONTABLE_T17              17u
#define SPT_COMCONFIG_T18                         18u
#define SPT_GPIOPWM_T19                           19u
#define PROCI_GRIPFACESUPPRESSION_T20             20u
#define RESERVED_T21                              21u
#define PROCG_NOISESUPPRESSION_T22                22u
#define TOUCH_PROXIMITY_T23                       23u
#define PROCI_ONETOUCHGESTUREPROCESSOR_T24        24u
#define SPT_SELFTEST_T25                          25u
#define DEBUG_CTERANGE_T26                        26u
#define PROCI_TWOTOUCHGESTUREPROCESSOR_T27        27u
#define SPT_CTECONFIG_T28                         28u
#define SPT_GPI_T29                               29u
#define SPT_GATE_T30                              30u
#define TOUCH_KEYSET_T31                          31u
#define TOUCH_XSLIDERSET_T32                      32u


#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
// #include <asm/semaphore.h>
#include <linux/semaphore.h>	// ryun
#include <asm/mach-types.h>

//#include <asm/arch/gpio.h>
//#include <asm/arch/mux.h>
#include <plat/gpio.h>	//ryun
#include <plat/mux.h>	//ryun 
#include <linux/delay.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <linux/firmware.h>
//#include "dprintk.h"
//#include "message.h"
#include <linux/time.h>

#include <linux/i2c/twl.h>	// ryun 20091125 
#include <linux/earlysuspend.h>	// ryun 20200107 for early suspend

#include "atmel_touch.h"

#ifdef CONFIG_TOUCHKEY_LOCK
extern unsigned int touchkey_lock_flag;
#endif

extern unsigned char g_version;
extern Atmel_model_type g_model;
extern uint8_t cal_check_flag;//20100208
extern unsigned int qt_time_point;
extern unsigned int qt_timer_state ;
extern int ta_state;
//#define TEST_TOUCH_KEY_IN_ATMEL

int fh_err_count;
extern void set_frequency_hopping_table(int mode);

extern int is_suspend_state; //to check suspend mode

// for reseting on I2C fail
extern uint8_t touch_state;
extern uint8_t get_message(void);

//modified for samsung customisation -touchscreen 
static ssize_t ts_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t ts_store(struct kobject *k, struct kobj_attribute *,
			  const char *buf, size_t n);
static ssize_t firmware_show(struct device *dev, struct device_attribute *attr, char *buf);
//modified for samsung customisation

const unsigned char fw_bin_version = 0x16;
const unsigned char fw_bin_build = 0xAB;

#define __CONFIG_ATMEL__

#define TOUCHSCREEN_NAME		"touchscreen"
#define DEFAULT_PRESSURE_UP		0
#define DEFAULT_PRESSURE_DOWN		256

static struct touchscreen_t tsp;
//static struct work_struct tsp_work;	//	 ryun
static struct workqueue_struct *tsp_wq;
//static int g_touch_onoff_status = 0;
static int g_enable_touchscreen_handler = 0;	// fixed for i2c timeout error.
//static unsigned int g_version_read_addr = 0;
//static unsigned short g_position_read_addr = 0;

#define DRIVER_FILTER

#ifdef DRIVER_FILTER
static int driver_filter_enabled = 1;
#endif

#if defined(CONFIG_MACH_SAMSUNG_LATONA) || defined(CONFIG_MACH_SAMSUNG_P1WIFI)
#define MAX_TOUCH_X_RESOLUTION	480
#define MAX_TOUCH_Y_RESOLUTION	800
int atmel_ts_tk_keycode[] = {KEY_MENU, KEY_BACK};
#endif

struct touchscreen_t;

struct touchscreen_t {
	struct input_dev * inputdevice;
	int touched;
	int irq;
	int irq_type;
	int irq_enabled;
	struct ts_device *dev;
	struct early_suspend	early_suspend;// ryun 20200107 for early suspend
	struct work_struct  tsp_work;	// ryun 20100107 
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void atmel_ts_early_suspend(struct early_suspend *h);
void atmel_ts_late_resume(struct early_suspend *h);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

// [[ ryun 20100113 
typedef struct
{
	int x;
	int y;
	int press;
	int size;
} dec_input;

static dec_input touch_info[MAX_TOUCH_NUM] = {{0}};
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
static int prev_touch_count = 0;
#endif

#define REPORT( touch, width, x, y, id) \
{	input_report_abs(tsp.inputdevice, ABS_MT_TOUCH_MAJOR, touch ); \
	input_report_abs(tsp.inputdevice, ABS_MT_WIDTH_MAJOR, width ); \
	input_report_abs(tsp.inputdevice, ABS_MT_POSITION_X, x); \
	input_report_abs(tsp.inputdevice, ABS_MT_POSITION_Y, y); \
	input_report_abs(tsp.inputdevice, ABS_MT_TRACKING_ID, id); \
	input_mt_sync(tsp.inputdevice); }

// ]] ryun 20100113 

void read_func_for_only_single_touch(struct work_struct *work);
void read_func_for_multi_touch(struct work_struct *work);
void keyarray_handler(uint8_t * atmel_msg);
void handle_multi_touch(uint8_t *atmel_msg);
void handle_keyarray(uint8_t * atmel_msg);
void initialize_multi_touch(void);



void (*atmel_handler_functions[MODEL_TYPE_MAX])(struct work_struct *work) =
{
	read_func_for_only_single_touch, // default handler
	read_func_for_multi_touch, // LATONA
};

static irqreturn_t touchscreen_handler(int irq, void *dev_id);
void set_touch_irq_gpio_init(void);
void set_touch_irq_gpio_disable(void);	// ryun 20091203
void clear_touch_history(void);

//samsung customisation
static struct kobj_attribute firmware_binary_attr = __ATTR(show_firmware_version, 0444, firmware_show, NULL);

/*------------------------------ for tunning ATmel - start ----------------------------*/
extern  ssize_t set_power_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_power_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_acquisition_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_acquisition_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_touchscreen_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_touchscreen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_keyarray_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_keyarray_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_grip_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_grip_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_noise_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_noise_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_total_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_total_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
extern  ssize_t set_write_show(struct device *dev, struct device_attribute *attr, char *buf);
extern  ssize_t set_write_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);

static DEVICE_ATTR(set_power, S_IRUGO | S_IWUSR, set_power_show, set_power_store);
static DEVICE_ATTR(set_acquisition, S_IRUGO | S_IWUSR, set_acquisition_show, set_acquisition_store);
static DEVICE_ATTR(set_touchscreen, S_IRUGO | S_IWUSR, set_touchscreen_show, set_touchscreen_store);
static DEVICE_ATTR(set_keyarray, S_IRUGO | S_IWUSR, set_keyarray_show, set_keyarray_store);
static DEVICE_ATTR(set_grip , S_IRUGO | S_IWUSR, set_grip_show, set_grip_store);
static DEVICE_ATTR(set_noise, S_IRUGO | S_IWUSR, set_noise_show, set_noise_store);
static DEVICE_ATTR(set_total, S_IRUGO | S_IWUSR, set_total_show, set_total_store);
static DEVICE_ATTR(set_write, S_IRUGO | S_IWUSR, set_write_show, set_write_store);

static ssize_t bootcomplete_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n);
static struct kobj_attribute bootcomplete_attr =        __ATTR(bootcomplete, 0220, NULL, bootcomplete_store);

#ifdef DRIVER_FILTER
static ssize_t driver_filter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size);
static ssize_t driver_filter_show(struct device *dev, struct device_attribute *attr, char *buf);
static DEVICE_ATTR(driver_filter, S_IRUGO | S_IWUSR, driver_filter_show, driver_filter_store);
#endif

extern void bootcomplete(void);
extern void enable_autocal_timer(unsigned int value);
static ssize_t bootcomplete_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1) {
		printk(KERN_ERR "bootcomplete_store: Invalid value\n");
		return -EINVAL;
	}

	if (attr == &bootcomplete_attr) {
		bootcomplete();
	} 
	else {
		return -EINVAL;
	}

	return n;
}

#ifdef DRIVER_FILTER
static ssize_t driver_filter_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);

	if (value == 0) {
		driver_filter_enabled = 0;
	} else if (value == 1) {
		driver_filter_enabled = 1;
	} else {
		printk(KERN_ERR "driver_filter_store: Invalid value\n");
		return -EINVAL;
	}

	return size;
}

static ssize_t driver_filter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", driver_filter_enabled);
}
#endif
/*------------------------------ for tunning ATmel - end ----------------------------*/

extern void restore_acquisition_config(void);
extern void restore_power_config(void);
extern uint8_t calibrate_chip(void);

static unsigned char menu_button = 0;
static unsigned char back_button = 0;

extern uint8_t report_id_to_type(uint8_t report_id, uint8_t *instance);

void clear_touch_history(void)
{
	int i;
	// Clear touch history
	for(i=0;i<MAX_TOUCH_NUM;i++)
	{
		if(touch_info[i].press == -1) continue;
		if(touch_info[i].press > 0)
		{
			touch_info[i].press = 0;
			REPORT( touch_info[i].press, touch_info[i].size, touch_info[i].x, touch_info[i].y, i);
		}
		if(touch_info[i].press == 0) touch_info[i].press = -1;
	}
	input_sync(tsp.inputdevice);
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
	prev_touch_count = 0;
#endif
}

//samsung customisation

/* ryun 20091125 
struct omap3430_pin_config touch_i2c_gpio_init[] = {
	
	OMAP3430_PAD_CFG("TOUCH I2C SCL", OMAP3430_PAD_I2C_TOUCH_SCL, PAD_MODE0, PAD_INPUT_PU, PAD_OFF_PU, PAD_WAKEUP_NA),
	OMAP3430_PAD_CFG("TOUCH I2C SDA", OMAP3430_PAD_I2C_TOUCH_SDA, PAD_MODE0, PAD_INPUT_PU, PAD_OFF_PU, PAD_WAKEUP_NA),
};

struct omap3430_pin_config touch_irq_gpio_init[] = {
	OMAP3430_PAD_CFG("TOUCH INT",		OMAP3430_PAD_TOUCH_INT, PAD_MODE4, PAD_INPUT_PU, PAD_OFF_PU, PAD_WAKEUP_NA),
};
*/
void set_touch_i2c_gpio_init(void)
{
	printk(KERN_DEBUG "[TSP] %s() \n", __FUNCTION__);
}

void set_touch_irq_gpio_init(void)
{
	printk(KERN_DEBUG "[TSP] %s() \n", __FUNCTION__);
	gpio_direction_input(OMAP_GPIO_TOUCH_INT);
}

void set_touch_irq_gpio_disable(void)
{
	printk(KERN_DEBUG "[TSP] %s() \n", __FUNCTION__);
	if(g_enable_touchscreen_handler == 1)
	{
		free_irq(tsp.irq, &tsp);
		gpio_free(OMAP_GPIO_TOUCH_INT);
		g_enable_touchscreen_handler = 0;
	}
}

#define U8		__u8
#define  U16 		unsigned short int
#define READ_MEM_OK	1u

extern U8 read_mem(U16 start, U8 size, U8 *mem);
extern uint16_t message_processor_address;
extern uint8_t max_message_length;
extern uint8_t *atmel_msg;
extern unsigned char g_version, g_build, qt60224_notfound_flag;


void disable_tsp_irq(void)
{
#if TSP_DEBUG
	printk(KERN_DEBUG "[TSP] disabling tsp irq and flushing workqueue\n");
#endif
	disable_irq(tsp.irq);
	flush_workqueue(tsp_wq);
}

void enable_tsp_irq(void)
{
#if TSP_DEBUG
	printk(KERN_DEBUG "[TSP] enabling tsp irq\n");
#endif
	enable_irq(tsp.irq);
}

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{	// v1.2 = 18 , v1.4 = 20 , v1.5 = 21
	printk(KERN_DEBUG "[TSP] QT602240 Firmware Ver.\n");
	printk(KERN_DEBUG "[TSP] version = %d\n", g_version);
	printk(KERN_DEBUG "[TSP] Build = %d\n", g_build);
	//	printk("[TSP] version = %d\n", info_block->info_id.version);
	//	sprintf(buf, "QT602240 Firmware Ver. %x\n", info_block->info_id.version);
	sprintf(buf, "QT602240 Firmware Ver. %d \nQT602240 Firmware Build. %d\n", g_version, g_build );

	return sprintf(buf, "%s", buf );
}

void initialize_multi_touch(void)
{
	int i;
	for(i = 0;i < MAX_TOUCH_NUM;i++)
	{
		touch_info[i].x = 0;
		touch_info[i].y = 0;
		touch_info[i].press = -1;
		touch_info[i].size = 0;
	}
}

void keyarray_handler(uint8_t * atmel_msg)
{
	if( (atmel_msg[2] & 0x1) && (menu_button==0) ) // menu press
	{
		menu_button = 1;
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] menu_button is pressed\n");
#endif
		input_report_key(tsp.inputdevice, KEY_MENU, DEFAULT_PRESSURE_DOWN);
		input_sync(tsp.inputdevice);
		trigger_touchkey_led(1);
	}
	else if( (atmel_msg[2] & 0x2) && (back_button==0) ) // back press
	{
		back_button = 1;
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] back_button is pressed\n");
#endif
		input_report_key(tsp.inputdevice, KEY_BACK, DEFAULT_PRESSURE_DOWN);
		input_sync(tsp.inputdevice);
		trigger_touchkey_led(2);
	}
	else if( (~atmel_msg[2] & (0x1)) && menu_button==1 ) // menu_release
	{
		menu_button = 0;
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] menu_button is released\n");
#endif
		input_report_key(tsp.inputdevice, KEY_MENU, DEFAULT_PRESSURE_UP);
		input_sync(tsp.inputdevice);
		trigger_touchkey_led(3);
	}
	else if( (~atmel_msg[2] & (0x2)) && back_button==1 ) // menu_release
	{
		back_button = 0;
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] back_button is released\n");
#endif
		input_report_key(tsp.inputdevice, KEY_BACK, DEFAULT_PRESSURE_UP); 
		input_sync(tsp.inputdevice);
		trigger_touchkey_led(3);
	}
	else
	{
		menu_button=0; 
		back_button=0;
		printk("Unknow state of touch key\n");
	}

}

void handle_keyarray(uint8_t * atmel_msg)
{

    switch(g_model)
    {
        case LATONA:
        {
            keyarray_handler(atmel_msg);
        }
        break;
        default:
        {
            printk("[TSP][ERROR] atmel message of key_array was not handled normally\n");
        }
    }
}

#if defined(DRIVER_FILTER)
static void equalize_coordinate(bool detect, u8 id, u16 *px, u16 *py)
{
	static int tcount[MAX_TOUCH_NUM] = { 0, };
	static u16 pre_x[MAX_TOUCH_NUM][4] = {{0}, };
	static u16 pre_y[MAX_TOUCH_NUM][4] = {{0}, };
	int coff[4] = {0,};
	int distance = 0;

	if(detect)
	{
		tcount[id] = 0;
	}

	pre_x[id][tcount[id]%4] = *px;
	pre_y[id][tcount[id]%4] = *py;

	if(tcount[id] >3)
	{
		distance = abs(pre_x[id][(tcount[id]-1)%4] - *px) + abs(pre_y[id][(tcount[id]-1)%4] - *py);

		coff[0] = (u8)(4 + distance/5);
		if(coff[0] < 8)
		{
			coff[0] = max(4, coff[0]);
			coff[1] = min((10 - coff[0]), (coff[0]>>1)+1);
			coff[2] = min((10 - coff[0] - coff[1]), (coff[1]>>1)+1);
			coff[3] = 10 - coff[0] - coff[1] - coff[2];

//			printk(KERN_DEBUG "[TSP] %d, %d, %d, %d \n", coff[0], coff[1], coff[2], coff[3]);

			*px = (u16)((*px*(coff[0]) + pre_x[id][(tcount[id]-1)%4]*(coff[1])
				+ pre_x[id][(tcount[id]-2)%4]*(coff[2]) + pre_x[id][(tcount[id]-3)%4]*(coff[3]))/10);
			*py = (u16)((*py*(coff[0]) + pre_y[id][(tcount[id]-1)%4]*(coff[1])
				+ pre_y[id][(tcount[id]-2)%4]*(coff[2]) + pre_y[id][(tcount[id]-3)%4]*(coff[3]))/10);
		}
		else
		{
			*px = (u16)((*px*4 + pre_x[id][(tcount[id]-1)%4])/5);
			*py = (u16)((*py*4 + pre_y[id][(tcount[id]-1)%4])/5);
		}
	}

	tcount[id]++;
}
#endif  //DRIVER_FILTER

////ryun 20100208 add
extern void check_chip_calibration(unsigned char one_touch_input_flag);
void handle_multi_touch(uint8_t *atmel_msg)
{
	u16 x=0, y=0;
	unsigned int size ;	// ryun 20100113 
	uint8_t touch_message_flag = 0;// ryun 20100208
	unsigned char one_touch_input_flag=0;
	int id;
	int i;
	static int touch_count = 0;

	x = atmel_msg[2];
	x = x << 2;
	x = x | (atmel_msg[4] >> 6);

	y = atmel_msg[3];
	y = y << 2;
	y = y | ((atmel_msg[4] & 0x6)  >> 2);

	size = atmel_msg[5];

	/* For valid inputs. */
	if ((atmel_msg[0] > 1) && (atmel_msg[0] < MAX_TOUCH_NUM+2))
	{
		if((x > MAX_TOUCH_X_RESOLUTION) || (y > MAX_TOUCH_Y_RESOLUTION))
		{
			int repeat_count=0;
			unsigned char do_reset=1;
			unsigned char resume_success=0;

			do {
				if( do_reset )
				{
					printk(KERN_DEBUG "[TSP] Reset TSP IC on wrong coordination\n");
					gpio_direction_output(OMAP_GPIO_TOUCH_INT, 0);
					gpio_set_value(OMAP_GPIO_TOUCH_EN, 0);
					msleep(200);
					gpio_direction_output(OMAP_GPIO_TOUCH_INT, 1);
					gpio_direction_input(OMAP_GPIO_TOUCH_INT);
					gpio_set_value(OMAP_GPIO_TOUCH_EN, 1);
					msleep(80); // recommended value
					calibrate_chip();
				}

				touch_state = 0;
				get_message();

				if( (touch_state & 0x10) == 0x10 )
				{
#if TSP_DEBUG
					printk(KERN_DEBUG "[TSP] reset and calibration success\n");
#endif
					resume_success = 1;
				}
				else
				{
#if TSP_DEBUG
					printk("[TSP] retry to reset\n");
#endif
					resume_success = 0;
					do_reset = 1;
				}
			} while (resume_success != 1 && repeat_count++ < RETRY_COUNT);
			clear_touch_history();
			return;
		}

		id = atmel_msg[0] - 2;
		/* case.1 - 11000000 -> DETECT & PRESS */
		if( ( atmel_msg[1] & 0xC0 ) == 0xC0  ) 
		{
			touch_message_flag = 1;
#ifdef CONFIG_TOUCHKEY_LOCK
			touchkey_lock_flag = 1;
#endif
			touch_info[id].press = 40;
			touch_info[id].size = size;
			touch_info[id].x = x;
			touch_info[id].y = y;
#if defined(DRIVER_FILTER)
			if (driver_filter_enabled)
				equalize_coordinate(1, id, &touch_info[id].x, &touch_info[id].y);
#endif
		}
		/* case.2 - case 10010000 -> DETECT & MOVE */
		else if( ( atmel_msg[1] & 0x90 ) == 0x90 )
		{
			touch_message_flag = 1;
			touch_info[id].press = 40;
			touch_info[id].size = size;
			touch_info[id].x = x;
			touch_info[id].y = y;
#if defined(DRIVER_FILTER)
			if (driver_filter_enabled)
				equalize_coordinate(0, id, &touch_info[id].x, &touch_info[id].y);
#endif
		}
		/* case.3 - case 00100000 -> RELEASE */
		else if( ((atmel_msg[1] & 0x20 ) == 0x20))   
		{
			touch_info[id].press = 0;
			touch_info[id].size = size;
		}
		else
		{
			//printk("[TSP] exception case id[%d],x=%d,y=%d\n", id, x, y);
			return;
		}

		for(i = 0, touch_count = 0;i < MAX_TOUCH_NUM;i++)
		{
			if(touch_info[i].press == -1) continue;
			REPORT( touch_info[i].press, touch_info[i].size, touch_info[i].x, touch_info[i].y, i);
			if(touch_info[i].press == 0) touch_info[i].press = -1;
			else touch_count++;
		}
		input_sync(tsp.inputdevice);
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		if(prev_touch_count != touch_count) {
			//printk(KERN_DEBUG "[TSP] id[%d],x=%d,y=%d,%dpoint(s)\n", id, x, y, touch_count);
			prev_touch_count = touch_count;
		}
#endif
#ifdef CONFIG_TOUCHKEY_LOCK
		if(touch_count == 0) touchkey_lock_flag = 0;
#endif
	}
	/* case.4 - Palm Touch & Unknow sate */
	else if ( atmel_msg[0] == 14 )
	{
		if((atmel_msg[1]&0x01) == 0x00)
		{
#ifdef CONFIG_TOUCHKEY_LOCK
			touchkey_lock_flag = 0;
#endif
			printk("[TSP] Palm Touch! - %d <released from palm touch>\n", atmel_msg[1]);
			clear_touch_history();
		}
		else
		{
#ifdef CONFIG_TOUCHKEY_LOCK
			touchkey_lock_flag = 1;
#endif
			printk("[TSP] test Palm Touch! - %d <face suppresstion status bit>\n", atmel_msg[1] );	
			touch_message_flag = 1;// ryun 20100208
			enable_autocal_timer(10);
		}
	}	
	else if ( atmel_msg[0] == 0 )
	{
	printk("[TSP] Error : %d - What happen to TSP chip?\n", __LINE__ );

//		touch_hw_rst( );  // TOUCH HW RESET
//		TSP_set_for_ta_charging( config_set_mode );   // 0 for battery, 1 for TA, 2 for USB
	}

	if ( atmel_msg[0] == 1 )
	{
		if((atmel_msg[1]&0x10) == 0x10)
		{
#if TSP_DEBUG
			printk(KERN_DEBUG "[TSP] The device is calibrating...\n");
#endif
			cal_check_flag = 1;
			qt_timer_state = 0;
			qt_time_point = 0;
		}
	}
	else if(touch_message_flag && (cal_check_flag))
	{
		check_chip_calibration(one_touch_input_flag);
		if(touch_count >= 2)
		{
			enable_autocal_timer(10);
		}
	}
}

void read_func_for_only_single_touch(struct work_struct *work)
{
//	uint8_t ret_val = MESSAGE_READ_FAILED;
	u16 x=0, y=0;
	u16 x480, y800, press;
//	PRINT_FUNCTION_ENTER;
	struct touchscreen_t *ts = container_of(work,
					struct touchscreen_t, tsp_work);

	if(read_mem(message_processor_address, max_message_length, atmel_msg) == READ_MEM_OK)
	{
		if(atmel_msg[0]<2 || atmel_msg[0]>11)
		{
			printk("[TSP][ERROR] %s() - read fail \n", __FUNCTION__);
			enable_irq(tsp.irq);
			return ; 
		}

		//printk(DCM_INP, "[TSP][REAL]x: 0x%02x, 0x%02x \n", atmel_msg[2], atmel_msg[3]);
		x = atmel_msg[2];
		x = x << 2;
		x = x | (atmel_msg[4] >> 6);

		y = atmel_msg[3];
		y = y << 2;
		y = y | ((atmel_msg[4] & 0x6)  >> 2);
		x480 = x;
		y800 = y;
		if( ((atmel_msg[1] & 0x40) == 0x40  || (atmel_msg[1] & 0x10) == 0x10) && (atmel_msg[1] & 0x20) == 0)
			press = 1;
		else if( (atmel_msg[1] & 0x40) == 0 && (atmel_msg[1] & 0x20) == 0x20)
			press = 0;
		else
		{
			//press = 3;
			//printk("[TSP][WAR] unknow state : 0x%x\n", msg[1]);
			enable_irq(tsp.irq);
			return;
		}

		if(press == 1)
		{
			input_report_abs(tsp.inputdevice, ABS_X, x480);
			input_report_abs(tsp.inputdevice, ABS_Y, y800);
			input_report_key(tsp.inputdevice, BTN_TOUCH, DEFAULT_PRESSURE_DOWN);
			input_report_abs(tsp.inputdevice, ABS_PRESSURE, DEFAULT_PRESSURE_DOWN);
			input_sync(tsp.inputdevice);
#if TSP_DEBUG
			if(en_touch_log)
			{
				printk("[TSP][DOWN] id=%d, x=%d, y=%d, press=%d \n",(int)atmel_msg[0], x480, y800, press);
				en_touch_log = 0;
			}
#endif
		}else if(press == 0)
		{
			input_report_key(tsp.inputdevice, BTN_TOUCH, DEFAULT_PRESSURE_UP	);
			input_report_abs(tsp.inputdevice, ABS_PRESSURE, DEFAULT_PRESSURE_UP);
			input_sync(tsp.inputdevice);
#if TSP_DEBUG
			printk("[TSP][UP] id=%d, x=%d, y=%d, press=%d \n",(int)atmel_msg[0], x480, y800, press);
			en_touch_log = 1;
#endif
		}
//		ret_val = MESSAGE_READ_OK;
	}else
	{
		printk("[TSP][ERROR] %s() - read fail \n", __FUNCTION__);
	}
//	PRINT_FUNCTION_EXIT;
	enable_irq(tsp.irq);
	return;
}

void check_frequency_hopping_error(uint8_t *atmel_msg)
{
	if(ta_state) {
		if(atmel_msg[1] & 0x8) {
			if(++fh_err_count == 12) fh_err_count = 0;
			if(!(fh_err_count % 3)) {
				set_frequency_hopping_table(fh_err_count/3);
			}
		}
	}
}

void read_func_for_multi_touch(struct work_struct *work)
{
	uint8_t object_type, instance;

//	PRINT_FUNCTION_ENTER;
	struct touchscreen_t *ts = container_of(work,
					struct touchscreen_t, tsp_work);


	if(read_mem(message_processor_address, max_message_length, atmel_msg) != READ_MEM_OK)
	{
		printk("[TSP][ERROR] %s() - read fail \n", __FUNCTION__);
		enable_irq(tsp.irq);
		return ;
	}

	object_type = report_id_to_type(atmel_msg[0], &instance);


	switch(object_type)
	{
		case GEN_COMMANDPROCESSOR_T6:
		case PROCI_GRIPFACESUPPRESSION_T20:
		case TOUCH_MULTITOUCHSCREEN_T9:
			handle_multi_touch(atmel_msg);
			break;
		case TOUCH_KEYARRAY_T15:
			handle_keyarray(atmel_msg);
			break;
		case PROCG_NOISESUPPRESSION_T22:
			check_frequency_hopping_error(atmel_msg);
		case SPT_GPIOPWM_T19:
		case PROCI_ONETOUCHGESTUREPROCESSOR_T24:
		case PROCI_TWOTOUCHGESTUREPROCESSOR_T27:
		case SPT_SELFTEST_T25:
		case SPT_CTECONFIG_T28:
		default:
#if TSP_DEBUG
			printk(KERN_DEBUG "[TSP] 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", atmel_msg[0], atmel_msg[1],atmel_msg[2], 
				    atmel_msg[3], atmel_msg[4],atmel_msg[5], atmel_msg[6], atmel_msg[7], atmel_msg[8]);
#endif
			break;
	};

	//if( atmel_msg[0] == 12 || atmel_msg[0] == 13)
	//{
	//	handle_keyarray(atmel_msg);
	//}

	//if( atmel_msg[0] >=2 && atmel_msg[0] <=11 )
	//{
	//	handle_multi_touch(atmel_msg);
	//}

	enable_irq(tsp.irq);

	return;
}

void atmel_touchscreen_read(struct work_struct *work)
{
	atmel_handler_functions[g_model](work);
}

static irqreturn_t touchscreen_handler(int irq, void *dev_id)
{
	disable_irq_nosync(irq);
	queue_work(tsp_wq, &tsp.tsp_work);
	return IRQ_HANDLED;
}

extern void atmel_touch_probe(void);
//extern void atmel_touchscreen_read(struct work_struct *work); 

int enable_irq_handler(void)
{
	if (tsp.irq != -1)
	{
		tsp.irq = OMAP_GPIO_IRQ(OMAP_GPIO_TOUCH_INT);	// ryun .. move to board-xxx.c
		tsp.irq_type = IRQF_TRIGGER_LOW; 

		if (request_irq(tsp.irq, touchscreen_handler, tsp.irq_type, TOUCHSCREEN_NAME, &tsp))
		{
			printk("[TSP][ERR] Could not allocate touchscreen IRQ!\n");
			tsp.irq = -1;
			input_free_device(tsp.inputdevice);
			return -EINVAL;
		}
		else
		{
			printk(KERN_DEBUG "[TSP] register touchscreen IRQ!\n");
		}

		if(g_enable_touchscreen_handler == 0)
			g_enable_touchscreen_handler = 1;

		tsp.irq_enabled = 1;
	}
	return 0;
}

static int __init touchscreen_probe(struct platform_device *pdev)
{
	int ret;
	int error = -1;
//	u8 data[2] = {0,};
	struct kobject *ts_kobj;

	printk(KERN_DEBUG "[TSP] touchscreen_probe !! \n");
	set_touch_irq_gpio_disable();	// ryun 20091203

	printk(KERN_DEBUG "[TSP] atmel touch driver!! \n");
	
	memset(&tsp, 0, sizeof(tsp));
	
	tsp.inputdevice = input_allocate_device();

	if (!tsp.inputdevice)
	{
		printk("[TSP][ERR] input_allocate_device fail \n");
		return -ENOMEM;
	}

	/* request irq */
	if (tsp.irq != -1)
	{
		tsp.irq = OMAP_GPIO_IRQ(OMAP_GPIO_TOUCH_INT);
		tsp.irq_type = IRQF_TRIGGER_LOW; 
		tsp.irq_enabled = 1;
	}

	// default and common settings
	tsp.inputdevice->name = "sec_touchscreen";
	tsp.inputdevice->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_SYN);	// ryun
	tsp.inputdevice->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);		// ryun 20091127 
	tsp.inputdevice->id.bustype = BUS_I2C;
	tsp.inputdevice->id.vendor  = 0;
	tsp.inputdevice->id.product =0;
	tsp.inputdevice->id.version =0;

	switch(g_model) 
	{
		case LATONA:
		{
			tsp.inputdevice->keybit[BIT_WORD(KEY_MENU)] |= BIT_MASK(KEY_MENU);
			tsp.inputdevice->keybit[BIT_WORD(KEY_BACK)] |= BIT_MASK(KEY_BACK);
			tsp.inputdevice->keycode = atmel_ts_tk_keycode;
			input_set_abs_params(tsp.inputdevice, ABS_MT_POSITION_X, 0, MAX_TOUCH_X_RESOLUTION, 0, 0);
			input_set_abs_params(tsp.inputdevice, ABS_MT_POSITION_Y, 0, MAX_TOUCH_Y_RESOLUTION, 0, 0);
			input_set_abs_params(tsp.inputdevice, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
			input_set_abs_params(tsp.inputdevice, ABS_MT_WIDTH_MAJOR, 0, 30, 0, 0);
			input_set_abs_params(tsp.inputdevice, ABS_MT_TRACKING_ID, 0, MAX_TOUCH_NUM - 1, 0, 0);
		}
		break;
	}

	ret = input_register_device(tsp.inputdevice);
	if (ret) {
	printk(KERN_ERR "atmel_ts_probe: Unable to register %s \
			input device\n", tsp.inputdevice->name);
	}

	tsp_wq = create_singlethread_workqueue("tsp_wq");
#ifdef __CONFIG_ATMEL__
	INIT_WORK(&tsp.tsp_work, atmel_touchscreen_read);
#endif

#ifdef __CONFIG_ATMEL__	
	atmel_touch_probe();		// ryun !!!???
#endif
	if(qt60224_notfound_flag)
		return -EINVAL;

#ifdef CONFIG_HAS_EARLYSUSPEND
	tsp.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	tsp.early_suspend.suspend = atmel_ts_early_suspend;
	tsp.early_suspend.resume = atmel_ts_late_resume;
	register_early_suspend(&tsp.early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

// [[ This will create the touchscreen sysfs entry under the /sys directory
ts_kobj = kobject_create_and_add("touchscreen", NULL);
	if (!ts_kobj)
		return -ENOMEM;

	error = sysfs_create_file(ts_kobj,
				  &firmware_binary_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
		}

	/*------------------------------ for tunning ATmel - start ----------------------------*/
	error = sysfs_create_file(ts_kobj, &dev_attr_set_power.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_acquisition.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_touchscreen.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_keyarray.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_grip.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_noise.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_total.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(ts_kobj, &dev_attr_set_write.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	/*------------------------------ for tunning ATmel - end ----------------------------*/

	error = sysfs_create_file(ts_kobj,
				  &bootcomplete_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
#ifdef DRIVER_FILTER
	error = sysfs_create_file(ts_kobj, &dev_attr_driver_filter.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
#endif

// ]] This will create the touchscreen sysfs entry under the /sys directory

	printk(KERN_DEBUG "[TSP] success probe() !\n");

	return 0;
}


static int touchscreen_remove(struct platform_device *pdev)
{

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&tsp.early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

	input_unregister_device(tsp.inputdevice);

	if (tsp.irq != -1)
	{
//		down_interruptible(&sem_touch_handler);
		if(g_enable_touchscreen_handler == 1)
		{
			free_irq(tsp.irq, &tsp);
			g_enable_touchscreen_handler = 0;
		}
//		up(&sem_touch_handler);
	}

	gpio_set_value(OMAP_GPIO_TOUCH_EN, 0);
	return 0;
}

extern int atmel_suspend(void);
extern int atmel_resume(void);

static int touchscreen_suspend(struct platform_device *pdev, pm_message_t state)
{
#if TSP_DEBUG
	printk(KERN_DEBUG "[TSP] touchscreen_suspend : touch power off\n");
#endif
	atmel_suspend();
	if (menu_button == 1)
	{
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] menu_button force released\n");
#endif
		input_report_key(tsp.inputdevice, KEY_MENU, DEFAULT_PRESSURE_UP);
		input_sync(tsp.inputdevice);
	}
	if (back_button == 1)
	{
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
		printk(KERN_DEBUG "[TSP] back_button force released\n");
#endif
		input_report_key(tsp.inputdevice, KEY_BACK, DEFAULT_PRESSURE_UP);
		input_sync(tsp.inputdevice);
	}

// Workaround for losing state when suspend mode(power off)
#ifdef CONFIG_TOUCHKEY_LOCK
	touchkey_lock_flag = 0;
#endif
	suspend_touchkey_led();
	return 0;
}

static int touchscreen_resume(struct platform_device *pdev)
{
#if TSP_DEBUG
	printk(KERN_DEBUG "[TSP] touchscreen_resume : touch power on\n");
#endif

	atmel_resume();
//	initialize_multi_touch(); 
	enable_irq(tsp.irq);
	trigger_touchkey_led(0);
	return 0;
}

static void touchscreen_shutdown(struct platform_device *pdev)
{
	qt60224_notfound_flag = 1; // to prevent misorder

	disable_irq(tsp.irq);
	flush_workqueue(tsp_wq);

	if (tsp.irq != -1)
	{
		if(g_enable_touchscreen_handler == 1)
		{
			free_irq(tsp.irq, &tsp);
			g_enable_touchscreen_handler = 0;
		}
	}
	gpio_set_value(OMAP_GPIO_TOUCH_EN, 0);

	printk("[TSP] %s   !!!\n", __func__);
}

static void touchscreen_device_release(struct device *dev)
{
	/* Nothing */
}

static struct platform_driver touchscreen_driver = {
	.probe 		= touchscreen_probe,
	.remove 	= touchscreen_remove,
	.shutdown = touchscreen_shutdown,
#ifndef CONFIG_HAS_EARLYSUSPEND		// 20100113 ryun 
	.suspend 	= &touchscreen_suspend,
	.resume 	= &touchscreen_resume,
#endif	
	.driver = {
		.name	= TOUCHSCREEN_NAME,
	},
};

static struct platform_device touchscreen_device = {
	.name 		= TOUCHSCREEN_NAME,
	.id 		= -1,
	.dev = {
		.release 	= touchscreen_device_release,
	},
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void atmel_ts_early_suspend(struct early_suspend *h)
{
//	melfas_ts_suspend(PMSG_SUSPEND);
	touchscreen_suspend(&touchscreen_device, PMSG_SUSPEND);
}

void atmel_ts_late_resume(struct early_suspend *h)
{
//	melfas_ts_resume();
	touchscreen_resume(&touchscreen_device);
}
#endif	/* CONFIG_HAS_EARLYSUSPEND */

static int __init touchscreen_init(void)
{
	int ret;

#if defined(CONFIG_MACH_SAMSUNG_LATONA) || defined(CONFIG_MACH_SAMSUNG_P1WIFI)
    g_model = LATONA;
#else
    g_model = DEFAULT_MODEL;
#endif

	gpio_set_value(OMAP_GPIO_TOUCH_EN, 1);  // TOUCH EN
	msleep(80);

	ret = platform_device_register(&touchscreen_device);
	if (ret != 0)
		return -ENODEV;

	ret = platform_driver_register(&touchscreen_driver);
	if (ret != 0) {
		platform_device_unregister(&touchscreen_device);
		return -ENODEV;
	}

	return 0;
}

static void __exit touchscreen_exit(void)
{
	platform_driver_unregister(&touchscreen_driver);
	platform_device_unregister(&touchscreen_device);
}

int touchscreen_get_tsp_int_num(void)
{
	return tsp.irq;
}
module_init(touchscreen_init);
module_exit(touchscreen_exit);

MODULE_LICENSE("GPL");
