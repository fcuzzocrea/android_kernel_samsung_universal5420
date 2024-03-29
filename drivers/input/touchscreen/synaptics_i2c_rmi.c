/* Synaptics Register Mapped Interface (RMI4) I2C Physical Layer Driver.
 * Copyright (c) 2007-2012, Synaptics Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <mach/cpufreq.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "synaptics_i2c_rmi.h"

#if defined(TSP_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
unsigned int tsp_booster_enabled = 1;
#endif

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_stop_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_start_device(struct synaptics_rmi4_data *rmi4_data);

#ifdef CONFIG_HAS_EARLYSUSPEND
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static void synaptics_rmi4_early_suspend(struct early_suspend *h);

static void synaptics_rmi4_late_resume(struct early_suspend *h);

#else

static int synaptics_rmi4_suspend(struct device *dev);

static int synaptics_rmi4_resume(struct device *dev);
#endif

#ifdef PROXIMITY_TSP
static void synaptics_rmi4_f51_finger_timer(unsigned long data);

static ssize_t synaptics_rmi4_f51_enables_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f51_enables_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
#endif

#ifdef USE_OPEN_CLOSE
static int synaptics_rmi4_input_open(struct input_dev *dev);
static void synaptics_rmi4_input_close(struct input_dev *dev);
#ifdef USE_OPEN_DWORK
static void synaptics_rmi4_open_work(struct work_struct *work);
#endif
#endif

#ifdef TSP_BOOSTER
static void synaptics_set_dvfs_lock(struct synaptics_rmi4_data *rmi4_data,
					unsigned int mode, bool restart);
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_register_read_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_register_write_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct synaptics_rmi4_f01_device_status {
	union {
		struct {
			unsigned char status_code:4;
			unsigned char reserved:2;
			unsigned char flash_prog:1;
			unsigned char unconfigured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f12_query_5 {
	union {
		struct {
			unsigned char size_of_query6;
			struct {
				unsigned char ctrl0_is_present:1;
				unsigned char ctrl1_is_present:1;
				unsigned char ctrl2_is_present:1;
				unsigned char ctrl3_is_present:1;
				unsigned char ctrl4_is_present:1;
				unsigned char ctrl5_is_present:1;
				unsigned char ctrl6_is_present:1;
				unsigned char ctrl7_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl8_is_present:1;
				unsigned char ctrl9_is_present:1;
				unsigned char ctrl10_is_present:1;
				unsigned char ctrl11_is_present:1;
				unsigned char ctrl12_is_present:1;
				unsigned char ctrl13_is_present:1;
				unsigned char ctrl14_is_present:1;
				unsigned char ctrl15_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl16_is_present:1;
				unsigned char ctrl17_is_present:1;
				unsigned char ctrl18_is_present:1;
				unsigned char ctrl19_is_present:1;
				unsigned char ctrl20_is_present:1;
				unsigned char ctrl21_is_present:1;
				unsigned char ctrl22_is_present:1;
				unsigned char ctrl23_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl24_is_present:1;
				unsigned char ctrl25_is_present:1;
				unsigned char ctrl26_is_present:1;
				unsigned char ctrl27_is_present:1;
				unsigned char ctrl28_is_present:1;
				unsigned char ctrl29_is_present:1;
				unsigned char ctrl30_is_present:1;
				unsigned char ctrl31_is_present:1;
			} __packed;
		};
		unsigned char data[5];
	};
};

struct synaptics_rmi4_f12_query_8 {
	union {
		struct {
			unsigned char size_of_query9;
			struct {
				unsigned char data0_is_present:1;
				unsigned char data1_is_present:1;
				unsigned char data2_is_present:1;
				unsigned char data3_is_present:1;
				unsigned char data4_is_present:1;
				unsigned char data5_is_present:1;
				unsigned char data6_is_present:1;
				unsigned char data7_is_present:1;
			} __packed;
			struct {
				unsigned char data8_is_present:1;
				unsigned char data9_is_present:1;
				unsigned char data10_is_present:1;
				unsigned char data11_is_present:1;
				unsigned char data12_is_present:1;
				unsigned char data13_is_present:1;
				unsigned char data14_is_present:1;
				unsigned char data15_is_present:1;
			} __packed;
		};
		unsigned char data[3];
	};
};

struct synaptics_rmi4_f12_ctrl_8 {
	union {
		struct {
			unsigned char max_x_coord_lsb;
			unsigned char max_x_coord_msb;
			unsigned char max_y_coord_lsb;
			unsigned char max_y_coord_msb;
			unsigned char rx_pitch_lsb;
			unsigned char rx_pitch_msb;
			unsigned char tx_pitch_lsb;
			unsigned char tx_pitch_msb;
			unsigned char low_rx_clip;
			unsigned char high_rx_clip;
			unsigned char low_tx_clip;
			unsigned char high_tx_clip;
			unsigned char num_of_rx;
			unsigned char num_of_tx;
		};
		unsigned char data[14];
	};
};

struct synaptics_rmi4_f12_ctrl_9 {
	union {
		struct {
			unsigned char touch_threshold;
			unsigned char lift_hysteresis;
			unsigned char small_z_scale_factor_lsb;
			unsigned char small_z_scale_factor_msb;
			unsigned char large_z_scale_factor_lsb;
			unsigned char large_z_scale_factor_msb;
			unsigned char small_large_boundary;
			unsigned char wx_scale;
			unsigned char wx_offset;
			unsigned char wy_scale;
			unsigned char wy_offset;
			unsigned char x_size_lsb;
			unsigned char x_size_msb;
			unsigned char y_size_lsb;
			unsigned char y_size_msb;
			unsigned char gloved_finger;
		};
		unsigned char data[16];
	};
};

struct synaptics_rmi4_f12_ctrl_11 {
	union {
		struct {
			unsigned char small_corner;
			unsigned char large_corner;
			unsigned char jitter_filter_strength;
			unsigned char x_minimum_z;
			unsigned char y_minimum_z;
			unsigned char x_maximum_z;
			unsigned char y_maximum_z;
			unsigned char x_amplitude;
			unsigned char y_amplitude;
			unsigned char gloved_finger_jitter_filter_strength;
		};
		unsigned char data[10];
	};
};

struct synaptics_rmi4_f12_ctrl_23 {
	union {
		struct {
			unsigned char obj_type_enable;
			unsigned char max_reported_objects;
		};
		unsigned char data[2];
	};
};

#ifdef CONFIG_GLOVE_TOUCH
struct synaptics_rmi4_f12_ctrl_26 {
	union {
		struct {
			unsigned char glove_feature_enable;
		};
		unsigned char data[1];
	};
};
#endif

struct synaptics_rmi4_f12_finger_data {
	unsigned char object_type_and_status;
	unsigned char x_lsb;
	unsigned char x_msb;
	unsigned char y_lsb;
	unsigned char y_msb;
#ifdef REPORT_2D_Z
	unsigned char z;
#endif
#ifdef REPORT_2D_W
	unsigned char wx;
	unsigned char wy;
#endif
};

struct synaptics_rmi4_f1a_query {
	union {
		struct {
			unsigned char max_button_count:3;
			unsigned char reserved:5;
			unsigned char has_general_control:1;
			unsigned char has_interrupt_enable:1;
			unsigned char has_multibutton_select:1;
			unsigned char has_tx_rx_map:1;
			unsigned char has_perbutton_threshold:1;
			unsigned char has_release_threshold:1;
			unsigned char has_strongestbtn_hysteresis:1;
			unsigned char has_filter_strength:1;
		} __packed;
		unsigned char data[2];
	};
};

struct synaptics_rmi4_f1a_control_0 {
	union {
		struct {
			unsigned char multibutton_report:2;
			unsigned char filter_mode:2;
			unsigned char reserved:4;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f1a_control_3_4 {
	unsigned char transmitterbutton;
	unsigned char receiverbutton;
};

struct synaptics_rmi4_f1a_control {
	struct synaptics_rmi4_f1a_control_0 general_control;
	unsigned char *button_int_enable;
	unsigned char *multi_button;
	struct synaptics_rmi4_f1a_control_3_4 *electrode_map;
	unsigned char *button_threshold;
	unsigned char button_release_threshold;
	unsigned char strongest_button_hysteresis;
	unsigned char filter_strength;
};

struct synaptics_rmi4_f1a_handle {
	int button_bitmask_size;
	unsigned char button_count;
	unsigned char valid_button_count;
	unsigned char *button_data_buffer;
	unsigned char *button_map;
	struct synaptics_rmi4_f1a_query button_query;
	struct synaptics_rmi4_f1a_control button_control;
};

struct synaptics_rmi4_f34_ctrl_3 {
	union {
		struct {
			unsigned char fw_release_month;
			unsigned char fw_release_date;
			unsigned char fw_release_revision;
			unsigned char fw_release_version;
		};
		unsigned char data[4];
	};
};

struct synaptics_rmi4_f34_fn_ptr {
	int (*read)(struct synaptics_rmi4_data *rmi4_data, unsigned short addr,
			unsigned char *data, unsigned short length);
	int (*write)(struct synaptics_rmi4_data *rmi4_data, unsigned short addr,
			unsigned char *data, unsigned short length);
	int (*enable)(struct synaptics_rmi4_data *rmi4_data, bool enable);
};

struct synaptics_rmi4_f34_handle {
	unsigned char status;
	unsigned char cmd;
	unsigned short bootloaderid;
	unsigned short blocksize;
	unsigned short imageblockcount;
	unsigned short configblockcount;
	unsigned short blocknum;
	unsigned short query_base_addr;
	unsigned short control_base_addr;
	unsigned short data_base_addr;
	bool inflashprogmode;
	unsigned char intr_mask;
	struct mutex attn_mutex;
	struct synaptics_rmi4_f34_fn_ptr *fn_ptr;
};

#ifdef PROXIMITY_TSP
struct synaptics_rmi4_f51_query {
	union {
		struct {
			unsigned char query_register_count;
			unsigned char data_register_count;
			unsigned char control_register_count;
			unsigned char command_register_count;
			unsigned char proximity_controls;
		};
		unsigned char data[5];
	};
};

struct synaptics_rmi4_f51_data {
	union {
		struct {
			unsigned char finger_hover_det:1;
			unsigned char air_swipe_det:1;
			unsigned char large_obj_det:1;
			unsigned char hover_pinch_det:1;
			unsigned char large_obj_wakeup_gesture_det:1;
			unsigned char profile_handedness_status:2;
			unsigned char f51_data0_b7:1;
			unsigned char hover_finger_x_4__11;
			unsigned char hover_finger_y_4__11;
			unsigned char hover_finger_xy_0__3;
			unsigned char hover_finger_z;
			unsigned char air_swipe_dir_0:1;
			unsigned char air_swipe_dir_1:1;
			unsigned char f51_data3_b2__4:3;
			unsigned char object_present:1;
			unsigned char large_obj_act:2;
		} __packed;
		unsigned char proximity_data[6];
	};
#ifdef EDGE_SWIPE
	union {
		struct {
			unsigned char edge_swipe_x_lsb;
			unsigned char edge_swipe_x_msb;
			unsigned char edge_swipe_y_lsb;
			unsigned char edge_swipe_y_msb;
			unsigned char edge_swipe_z;
			unsigned char edge_swipe_wx;
			unsigned char edge_swipe_wy;
			unsigned char edge_swipe_mm;
			signed char edge_swipe_dg;
		} __packed;
		unsigned char edge_swipe_data[9];
	};
#endif
};
#endif

static struct device_attribute synaptics_rmi4_attrs[] = {
#ifdef CONFIG_HAS_EARLYSUSPEND
	__ATTR(full_pm_cycle, (S_IRUGO | S_IWUSR | S_IWGRP),
			synaptics_rmi4_full_pm_cycle_show,
			synaptics_rmi4_full_pm_cycle_store),
#endif
#ifdef PROXIMITY_TSP
	__ATTR(proximity_enables, (S_IRUGO | S_IWUSR | S_IWGRP),
			synaptics_rmi4_f51_enables_show,
			synaptics_rmi4_f51_enables_store),
#endif
	__ATTR(reset, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			synaptics_rmi4_f01_reset_store),
	__ATTR(productinfo, S_IRUGO,
			synaptics_rmi4_f01_productinfo_show,
			synaptics_rmi4_store_error),
	__ATTR(buildid, S_IRUGO,
			synaptics_rmi4_f01_buildid_show,
			synaptics_rmi4_store_error),
	__ATTR(flashprog, S_IRUGO,
			synaptics_rmi4_f01_flashprog_show,
			synaptics_rmi4_store_error),
	__ATTR(0dbutton, (S_IRUGO | S_IWUSR | S_IWGRP),
			synaptics_rmi4_0dbutton_show,
			synaptics_rmi4_0dbutton_store),
	__ATTR(register_read, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			synaptics_rmi4_register_read_store),
	__ATTR(register_write, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			synaptics_rmi4_register_write_store),
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->full_pm_cycle);
}

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->full_pm_cycle = input > 0 ? 1 : 0;

	return count;
}
#endif

#ifdef TSP_BOOSTER
static void synaptics_change_dvfs_lock(struct work_struct *work)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(work,
				struct synaptics_rmi4_data, work_dvfs_chg.work);

	mutex_lock(&rmi4_data->dvfs_lock);

	switch (rmi4_data->boost_level) {
	case TSP_BOOSTER_LEVEL1:
	case TSP_BOOSTER_LEVEL3:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_remove_request(&rmi4_data->tsp_cpu_qos);

		if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
			pm_qos_remove_request(&rmi4_data->tsp_mif_qos);

		if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
			pm_qos_remove_request(&rmi4_data->tsp_int_qos);
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] level[%d] : DVFS OFF\n",
			__func__, rmi4_data->boost_level);
		break;
	case TSP_BOOSTER_LEVEL2:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_update_request(&rmi4_data->tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_2);

		if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
			pm_qos_update_request(&rmi4_data->tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_2);

		if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
			pm_qos_update_request(&rmi4_data->tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_2);
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] level[%d] : DVFS CHANGED\n",
			__func__, rmi4_data->boost_level);
		break;
	case TSP_BOOSTER_LEVEL9:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_update_request(&rmi4_data->tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_9_T);

		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] level[%d] : DVFS CHANGED\n",
			__func__, rmi4_data->boost_level);
		break;
	default:
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] Undefined type passed %d\n",
			__func__, rmi4_data->boost_level);
		break;
	}

	mutex_unlock(&rmi4_data->dvfs_lock);
}

static void synaptics_set_dvfs_off(struct work_struct *work)
{
	struct synaptics_rmi4_data *rmi4_data =
		container_of(work,
			struct synaptics_rmi4_data, work_dvfs_off.work);

	mutex_lock(&rmi4_data->dvfs_lock);

	if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
		pm_qos_remove_request(&rmi4_data->tsp_cpu_qos);

	if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
		pm_qos_remove_request(&rmi4_data->tsp_mif_qos);

	if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
		pm_qos_remove_request(&rmi4_data->tsp_int_qos);

	rmi4_data->dvfs_lock_status = false;
	mutex_unlock(&rmi4_data->dvfs_lock);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] level[%d] : DVFS OFF\n",
		__func__, rmi4_data->boost_level);
}

static void synaptics_init_dvfs_level(struct synaptics_rmi4_data *rmi4_data)
{
	/* DVFS level is depend on booster_level which is writed by sysfs
	 * booster_level : 1	(press)CPU 1600000, MIF 667000, INT 333000 -> after 130msec -> OFF
	 * booster_level : 2	(press)CPU 1600000, MIF 667000, INT 333000 -> after 130msec
	 *							-> CPU 650000, MIF 400000, INT 111000 -> after 500msec -> OFF
	 * booster_level : 3	(press)CPU 650000, MIF 667000, INT 333000 -> after 130msec -> OFF
	 * booster_level : 9	(press)CPU 1900000, MIF 800000, INT 400000 -> after 500msec
	 *							-> CPU 1500000, MIF 800000, INT 400000 -> after 1000msec -> OFF
	 */
	unsigned int level = rmi4_data->boost_level;

	switch (level) {
	case TSP_BOOSTER_LEVEL1:
	case TSP_BOOSTER_LEVEL2:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_update_request(&rmi4_data->tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_1);
		else
			pm_qos_add_request(&rmi4_data->tsp_cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCH_BOOSTER_CPU_FRQ_1);

		if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
			pm_qos_update_request(&rmi4_data->tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_1);
		else
			pm_qos_add_request(&rmi4_data->tsp_mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCH_BOOSTER_MIF_FRQ_1);

		if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
			pm_qos_update_request(&rmi4_data->tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_1);
		else
			pm_qos_add_request(&rmi4_data->tsp_int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCH_BOOSTER_INT_FRQ_1);
		break;
	case TSP_BOOSTER_LEVEL3:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_update_request(&rmi4_data->tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_3);
		else
			pm_qos_add_request(&rmi4_data->tsp_cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCH_BOOSTER_CPU_FRQ_3);

		if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
			pm_qos_update_request(&rmi4_data->tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_3);
		else
			pm_qos_add_request(&rmi4_data->tsp_mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCH_BOOSTER_MIF_FRQ_3);

		if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
			pm_qos_update_request(&rmi4_data->tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_3);
		else
			pm_qos_add_request(&rmi4_data->tsp_int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCH_BOOSTER_INT_FRQ_3);
		break;
	case TSP_BOOSTER_LEVEL9:
		if (pm_qos_request_active(&rmi4_data->tsp_cpu_qos))
			pm_qos_update_request(&rmi4_data->tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_9);
		else
			pm_qos_add_request(&rmi4_data->tsp_cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCH_BOOSTER_CPU_FRQ_9);

		if (pm_qos_request_active(&rmi4_data->tsp_mif_qos))
			pm_qos_update_request(&rmi4_data->tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_9);
		else
			pm_qos_add_request(&rmi4_data->tsp_mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCH_BOOSTER_MIF_FRQ_9);

		if (pm_qos_request_active(&rmi4_data->tsp_int_qos))
			pm_qos_update_request(&rmi4_data->tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_9);
		else
			pm_qos_add_request(&rmi4_data->tsp_int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCH_BOOSTER_INT_FRQ_9);
		break;
	default:
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] Undefined type passed %d\n",
			__func__, level);
		break;
	}
}

static void synaptics_set_dvfs_lock(struct synaptics_rmi4_data *rmi4_data, unsigned int mode, bool restart)
{
	if (rmi4_data->boost_level == TSP_BOOSTER_DISABLE || tsp_booster_enabled == 0)
		return;

	mutex_lock(&rmi4_data->dvfs_lock);

	switch (mode) {
	case TSP_BOOSTER_OFF:
		if (rmi4_data->dvfs_lock_status) {
			if (rmi4_data->boost_level == TSP_BOOSTER_LEVEL9)
				schedule_delayed_work(&rmi4_data->work_dvfs_off,
					msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME_9));
			else
				schedule_delayed_work(&rmi4_data->work_dvfs_off,
					msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME));
		}
		break;
	case TSP_BOOSTER_ON:
		cancel_delayed_work(&rmi4_data->work_dvfs_off);

		if (!rmi4_data->dvfs_lock_status || restart) {
			cancel_delayed_work(&rmi4_data->work_dvfs_chg);
			synaptics_init_dvfs_level(rmi4_data);

			if (rmi4_data->boost_level == TSP_BOOSTER_LEVEL9)
				schedule_delayed_work(&rmi4_data->work_dvfs_chg,
							msecs_to_jiffies(TOUCH_BOOSTER_CHG_TIME_9));
			else
				schedule_delayed_work(&rmi4_data->work_dvfs_chg,
							msecs_to_jiffies(TOUCH_BOOSTER_CHG_TIME));

			tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [TSP_DVFS] level[%d] : DVFS ON\n",
				__func__, rmi4_data->boost_level);

			rmi4_data->dvfs_lock_status = true;
		}
		break;
	case TSP_BOOSTER_FORCE_OFF:
		if (rmi4_data->dvfs_lock_status) {
			cancel_delayed_work(&rmi4_data->work_dvfs_off);
			cancel_delayed_work(&rmi4_data->work_dvfs_chg);
			schedule_work(&rmi4_data->work_dvfs_off.work);
		}
		break;
	default:
		break;
	}
	mutex_unlock(&rmi4_data->dvfs_lock);
}


static int synaptics_init_dvfs(struct synaptics_rmi4_data *rmi4_data)
{
	mutex_init(&rmi4_data->dvfs_lock);

	INIT_DELAYED_WORK(&rmi4_data->work_dvfs_off, synaptics_set_dvfs_off);
	INIT_DELAYED_WORK(&rmi4_data->work_dvfs_chg, synaptics_change_dvfs_lock);

	rmi4_data->dvfs_lock_status = false;
	return 0;
}
#endif

#ifdef PROXIMITY_TSP
static ssize_t synaptics_rmi4_f51_enables_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n",
			f51->proximity_enables);
}

static ssize_t synaptics_rmi4_f51_enables_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	if (sscanf(buf, "%x", &input) != 1)
		return -EINVAL;

	f51->proximity_enables = (unsigned char)input;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->proximity_enables_addr,
			&f51->proximity_enables,
			sizeof(f51->proximity_enables));
	if (retval < 0) {
		tsp_debug_err(true, dev, "%s: Failed to write proximity enables, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int reset;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &reset) != 1)
		return -EINVAL;

	if (reset != 1)
		return -EINVAL;

	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			(rmi4_data->rmi4_mod_info.product_info[0]),
			(rmi4_data->rmi4_mod_info.product_info[1]));
}

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int build_id;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	build_id = (unsigned int)rmi->build_id[0] +
			(unsigned int)rmi->build_id[1] * 0x100 +
			(unsigned int)rmi->build_id[2] * 0x10000;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			build_id);
}

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct synaptics_rmi4_f01_device_status device_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			device_status.data,
			sizeof(device_status.data));
	if (retval < 0) {
		tsp_debug_err(true, dev,
				"%s: Failed to read device status, error = %d\n",
				__func__, retval);
		return retval;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n",
			device_status.flash_prog);
}

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->button_0d_enabled);
}

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	unsigned char ii;
	unsigned char intr_enable;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	if (rmi4_data->button_0d_enabled == input)
		return count;

	if (list_empty(&rmi->support_fn_list))
		return -ENODEV;

	list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
		if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
			ii = fhandler->intr_reg_num;

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
			if (input == 1)
				intr_enable |= fhandler->intr_mask;
			else
				intr_enable &= ~fhandler->intr_mask;

			retval = synaptics_rmi4_i2c_write(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
		}
	}

	rmi4_data->button_0d_enabled = input;

	return count;
}

static ssize_t synaptics_rmi4_register_read_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	unsigned int page, addr, length;
	unsigned char i;
	unsigned char *register_val;
	unsigned short register_addr;

	if (sscanf(buf, "%x%x%x", &page, &addr, &length) != 3) {
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### Keep this format : page_num, address, length(Ex: 4 15 10 > register_read) ###\n");
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### That mean that read 10byte from the 0x415 address ###\n");
		goto out;
	}

	register_addr = (page << 8) | addr;

	tsp_debug_info(false, &rmi4_data->i2c_client->dev, "### Register address 0x%04x, Length %u ###\n",
		register_addr, length);

	if (rmi4_data->touch_stopped) {
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		goto out;
	}

	register_val = kzalloc(length, GFP_KERNEL);

	synaptics_rmi4_i2c_read(rmi4_data, register_addr, register_val, sizeof(register_val));

	for (i = 0; i < sizeof(register_val); i++)
			tsp_debug_info(false, &rmi4_data->i2c_client->dev, "### offset[%d] --> 0x%02x ###\n",
				i, register_val[i]);

	kfree(register_val);

out:
	return count;
}

/**
 * synaptics_rmi4_register_store()
 *
 * Called by synaptics_rmi4_i2c_read() and synaptics_rmi4_i2c_write().
 *
 * This function writes to the page select register to switch to the
 * assigned page.
 */
static ssize_t synaptics_rmi4_register_write_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	unsigned int page, addr, data, offset;
	unsigned char i;
	unsigned short register_addr;
	unsigned char *register_val;

	if (sscanf(buf, "%x%x%x%x", &page, &addr, &offset, &data) != 4) {
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### Kep this format : page_num, address, offset, data(EX : 0 11 1 10 > register_write) ###\n");
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### That mean Write 0x10 value to the 0x415 + offset address ###\n");
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### If the register is packet register, offset mean [register/offset] ###\n");
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "### Otherwise offset mean [register = register + offset] ###\n");
		goto out;
	}

	register_addr = (page << 8) | addr;

	tsp_debug_info(false, &rmi4_data->i2c_client->dev, "### Register address 0x%04x, offset 0x%02x, data 0x%02x ###\n",
		register_addr, offset, data);

	if (rmi4_data->touch_stopped) {
		tsp_debug_err(false, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		goto out;
	}

	register_val = kzalloc(offset + 1, GFP_KERNEL);

	synaptics_rmi4_i2c_read(rmi4_data, register_addr, register_val, sizeof(register_val));

	register_val[offset] = data;

	synaptics_rmi4_i2c_write(rmi4_data, register_addr, register_val, sizeof(register_val));
	synaptics_rmi4_i2c_read(rmi4_data, register_addr, register_val, sizeof(register_val));

	for (i = 0; i < sizeof(register_val); i++)
			tsp_debug_info(false, &rmi4_data->i2c_client->dev, "### offset[%d] --> 0x%02x ###\n",
				i, register_val[i]);

	kfree(register_val);

out:
	return count;
}

 /**
 * synaptics_rmi4_set_page()
 *
 * Called by synaptics_rmi4_i2c_read() and synaptics_rmi4_i2c_write().
 *
 * This function writes to the page select register to switch to the
 * assigned page.
 */
static int synaptics_rmi4_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int address)
{
	int retval = 0;
	unsigned char retry;
	unsigned char buf[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_client *i2c = rmi4_data->i2c_client;

	page = ((address >> 8) & MASK_8BIT);
	if (page != rmi4_data->current_page) {
		buf[0] = MASK_8BIT;
		buf[1] = page;
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			retval = i2c_master_send(i2c, buf, PAGE_SELECT_LEN);
			if (retval != PAGE_SELECT_LEN) {
				if ((rmi4_data->tsp_probe != true) && (retry >= 1)) {
					tsp_debug_err(true, &i2c->dev, "%s: TSP needs to reboot\n", __func__);
					retval = TSP_NEEDTO_REBOOT;
					return retval;
				}
				tsp_debug_err(true, &i2c->dev, "%s: I2C retry = %d, i2c_master_send retval = %d\n",
						__func__, retry + 1, retval);
				if (retval == 0)
					retval = -EAGAIN;
				msleep(20);
			} else {
				rmi4_data->current_page = page;
				break;
			}
		}
	} else {
		retval = PAGE_SELECT_LEN;
	}

	return retval;
}

 /**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */
static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		},
	};

	buf = addr & MASK_8BIT;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	if (rmi4_data->touch_stopped) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		retval = 0;
		goto exit;
	}

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set page. error = %d\n",
				__func__, retval);
		goto exit;
	}

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: I2C read over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char *buf;
	struct i2c_msg msg[1];
	buf = kzalloc(length + 1, GFP_KERNEL);
	if (!buf) {
		dev_err(&rmi4_data->i2c_client->dev, 
			"%s: Failed to alloc mem for buffer\n",
			__func__);
		return -ENOMEM;
	}


	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	if (rmi4_data->touch_stopped) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		retval = 0;
		goto exit;
	}

	msg[0].addr = rmi4_data->i2c_client->addr;
	msg[0].flags = 0;
	msg[0].len = length + 1;
	msg[0].buf = buf;

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set page. error = %d\n",
				__func__, retval);
		goto exit;
	}
	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));
	kfree(buf);

	return retval;
}

 /**
 * synaptics_rmi4_f11_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $11
 * finger data has been detected.
 *
 * This function reads the Function $11 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f11_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char reg_index;
	unsigned char finger;
	unsigned char fingers_supported;
	unsigned char num_of_finger_status_regs;
	unsigned char finger_shift;
	unsigned char finger_status;
	unsigned char data_reg_blk_size;
	unsigned char finger_status_reg[3];
	unsigned char data[F11_STD_DATA_LEN];
	unsigned short data_addr;
	unsigned short data_offset;
	int x;
	int y;
	int wx;
	int wy;

	/*
	 * The number of finger status registers is determined by the
	 * maximum number of fingers supported - 2 bits per finger. So
	 * the number of finger status registers to read is:
	 * register_count = ceil(max_num_of_fingers / 4)
	 */
	fingers_supported = fhandler->num_of_data_points;
	num_of_finger_status_regs = (fingers_supported + 3) / 4;
	data_addr = fhandler->full_addr.data_base;
	data_reg_blk_size = fhandler->size_of_data_register_block;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			finger_status_reg,
			num_of_finger_status_regs);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return 0;
	}
	for (finger = 0; finger < fingers_supported; finger++) {
		reg_index = finger / 4;
		finger_shift = (finger % 4) * 2;
		finger_status = (finger_status_reg[reg_index] >> finger_shift)
				& MASK_2BIT;

		/*
		 * Each 2-bit finger status field represents the following:
		 * 00 = finger not present
		 * 01 = finger present and data accurate
		 * 10 = finger present but data may be inaccurate
		 * 11 = reserved
		 */
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status);
#endif

		if (finger_status) {
			data_offset = data_addr +
					num_of_finger_status_regs +
					(finger * data_reg_blk_size);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					data_offset,
					data,
					data_reg_blk_size);
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
					__func__, __LINE__, retval);
				return 0;
			}
			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wx = (data[3] & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;

			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif

			tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y, wx, wy);

			touch_count++;
		}
	}

#ifndef TYPE_B_PROTOCOL
	if (touch_count == 0)
		input_mt_sync(rmi4_data->input_dev);
#endif

	input_sync(rmi4_data->input_dev);

	return touch_count;
}

 /**
 * synaptics_rmi4_f12_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $12
 * finger data has been detected.
 *
 * This function reads the Function $12 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f12_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char finger;
	unsigned char fingers_to_process;
	unsigned char finger_status;
	unsigned char size_of_2d_data;
	unsigned short data_addr;
	int x;
	int y;
	int wx;
	int wy;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_finger_data *data;
	struct synaptics_rmi4_f12_finger_data *finger_data;

#if defined(CONFIG_INPUT_BOOSTER) || defined(TSP_BOOSTER)
	bool booster_restart = false;
#endif
#ifdef PROXIMITY_TSP
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
#endif
	static unsigned char fingers_already_present;

	fingers_to_process = fhandler->num_of_data_points;
	data_addr = fhandler->full_addr.data_base;
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	/* Determine the total number of fingers to process */
	if (extra_data->data15_size) {
		unsigned short object_attention = 0;

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				data_addr + extra_data->data15_offset,
				extra_data->data15_data,
				extra_data->data15_size);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return 0;
		}

		/* Object_attention : [000000xx xxxxxxxxx] : "x" represent that finger is on or not
		 * Get the highest finger number to read finger data efficently.
		 */
		object_attention = extra_data->data15_data[0];
		if (extra_data->data15_size > 1)
			object_attention |= (extra_data->data15_data[1] & MASK_3BIT) << 8;

		for (; fingers_to_process > 0; fingers_to_process--) {
			if (object_attention & (1 << (fingers_to_process - 1)))
				break;
		}

		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "fingers[%d, %d] object_attention[0x%02x]\n",
			fingers_to_process, fingers_already_present, object_attention);
	}

	fingers_to_process = max(fingers_to_process, fingers_already_present);

	if (!fingers_to_process) {
		synaptics_rmi4_release_all_finger(rmi4_data);
		return 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr + extra_data->data1_offset,
			(unsigned char *)fhandler->data,
			fingers_to_process * size_of_2d_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return 0;
	}
	data = (struct synaptics_rmi4_f12_finger_data *)fhandler->data;

	for (finger = 0; finger < fingers_to_process; finger++) {
		finger_data = data + finger;
		finger_status = finger_data->object_type_and_status;

		x = (finger_data->x_msb << 8) | (finger_data->x_lsb);
		y = (finger_data->y_msb << 8) | (finger_data->y_lsb);
		if ((x == INVALID_X) && (y == INVALID_Y))
			finger_status = 0;

		/*
		 * Each 3-bit finger status field represents the following:
		 * 000 = finger not present
		 * 001 = finger present and data accurate
		 * 010 = finger present but data may be inaccurate
		 * 011 = palm
		 * 110 = glove touch
		 */

#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status);
#endif

		if (finger_status) {
			fingers_already_present = finger + 1;
#ifdef CONFIG_GLOVE_TOUCH
			if ((finger_status == 0x06) &&
				!rmi4_data->touchkey_glove_mode_status) {
				rmi4_data->touchkey_glove_mode_status = true;
				input_report_switch(rmi4_data->input_dev,
					SW_GLOVE, true);
			} else if ((finger_status != 0x06) &&
				rmi4_data->touchkey_glove_mode_status) {
				rmi4_data->touchkey_glove_mode_status = false;
				input_report_switch(rmi4_data->input_dev,
					SW_GLOVE, false);
			}
#endif
#ifdef REPORT_2D_W
			wx = finger_data->wx;
			wy = finger_data->wy;
#ifdef EDGE_SWIPE
			if ((finger_status == 0x03) &&
				(f51->surface_data.palm == 0)) {
				tsp_debug_info(true, &rmi4_data->i2c_client->dev,
						"%s: finger status 0x3, but No Palm. Reject!\n",
						__func__);
				continue;
			}
#if !defined(CONFIG_SEC_FACTORY)
			if (f51) {
				if ((f51->proximity_controls & HAS_EDGE_SWIPE)
					&& f51->surface_data.palm) {
					wx = f51->surface_data.wx;
					wy = f51->surface_data.wy;
				}
			}
#endif
#endif
#endif
			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#if defined(EDGE_SWIPE) && !defined(CONFIG_SEC_FACTORY)
			/* TODO : Do not report palm info in factory binary
			 * Disable the EDGE_SWIPE feature in factory binary not so far
			 */
			if (f51) {
				if (f51->proximity_controls & HAS_EDGE_SWIPE) {
					input_report_abs(rmi4_data->input_dev,
							ABS_MT_WIDTH_MAJOR, f51->surface_data.width_major);
					input_report_abs(rmi4_data->input_dev,
							ABS_MT_PALM, f51->surface_data.palm);
				}
			}
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif

			if (!rmi4_data->finger[finger].state) {
#if defined(CONFIG_INPUT_BOOSTER) || defined(TSP_BOOSTER)
				booster_restart = true;
#endif
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				tsp_debug_info(true, &rmi4_data->i2c_client->dev,
					"[%d][P] 0x%02x, x = %d, y = %d, wx = %d, wy = %d\n",
					finger, finger_status, x, y, wx, wy);
#else
				tsp_debug_info(true, &rmi4_data->i2c_client->dev, "[%d][P] 0x%02x\n",
					finger, finger_status);
#endif
			} else {
				rmi4_data->finger[finger].mcount++;
			}
			touch_count++;

		} else {
			if (rmi4_data->finger[finger].state) {
				/* TODO : Remove version information when H/W dose not changed */
				tsp_debug_info(true, &rmi4_data->i2c_client->dev,
					"[%d][R] 0x%02x M[%d], Ver[%02X%02X%02X %02X%02X]\n",
					finger, finger_status, rmi4_data->finger[finger].mcount,
					rmi4_data->ic_revision_of_ic, rmi4_data->panel_revision,
					rmi4_data->fw_version_of_ic, rmi4_data->glove_mode_enables,
					rmi4_data->ddi_type);

				rmi4_data->finger[finger].mcount = 0;
			}
		}

		rmi4_data->finger[finger].state = finger_status;
	}

	if (touch_count == 0) {
		/* Clear BTN_TOUCH when All touch are released  */
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
		fingers_already_present = 0;
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(rmi4_data->input_dev);
#endif
	}
	input_sync(rmi4_data->input_dev);

#ifdef TSP_BOOSTER
	if (touch_count)
		synaptics_set_dvfs_lock(rmi4_data, TSP_BOOSTER_ON, booster_restart);
	else
		synaptics_set_dvfs_lock(rmi4_data, TSP_BOOSTER_OFF, false);
#endif
#if defined(CONFIG_INPUT_BOOSTER)
	if (booster_restart && tsp_booster_enabled == 1) {
		INPUT_BOOSTER_REPORT_KEY_EVENT(rmi4_data->input_dev, KEY_BOOSTER_TOUCH, 0);
		INPUT_BOOSTER_REPORT_KEY_EVENT(rmi4_data->input_dev, KEY_BOOSTER_TOUCH, 1);
		INPUT_BOOSTER_SEND_EVENT(KEY_BOOSTER_TOUCH,
			BOOSTER_MODE_ON);
	}
	if (!touch_count) {
		INPUT_BOOSTER_REPORT_KEY_EVENT(rmi4_data->input_dev, KEY_BOOSTER_TOUCH, 0);
		INPUT_BOOSTER_SEND_EVENT(KEY_BOOSTER_TOUCH, BOOSTER_MODE_OFF);
	}
#endif
	return touch_count;
}

static void synaptics_rmi4_f1a_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0;
	unsigned char button;
	unsigned char index;
	unsigned char shift;
	unsigned char status;
	unsigned char *data;
	unsigned short data_addr = fhandler->full_addr.data_base;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	static unsigned char do_once = 1;
	static bool current_status[MAX_NUMBER_OF_BUTTONS];
#ifdef NO_0D_WHILE_2D
	static bool before_2d_status[MAX_NUMBER_OF_BUTTONS];
	static bool while_2d_status[MAX_NUMBER_OF_BUTTONS];
#endif

	if (do_once) {
		memset(current_status, 0, sizeof(current_status));
#ifdef NO_0D_WHILE_2D
		memset(before_2d_status, 0, sizeof(before_2d_status));
		memset(while_2d_status, 0, sizeof(while_2d_status));
#endif
		do_once = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read button data registers\n",
				__func__);
		return;
	}

	data = f1a->button_data_buffer;

	for (button = 0; button < f1a->valid_button_count; button++) {
		index = button / 8;
		shift = button % 8;
		status = ((data[index] >> shift) & MASK_1BIT);

		if (current_status[button] == status)
			continue;
		else
			current_status[button] = status;

		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Button %d (code %d) ->%d\n",
				__func__, button,
				f1a->button_map[button],
				status);
#ifdef NO_0D_WHILE_2D
		if (rmi4_data->fingers_on_2d == false) {
			if (status == 1) {
				before_2d_status[button] = 1;
			} else {
				if (while_2d_status[button] == 1) {
					while_2d_status[button] = 0;
					continue;
				} else {
					before_2d_status[button] = 0;
				}
			}
			touch_count++;
			input_report_key(rmi4_data->input_dev,
					f1a->button_map[button],
					status);
		} else {
			if (before_2d_status[button] == 1) {
				before_2d_status[button] = 0;
				touch_count++;
				input_report_key(rmi4_data->input_dev,
						f1a->button_map[button],
						status);
			} else {
				if (status == 1)
					while_2d_status[button] = 1;
				else
					while_2d_status[button] = 0;
			}
		}
#else
		touch_count++;
		input_report_key(rmi4_data->input_dev,
				f1a->button_map[button],
				status);
#endif
	}

	if (touch_count)
		input_sync(rmi4_data->input_dev);

	return;
}

#ifdef PROXIMITY_TSP
#ifdef EDGE_SWIPE
static int synaptics_rmi4_f51_edge_swipe(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned short data_base_addr;
	struct synaptics_rmi4_f51_data *data;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	data_base_addr = fhandler->full_addr.data_base;
	data = (struct synaptics_rmi4_f51_data *)fhandler->data;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_base_addr + EDGE_SWIPE_DATA_OFFSET,
			data->edge_swipe_data,
			sizeof(data->edge_swipe_data));

	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	if (!f51)
		return -ENODEV;

	if (data->edge_swipe_dg >= -90 && data->edge_swipe_dg <= 90)
		f51->surface_data.angle = data->edge_swipe_dg;
	else
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "Skip wrong edge swipe angle [%d]\n",
			data->edge_swipe_dg);

	f51->surface_data.width_major = data->edge_swipe_mm;
	f51->surface_data.wx = data->edge_swipe_wx;
	f51->surface_data.wy = data->edge_swipe_wy;
	f51->surface_data.palm = data->edge_swipe_z;

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
		"%s: edge_data : x[%d], y[%d], z[%d] ,wx[%d], wy[%d], area[%d], angle[%d][%d]\n", __func__,
		data->edge_swipe_x_msb << 8 | data->edge_swipe_x_lsb,
		data->edge_swipe_y_msb << 8 | data->edge_swipe_y_lsb,
		data->edge_swipe_z, data->edge_swipe_wx, data->edge_swipe_wy,
		data->edge_swipe_mm, data->edge_swipe_dg, f51->surface_data.angle);

	return retval;
}
#endif

static void synaptics_rmi4_f51_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned short data_base_addr;
	int x;
	int y;
	int z;
	struct synaptics_rmi4_f51_data *data;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

#ifdef EDGE_SWIPE
	/* Read edge swipe data */
	if (f51->proximity_controls & HAS_EDGE_SWIPE) {
		retval = synaptics_rmi4_f51_edge_swipe(rmi4_data, fhandler);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to read edge swipe data\n",
					__func__);
			return;
		}
	}
#endif

	data_base_addr = fhandler->full_addr.data_base;
	data = (struct synaptics_rmi4_f51_data *)fhandler->data;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_base_addr,
			data->proximity_data,
			sizeof(data->proximity_data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read proximity data registers\n",
				__func__);
		return;
	}

	if (data->proximity_data[0] == 0x00)
		return;

#ifdef HAND_GRIP_MODE
	if (f51->handgrip_data.hand_grip_mode) {
		struct synaptics_rmi4_handgrip *handgrip_data = &f51->handgrip_data;

		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: profile_handedness_status, 0x%x\n",
			__func__, data->profile_handedness_status);

		if (handgrip_data->old_code != data->profile_handedness_status) {
			if (data->profile_handedness_status == 0x2)
				handgrip_data->hand_grip = SW_RIGHT_HAND;
			else if (data->profile_handedness_status == 0x1)
				handgrip_data->hand_grip = SW_LEFT_HAND;
			else
				handgrip_data->hand_grip = SW_BOTH_HAND;

			input_report_switch(rmi4_data->input_dev, handgrip_data->old_hand_grip, false);
			input_sync(rmi4_data->input_dev);

			input_report_switch(rmi4_data->input_dev, handgrip_data->hand_grip, true);
			input_sync(rmi4_data->input_dev);

			tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: (%d) Hand grip %s ---> %s\n",
					__func__, data->profile_handedness_status,
					handgrip_data->old_hand_grip == SW_RIGHT_HAND ? "RIGHT" :
					handgrip_data->old_hand_grip == SW_LEFT_HAND ? "LEFT" : "BOTH",
					handgrip_data->hand_grip == SW_RIGHT_HAND ? "RIGHT" :
					handgrip_data->hand_grip == SW_LEFT_HAND ? "LEFT" : "BOTH");

		}

		handgrip_data->old_code = data->profile_handedness_status;
		handgrip_data->old_hand_grip = handgrip_data->hand_grip;

		if (handgrip_data->hand_grip_mode && !rmi4_data->hover_status_in_normal_mode)
			return;
	}
#endif
	if (data->finger_hover_det && (data->hover_finger_z > 0)) {
		x = (data->hover_finger_x_4__11 << 4) |
				(data->hover_finger_xy_0__3 & 0x0f);
		y = (data->hover_finger_y_4__11 << 4) |
				(data->hover_finger_xy_0__3 >> 4);
		z = HOVER_Z_MAX - data->hover_finger_z;

#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, 0);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 1);
#endif
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
		input_report_key(rmi4_data->input_dev,
				BTN_TOOL_FINGER, 1);
		input_report_abs(rmi4_data->input_dev,
				ABS_MT_POSITION_X, x);
		input_report_abs(rmi4_data->input_dev,
				ABS_MT_POSITION_Y, y);
		input_report_abs(rmi4_data->input_dev,
				ABS_MT_DISTANCE, z);

#ifndef TYPE_B_PROTOCOL
		input_mt_sync(rmi4_data->input_dev);
#endif
		input_sync(rmi4_data->input_dev);

		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Hover finger:\n"
				"x = %d\n"
				"y = %d\n"
				"z = %d\n",
				__func__, x, y, z);

		rmi4_data->f51_finger = true;
		rmi4_data->fingers_on_2d = false;
		synaptics_rmi4_f51_finger_timer((unsigned long)rmi4_data);
	}

	if (data->air_swipe_det) {
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Swipe direction 0 = %d\n",
				__func__, data->air_swipe_dir_0);
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Swipe direction 1 = %d\n",
				__func__, data->air_swipe_dir_1);
	}

	if (data->large_obj_det) {
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Large object activity = %d\n",
				__func__, data->large_obj_act);
	}
/*
	if (data->hover_pinch_det) {
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Hover pinch direction = %d\n",
				__func__, data->hover_pinch_dir);
	}
*/
	if (data->object_present) {
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Object presence detected\n",
				__func__);
	}

	return;
}
#endif

 /**
 * synaptics_rmi4_report_touch()
 *
 * Called by synaptics_rmi4_sensor_report().
 *
 * This function calls the appropriate finger data reporting function
 * based on the function handler it receives and returns the number of
 * fingers detected.
 */
static void synaptics_rmi4_report_touch(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	unsigned char touch_count_2d;

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
			"%s: Function %02x reporting\n",
			__func__, fhandler->fn_number);

	switch (fhandler->fn_number) {
	case SYNAPTICS_RMI4_F11:
		touch_count_2d = synaptics_rmi4_f11_abs_report(rmi4_data,
				fhandler);

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F12:
		touch_count_2d = synaptics_rmi4_f12_abs_report(rmi4_data,
				fhandler);

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F1A:
		synaptics_rmi4_f1a_report(rmi4_data, fhandler);
		break;
#ifdef PROXIMITY_TSP
	case SYNAPTICS_RMI4_F51:
		synaptics_rmi4_f51_report(rmi4_data, fhandler);
		break;
#endif
	default:
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Undefined RMI function number[0X%02X]\n",
				__func__, fhandler->fn_number);
		break;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_report()
 *
 * Called by synaptics_rmi4_irq().
 *
 * This function determines the interrupt source(s) from the sensor
 * and calls synaptics_rmi4_report_touch() with the appropriate
 * function handler for each function with valid data inputs.
 */
static int synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char data[MAX_INTR_REGISTERS + 1];
	unsigned char *intr = &data[1];
	struct synaptics_rmi4_f01_device_status status;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	/*
	 * Get interrupt status information from F01 Data1 register to
	 * determine the source(s) that are flagging the interrupt.
	 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			data,
			rmi4_data->num_of_intr_regs + 1);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return retval;
	}

	status.data[0] = data[0];
	if (status.unconfigured) {
		if (rmi4_data->doing_reflash) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"Spontaneous reset detected during reflash.\n");
			return 0;
		}

		tsp_debug_info(true, &rmi4_data->i2c_client->dev,
			"Spontaneous reset detected\n");
		retval = synaptics_rmi4_reinit_device(rmi4_data);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to reinit device\n",
					__func__);
		}
		return 0;
	}

	/*
	 * Traverse the function handler list and service the source(s)
	 * of the interrupt accordingly.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				if (fhandler->intr_mask &
						intr[fhandler->intr_reg_num]) {
					synaptics_rmi4_report_touch(rmi4_data,
							fhandler);
				}
			}
		}
	}

	if (!list_empty(&rmi->exp_fn_list)) {
		list_for_each_entry(exp_fhandler, &rmi->exp_fn_list, link) {
			if (exp_fhandler->func_attn != NULL)
				exp_fhandler->func_attn(rmi4_data, intr[0]);
		}
	}

	return 0;
}

 /**
 * synaptics_rmi4_irq()
 *
 * Called by the kernel when an interrupt occurs (when the sensor
 * asserts the attention irq).
 *
 * This function is the ISR thread and handles the acquisition
 * and the reporting of finger data when the presence of fingers
 * is detected.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = data;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	do {
		retval = synaptics_rmi4_sensor_report(rmi4_data);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read",
				__func__);
			goto out;
		}

		if (!rmi4_data->touch_stopped)
			goto out;

	} while (!gpio_get_value(pdata->gpio));

out:
	return IRQ_HANDLED;
}

 /**
 * synaptics_rmi4_irq_enable()
 *
 * Called by synaptics_rmi4_probe() and the power management functions
 * in this driver and also exported to other expansion Function modules
 * such as rmi_dev.
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_enable(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	unsigned char intr_status;
	const struct synaptics_rmi4_platform_data *platform_data =
			rmi4_data->i2c_client->dev.platform_data;

	if (enable) {
		if (rmi4_data->irq_enabled)
			return retval;

		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				&intr_status,
				rmi4_data->num_of_intr_regs);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return retval;
		}

		retval = request_threaded_irq(rmi4_data->irq, NULL,
				synaptics_rmi4_irq, platform_data->irq_type,
				DRIVER_NAME, rmi4_data);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to create irq thread\n",
					__func__);
			return retval;
		}

		rmi4_data->irq_enabled = true;
	} else {
		if (rmi4_data->irq_enabled) {
			disable_irq(rmi4_data->irq);
			free_irq(rmi4_data->irq, rmi4_data);
			rmi4_data->irq_enabled = false;
		}
	}

	return retval;
}

 /**
 * synaptics_rmi4_f11_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 11 registers
 * and determines the number of fingers supported, x and y data ranges,
 * offset to the associated interrupt status register, interrupt bit
 * mask, and gathers finger data acquisition capabilities from the query
 * registers.
 */
static int synaptics_rmi4_f11_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char abs_data_size;
	unsigned char abs_data_blk_size;
	unsigned char query[F11_STD_QUERY_LEN];
	unsigned char control[F11_STD_CTRL_LEN];

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query,
			sizeof(query));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum number of fingers supported */
	if ((query[1] & MASK_3BIT) <= 4)
		fhandler->num_of_data_points = (query[1] & MASK_3BIT) + 1;
	else if ((query[1] & MASK_3BIT) == 5)
		fhandler->num_of_data_points = 10;

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			control,
			sizeof(control));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum x and y */
	rmi4_data->sensor_max_x = ((control[6] & MASK_8BIT) << 0) |
			((control[7] & MASK_4BIT) << 8);
	rmi4_data->sensor_max_y = ((control[8] & MASK_8BIT) << 0) |
			((control[9] & MASK_4BIT) << 8);
	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	rmi4_data->max_touch_width = MAX_F11_TOUCH_WIDTH;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	abs_data_size = query[5] & MASK_2BIT;
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	fhandler->size_of_data_register_block = abs_data_blk_size;
	fhandler->data = NULL;
	fhandler->extra = NULL;

	return retval;
}

#ifdef CONFIG_GLOVE_TOUCH
int synaptics_rmi4_glove_mode_enables(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;

	if (GLOVE_FEATURE_EN != rmi4_data->glove_mode_feature) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
			"%s: Ver[%02X%02X%02X] FW does not support glove mode %02X\n",
			__func__, rmi4_data->panel_revision, rmi4_data->ic_revision_of_ic,
			rmi4_data->fw_version_of_ic, rmi4_data->glove_mode_feature);
		return 0;
	}

	if (rmi4_data->touch_stopped)
		return 0;

	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: [%02X]: %s\n",
		 __func__, rmi4_data->glove_mode_enables,
		(rmi4_data->glove_mode_enables == 0x07) ? "fast glove & clear cover enable" :
		(rmi4_data->glove_mode_enables == 0x06) ? "fast glove & flip cover enable" :
		(rmi4_data->glove_mode_enables == 0x05) ? "only fast glove enable" :
		(rmi4_data->glove_mode_enables == 0x03) ? "only clear cover enable" :
		(rmi4_data->glove_mode_enables == 0x02) ? "only flip cover enable" :
		(rmi4_data->glove_mode_enables == 0x01) ? "only glove enable" : "glove disable");

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->glove_mode_enables_addr,
			&rmi4_data->glove_mode_enables,
			sizeof(rmi4_data->glove_mode_enables));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}
#endif

int synaptics_rmi4_f12_ctrl11_set(struct synaptics_rmi4_data *rmi4_data,
		unsigned char data)
{
	struct synaptics_rmi4_f12_ctrl_11 ctrl_11;
	int retval;

	if (rmi4_data->touch_stopped)
		return 0;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f12_ctrl11_addr,
			ctrl_11.data,
			sizeof(ctrl_11.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	ctrl_11.data[2] = data; /* set a value of jitter filter */

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f12_ctrl11_addr,
			ctrl_11.data,
			sizeof(ctrl_11.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}

static int synaptics_rmi4_f12_set_enables(struct synaptics_rmi4_data *rmi4_data,
		unsigned short address)
{
	int retval;
	unsigned char enable_mask;
	static unsigned short ctrl_28_address;

	if (address)
		ctrl_28_address = address;

	enable_mask = RPT_DEFAULT;
#ifdef REPORT_2D_Z
	enable_mask |= RPT_Z;
#endif
#ifdef REPORT_2D_W
	enable_mask |= (RPT_WX | RPT_WY);
#endif

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			ctrl_28_address,
			&enable_mask,
			sizeof(enable_mask));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return retval;
}

 /**
 * synaptics_rmi4_f12_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 12 registers and
 * determines the number of fingers supported, offset to the data1
 * register, x and y data ranges, offset to the associated interrupt
 * status register, interrupt bit mask, and allocates memory resources
 * for finger data acquisition.
 */
static int synaptics_rmi4_f12_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char size_of_2d_data;
	unsigned char size_of_query8;
	unsigned char ctrl_8_offset;
	unsigned char ctrl_9_offset;
	unsigned char ctrl_11_offset;
	unsigned char ctrl_15_offset;
	unsigned char ctrl_23_offset;
	unsigned char ctrl_26_offset;
	unsigned char ctrl_28_offset;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_query_5 query_5;
	struct synaptics_rmi4_f12_query_8 query_8;
	struct synaptics_rmi4_f12_ctrl_8 ctrl_8;
	struct synaptics_rmi4_f12_ctrl_9 ctrl_9;
	struct synaptics_rmi4_f12_ctrl_23 ctrl_23;
#ifdef CONFIG_GLOVE_TOUCH
	struct synaptics_rmi4_f12_ctrl_26 ctrl_26;
#endif

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;
	fhandler->extra = kzalloc(sizeof(*extra_data), GFP_KERNEL);
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 5,
			query_5.data,
			sizeof(query_5.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	ctrl_8_offset = query_5.ctrl0_is_present +
			query_5.ctrl1_is_present +
			query_5.ctrl2_is_present +
			query_5.ctrl3_is_present +
			query_5.ctrl4_is_present +
			query_5.ctrl5_is_present +
			query_5.ctrl6_is_present +
			query_5.ctrl7_is_present;

	ctrl_9_offset = ctrl_8_offset +
			query_5.ctrl8_is_present;

	ctrl_11_offset = ctrl_9_offset +
			query_5.ctrl9_is_present +
			query_5.ctrl10_is_present;

	ctrl_15_offset = ctrl_11_offset +
			query_5.ctrl11_is_present +
			query_5.ctrl12_is_present +
			query_5.ctrl13_is_present +
			query_5.ctrl14_is_present;

	ctrl_23_offset = ctrl_15_offset +
			query_5.ctrl15_is_present +
			query_5.ctrl16_is_present +
			query_5.ctrl17_is_present +
			query_5.ctrl18_is_present +
			query_5.ctrl19_is_present +
			query_5.ctrl20_is_present +
			query_5.ctrl21_is_present +
			query_5.ctrl22_is_present;

	ctrl_26_offset = ctrl_23_offset +
			query_5.ctrl23_is_present +
			query_5.ctrl24_is_present +
			query_5.ctrl25_is_present;

	ctrl_28_offset = ctrl_26_offset +
			query_5.ctrl26_is_present +
			query_5.ctrl27_is_present;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_23_offset,
			ctrl_23.data,
			sizeof(ctrl_23.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	/* Maximum number of fingers supported */
	fhandler->num_of_data_points = min(ctrl_23.max_reported_objects,
			(unsigned char)F12_FINGERS_TO_SUPPORT);

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_f12_set_enables(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_28_offset);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to f12_set enable error = %d\n",
			__func__, retval);
		return retval;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 7,
			&size_of_query8,
			sizeof(size_of_query8));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 8,
			query_8.data,
			size_of_query8);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Determine the presence of the Data0 register */
	extra_data->data1_offset = query_8.data0_is_present;

	if ((size_of_query8 >= 3) && (query_8.data15_is_present)) {
		extra_data->data15_offset = query_8.data0_is_present +
			query_8.data1_is_present +
			query_8.data2_is_present +
			query_8.data3_is_present +
			query_8.data4_is_present +
			query_8.data5_is_present +
			query_8.data6_is_present +
			query_8.data7_is_present +
			query_8.data8_is_present +
			query_8.data9_is_present +
			query_8.data10_is_present +
			query_8.data11_is_present +
			query_8.data12_is_present +
			query_8.data13_is_present +
			query_8.data14_is_present;
		extra_data->data15_size = (rmi4_data->num_of_fingers + 7) / 8;
	} else {
		extra_data->data15_size = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_8_offset,
			ctrl_8.data,
			sizeof(ctrl_8.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	/* Maximum x and y */
	rmi4_data->sensor_max_x =
			((unsigned short)ctrl_8.max_x_coord_lsb << 0) |
			((unsigned short)ctrl_8.max_x_coord_msb << 8);
	rmi4_data->sensor_max_y =
			((unsigned short)ctrl_8.max_y_coord_lsb << 0) |
			((unsigned short)ctrl_8.max_y_coord_msb << 8);
	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	if (!rmi4_data->board->num_of_rx && !rmi4_data->board->num_of_tx) {
		rmi4_data->num_of_rx = ctrl_8.num_of_rx;
		rmi4_data->num_of_tx = ctrl_8.num_of_tx;
		rmi4_data->max_touch_width = max(rmi4_data->num_of_rx,
				rmi4_data->num_of_tx);
		rmi4_data->num_of_node = ctrl_8.num_of_rx * ctrl_8.num_of_tx;
	} else {
		rmi4_data->num_of_rx = rmi4_data->board->num_of_rx;
		rmi4_data->num_of_tx = rmi4_data->board->num_of_tx;
		rmi4_data->max_touch_width = max(rmi4_data->num_of_rx,
				rmi4_data->num_of_tx);
		rmi4_data->num_of_node = rmi4_data->num_of_rx * rmi4_data->num_of_tx;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_9_offset,
			ctrl_9.data,
			sizeof(ctrl_9.data));

	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	rmi4_data->touch_threshold = (int)ctrl_9.touch_threshold;
	rmi4_data->gloved_sensitivity = (int)ctrl_9.gloved_finger;
	tsp_debug_info(true, &rmi4_data->i2c_client->dev,
			"%s: %02x num_Rx:%d num_Tx:%d, node:%d, threshold:%d, gloved sensitivity:%x\n",
			__func__, fhandler->fn_number,
			rmi4_data->num_of_rx, rmi4_data->num_of_tx,
			rmi4_data->num_of_node, rmi4_data->touch_threshold,
			rmi4_data->gloved_sensitivity);

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	/* Allocate memory for finger data storage space */
	fhandler->data_size = fhandler->num_of_data_points * size_of_2d_data;
	fhandler->data = kzalloc(fhandler->data_size, GFP_KERNEL);

	rmi4_data->f12_ctrl11_addr = fhandler->full_addr.ctrl_base +
				ctrl_11_offset;

	rmi4_data->f12_ctrl15_addr = fhandler->full_addr.ctrl_base +
				ctrl_15_offset;
#ifdef CONFIG_GLOVE_TOUCH
	rmi4_data->glove_mode_enables_addr = fhandler->full_addr.ctrl_base +
				ctrl_26_offset;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 10,
			ctrl_26.data,
			sizeof(ctrl_26.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	rmi4_data->glove_mode_feature = ctrl_26.glove_feature_enable;
	synaptics_rmi4_glove_mode_enables(rmi4_data);
#endif

	return retval;
}

static int synaptics_rmi4_f1a_alloc_mem(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f1a_handle *f1a;

	f1a = kzalloc(sizeof(*f1a), GFP_KERNEL);
	if (!f1a) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for function handle\n",
				__func__);
		return -ENOMEM;
	}

	fhandler->data = (void *)f1a;
	fhandler->extra = NULL;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			f1a->button_query.data,
			sizeof(f1a->button_query.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read query registers\n",
				__func__);
		return retval;
	}

	f1a->button_count = f1a->button_query.max_button_count + 1;
	f1a->button_bitmask_size = (f1a->button_count + 7) / 8;

	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
			sizeof(*(f1a->button_data_buffer)), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for data buffer\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->button_count,
			sizeof(*(f1a->button_map)), GFP_KERNEL);
	if (!f1a->button_map) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for button map\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int synaptics_rmi4_f1a_button_map(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	if (!pdata->f1a_button_map) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: f1a_button_map is NULL in board file\n",
				__func__);
		return -ENODEV;
	} else if (!pdata->f1a_button_map->map) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Button map is missing in board file\n",
				__func__);
		return -ENODEV;
	} else {
		if (pdata->f1a_button_map->nbuttons != f1a->button_count) {
			f1a->valid_button_count = min(f1a->button_count,
					pdata->f1a_button_map->nbuttons);
		} else {
			f1a->valid_button_count = f1a->button_count;
		}

		for (ii = 0; ii < f1a->valid_button_count; ii++)
			f1a->button_map[ii] = pdata->f1a_button_map->map[ii];
	}

	return 0;
}

static void synaptics_rmi4_f1a_kfree(struct synaptics_rmi4_fn *fhandler)
{
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;

	if (f1a) {
		kfree(f1a->button_data_buffer);
		kfree(f1a->button_map);
		kfree(f1a);
		fhandler->data = NULL;
	}

	return;
}

static int synaptics_rmi4_f1a_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	retval = synaptics_rmi4_f1a_alloc_mem(rmi4_data, fhandler);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Fail to alloc mem. error = %d\n",
			__func__, retval);
		goto error_exit;
	}
	retval = synaptics_rmi4_f1a_button_map(rmi4_data, fhandler);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to make button map. error = %d\n",
			__func__, __LINE__, retval);
		goto error_exit;
	}
	rmi4_data->button_0d_enabled = 1;

	return 0;

error_exit:
	synaptics_rmi4_f1a_kfree(fhandler);

	return retval;
}

static int synaptics_rmi4_f34_read_version(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f34_ctrl_3 ctrl_3;

	rmi4_data->f34_ctrl_base_addr = fhandler->full_addr.ctrl_base;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			ctrl_3.data,
			sizeof(ctrl_3.data));

	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Ctrl_3.fw_release_month is composed
	 * MSB 4bit are used for factory test in TSP. and LSB 4bit represent month of fw release
	 */
	rmi4_data->fw_release_date_of_ic =
		((ctrl_3.fw_release_month << 8) | ctrl_3.fw_release_date);
	rmi4_data->ic_revision_of_ic = ctrl_3.fw_release_revision;
	rmi4_data->fw_version_of_ic = ctrl_3.fw_release_version;

	return retval;
}

static int synaptics_rmi4_f34_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_f34_read_version(rmi4_data, fhandler);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read version. error = %d\n",
			__func__, retval);
		return retval;
	}
	fhandler->data = NULL;

	return retval;
}

#ifdef PROXIMITY_TSP
#ifdef USE_CUSTOM_REZERO
static int synaptics_rmi4_f51_set_custom_rezero(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char custom_rezero;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			f51->general_control_addr,
			&custom_rezero, sizeof(custom_rezero));

	custom_rezero |= HOST_REZERO_COMMAND;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->general_control_addr,
			&custom_rezero, sizeof(custom_rezero));

	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail to write custom rezero\n",
			__func__);
		return retval;
	}
	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	return 0;
}

static void synaptics_rmi4_rezero_work(struct work_struct *work)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(work, struct synaptics_rmi4_data,
			rezero_work.work);

	/* Do not check hover enable status, because rezero bit does not effect
	 * to doze(mutual only) mdoe.
	 */
	retval = synaptics_rmi4_f51_set_custom_rezero(rmi4_data);
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to rezero device\n", __func__);
}
#endif

int synaptics_rmi4_f51_grip_edge_exclusion_rx(struct synaptics_rmi4_data *rmi4_data, bool enables)
{
	int retval;
	unsigned char grip_edge = 0;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	if (enables)
		grip_edge = 0x8;
	else
		grip_edge = 0x10;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->proximity_enables_addr + F51_GPIP_EDGE_EXCLUSION_RX_OFFSET,
			&grip_edge,
			sizeof(grip_edge));
	if (retval <= 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail to set grip_edge[%X][ret:%d]\n",
				__func__, grip_edge, retval);
	else
		tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: value is 0x%02x\n",
				__func__, grip_edge);

	return retval;
}

int synaptics_proximity_no_sleep_set(struct synaptics_rmi4_data *rmi4_data, bool enables)
{
	int retval;
	unsigned char no_sleep = 0;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&no_sleep,
			sizeof(no_sleep));

	if (retval <= 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail to read no_sleep[ret:%d]\n",
				__func__, retval);

	if (enables)
		no_sleep |= NO_SLEEP_ON;
	else
		no_sleep &= ~(NO_SLEEP_ON);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&no_sleep,
			sizeof(no_sleep));
	if (retval <= 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail to set no_sleep[%X][ret:%d]\n",
				__func__, no_sleep, retval);

	return retval;
}
EXPORT_SYMBOL(synaptics_proximity_no_sleep_set);

static int synaptics_rmi4_f51_set_enables(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	int retval;

	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: Hover: %s, Proximity controls: [0x%02X]\n", __func__,
		(f51->proximity_enables & FINGER_HOVER_EN) ? "enabled" : "disabled", f51->proximity_enables);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->proximity_enables_addr,
			&f51->proximity_enables,
			sizeof(f51->proximity_enables));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
#ifdef USE_CUSTOM_REZERO
	cancel_delayed_work(&rmi4_data->rezero_work);
	if (f51->proximity_enables & FINGER_HOVER_EN)
		schedule_delayed_work(&rmi4_data->rezero_work,
						msecs_to_jiffies(SYNAPTICS_REZERO_TIME*3)); /* 300msec*/
#endif

	return 0;
}

static int synaptics_rmi4_f51_set_init(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	/* Read the Hsync's status */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			f51->general_control_addr,
			&f51->general_control,
			sizeof(f51->general_control));

	/* Write the Host ID bit to decide notification type in charger
	 * HOST ID		: 1 -> I2C(RMI)	, 0 -> INT(GPIO)
	 */
	if (pdata->charger_noti_type)
		f51->general_control |= HOST_ID;
	else
		f51->general_control &= ~HOST_ID;

	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: Hsync: %s, General control[0X%02X]\n",
			__func__, (f51->general_control & HSYNC_STATUS) ? "GD" : "NG", f51->general_control);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->general_control_addr,
			&f51->general_control,
			sizeof(f51->general_control));
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);

	retval = synaptics_rmi4_f51_set_enables(rmi4_data);
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set enable\n", __func__);

	return retval;
}

static int synaptics_rmi4_f51_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;
	struct synaptics_rmi4_f51_data *data_register;
	struct synaptics_rmi4_f51_query query_register;
	struct synaptics_rmi4_f51_handle *f51;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) + intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	fhandler->data_size = sizeof(*data_register);
	data_register = kzalloc(fhandler->data_size, GFP_KERNEL);
	fhandler->data = (void *)data_register;
	fhandler->extra = NULL;

	rmi4_data->f51 = kzalloc(sizeof(*rmi4_data->f51), GFP_KERNEL);
	f51 = rmi4_data->f51;

	f51->num_of_data_sources = fhandler->num_of_data_sources;
	f51->proximity_enables = AIR_SWIPE_EN | SLEEP_PROXIMITY;
	f51->general_control = NO_PROXIMITY_ON_TOUCH | EDGE_SWIPE_EN;
	f51->proximity_enables_addr = fhandler->full_addr.ctrl_base +
		F51_PROXIMITY_ENABLES_OFFSET;
	f51->general_control_addr = fhandler->full_addr.ctrl_base +
		F51_GENERAL_CONTROL_OFFSET;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query_register.data,
			sizeof(query_register));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	/* Save proximity controls(query 4 register) to get which functions
	 * are supported by firmware.
	 */
	f51->proximity_controls = query_register.proximity_controls;

	retval = synaptics_rmi4_f51_set_init(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail proximity set init\n",
			__func__);
		return retval;
	}

	return 0;
}

int synaptics_rmi4_proximity_enables(struct synaptics_rmi4_data *rmi4_data,
	unsigned char enables)
{
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	int retval;
#ifdef SECURE_TSP
	rmi4_data->hover_status_in_normal_mode = enables;
#endif
	if (!f51)
		return -ENODEV;

#ifdef HAND_GRIP_MODE
	if (f51->handgrip_data.hand_grip_mode)
		f51->proximity_enables |= ENABLE_HANDGRIP_RECOG;
	else
		f51->proximity_enables &= ~(ENABLE_HANDGRIP_RECOG);
#endif

	if (enables) {
		f51->proximity_enables |= FINGER_HOVER_EN;
		f51->proximity_enables &= ~(SLEEP_PROXIMITY);
	} else {
		f51->proximity_enables |= SLEEP_PROXIMITY;
		f51->proximity_enables &= ~(FINGER_HOVER_EN);
	}

	if (rmi4_data->touch_stopped)
		return 0;

	retval = synaptics_rmi4_f51_set_enables(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}
EXPORT_SYMBOL(synaptics_rmi4_proximity_enables);
#endif

static int synaptics_rmi4_check_status(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int timeout = CHECK_STATUS_TIMEOUT_MS;
	unsigned char data;
	struct synaptics_rmi4_f01_device_status status;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			status.data,
			sizeof(status.data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail proximity set init\n",
			__func__);
		return retval;
	}
	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: Device status[0x%02x] status.code[%d]\n",
			__func__, status.data[0], status.status_code);

	while (status.status_code == STATUS_CRC_IN_PROGRESS) {
		if (timeout > 0)
			msleep(20);
		else
			return -1;

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr,
				status.data,
				sizeof(status.data));
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return retval;
		}
		timeout -= 20;
	}

	if (status.flash_prog == 1) {
		rmi4_data->flash_prog_mode = true;
		tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: In flash prog mode, status = 0x%02x\n",
				__func__, status.status_code);
	} else {
		rmi4_data->flash_prog_mode = false;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			&data,
			sizeof(data));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return retval;
	}

	return 0;
}

static void synaptics_rmi4_set_configured(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to set configured\n",
				__func__);
		return;
	}

	device_ctrl |= CONFIGURED;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to set configured\n",
				__func__);
	}

	return;
}

static int synaptics_rmi4_alloc_fh(struct synaptics_rmi4_fn **fhandler,
		struct synaptics_rmi4_fn_desc *rmi_fd, int page_number)
{
	*fhandler = kzalloc(sizeof(**fhandler), GFP_KERNEL);
	if (!(*fhandler))
		return -ENOMEM;

	(*fhandler)->full_addr.data_base =
			(rmi_fd->data_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.ctrl_base =
			(rmi_fd->ctrl_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.cmd_base =
			(rmi_fd->cmd_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.query_base =
			(rmi_fd->query_base_addr |
			(page_number << 8));

	return 0;
}

 /**
 * synaptics_get_product_id()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This function get the product id.
 */
static void	synaptics_get_product_id(struct synaptics_rmi4_data *rmi4_data)
{
	if (strncmp(rmi4_data->rmi4_mod_info.product_id_string, "s5050", 5) == 0) {
		rmi4_data->ic_product_id = SYNAPTICS_PRODUCT_ID_S5050;
	} else if (strncmp(rmi4_data->rmi4_mod_info.product_id_string, "S5000", 5) == 0) {
		rmi4_data->ic_product_id = SYNAPTICS_PRODUCT_ID_S5000;
	} else if (strncmp(rmi4_data->rmi4_mod_info.product_id_string, "SY 01", 5) == 0
			|| strncmp(rmi4_data->rmi4_mod_info.product_id_string, "S5000B", 6) == 0) {
		/* Code are temporary in this loop....
		 * Because "SY 01","S5000B" are just allocated specific project..
		 */
		rmi4_data->ic_product_id = SYNAPTICS_PRODUCT_ID_S5000;
		rmi4_data->ic_revision_of_ic = 0xB0;
	} else {
		rmi4_data->ic_product_id = SYNAPTICS_PRODUCT_ID_NONE;
	}

	/* Display product ID and firmware version, revision... */
	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: Product ID [%s. %d]\n",
		__func__, rmi4_data->rmi4_mod_info.product_id_string, rmi4_data->ic_product_id);
	tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: Result, date, revision, version [%d, %02d/%02d, 0x%02X, 0x%02X]\n",
		__func__, rmi4_data->fw_release_date_of_ic >> 12,
		(rmi4_data->fw_release_date_of_ic >> 8) & 0x0F,
		rmi4_data->fw_release_date_of_ic & 0x00FF,
		rmi4_data->ic_revision_of_ic, rmi4_data->fw_version_of_ic);
}

 /**
 * synaptics_rmi4_query_device()
 *
 * Called by synaptics_rmi4_probe().
 *
 * This funtion scans the page description table, records the offsets
 * to the register types of Function $01, sets up the function handlers
 * for Function $11 and Function $12, determines the number of interrupt
 * sources from the sensor, adds valid Functions with data inputs to the
 * Function linked list, parses information from the query registers of
 * Function $01, and enables the interrupt sources from the valid Functions
 * with data inputs.
 */
static int synaptics_rmi4_query_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii = 0;
	unsigned char page_number;
	unsigned char intr_count = 0;
	unsigned char f01_query[F01_STD_QUERY_LEN];
	unsigned short pdt_entry_addr;
	unsigned short intr_addr;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	INIT_LIST_HEAD(&rmi->support_fn_list);

	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
			fhandler = NULL;

			if (rmi_fd.fn_number == 0) {
				tsp_debug_dbg(false, &rmi4_data->i2c_client->dev,
						"%s: Reached end of PDT\n",
						__func__);
				break;
			}

			/* Display function description infomation */
			tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: F%02x found (page %d): INT_SRC[%02X] BASE_ADDRS[%02X,%02X,%02X,%02x]\n",
				__func__, rmi_fd.fn_number, page_number,
				rmi_fd.intr_src_count, rmi_fd.data_base_addr,
				rmi_fd.ctrl_base_addr, rmi_fd.cmd_base_addr,
				rmi_fd.query_base_addr);

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_query_base_addr =
						rmi_fd.query_base_addr;
				rmi4_data->f01_ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				rmi4_data->f01_data_base_addr =
						rmi_fd.data_base_addr;
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;

				retval = synaptics_rmi4_check_status(rmi4_data);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to check status\n",
							__func__);
					return retval;
				}

				if (rmi4_data->flash_prog_mode)
					goto flash_prog_mode;

				break;
			case SYNAPTICS_RMI4_F11:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f11_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F12:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f12_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F1A:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f1a_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F34:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f34_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					return retval;
				}
				break;

#ifdef PROXIMITY_TSP
			case SYNAPTICS_RMI4_F51:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f51_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					return retval;
				}
				break;
#endif
			}

			/* Accumulate the interrupt count */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);

			if (fhandler && rmi_fd.intr_src_count) {
				list_add_tail(&fhandler->link,
						&rmi->support_fn_list);
			}
		}
	}

flash_prog_mode:
	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	tsp_debug_info(true, &rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d sum of intr_count = %d\n",
			__func__, rmi4_data->num_of_intr_regs, intr_count);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr,
			f01_query,
			sizeof(f01_query));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	/* RMI Version 4.0 currently supported */
	rmi->version_major = 4;
	rmi->version_minor = 0;

	rmi->manufacturer_id = f01_query[0];
	rmi->product_props = f01_query[1];
	rmi->product_info[0] = f01_query[2] & MASK_7BIT;
	rmi->product_info[1] = f01_query[3] & MASK_7BIT;
	rmi->date_code[0] = f01_query[4] & MASK_5BIT;
	rmi->date_code[1] = f01_query[5] & MASK_4BIT;
	rmi->date_code[2] = f01_query[6] & MASK_5BIT;
	rmi->tester_id = ((f01_query[7] & MASK_7BIT) << 8) |
			(f01_query[8] & MASK_7BIT);
	rmi->serial_number = ((f01_query[9] & MASK_7BIT) << 8) |
			(f01_query[10] & MASK_7BIT);
	memcpy(rmi->product_id_string, &f01_query[11], 10);

	synaptics_get_product_id(rmi4_data);

	if (rmi->manufacturer_id != 1) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Non-Synaptics device found, manufacturer ID = %d\n",
				__func__, rmi->manufacturer_id);
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + F01_BUID_ID_OFFSET,
			rmi->build_id,
			sizeof(rmi->build_id));
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	memset(rmi4_data->intr_mask, 0x00, sizeof(rmi4_data->intr_mask));

	/*
	 * Map out the interrupt bit masks for the interrupt sources
	 * from the registered function handlers.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				rmi4_data->intr_mask[fhandler->intr_reg_num] |=
						fhandler->intr_mask;
			}

			/* To display each fhandler data */
			tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s: F%02x : NUM_SOURCE[%02X] NUM_INT_REG[%02X] INT_MASK[%02X]\n",
				__func__, fhandler->fn_number,
				fhandler->num_of_data_sources, fhandler->intr_reg_num,
				fhandler->intr_mask);
		}
	}

	/* Enable the interrupt sources */
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			tsp_debug_info(true, &rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask %d = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
		}
	}

	synaptics_rmi4_set_configured(rmi4_data);

	return 0;
}

static void synaptics_rmi4_release_support_fn(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_fn *fhandler, *n;
	struct synaptics_rmi4_device_info *rmi;
#ifdef PROXIMITY_TSP
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
#endif
	rmi = &(rmi4_data->rmi4_mod_info);

	if (list_empty(&rmi->support_fn_list)) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: support_fn_list is empty\n",
				__func__);
		return;
	}

	list_for_each_entry_safe(fhandler, n, &rmi->support_fn_list, link) {
		tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: fn_number = %x\n",
				__func__, fhandler->fn_number);
		if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
			synaptics_rmi4_f1a_kfree(fhandler);
		} else {
#ifdef PROXIMITY_TSP
			if (fhandler->fn_number == SYNAPTICS_RMI4_F51) {
				kfree(f51);
				f51 = NULL;
			}
#endif
			kfree(fhandler->data);
		}
		kfree(fhandler);
	}
}

static int synaptics_rmi4_set_input_device
		(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to allocate input device\n",
				__func__);
		retval = -ENOMEM;
		goto err_input_device;
	}

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
		goto err_query_device;
	}

	rmi4_data->input_dev->name = "sec_touchscreen";
	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->dev.parent = &rmi4_data->i2c_client->dev;
#ifdef USE_OPEN_CLOSE
	rmi4_data->input_dev->open = synaptics_rmi4_input_open;
	rmi4_data->input_dev->close = synaptics_rmi4_input_close;
#endif
	input_set_drvdata(rmi4_data->input_dev, rmi4_data);

#ifdef CONFIG_GLOVE_TOUCH
	input_set_capability(rmi4_data->input_dev, EV_SW, SW_GLOVE);
#endif
#ifdef HAND_GRIP_MODE
	input_set_capability(rmi4_data->input_dev, EV_SW, SW_LEFT_HAND);
	input_set_capability(rmi4_data->input_dev, EV_SW, SW_RIGHT_HAND);
	input_set_capability(rmi4_data->input_dev, EV_SW, SW_BOTH_HAND);
#endif

	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);
	set_bit(BTN_TOUCH, rmi4_data->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, rmi4_data->input_dev->keybit);
#ifdef CONFIG_INPUT_BOOSTER
	set_bit(KEY_BOOSTER_TOUCH, rmi4_data->input_dev->keybit);
#endif
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, rmi4_data->input_dev->propbit);
#endif

	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_X, 0,
			rmi4_data->board->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_Y, 0,
			rmi4_data->board->sensor_max_y, 0, 0);
#ifdef REPORT_2D_W
#ifdef EDGE_SWIPE
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			EDGE_SWIPE_WIDTH_MAX, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MINOR, 0,
			EDGE_SWIPE_WIDTH_MAX, 0, 0);
#else
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			rmi4_data->board->max_touch_width, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MINOR, 0,
			rmi4_data->board->max_touch_width, 0, 0);
#endif
#endif
#ifdef PROXIMITY_TSP
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_DISTANCE, 0,
			HOVER_Z_MAX, 0, 0);
#ifdef EDGE_SWIPE
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_WIDTH_MAJOR, 0,
			EDGE_SWIPE_WIDTH_MAX, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_PALM, 0,
			EDGE_SWIPE_PALM_MAX, 0, 0);
#endif
	setup_timer(&rmi4_data->f51_finger_timer,
			synaptics_rmi4_f51_finger_timer,
			(unsigned long)rmi4_data);
#endif

#ifdef TYPE_B_PROTOCOL
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers);
#endif

	f1a = NULL;
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				f1a = fhandler->data;
		}
	}

	if (f1a) {
		for (ii = 0; ii < f1a->valid_button_count; ii++) {
			set_bit(f1a->button_map[ii],
					rmi4_data->input_dev->keybit);
			input_set_capability(rmi4_data->input_dev,
					EV_KEY, f1a->button_map[ii]);
		}
	}

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register input device\n",
				__func__);
		goto err_register_input;
	}

	return 0;

err_register_input:
	input_free_device(rmi4_data->input_dev);

err_query_device:
	synaptics_rmi4_release_support_fn(rmi4_data);

err_input_device:
	return retval;
}

static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii = 0;
	unsigned short intr_addr;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;
#ifdef PROXIMITY_TSP
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
#endif

	/* Protection code to treat about unintented pressed touch.
	 * Mustly there shoud be no touch in here. but some case touch is ramained
	 * specific case(after executing run_rawcap_read). so add release all finger fucntion.
	 */
	synaptics_rmi4_release_all_finger(rmi4_data);

	rmi = &(rmi4_data->rmi4_mod_info);

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F12) {
				retval = synaptics_rmi4_f12_set_enables(rmi4_data, 0);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to f12 control error = %d\n",
						__func__, retval);
					return retval;
				}
			}
#ifdef CONFIG_SEC_FACTORY
			/* Read firmware version from IC when every power up IC.
			 * During Factory process touch panel can be changed manually.
			 */
			if (fhandler->fn_number == SYNAPTICS_RMI4_F34) {
				retval = synaptics_rmi4_f34_read_version(rmi4_data, fhandler);
				if (retval < 0) {
					tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to f34 read version error = %d\n",
						__func__, retval);
					return retval;
				}
			}
#endif
		}
	}
#ifdef CONFIG_GLOVE_TOUCH
	synaptics_rmi4_glove_mode_enables(rmi4_data);
#endif
#ifdef PROXIMITY_TSP
	if (!f51) {
		tsp_debug_info(true, &rmi4_data->i2c_client->dev,
			"%s: f51 is no available.\n", __func__);
	} else {
		retval = synaptics_rmi4_f51_set_init(rmi4_data);
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: fail proximity set init\n",
				__func__);
			return retval;
		}
	}
#endif
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			tsp_debug_info(true, &rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask register[%d] = 0x%02x, DDi type[%d]:%s\n",
					__func__, ii, rmi4_data->intr_mask[ii],
					rmi4_data->ddi_type, rmi4_data->ddi_type ? "MAGNA" : "SDC");
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
		}
	}

	synaptics_rmi4_set_configured(rmi4_data);

	return 0;
}

void synaptics_rmi4_release_all_finger(
	struct synaptics_rmi4_data *rmi4_data)
{
	int ii;

#ifdef TYPE_B_PROTOCOL
	for (ii = 0; ii < rmi4_data->num_of_fingers; ii++) {
		input_mt_slot(rmi4_data->input_dev, ii);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 0);

		if (rmi4_data->finger[ii].state)
			dev_info(&rmi4_data->i2c_client->dev, "[%d][RA] 0x%02x M[%d], Ver[%02X%02X%02X %02X%02X]\n",
				ii, rmi4_data->finger[ii].state, rmi4_data->finger[ii].mcount,
				rmi4_data->ic_revision_of_ic, rmi4_data->panel_revision,
				rmi4_data->fw_version_of_ic, rmi4_data->glove_mode_enables,
				rmi4_data->ddi_type);

		rmi4_data->finger[ii].state = 0;
		rmi4_data->finger[ii].mcount = 0;
	}
#else
	input_mt_sync(rmi4_data->input_dev);
#endif
	input_report_key(rmi4_data->input_dev,
			BTN_TOUCH, 0);
#ifdef CONFIG_GLOVE_TOUCH
	input_report_switch(rmi4_data->input_dev,
		SW_GLOVE, false);
	rmi4_data->touchkey_glove_mode_status = false;
#endif
#ifdef CONFIG_INPUT_BOOSTER
	INPUT_BOOSTER_REPORT_KEY_EVENT(rmi4_data->input_dev, KEY_BOOSTER_TOUCH, 0);
	INPUT_BOOSTER_SEND_EVENT(KEY_BOOSTER_TOUCH, BOOSTER_MODE_FORCE_OFF);
#endif
	input_sync(rmi4_data->input_dev);

	rmi4_data->fingers_on_2d = false;
#ifdef PROXIMITY_TSP
	rmi4_data->f51_finger = false;
#endif
#ifdef TSP_BOOSTER
	synaptics_set_dvfs_lock(rmi4_data, TSP_BOOSTER_FORCE_OFF, false);
#endif
#ifdef USE_CUSTOM_REZERO
	cancel_delayed_work(&rmi4_data->rezero_work);
#endif
}

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char command = 0x01;

	mutex_lock(&(rmi4_data->rmi4_reset_mutex));

	disable_irq(rmi4_data->i2c_client->irq);

	synaptics_rmi4_release_all_finger(rmi4_data);

	if (!rmi4_data->stay_awake) {
		retval = synaptics_rmi4_i2c_write(rmi4_data,
				rmi4_data->f01_cmd_base_addr,
				&command,
				sizeof(command));
		if (retval < 0) {
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to issue reset command, error = %d\n",
					__func__, retval);
			goto out;
		}

		msleep(SYNAPTICS_HW_RESET_TIME);
	} else {
		rmi4_data->board->power(false);
		msleep(30);
		rmi4_data->board->power(true);
		rmi4_data->current_page = MASK_8BIT;

		msleep(SYNAPTICS_HW_RESET_TIME);

		/* Read the F54's register again because sometimes after firmware update,
		 * the register map of F54 can be changed.
		 */
		retval = synaptics_rmi4_f54_set_control(rmi4_data);
		if (retval < 0)
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to set f54 control\n",
					__func__);
	}

	synaptics_rmi4_release_support_fn(rmi4_data);

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
	}

out:
	enable_irq(rmi4_data->i2c_client->irq);
	mutex_unlock(&(rmi4_data->rmi4_reset_mutex));

	return 0;
}

#ifdef SYNAPTICS_RMI_INFORM_CHARGER
extern void synaptics_tsp_register_callback(struct synaptics_rmi_callbacks *cb);
static void synaptics_charger_conn(struct synaptics_rmi4_data *rmi4_data,
				int ta_status)
{
	int retval;
	unsigned char charger_connected;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&charger_connected,
			sizeof(charger_connected));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->input_dev->dev),
				"%s: Failed to set configured\n",
				__func__);
		return;
	}

	if (ta_status == 0x01 || ta_status == 0x03)
		charger_connected |= CHARGER_CONNECTED;
	else
		charger_connected &= ~(CHARGER_CONNECTED);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
		rmi4_data->f01_ctrl_base_addr,
		&charger_connected,
		sizeof(charger_connected));

	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->input_dev->dev),
				"%s: Failed to set configured\n",
				__func__);
	}

	tsp_debug_info(true, &rmi4_data->i2c_client->dev,
		"%s: device_control : 0x%x, ta_status : %x\n",
		__func__, charger_connected, ta_status);
}

static void synaptics_ta_cb(struct synaptics_rmi_callbacks *cb, int ta_status)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(cb, struct synaptics_rmi4_data, callbacks);
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	tsp_debug_info(true, &rmi4_data->i2c_client->dev,
		"%s: ta_status : %x\n", __func__, ta_status);

	rmi4_data->ta_status = ta_status;

	/* if do not completed driver loading, ta_cb will not run. */
	if (!rmi4_data->init_done.done) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
			"%s: until driver loading done.\n",
			__func__);
		return;
	}
	if (rmi4_data->touch_stopped || rmi4_data->doing_reflash) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
			"%s: device is in suspend state or reflash.\n",
			__func__);
		return;
	}

	if (pdata->charger_noti_type)
		synaptics_charger_conn(rmi4_data, ta_status);
}
#endif

#ifdef PROXIMITY_TSP
static void synaptics_rmi4_f51_finger_timer(unsigned long data)
{
	struct synaptics_rmi4_data *rmi4_data =
			(struct synaptics_rmi4_data *)data;

	if (rmi4_data->f51_finger) {
		rmi4_data->f51_finger = false;
		mod_timer(&rmi4_data->f51_finger_timer,
				jiffies + msecs_to_jiffies(F51_FINGER_TIMEOUT));
		return;
	}

	if (!rmi4_data->fingers_on_2d) {
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, 0);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 0);
#else
		input_mt_sync(rmi4_data->input_dev);
#endif
		input_sync(rmi4_data->input_dev);
	}

	return;
}
#endif

static void synaptics_rmi4_remove_exp_fn(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_exp_fn *exp_fhandler, *n;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	if (list_empty(&rmi->exp_fn_list)) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s: exp_fn_list empty\n",
				__func__);
		return;
	}

	list_for_each_entry_safe(exp_fhandler, n, &rmi->exp_fn_list, link) {
		if (exp_fhandler->initialized &&
				(exp_fhandler->func_remove != NULL)) {
			tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: [%d]\n",
				__func__, exp_fhandler->fn_type);
			exp_fhandler->func_remove(rmi4_data);
		}
		list_del(&exp_fhandler->link);
		kfree(exp_fhandler);
	}
}

static int synaptics_rmi4_init_exp_fn(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	INIT_LIST_HEAD(&rmi->exp_fn_list);

	retval = rmidev_module_register(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register rmidev module\n",
				__func__);
		goto error_exit;
	}

	retval = rmi4_f54_module_register(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register f54 module\n",
				__func__);
		goto error_exit;
	}

	retval = rmi4_fw_update_module_register(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register fw update module\n",
				__func__);
		goto error_exit;
	}

	if (list_empty(&rmi->exp_fn_list))
		return -ENODEV;

	list_for_each_entry(exp_fhandler, &rmi->exp_fn_list, link) {
		if (exp_fhandler->func_init != NULL) {
			tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s: run [%d]'s init function\n",
						__func__, exp_fhandler->fn_type);
			retval = exp_fhandler->func_init(rmi4_data);
			if (retval < 0) {
				tsp_debug_err(true, &rmi4_data->i2c_client->dev,
						"%s: Failed to init exp fn\n",
						__func__);
				goto error_exit;
			} else {
				exp_fhandler->initialized = true;
			}
		}
	}

	return 0;

error_exit:
	synaptics_rmi4_remove_exp_fn(rmi4_data);

	return retval;
}

/**
* synaptics_rmi4_new_function()
*
* Called by other expansion Function modules in their module init and
* module exit functions.
*
* This function is used by other expansion Function modules such as
* rmi_dev to register themselves with the driver by providing their
* initialization and removal callback function pointers so that they
* can be inserted or removed dynamically at module init and exit times,
* respectively.
*/
int synaptics_rmi4_new_function(enum exp_fn fn_type,
		struct synaptics_rmi4_data *rmi4_data,
		int (*func_init)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_remove)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_attn)(struct synaptics_rmi4_data *rmi4_data,
		unsigned char intr_mask))
{
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	exp_fhandler = kzalloc(sizeof(*exp_fhandler), GFP_KERNEL);

	if (!exp_fhandler) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
						"%s: Failed to alloc mem for expansion function\\n",
						__func__);
		return -ENOMEM;
	}
	exp_fhandler->fn_type = fn_type;
	exp_fhandler->func_init = func_init;
	exp_fhandler->func_attn = func_attn;
	exp_fhandler->func_remove = func_remove;
	list_add_tail(&exp_fhandler->link, &rmi->exp_fn_list);

	return 0;
}

 /**
 * synaptics_rmi4_probe()
 *
 * Called by the kernel when an association with an I2C device of the
 * same name is made (after doing i2c_add_driver).
 *
 * This funtion allocates and initializes the resources for the driver
 * as an input driver, turns on the power to the sensor, queries the
 * sensor for its supported Functions and characteristics, registers
 * the driver to the input subsystem, sets up the interrupt, handles
 * the registration of the early_suspend and late_resume functions,
 * and creates a work queue for detection of other expansion Function
 * modules.
 */
static int __devinit synaptics_rmi4_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval;
	unsigned char attr_count;
	int attr_count_num;
	struct synaptics_rmi4_data *rmi4_data;
	struct synaptics_rmi4_device_info *rmi;
	const struct synaptics_rmi4_platform_data *platform_data =
			client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		tsp_debug_err(true, &client->dev,
				"%s: SMBus byte data not supported\n",
				__func__);
		return -EIO;
	}

	if (!platform_data) {
		tsp_debug_err(true, &client->dev,
				"%s: No platform data found\n",
				__func__);
		return -EINVAL;
	}

	rmi4_data = kzalloc(sizeof(*rmi4_data), GFP_KERNEL);
	if (!rmi4_data) {
		tsp_debug_err(true, &client->dev,
				"%s: Failed to alloc mem for rmi4_data\n",
				__func__);
		return -ENOMEM;
	}

	rmi = &(rmi4_data->rmi4_mod_info);

	rmi4_data->i2c_client = client;
	rmi4_data->current_page = MASK_8BIT;
	rmi4_data->board = platform_data;
	rmi4_data->touch_stopped = false;
	rmi4_data->sensor_sleep = false;
	rmi4_data->irq_enabled = false;
	rmi4_data->tsp_probe = false;
	rmi4_data->rebootcount = 0;
	rmi4_data->panel_revision = rmi4_data->board->panel_revision;
	rmi4_data->i2c_read = synaptics_rmi4_i2c_read;
	rmi4_data->i2c_write = synaptics_rmi4_i2c_write;
	rmi4_data->irq_enable = synaptics_rmi4_irq_enable;
	rmi4_data->reset_device = synaptics_rmi4_reset_device;
	rmi4_data->stop_device = synaptics_rmi4_stop_device;
	rmi4_data->start_device = synaptics_rmi4_start_device;
	rmi4_data->irq = rmi4_data->i2c_client->irq;

	rmi4_data->num_of_rx = rmi4_data->board->num_of_rx;
	rmi4_data->num_of_tx = rmi4_data->board->num_of_tx;

	mutex_init(&(rmi4_data->rmi4_io_ctrl_mutex));
	mutex_init(&(rmi4_data->rmi4_reset_mutex));
	mutex_init(&(rmi4_data->rmi4_reflash_mutex));
	mutex_init(&(rmi4_data->rmi4_device_mutex));
	init_completion(&rmi4_data->init_done);

#ifdef USE_OPEN_DWORK
	INIT_DELAYED_WORK(&rmi4_data->open_work, synaptics_rmi4_open_work);
#endif
#ifdef USE_CUSTOM_REZERO
	INIT_DELAYED_WORK(&rmi4_data->rezero_work, synaptics_rmi4_rezero_work);
#endif

	i2c_set_clientdata(client, rmi4_data);

	if (!platform_data->get_ddi_type) {
		tsp_debug_err(true, &client->dev, "%s: Do not support getting DDI Type\n", __func__);
	} else {
		rmi4_data->ddi_type = platform_data->get_ddi_type();
		tsp_debug_info(true, &client->dev, "%s: DDI Type is %s[%d]\n",
			__func__, rmi4_data->ddi_type ? "MAGNA" : "SDC", rmi4_data->ddi_type);
	}

err_tsp_reboot:
	platform_data->power(true);
	msleep(SYNAPTICS_POWER_MARGIN_TIME);

#ifdef TSP_BOOSTER
	synaptics_init_dvfs(rmi4_data);
	rmi4_data->boost_level = TSP_BOOSTER_LEVEL2;
#endif

	retval = synaptics_rmi4_set_input_device(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &client->dev, "%s: Failed to set up input device\n",
				__func__);

		if ((retval == TSP_NEEDTO_REBOOT)
				&& (rmi4_data->rebootcount < MAX_TSP_REBOOT)) {
			platform_data->power(false);
			msleep(SYNAPTICS_POWER_MARGIN_TIME);
			msleep(SYNAPTICS_POWER_MARGIN_TIME);
			rmi4_data->rebootcount++;
			tsp_debug_err(true, &client->dev, "%s: reboot sequence by i2c fail\n",
					__func__);
			goto err_tsp_reboot;
		} else {
			goto err_set_input_device;
		}
	}

	retval = synaptics_rmi4_init_exp_fn(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &client->dev, "%s: Failed to register rmidev module\n",
				__func__);
		goto err_init_exp_fn;
	}

	retval = synaptics_rmi4_irq_enable(rmi4_data, true);
	if (retval < 0) {
		tsp_debug_err(true, &client->dev, "%s: Failed to enable attention interrupt\n",
				__func__);
		goto err_enable_irq;
	}

	for (attr_count = 0; attr_count < ARRAY_SIZE(synaptics_rmi4_attrs); attr_count++) {
		retval = sysfs_create_file(&rmi4_data->input_dev->dev.kobj,
				&synaptics_rmi4_attrs[attr_count].attr);
		if (retval < 0) {
			tsp_debug_err(true, &client->dev, "%s: Failed to create sysfs attributes\n",
					__func__);
			goto err_sysfs;
		}
	}

	retval = synaptics_rmi4_fw_update_on_probe(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &client->dev, "%s: Failed to firmware update\n",
					__func__);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	rmi4_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	rmi4_data->early_suspend.suspend = synaptics_rmi4_early_suspend;
	rmi4_data->early_suspend.resume = synaptics_rmi4_late_resume;
	register_early_suspend(&rmi4_data->early_suspend);
#endif

#ifdef SYNAPTICS_RMI_INFORM_CHARGER
	rmi4_data->register_cb = synaptics_tsp_register_callback;

	rmi4_data->callbacks.inform_charger = synaptics_ta_cb;
	if (rmi4_data->register_cb) {
		tsp_debug_err(true, &client->dev, "Register TA Callback\n");
		rmi4_data->register_cb(&rmi4_data->callbacks);
	}
#endif

	/* for blocking to be excuted open function until probing */
	rmi4_data->tsp_probe = true;
	complete_all(&rmi4_data->init_done);

	return retval;

err_sysfs:
	attr_count_num = (int)attr_count;
	for (attr_count_num--; attr_count_num >= 0; attr_count_num--) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&synaptics_rmi4_attrs[attr_count].attr);
	}
	synaptics_rmi4_irq_enable(rmi4_data, false);

err_enable_irq:
	synaptics_rmi4_remove_exp_fn(rmi4_data);

err_init_exp_fn:
	synaptics_rmi4_release_support_fn(rmi4_data);

	input_unregister_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;

err_set_input_device:
	platform_data->power(false);
	complete_all(&rmi4_data->init_done);
	kfree(rmi4_data);

	return retval;
}

 /**
 * synaptics_rmi4_remove()
 *
 * Called by the kernel when the association with an I2C device of the
 * same name is broken (when the driver is unloaded).
 *
 * This funtion terminates the work queue, stops sensor data acquisition,
 * frees the interrupt, unregisters the driver from the input subsystem,
 * turns off the power to the sensor, and frees other allocated resources.
 */
static int __devexit synaptics_rmi4_remove(struct i2c_client *client)
{
	unsigned char attr_count;
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&rmi4_data->early_suspend);
#endif
	synaptics_rmi4_irq_enable(rmi4_data, false);

	for (attr_count = 0; attr_count < ARRAY_SIZE(synaptics_rmi4_attrs); attr_count++) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&synaptics_rmi4_attrs[attr_count].attr);
	}

	synaptics_rmi4_remove_exp_fn(rmi4_data);
	synaptics_rmi4_release_support_fn(rmi4_data);

	input_unregister_device(rmi4_data->input_dev);

	rmi4_data->board->power(false);

	kfree(rmi4_data);

	return 0;
}

#ifdef USE_SENSOR_SLEEP
 /**
 * synaptics_rmi4_sensor_sleep()
 *
 * Called by synaptics_rmi4_early_suspend() and synaptics_rmi4_suspend().
 *
 * This function stops finger data acquisition and puts the sensor to sleep.
 */
static void synaptics_rmi4_sensor_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | SENSOR_SLEEP);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	} else {
		rmi4_data->sensor_sleep = true;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_wake()
 *
 * Called by synaptics_rmi4_resume() and synaptics_rmi4_late_resume().
 *
 * This function wakes the sensor from sleep.
 */
static void synaptics_rmi4_sensor_wake(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NORMAL_OPERATION);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		tsp_debug_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	} else {
		rmi4_data->sensor_sleep = false;
	}

	return;
}
#endif

static int synaptics_rmi4_stop_device(struct synaptics_rmi4_data *rmi4_data)
{
	const struct synaptics_rmi4_platform_data *platform_data =
			rmi4_data->i2c_client->dev.platform_data;

#ifdef SECURE_TSP
	if (rmi4_data->secure_mode_status)
		return 0;
#endif

	mutex_lock(&rmi4_data->rmi4_device_mutex);

	if (rmi4_data->touch_stopped) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s already power off\n",
			__func__);
		goto out;
	}

	disable_irq(rmi4_data->i2c_client->irq);
	synaptics_rmi4_release_all_finger(rmi4_data);
	rmi4_data->touch_stopped = true;
	platform_data->power(false);

	if (platform_data->enable_sync)
		platform_data->enable_sync(false);

	tsp_debug_dbg(true, &rmi4_data->i2c_client->dev, "%s\n", __func__);

out:
	mutex_unlock(&rmi4_data->rmi4_device_mutex);
	return 0;
}

static int synaptics_rmi4_start_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;
	const struct synaptics_rmi4_platform_data *platform_data =
			rmi4_data->i2c_client->dev.platform_data;

	mutex_lock(&rmi4_data->rmi4_device_mutex);

	if (!rmi4_data->touch_stopped) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev, "%s already power on\n",
			__func__);
		goto out;
	}

	platform_data->power(true);
	rmi4_data->current_page = MASK_8BIT;
	rmi4_data->touch_stopped = false;

	if (platform_data->enable_sync)
		platform_data->enable_sync(true);

	msleep(SYNAPTICS_HW_RESET_TIME);

	retval = synaptics_rmi4_reinit_device(rmi4_data);
	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to reinit device\n",
				__func__);
	}
	enable_irq(rmi4_data->i2c_client->irq);

	tsp_debug_dbg(true, &rmi4_data->i2c_client->dev, "%s\n", __func__);

out:
	mutex_unlock(&rmi4_data->rmi4_device_mutex);
	return retval;
}

#ifdef USE_OPEN_CLOSE
#ifdef USE_OPEN_DWORK
static void synaptics_rmi4_open_work(struct work_struct *work)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(work, struct synaptics_rmi4_data,
			open_work.work);

	retval = synaptics_rmi4_start_device(rmi4_data);
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to start device\n", __func__);
}
#endif

static int synaptics_rmi4_input_open(struct input_dev *dev)
{
	struct synaptics_rmi4_data *rmi4_data = input_get_drvdata(dev);
	int retval;

	retval = wait_for_completion_interruptible_timeout(&rmi4_data->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));

	if (retval < 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
			"error while waiting for device to init (%d)\n", retval);
		retval = -ENXIO;
		goto err_open;
	}
	if (retval == 0) {
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
			"timedout while waiting for device to init\n");
		retval = -ENXIO;
		goto err_open;
	}

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

#ifdef USE_OPEN_DWORK
	schedule_delayed_work(&rmi4_data->open_work,
					msecs_to_jiffies(TOUCH_OPEN_DWORK_TIME));
#else
	retval = synaptics_rmi4_start_device(rmi4_data);
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to start device\n", __func__);
#endif

	return 0;

err_open:
	return retval;
}

static void synaptics_rmi4_input_close(struct input_dev *dev)
{
	struct synaptics_rmi4_data *rmi4_data = input_get_drvdata(dev);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);
#ifdef USE_OPEN_DWORK
	cancel_delayed_work(&rmi4_data->open_work);
#endif
	synaptics_rmi4_stop_device(rmi4_data);
}
#endif

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
#define synaptics_rmi4_suspend NULL
#define synaptics_rmi4_resume NULL

 /**
 * synaptics_rmi4_early_suspend()
 *
 * Called by the kernel during the early suspend phase when the system
 * enters suspend.
 *
 * This function calls synaptics_rmi4_sensor_sleep() to stop finger
 * data acquisition and put the sensor to sleep.
 */
static void synaptics_rmi4_early_suspend(struct early_suspend *h)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	if (rmi4_data->stay_awake) {
		rmi4_data->staying_awake = true;
		tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s : return due to staying_awake\n",
			__func__);
		return;
	} else {
		rmi4_data->staying_awake = false;
	}

	synaptics_rmi4_stop_device(rmi4_data);

	return;
}

 /**
 * synaptics_rmi4_late_resume()
 *
 * Called by the kernel during the late resume phase when the system
 * wakes up from suspend.
 *
 * This function goes through the sensor wake process if the system wakes
 * up from early suspend (without going into suspend).
 */
static void synaptics_rmi4_late_resume(struct early_suspend *h)
{
	int retval = 0;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	if (rmi4_data->staying_awake) {
		tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s : return due to staying_awake\n",
			__func__);
		return;
	}

	retval = synaptics_rmi4_start_device(rmi4_data);
	if (retval < 0)
		tsp_debug_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to start device\n", __func__);

	return;
}
#else

 /**
 * synaptics_rmi4_suspend()
 *
 * Called by the kernel during the suspend phase when the system
 * enters suspend.
 *
 * This function stops finger data acquisition and puts the sensor to
 * sleep (if not already done so during the early suspend phase),
 * disables the interrupt, and turns off the power to the sensor.
 */
static int synaptics_rmi4_suspend(struct device *dev)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	if (rmi4_data->staying_awake) {
		tsp_debug_info(true, &rmi4_data->i2c_client->dev, "%s : return due to staying_awake\n",
			__func__);
		return 0;
	}

	mutex_lock(&rmi4_data->input_dev->mutex);

	if (rmi4_data->input_dev->users)
		synaptics_rmi4_stop_device(rmi4_data);

	mutex_unlock(&rmi4_data->input_dev->mutex);

	return 0;
}

 /**
 * synaptics_rmi4_resume()
 *
 * Called by the kernel during the resume phase when the system
 * wakes up from suspend.
 *
 * This function turns on the power to the sensor, wakes the sensor
 * from sleep, enables the interrupt, and starts finger data
 * acquisition.
 */
static int synaptics_rmi4_resume(struct device *dev)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	tsp_debug_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	mutex_lock(&rmi4_data->input_dev->mutex);

	if (rmi4_data->input_dev->users) {
		retval = synaptics_rmi4_start_device(rmi4_data);
		if (retval < 0)
			tsp_debug_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to start device\n", __func__);
	}

	mutex_unlock(&rmi4_data->input_dev->mutex);

	return 0;
}
#endif

static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
	.suspend = synaptics_rmi4_suspend,
	.resume  = synaptics_rmi4_resume,
};
#endif

static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);

static struct i2c_driver synaptics_rmi4_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &synaptics_rmi4_dev_pm_ops,
#endif
	},
	.probe = synaptics_rmi4_probe,
	.remove = __devexit_p(synaptics_rmi4_remove),
	.id_table = synaptics_rmi4_id_table,
};

 /**
 * synaptics_rmi4_init()
 *
 * Called by the kernel during do_initcalls (if built-in)
 * or when the driver is loaded (if a module).
 *
 * This function registers the driver to the I2C subsystem.
 *
 */
static int __init synaptics_rmi4_init(void)
{
	return i2c_add_driver(&synaptics_rmi4_driver);
}

 /**
 * synaptics_rmi4_exit()
 *
 * Called by the kernel when the driver is unloaded.
 *
 * This funtion unregisters the driver from the I2C subsystem.
 *
 */
static void __exit synaptics_rmi4_exit(void)
{
	i2c_del_driver(&synaptics_rmi4_driver);
}

module_init(synaptics_rmi4_init);
module_exit(synaptics_rmi4_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics RMI4 I2C Touch Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(SYNAPTICS_RMI4_DRIVER_VERSION);
