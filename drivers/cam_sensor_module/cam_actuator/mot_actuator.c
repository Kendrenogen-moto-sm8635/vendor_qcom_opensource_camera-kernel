/*
 * Copyright (C) 2020 Motorola Mobility LLC.
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
#include <linux/module.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <uapi/linux/media.h>
#include <linux/gpio.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <cam_subdev.h>
#include <cam_sensor_cmn_header.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include "cam_debug_util.h"
#include "cam_cci_dev.h"
#include "mot_actuator.h"
#include "mot_actuator_policy.h"
#include "cam_sensor_io.h"
#include "cam_req_mgr_dev.h"
#include "linux/pm_wakeup.h"

/*=================ACTUATOR HW INFO====================*/
#define DEVICE_NAME_LEN 32
#define MAX_ACTUATOR_NUM 4
#define REGULATOR_NAME_MAX_LEN 32
#define ANDROIDBOOT_DEVICE_MAX_LEN 32
#define LENS_MAX_STAGES 10

typedef enum {
	MOT_ACTUATOR_INVALID,
	MOT_ACTUATOR_FIRST,
	MOT_ACTUATOR_DW9800 = MOT_ACTUATOR_FIRST,
	MOT_ACTUATOR_AK7377,
	MOT_ACTUATOR_AK7316,
	MOT_ACTUATOR_NUM,
} mot_actuator_type;

static int workqueue_status = 0;

typedef enum {
	REGULATOR_IOVDD,
	REGULATOR_AFVDD,
	REGULATOR_NUM,
} mot_regulator_type;

typedef enum {
	MOT_DEVICE_CTWO,
	MOT_DEVICE_NUM,
} mot_dev_type;

typedef struct {
	uint16_t kneePoint;
	uint16_t step;
} mot_park_lens_step;

typedef struct {
	uint16_t kneePoint;
	uint16_t step;
} mot_reset_lens_step;

typedef struct {
	uint16_t kneePoint;
	uint16_t step;
} mot_launch_lens_step;

typedef struct {
	struct cam_sensor_i2c_reg_setting *init_setting;
	struct cam_sensor_i2c_reg_setting *dac_setting;
} mot_actuator_settings;
typedef struct {
	bool launch_lens_needed;
	mot_launch_lens_step launch_lens_step[LENS_MAX_STAGES];
} mot_launch_lens;

typedef struct {
	mot_actuator_type actuator_type;
	uint16_t dac_pos;
	uint16_t init_pos;
	uint16_t cci_addr;
	uint16_t cci_dev;
	uint16_t cci_master;
	char *regulator_list[REGULATOR_NUM];
	uint32_t regulator_volt_uv[REGULATOR_NUM];
	bool park_lens_needed;
	bool reset_lens_needed;
	bool has_ois;
	mot_launch_lens launch_lens;
	uint16_t poweron_delay;//ms
} mot_actuator_hw_info;

typedef struct {
	mot_dev_type dev_type;
	uint16_t actuator_num;
	char dev_name[DEVICE_NAME_LEN];
	mot_actuator_hw_info actuator_info[MAX_ACTUATOR_NUM];
} mot_dev_info;

// /*Register settings of GT9800*/ //todo
static struct cam_sensor_i2c_reg_array mot_dw9800_init_setting[] ={
	{0x02, 0x01, 0},
	{0x02, 0x00, 1000},
	{0x02, 0x02, 0},
	{0x06, 0x40, 0},
	{0x07, 0x7E, 0},
	{0x10, 0x00, 0},
};

static struct cam_sensor_i2c_reg_array mot_dw9800_dac_setting[] ={   //todo
	{0x03, 0xfff, 0}
};

static struct cam_sensor_i2c_reg_setting mot_dw9800_init_settings = {
	.reg_setting = mot_dw9800_init_setting,
	.size = ARRAY_SIZE(mot_dw9800_init_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
};

static struct cam_sensor_i2c_reg_setting mot_dw9800_dac_settings = {
	.reg_setting = mot_dw9800_dac_setting,
	.size = ARRAY_SIZE(mot_dw9800_dac_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_WORD,
};

static struct cam_sensor_i2c_reg_array mot_ak7377_init_setting[] = {
	{0x00, 0x8f, 100},
	{0x01, 0x00, 100},
	{0x02, 0x00, 100},
};

static struct cam_sensor_i2c_reg_array mot_ak7377_dac_setting[] = {
	{0x00, 0x8f00, 0},
};

static struct cam_sensor_i2c_reg_setting mot_ak7377_init_settings = {
	.reg_setting = mot_ak7377_init_setting,
	.size = ARRAY_SIZE(mot_ak7377_init_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
};

static struct cam_sensor_i2c_reg_setting mot_ak7377_dac_settings = {
	.reg_setting = mot_ak7377_dac_setting,
	.size = ARRAY_SIZE(mot_ak7377_dac_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_WORD,
};

static struct cam_sensor_i2c_reg_array mot_ak7316_init_setting[] = {
	{0x02, 0x00, 100},
};

static struct cam_sensor_i2c_reg_array mot_ak7316_dac_setting[] = {//need modify
	{0x00, 0xfff, 0},
};

static struct cam_sensor_i2c_reg_setting mot_ak7316_init_settings = {
	.reg_setting = mot_ak7316_init_setting,
	.size = ARRAY_SIZE(mot_ak7316_init_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
};

static struct cam_sensor_i2c_reg_setting mot_ak7316_dac_settings = {
	.reg_setting = mot_ak7316_dac_setting,
	.size = ARRAY_SIZE(mot_ak7316_dac_setting),
	.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.data_type = CAMERA_SENSOR_I2C_TYPE_WORD,
};

static mot_actuator_settings mot_actuator_list[MOT_ACTUATOR_NUM-1] = {
	//MUST be sorted as definition order in above structure: mot_actuator_type
	{&mot_dw9800_init_settings, &mot_dw9800_dac_settings},
	{&mot_ak7377_init_settings, &mot_ak7377_dac_settings},
	{&mot_ak7316_init_settings, &mot_ak7316_dac_settings},
};

static const mot_dev_info mot_dev_list[MOT_DEVICE_NUM] = {
	{
		.dev_type = MOT_DEVICE_CTWO,
		.actuator_num = 1,
		.dev_name = "ctwo",
		.actuator_info = {
			[0] = {
				.actuator_type = MOT_ACTUATOR_AK7316,
				.dac_pos = 0x4010,
				.cci_addr = 0x0c,
				.cci_dev = 0x00,
				.cci_master = 0x00,
				.has_ois = false,//close temporarily, main camera use SOIS
				.regulator_list = {"ldo4", "pmr735a_l7"},
				.regulator_volt_uv = {3300000, 1800000},
				.park_lens_needed = false,
				.poweron_delay = 6,
			},
		},
	},
};


int32_t mot_ois_start_protection(struct device *device);
int32_t mot_ois_stop_protection(struct device *device);
int32_t mot_ois_handle_shut_down(struct device *device);
int32_t mot_ois_select_device_by_name(char *dev_name);

static uint32_t mot_device_index = MOT_DEVICE_NUM;

static char *bootargs_str;
static char androidboot_device[ANDROIDBOOT_DEVICE_MAX_LEN];
int mot_actuator_get_bootarg(char *key, char **value)
{
	const char *bootargs_tmp = NULL;
	char *idx = NULL;
	char *kvpair = NULL;
	int err = 1;
	struct device_node *n = of_find_node_by_path("/chosen");
	size_t bootargs_tmp_len = 0;

	if (n == NULL)
		goto err;

	if (of_property_read_string(n, "mmi,bootconfig", &bootargs_tmp) != 0)
		goto putnode;

	bootargs_tmp_len = strlen(bootargs_tmp);
	if (!bootargs_str) {
		/* The following operations need a non-const
		 * version of bootargs
		 */
		bootargs_str = kzalloc(bootargs_tmp_len + 1, GFP_KERNEL);
		if (!bootargs_str)
			goto putnode;
	}
	strlcpy(bootargs_str, bootargs_tmp, bootargs_tmp_len + 1);

	idx = strnstr(bootargs_str, key, strlen(bootargs_str));
	if (idx) {
		kvpair = strsep(&idx, " ");
		if (kvpair)
			if (strsep(&kvpair, "=")) {
				*value = strsep(&kvpair, "\n");
				if (*value)
					err = 0;
			}
	}

putnode:
	of_node_put(n);
err:
	return err;
}

static int cam_select_actuator_by_device_name(void)
{
	char *str;
	uint32_t i;
	// TO DO: Create API for mot_actuator_get_bootarg() from mmi_info instead of copy and paste
	if (mot_actuator_get_bootarg("androidboot.device=", &str) == 0)
		strlcpy(androidboot_device, str, ANDROIDBOOT_DEVICE_MAX_LEN);

	mot_ois_select_device_by_name(str);

	for (i=0; i<MOT_DEVICE_NUM; i++) {
		if (!strcmp(str, mot_dev_list[i].dev_name)) {
			mot_device_index = i;
			CAM_DBG(CAM_ACTUATOR, "Found device:%s, index:%d", str, mot_device_index);
			break;
		}
	}

	if (i >= MOT_DEVICE_NUM) {
		CAM_ERR(CAM_ACTUATOR, "UNKNOWN DEVICE:%s", str);
	}
	return 1;
}

/*=================ACTUATOR RUNTIME====================*/
#define VIBRATING_MAX_INTERVAL 2000//ms
#define PHONE_DROP_MAX_DURATION 5000//ms
#define PARK_LENS_MAX_STAGES 10
#define RESET_LENS_MAX_STAGES 10
#define LAUNCH_LENS_MAX_STAGES LENS_MAX_STAGES

struct mot_actuator_ctrl_t {
	struct cam_subdev v4l2_dev_str;
	struct platform_device *pdev;
	char device_name[20];
	struct workqueue_struct *mot_actuator_wq;
	struct delayed_work delay_work;
	struct mutex actuator_lock;
	struct wakeup_source actuator_wakelock;
};

enum mot_actuator_state_e {
	MOT_ACTUATOR_IDLE,
	MOT_ACTUATOR_INITED,
	MOT_ACTUATOR_STARTED,
	MOT_ACTUATOR_RELEASED
};

enum mot_actuator_scenario_e {
	MOT_ACTUATOR_SCENE_INVALID,
	MOT_ACTUATOR_VIB,
	MOT_ACTUATOR_LENS_PROTECT,
};

typedef struct {
	struct cam_sensor_cci_client client;
	struct camera_io_master io_master;
	struct regulator * regulators[REGULATOR_NUM];
	uint32_t safe_dac_pos;
} mot_actuator_runtime_type;

static struct mot_actuator_ctrl_t mot_actuator_fctrl;
static enum mot_actuator_state_e mot_actuator_state = MOT_ACTUATOR_IDLE;
static enum mot_actuator_scenario_e mot_actuator_scene = MOT_ACTUATOR_VIB;
static mot_actuator_runtime_type mot_actuator_runtime[MAX_ACTUATOR_NUM];
static uint32_t lens_safe_pos_dac = 0;
static uint32_t lens_park_pos = 100;
static mot_park_lens_step lens_park_table[PARK_LENS_MAX_STAGES] = {
	{300, 60},
	{200, 40},
};
static mot_reset_lens_step lens_reset_table[RESET_LENS_MAX_STAGES] = {
	{400, 0},
	{100, 100},
};

static bool runtime_inited = 0;
static bool mot_actuator_driver_probed = false;

static int mot_actuator_init_runtime(void)
{
	int i;
	int regIdx;

	if (runtime_inited == true) {
		CAM_DBG(CAM_ACTUATOR, "Runtime has been inited!!!");
		return 0;
	}

	if (mot_device_index >= MOT_DEVICE_NUM) {
		CAM_ERR(CAM_ACTUATOR, "INVALID DEVICE!!!");
		return -1;
	}

	for (i = 0; i < mot_dev_list[mot_device_index].actuator_num; i++) {
		if (mot_dev_list[mot_device_index].actuator_info[i].actuator_type == MOT_ACTUATOR_INVALID) {
			CAM_DBG(CAM_ACTUATOR, "NO. %d actuator type is %d",
				i, mot_dev_list[mot_device_index].actuator_info[i].actuator_type);
			continue;
		}

		/*Init actuators' CCI data*/
		mot_actuator_runtime[i].io_master.master_type = CCI_MASTER;
		mot_actuator_runtime[i].io_master.cci_client = &mot_actuator_runtime[i].client;
		mot_actuator_runtime[i].client.sid = mot_dev_list[mot_device_index].actuator_info[i].cci_addr;
		mot_actuator_runtime[i].client.cci_device = mot_dev_list[mot_device_index].actuator_info[i].cci_dev;
		mot_actuator_runtime[i].client.cci_i2c_master = mot_dev_list[mot_device_index].actuator_info[i].cci_master;
		CAM_DBG(CAM_ACTUATOR, "NO. %d ACTUATOR sid:%x, cci_device:%d, cci_i2c_master:%d",
			i, mot_actuator_runtime[i].client.sid, mot_actuator_runtime[i].client.cci_device,
			mot_actuator_runtime[i].client.cci_i2c_master);

		/*Init actuators' DAC pos*/
		if (lens_safe_pos_dac != 0) {
			mot_actuator_runtime[i].safe_dac_pos = lens_safe_pos_dac;
		} else {
			mot_actuator_runtime[i].safe_dac_pos = mot_dev_list[mot_device_index].actuator_info[i].dac_pos;
		}
		CAM_DBG(CAM_ACTUATOR, "ACTUATOR NO. %d, DAC pos: %d", i, mot_actuator_runtime[i].safe_dac_pos);

		/*Init actuators' regulators*/
		for (regIdx = 0; regIdx < REGULATOR_NUM; regIdx++) {
			char *reg_name = mot_dev_list[mot_device_index].actuator_info[i].regulator_list[regIdx];
			if (reg_name[0] != '\0' && mot_actuator_runtime[i].regulators[regIdx] == NULL) {
				mot_actuator_runtime[i].regulators[regIdx] = regulator_get(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev, reg_name);
				if (mot_actuator_runtime[i].regulators[regIdx] == NULL) {
					CAM_DBG(CAM_ACTUATOR, "REGULATOR NO. %d of actuator %d IS NULL!", regIdx, i);
				} else {
					CAM_DBG(CAM_ACTUATOR, "REGULATOR GET SUCCESS:%p", mot_actuator_runtime[i].regulators[regIdx]);
					regulator_set_voltage(mot_actuator_runtime[i].regulators[regIdx],
						mot_dev_list[mot_device_index].actuator_info[i].regulator_volt_uv[regIdx],
						mot_dev_list[mot_device_index].actuator_info[i].regulator_volt_uv[regIdx]);
				}
			}
		}
	}

	runtime_inited = true;

	return 0;
}

static int mot_actuator_uninit_runtime(void)
{
	int i;
	int regIdx;

	if (runtime_inited == false) {
		CAM_DBG(CAM_ACTUATOR, "Runtime has been uninited!!!");
		return 0;
	}

	if (mot_device_index >= MOT_DEVICE_NUM) {
		CAM_ERR(CAM_ACTUATOR, "INVALID DEVICE!!!");
		return -1;
	}

	for (i = 0; i < mot_dev_list[mot_device_index].actuator_num; i++) {
		if (mot_dev_list[mot_device_index].actuator_info[i].actuator_type == MOT_ACTUATOR_INVALID) {
			CAM_DBG(CAM_ACTUATOR, "NO. %d actuator type is %d",
				i, mot_dev_list[mot_device_index].actuator_info[i].actuator_type);
			continue;
		}

		/*Init actuators' regulators*/
		for (regIdx = 0; regIdx < REGULATOR_NUM; regIdx++) {
			if (mot_actuator_runtime[i].regulators[regIdx] != NULL) {
				regulator_put(mot_actuator_runtime[i].regulators[regIdx]);
				mot_actuator_runtime[i].regulators[regIdx] = NULL;
			}
		}
	}

	runtime_inited = false;

	return 0;
}

static int32_t mot_actuator_apply_settings(struct camera_io_master *io_master_info,
	struct cam_sensor_i2c_reg_setting *write_setting)
{
	/*Usually actuator initial setting will execute power down reset(PD), actuator can't respond
	  CCI access for a while after PD. Add lock to avoid access actuator while PD operation.*/
	int32_t ret = 0;
	mot_actuator_lock();
	ret = camera_io_dev_write(io_master_info, write_setting);
	mot_actuator_unlock();
	return ret;
}

static struct cam_sensor_i2c_reg_setting *mot_actuator_get_init_settings(uint32_t index)
{
	uint32_t actuator_index = mot_dev_list[mot_device_index].actuator_info[index].actuator_type - MOT_ACTUATOR_FIRST;
	return mot_actuator_list[actuator_index].init_setting;
}

static struct cam_sensor_i2c_reg_setting *mot_actuator_get_dac_settings(uint32_t index)
{
	uint32_t actuator_index = mot_dev_list[mot_device_index].actuator_info[index].actuator_type - MOT_ACTUATOR_FIRST;
	return mot_actuator_list[actuator_index].dac_setting;
}

static int32_t mot_actuator_move_lens_by_dac(uint32_t index, uint32_t dac_value)
{
	struct cam_sensor_i2c_reg_setting *dac_setting = mot_actuator_get_dac_settings(index);
	/*Fill the DAC value*/
	dac_setting->reg_setting[0].reg_data = dac_value;
	CAM_DBG(CAM_ACTUATOR, "move lens to DAC pos: %d", dac_value);
	return mot_actuator_apply_settings(&mot_actuator_runtime[index].io_master, dac_setting);
}

static int32_t mot_actuator_power_on(uint32_t index)
{
	int i;
	int ret = 0;
	uint16_t delayms = mot_dev_list[mot_device_index].actuator_info[index].poweron_delay;

	for (i = 0; i < REGULATOR_NUM; i++) {
		if (mot_actuator_runtime[index].regulators[i] != NULL) {
			ret = regulator_enable(mot_actuator_runtime[index].regulators[i]);
			if(ret){
				CAM_ERR(CAM_ACTUATOR, "power on regulators[%d] failed, ret:%d", i,ret);
			}
		}
	}

	if (delayms > 0) {
		usleep_range(delayms*1000, (delayms+1)*1000);
	}

	return ret;
}

static int32_t mot_actuator_power_off(uint32_t index)
{
	int i;
	int ret = 0;

	for (i = 0; i < REGULATOR_NUM; i++) {
		if (mot_actuator_runtime[index].regulators[i] != NULL) {
			ret = regulator_disable(mot_actuator_runtime[index].regulators[i]);
			if(ret){
				CAM_ERR(CAM_ACTUATOR, "power off ret %d", ret);
			}
		}
	}

	return ret;
}

int32_t mot_actuator_init_cci(uint32_t index)
{
	int32_t ret = 0,rc = 0;

	CAM_DBG(CAM_ACTUATOR, "init cci.");
	ret = camera_io_init(&mot_actuator_runtime[index].io_master);
	if (ret != 0) {
		rc = camera_io_release(&mot_actuator_runtime[index].io_master);
		CAM_ERR(CAM_ACTUATOR, "init cci failed!!! ret=%d,release rc=%d. try again!", ret, rc);
		/*delay 100ms and try again*/
		usleep_range(100000, 100100);
		ret = camera_io_init(&mot_actuator_runtime[index].io_master);
		if (ret != 0) {
			rc = camera_io_release(&mot_actuator_runtime[index].io_master);
			CAM_ERR(CAM_ACTUATOR, "try again init cci failed!!! ret=%d, release rc=%d", ret, rc);
		}
	}
	return ret;
}

int32_t mot_actuator_release_cci(uint32_t index)
{
	int32_t ret = 0;

	CAM_DBG(CAM_ACTUATOR, "release cci.");
	ret = camera_io_release(&mot_actuator_runtime[index].io_master);
	if (ret != 0) {
		CAM_ERR(CAM_ACTUATOR, "release cci failed, ret=%d!!!", ret);
	}
	return ret;
}

static int32_t mot_actuator_launch_lens(uint32_t index, uint32_t dac_value)
{
	uint32_t init_len_pos = mot_dev_list[mot_device_index].actuator_info[0].init_pos;
	uint32_t cur_len_pos = init_len_pos;
	uint32_t last_len_pos = 0;
	int stageIndex;
	unsigned int consumers = 0;
	int32_t ret = 0;
	uint32_t lens_launch_pos = dac_value;
	mot_launch_lens_step const *lens_launch_table = mot_dev_list[mot_device_index].actuator_info[index].launch_lens.launch_lens_step;

	if (mot_dev_list[mot_device_index].actuator_info[index].launch_lens.launch_lens_needed == false) {
		CAM_DBG(CAM_ACTUATOR, "Launch lens is not needed.");
		return 0;
	}

	// from 0 to safe_dac_pos
	while (cur_len_pos < lens_launch_pos) {
		consumers = mot_actuator_get_consumers();
		if ((consumers & (~CLINET_VIBRATOR_MASK)) != 0) {
			CAM_WARN(CAM_ACTUATOR, "Launch lens was broken by other actuator requests.");
			break;
		}

		for (stageIndex=0; stageIndex<LAUNCH_LENS_MAX_STAGES; stageIndex++) {
			if (cur_len_pos < lens_launch_table[stageIndex].kneePoint && lens_launch_table[stageIndex].step > 0) {
				cur_len_pos += lens_launch_table[stageIndex].step;
				break;
			}
		}

		if (stageIndex == LAUNCH_LENS_MAX_STAGES && cur_len_pos < lens_launch_pos) {
			cur_len_pos += 10;
		}

		if(last_len_pos == cur_len_pos)
		        /*break the cycle when cur_len_pos didn't update*/
		        break;
		else
		        last_len_pos = cur_len_pos;

		ret = mot_actuator_move_lens_by_dac(index, cur_len_pos);

		if (ret < 0 ) {
			CAM_ERR(CAM_ACTUATOR, "Launch Lens encounter CCI error, break now.");
			break;
		}

		if (cur_len_pos >= lens_launch_pos) {
			/*For skipping the last delay*/
			CAM_WARN(CAM_ACTUATOR, "Launch lens done.");
			break;
		}

		usleep_range(10000, 12000);
	}

	// from 512 to 0
	while ((cur_len_pos > lens_launch_pos) && init_len_pos) {
		consumers = mot_actuator_get_consumers();
		if ((consumers & (~CLINET_VIBRATOR_MASK)) != 0) {
			CAM_WARN(CAM_ACTUATOR, "Launch lens was broken by other actuator requests.");
			break;
		}

		for (stageIndex = 0; stageIndex < LENS_MAX_STAGES; stageIndex++) {
			if (lens_launch_table[stageIndex].kneePoint == 0 && lens_launch_table[stageIndex].step == 0) {
				break;
			}

			if (cur_len_pos >= lens_launch_table[stageIndex].kneePoint && lens_launch_table[stageIndex].step > 0) {
				cur_len_pos -= lens_launch_table[stageIndex].step;
				break;
			}
		}

		if (lens_launch_table[stageIndex].step == 0 && cur_len_pos > lens_launch_pos) {
			cur_len_pos -= 10;
			if (cur_len_pos < 10)
				cur_len_pos = 0;
		}

		ret = mot_actuator_move_lens_by_dac(index, cur_len_pos);

		if (ret < 0 ) {
			CAM_ERR(CAM_ACTUATOR, "Launch Lens encounter CCI error, break now.");
			break;
		}

		if (cur_len_pos <= lens_launch_pos) {
			/*For skipping the last delay*/
			CAM_DBG(CAM_ACTUATOR, "Launch lens done.");
			break;
		}

		usleep_range(10000, 12000);
	}

	return ret;
}

static int32_t mot_actuator_vib_move_lens(uint32_t index)
{
	int32_t ret = 0;
	unsigned int consumers = 0;

	if (mot_device_index >= MOT_DEVICE_NUM || index > mot_dev_list[mot_device_index].actuator_num) {
		CAM_ERR(CAM_ACTUATOR, "INVALID device!!!");
		return -1;
	}

	consumers = mot_actuator_get_consumers();
	CAM_DBG(CAM_ACTUATOR, "consumers=%d",consumers);
	if (consumers & CLINET_CAMERA_MASK) {
		/*Don't init actuator when camera on*/
		CAM_WARN(CAM_ACTUATOR, "Camera is holding actuator,don't init actuator for vibrator,consumers=%d",consumers);
	} else if (mot_actuator_state <= MOT_ACTUATOR_IDLE || mot_actuator_state >= MOT_ACTUATOR_RELEASED) {
		mot_actuator_init_runtime();
		mot_actuator_power_on(index);
		ret = mot_actuator_init_cci(index);
		if (ret == 0) {
			ret = mot_actuator_apply_settings(&mot_actuator_runtime[index].io_master, mot_actuator_get_init_settings(index));
			if (ret == 0) {
				CAM_WARN(CAM_ACTUATOR, "init acutator sucess", ret);
				mot_actuator_state = MOT_ACTUATOR_INITED;
				usleep_range(500,1000);
			} else {
				mot_actuator_release_cci(index);
				CAM_ERR(CAM_ACTUATOR, "init acutator failed, ret=%d, index=%d!!!", ret, index);
			}
		} else {
			CAM_ERR(CAM_ACTUATOR, "init cci device failed, ret=%d!!!", ret);
		}

		if (ret != 0) {
			CAM_ERR(CAM_ACTUATOR, "init actuator encountered error, power off now. ret=%d", ret);
			mot_actuator_power_off(index);
		}
	}

	if (ret == 0 && mot_actuator_state == MOT_ACTUATOR_INITED) {
		/*Move lens to the specified position.*/
		CAM_WARN(CAM_ACTUATOR, "actuator consumers: %d", consumers);

		if ((consumers & CLINET_VIBRATOR_MASK) == 0) {
			mot_actuator_get(ACTUATOR_CLIENT_VIBRATOR);
		}

		if (consumers == 0) {
			/*Just move lens when camera off and before first vibrating*/
			/*use launch_lens to move lens step-by-step,reduce TICK noise*/
			if (mot_dev_list[mot_device_index].actuator_info[index].launch_lens.launch_lens_needed == true) {
				ret = mot_actuator_launch_lens(index, mot_actuator_runtime[index].safe_dac_pos);
			}else {
				ret = mot_actuator_move_lens_by_dac(index, mot_actuator_runtime[index].safe_dac_pos);
				/*delay 10~12ms to wait lens move to specify location*/
				if (mot_dev_list[mot_device_index].actuator_info[index].has_ois) {
					mot_ois_start_protection(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev);
				} else {
					usleep_range(10000, 12000);
				}
			}
			if (ret == 0) {
				CAM_WARN(CAM_ACTUATOR, "actuator is safe now, safe_dac_pos:%d, please start vibrating.",
					mot_actuator_runtime[index].safe_dac_pos);
			} else {
				CAM_ERR(CAM_ACTUATOR, "write dac failed, ret=%d!!!", ret);
			}
		} else {
			CAM_WARN(CAM_ACTUATOR, "actuator is already in position.");
		}

		mot_actuator_state = MOT_ACTUATOR_STARTED;
	} else {
		CAM_WARN(CAM_ACTUATOR, "May be already in position. state: %d ret=%d.", mot_actuator_state, ret);
	}

	return ret;
}

static int32_t mot_actuator_park_lens(uint32_t index)
{
	uint32_t cur_len_pos = mot_actuator_runtime[index].safe_dac_pos;
	int stageIndex;
	unsigned int consumers = 0;
	int32_t ret = 0;

	if (mot_dev_list[mot_device_index].actuator_info[index].park_lens_needed == false) {
		CAM_DBG(CAM_ACTUATOR, "Park lens is not needed.");
		return 0;
	}

	while (cur_len_pos > lens_park_pos) {
		consumers = mot_actuator_get_consumers();
		if ((consumers & (~CLINET_VIBRATOR_MASK)) != 0) {
			CAM_WARN(CAM_ACTUATOR, "Park lens was broken by other actuator requests.");
			break;
		}

		for (stageIndex=0; stageIndex<PARK_LENS_MAX_STAGES; stageIndex++) {
			if (cur_len_pos >= lens_park_table[stageIndex].kneePoint && lens_park_table[stageIndex].step > 0) {
				cur_len_pos -= lens_park_table[stageIndex].step;
				break;
			}
		}

		if (lens_park_table[stageIndex].step <= 0 && cur_len_pos > lens_park_pos) {
			cur_len_pos -= 10;
		}

		ret = mot_actuator_move_lens_by_dac(index, cur_len_pos);

		if (ret < 0 ) {
			CAM_ERR(CAM_ACTUATOR, "Park Lens encounter CCI error, break now.");
			break;
		}

		if (cur_len_pos <= lens_park_pos) {
			/*For skipping the last delay*/
			CAM_DBG(CAM_ACTUATOR, "Park lens done.");
			break;
		}

		usleep_range(10000, 12000);
	}

	return 0;
}

int mot_actuator_on_vibrate_start(void)
{
	bool is_exiting = false;
	ktime_t start,end,duration;
	start = ktime_get();
	__pm_stay_awake(&mot_actuator_fctrl.actuator_wakelock);
	cancel_delayed_work(&mot_actuator_fctrl.delay_work);
	if (mutex_is_locked(&mot_actuator_fctrl.actuator_lock)) {
		is_exiting = true;
	}
	mutex_lock(&mot_actuator_fctrl.actuator_lock);
	if (is_exiting) {
		CAM_WARN(CAM_ACTUATOR, "Wait 20~30us to let CCI release done.");
		usleep_range(20, 30);
	}
	mot_actuator_vib_move_lens(0);
	mutex_unlock(&mot_actuator_fctrl.actuator_lock);
	end = ktime_get();
	duration = ktime_sub(end, start);
	duration /= 1000;
	CAM_WARN(CAM_ACTUATOR, "Move lens delay: %dus", duration);
	return 0;
}
EXPORT_SYMBOL(mot_actuator_on_vibrate_start);

int mot_actuator_on_vibrate_stop(void)
{
	uint32_t delayms = VIBRATING_MAX_INTERVAL;
	if (mot_actuator_scene == MOT_ACTUATOR_LENS_PROTECT) {
		delayms = PHONE_DROP_MAX_DURATION;
	}
	if (mot_actuator_fctrl.mot_actuator_wq != NULL) {
		workqueue_status = 1;
		queue_delayed_work(mot_actuator_fctrl.mot_actuator_wq,
			&mot_actuator_fctrl.delay_work, msecs_to_jiffies(delayms));
	} else {
		//Dedicated work queue may create failed, use default one.
		schedule_delayed_work(&mot_actuator_fctrl.delay_work, msecs_to_jiffies(delayms));
	}
	return 0;
}
EXPORT_SYMBOL(mot_actuator_on_vibrate_stop);

static int32_t mot_actuator_handle_exit(uint32_t arg)
{
	int rc = 0;

	return rc;
}

static int32_t mot_actuator_handle_write(uint32_t arg)
{
	int rc = 0;

	if (mot_actuator_state != MOT_ACTUATOR_INITED) {
		pr_err("%s: mot actuator is not initiated!!", __func__);
		return -EINVAL;
	}

	return rc;
}

static void mot_actuator_handle_release(void)
{
	if (mot_actuator_state == MOT_ACTUATOR_RELEASED) {
		pr_err("%s: Already released.", __func__);
		return;
	}

	if (mot_actuator_state == MOT_ACTUATOR_INITED) {
		pr_debug("Disable AF power!");
		//regulator_disable(afVdd);
		//regulator_disable(ioVdd);
	}
	pr_debug("%s: Vsync released.", __func__);
	mot_actuator_state = MOT_ACTUATOR_RELEASED;
	return;
}

static int32_t mot_actuator_handle_cmd(
		struct v4l2_subdev *sd,
		uint32_t arg,
		unsigned int cmd)
{
	int32_t rc=0;

	switch (cmd) {
	case MOT_ACTUATOR_INIT:
		/* init */
		mot_actuator_init_runtime();
		pr_debug("%s: MOT_ACTUATOR_INIT ret:%d", __func__, rc);
		break;
	case MOT_ACTUATOR_READ:
		/* read */
		pr_debug("%s: MOT_ACTUATOR_READ ret:%d", __func__, rc);
		break;
	case MOT_ACTUATOR_WRITE:
		/* write */
		mot_actuator_handle_write(arg);
		pr_debug("%s: MOT_ACTUATOR_WRITE ret:%d", __func__, rc);
		break;
	case MOT_ACTUATOR_EXIT:
		/* exit */
		mot_actuator_handle_exit(arg);
		pr_debug("%s: MOT_ACTUATOR_EXIT ret:%d", __func__, rc);
		break;
	case MOT_ACTUATOR_RELEASE:
		/* release */
		mot_actuator_handle_release();
		pr_debug("%s: MOT_ACTUATOR_RELEASE ret:%d", __func__, rc);
		break;
	default:
		pr_err("%s: Unknown command (%d)\n", __func__, cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static long mot_actuator_ioctl(struct v4l2_subdev *sd,
		unsigned int cmd, void *arg)
{
	pr_debug("%s cmd=%x, arg=%x\n", __func__, cmd, *(int *)arg);

	switch (cmd) {
	case MOT_ACTUATOR_READ:
	case MOT_ACTUATOR_WRITE:
	case MOT_ACTUATOR_INIT:
	case MOT_ACTUATOR_EXIT:
	case MOT_ACTUATOR_RELEASE:
		return mot_actuator_handle_cmd(sd, *(int *)arg, cmd);
	default:
		pr_err("%s Unsupported cmd=%x, arg=%x\n", __func__, cmd, arg);
		pr_debug("%s supported cmd: %x, %x, %x, %x, %x\n", __func__,
		        MOT_ACTUATOR_READ,
		        MOT_ACTUATOR_WRITE,
		        MOT_ACTUATOR_INIT,
		        MOT_ACTUATOR_EXIT,
		        MOT_ACTUATOR_RELEASE);
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static long mot_actuator_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	int cmd_data;
	int32_t rc = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		pr_err("Failed to copy from user_ptr=%pK size=%zu", (void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	pr_debug("%s cmd=%x, arg=%x\n", __func__, cmd, arg);

	switch (cmd) {
		case MOT_ACTUATOR_READ32:
			cmd = MOT_ACTUATOR_READ;
			break;
		case MOT_ACTUATOR_WRITE32:
			cmd = MOT_ACTUATOR_WRITE;
			break;
		case MOT_ACTUATOR_INIT32:
			cmd = MOT_ACTUATOR_INIT;
			break;
		case MOT_ACTUATOR_EXIT32:
			cmd = MOT_ACTUATOR_EXIT;
			break;
		case MOT_ACTUATOR_RELEASE32:
			cmd = MOT_ACTUATOR_RELEASE;
			break;
		default:
			pr_err("%s Unsupported cmd=%x, arg=%x\n", __func__, cmd, arg);
			pr_debug("%s supported cmd: %x, %x, %x, %x, %x\n", __func__,
			        MOT_ACTUATOR_READ32,
			        MOT_ACTUATOR_WRITE32,
			        MOT_ACTUATOR_INIT32,
			        MOT_ACTUATOR_EXIT32,
			        MOT_ACTUATOR_RELEASE32);
			return -ENOIOCTLCMD;
	}

	rc = mot_actuator_ioctl(sd, cmd, (void *)&cmd_data);

	return rc;
}
#endif

static struct v4l2_subdev_core_ops mot_actuator_subdev_core_ops = {
	.ioctl = mot_actuator_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = mot_actuator_ioctl32,
#endif
};

static struct v4l2_subdev_ops mot_actuator_subdev_ops = {
	.core = &mot_actuator_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops mot_actuator_internal_ops;

static int32_t mot_actuator_reset_lens(uint32_t index)
{
	uint32_t cur_len_pos = mot_actuator_runtime[index].safe_dac_pos;
	int stageIndex;
	unsigned int consumers = 0;
	int32_t ret = 0;

	if (mot_dev_list[mot_device_index].actuator_info[index].reset_lens_needed == false) {
		CAM_DBG(CAM_ACTUATOR, "reset lens is not needed.");
		return 0;
	}
	/*The first step is to get back to a specify position,regardless of the step size*/
	if(cur_len_pos > lens_reset_table[0].kneePoint)
	{
		cur_len_pos = lens_reset_table[0].kneePoint;
		ret = mot_actuator_move_lens_by_dac(index, cur_len_pos);
		usleep_range(10000, 12000);
	}
	while (cur_len_pos > lens_park_pos) {
		consumers = mot_actuator_get_consumers();
		if ((consumers & (~CLINET_VIBRATOR_MASK)) != 0) {
			CAM_WARN(CAM_ACTUATOR, "reset lens was broken by other actuator requests.");
			break;
		}

		for (stageIndex=1; stageIndex<RESET_LENS_MAX_STAGES; stageIndex++) {
			if (cur_len_pos >= lens_reset_table[stageIndex].kneePoint && lens_reset_table[stageIndex].step > 0) {
				cur_len_pos -= lens_reset_table[stageIndex].step;
				break;
			}
		}

		if (lens_reset_table[stageIndex].step <= 0 && cur_len_pos > lens_park_pos) {
			cur_len_pos -= 10;
		}

		ret = mot_actuator_move_lens_by_dac(index, cur_len_pos);
		if (ret < 0 ) {
			CAM_ERR(CAM_ACTUATOR, "reset Lens encounter CCI error, break now.");
			break;
		}

		if (cur_len_pos <= lens_park_pos) {
			/*For skipping the last delay*/
			CAM_DBG(CAM_ACTUATOR, "reset lens done.");
			break;
		}
		usleep_range(10000, 12000);
	}
	return 0;
}

void mot_actuator_handle_exile(void)
{
	unsigned int consumers = 0,ret = 0;
	int actuatorUsers = 0;
	ktime_t start,end,duration;
	mutex_lock(&mot_actuator_fctrl.actuator_lock);
	consumers = mot_actuator_get_consumers();
	/*Only reset lens when vibrator in consumer list*/
	if ((consumers & CLINET_VIBRATOR_MASK) != 0) {
		actuatorUsers = mot_actuator_put(ACTUATOR_CLIENT_VIBRATOR);
		start = ktime_get();
		if (mot_actuator_state >= MOT_ACTUATOR_INITED && mot_actuator_state < MOT_ACTUATOR_RELEASED) {
			if ((consumers & (~CLINET_VIBRATOR_MASK)) == 0) {
				//Only reset lens when there's no other user holding actuator.
				CAM_DBG(CAM_ACTUATOR, "reset _lens_now");
				ret = mot_actuator_reset_lens(0);
			}
			mot_actuator_power_off(0);
			mot_actuator_release_cci(0);
			if (mot_dev_list[mot_device_index].actuator_info[0].has_ois) {
				mot_ois_stop_protection(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev);
			}
		}
		mot_actuator_state = MOT_ACTUATOR_RELEASED;
		end = ktime_get();
		duration = ktime_sub(end, start);
		duration /= 1000;
		CAM_WARN(CAM_ACTUATOR, "reset lens delay: %dus", duration);
	}
	mutex_unlock(&mot_actuator_fctrl.actuator_lock);
}

EXPORT_SYMBOL(mot_actuator_handle_exile);
static void mot_actuator_delayed_process(struct work_struct *work)
{
	struct mot_actuator_ctrl_t *actuator_fctrl = container_of(work,
                                  struct mot_actuator_ctrl_t, delay_work.work);
	unsigned int consumers = 0;
	int actuatorUsers = 0;

	mutex_lock(&actuator_fctrl->actuator_lock);
	consumers = mot_actuator_get_consumers();
	if ((consumers & CLINET_VIBRATOR_MASK) != 0) {
		actuatorUsers = mot_actuator_put(ACTUATOR_CLIENT_VIBRATOR);
	}
	if (mot_actuator_state >= MOT_ACTUATOR_INITED && mot_actuator_state < MOT_ACTUATOR_RELEASED) {
		if ((consumers & (~CLINET_VIBRATOR_MASK)) == 0) {
			//Only park lens when there's no other user holding actuator.
			mot_actuator_park_lens(0);
		}
		mot_actuator_power_off(0);
		mot_actuator_release_cci(0);
		if (mot_dev_list[mot_device_index].actuator_info[0].has_ois) {
			mot_ois_stop_protection(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev);
		}
	}
	mot_actuator_state = MOT_ACTUATOR_RELEASED;
	workqueue_status = 0;
	CAM_WARN(CAM_ACTUATOR, "release actuator done");
	mutex_unlock(&actuator_fctrl->actuator_lock);
	__pm_relax(&actuator_fctrl->actuator_wakelock);
}

static inline ssize_t msm_actuator_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	count = sprintf(buf, "\nmot_actuator_state: %d, device index: %d\n", mot_actuator_state, mot_device_index);
	return count;
}

static ssize_t msm_actuator_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (kstrtouint(buf, 10, &input) != 0) {
		retval = -EINVAL;
		goto exit;
	}

	if (input) {
		if (input == MOT_ACTUATOR_LENS_PROTECT) {
			mot_actuator_scene = MOT_ACTUATOR_LENS_PROTECT;
		} else {
			mot_actuator_scene = MOT_ACTUATOR_VIB;
		}
		CAM_WARN(CAM_ACTUATOR, "start actuator, scene: %d", mot_actuator_scene);
		mot_actuator_on_vibrate_start();
	} else {
		CAM_WARN(CAM_ACTUATOR, "stop actuator");
		mot_actuator_on_vibrate_stop();
	}
	retval = count;
exit:
	return retval;
}

//Tuning interface
static inline ssize_t mot_actuator_lens_park_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	count = sprintf(buf, "\n lens park pos: %d\n", lens_park_pos);
	return count;
}

static ssize_t mot_actuator_lens_park_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (kstrtouint(buf, 10, &input) != 0) {
		retval = -EINVAL;
		goto exit;
	}

	lens_park_pos = input;

	retval = count;
exit:
	return retval;
}

static inline ssize_t mot_actuator_safe_pos_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	count = sprintf(buf, "\n safe dac value steps: %d\n", lens_safe_pos_dac);
	return count;
}

static ssize_t mot_actuator_safe_pos_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (kstrtouint(buf, 10, &input) != 0) {
		retval = -EINVAL;
		goto exit;
	}

	lens_safe_pos_dac = input;
	//reinitialize when the safe_pos parameter is manually configured.
	runtime_inited = false;
	retval = count;
exit:
	return retval;
}

static inline ssize_t mot_actuator_dump_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	count = mot_actuator_dump(buf);
	return count;
}

static ssize_t mot_actuator_dump_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

int mot_actuator_get_workqueue_status(void)
{
	return workqueue_status;
}
EXPORT_SYMBOL(mot_actuator_get_workqueue_status);

static inline ssize_t mot_actuator_park_lens_table_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	int i;
	count += sprintf(buf, "\n  knee point  step\n");
	for (i=0; i<PARK_LENS_MAX_STAGES; i++) {
		if (lens_park_table[i].kneePoint != 0 || lens_park_table[i].step != 0) {
			count += sprintf(buf, "\n  %d        %d\n", lens_park_table[i].kneePoint, lens_park_table[i].step);
		}
	}
	return count;
}

static ssize_t mot_actuator_park_lens_table_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t kneePoint = 0;
	uint32_t step = 0;
	int index = 0;

	sscanf(buf, "%d %d", &kneePoint, &step);
	if (kneePoint == 0 && step == 0) {
		memset(lens_park_table, 0x0, sizeof(lens_park_table));
		return count;
	}

	if (step > 0) {
		for (index=0; index<PARK_LENS_MAX_STAGES; index++) {
			if (lens_park_table[index].kneePoint < kneePoint) {
				memcpy(&lens_park_table[index+1], &lens_park_table[index],
					(PARK_LENS_MAX_STAGES-index-1)*sizeof(lens_park_table[0]));
				lens_park_table[index].kneePoint = kneePoint;
				lens_park_table[index].step = step;
				break;
			}
		}
	}

	for (index=0; index<PARK_LENS_MAX_STAGES; index++) {
		CAM_DBG(CAM_ACTUATOR, "Park Lens Table[%d] %d, %d", index,
			lens_park_table[index].kneePoint, lens_park_table[index].step);
	}
	return count;
}

static struct device_attribute mot_actuator_attrs[] = {
	__ATTR(onekey_actuator, 0660,
			msm_actuator_show,
			msm_actuator_store),
	__ATTR(park_pos, 0660,
			mot_actuator_lens_park_show,
			mot_actuator_lens_park_store),
	__ATTR(safe_pos, 0660,
			mot_actuator_safe_pos_show,
			mot_actuator_safe_pos_store),
	__ATTR(park_lens_table, 0660,
			mot_actuator_park_lens_table_show,
			mot_actuator_park_lens_table_store),
	__ATTR(dump, 0440,
			mot_actuator_dump_show,
			mot_actuator_dump_store),
};

static int mot_actuator_init_subdev(struct device *dev, struct mot_actuator_ctrl_t *f_ctrl)
{
	int rc = 0;
	struct platform_device *pdev = to_platform_device(dev);

	f_ctrl->v4l2_dev_str.internal_ops = &mot_actuator_internal_ops;
	f_ctrl->v4l2_dev_str.ops = &mot_actuator_subdev_ops;
	strlcpy(f_ctrl->device_name, MOT_ACTUATOR_NAME, sizeof(f_ctrl->device_name));
	f_ctrl->v4l2_dev_str.name = f_ctrl->device_name;
	f_ctrl->v4l2_dev_str.sd_flags =
		(V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS);
	f_ctrl->v4l2_dev_str.ent_function = CAM_MOT_ACTUATOR_TYPE;
	f_ctrl->v4l2_dev_str.token = f_ctrl;

	f_ctrl->v4l2_dev_str.pdev = pdev;

	f_ctrl->mot_actuator_wq = NULL;
	f_ctrl->mot_actuator_wq = create_singlethread_workqueue("mot_actuator_wq");
	if (f_ctrl->mot_actuator_wq == NULL) {
		CAM_ERR(CAM_ACTUATOR, "create work queue failed!!!");
	}

	INIT_DELAYED_WORK(&f_ctrl->delay_work, mot_actuator_delayed_process);
	mutex_init(&f_ctrl->actuator_lock);
	memset(&f_ctrl->actuator_wakelock, 0, sizeof(f_ctrl->actuator_wakelock));
	f_ctrl->actuator_wakelock.name = "actuator_hold_wl";
	wakeup_source_add(&f_ctrl->actuator_wakelock);

	{//Debug/tuning interfaces
		int i;
		for (i=0; i<ARRAY_SIZE(mot_actuator_attrs); i++) {
			rc = sysfs_create_file(&f_ctrl->v4l2_dev_str.pdev->dev.kobj,
					&mot_actuator_attrs[i].attr);
		}
	}

	rc = cam_register_subdev(&(f_ctrl->v4l2_dev_str));
	if (rc)
		CAM_ERR(CAM_ACTUATOR, "Fail with cam_register_subdev rc: %d", rc);

	return rc;
}

static int mot_actuator_component_bind(struct device *dev,
	struct device *master_dev, void *data)
{
	CAM_WARN(CAM_ACTUATOR,"enter");

	(void)master_dev;
	(void)data;

	mot_actuator_init_subdev(dev, &mot_actuator_fctrl);
	cam_select_actuator_by_device_name();

	CAM_WARN(CAM_ACTUATOR,"exit");

	return 0;
}

static void mot_actuator_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
	CAM_WARN(CAM_ACTUATOR,"enter");

	(void)dev;
	(void)master_dev;
	(void)data;

	cancel_delayed_work_sync(&mot_actuator_fctrl.delay_work);
	if (mot_actuator_fctrl.mot_actuator_wq != NULL) {
		flush_workqueue(mot_actuator_fctrl.mot_actuator_wq);
		destroy_workqueue(mot_actuator_fctrl.mot_actuator_wq);
		mot_actuator_fctrl.mot_actuator_wq = NULL;
	}
	cam_unregister_subdev(&mot_actuator_fctrl.v4l2_dev_str);
	__pm_relax(&mot_actuator_fctrl.actuator_wakelock);
	wakeup_source_remove(&mot_actuator_fctrl.actuator_wakelock);

	CAM_WARN(CAM_ACTUATOR,"exit");
}

const static struct component_ops mot_actuator_component_ops = {
	.bind = mot_actuator_component_bind,
	.unbind = mot_actuator_component_unbind,
};

static int32_t mot_actuator_platform_remove(
	struct platform_device *pdev)
{
	component_del(&pdev->dev, &mot_actuator_component_ops);
	return 0;
}

static void mot_actuator_platform_shutdown(struct platform_device *pdev)
{
	unsigned int consumers = 0;
	int actuatorUsers = 0;
	ktime_t start,end,duration;
	if (mot_actuator_driver_probed == true) {
		mutex_lock(&mot_actuator_fctrl.actuator_lock);
		consumers = mot_actuator_get_consumers();
		/*Only reset lens when vibrator in consumer list*/
		if ((consumers & CLINET_VIBRATOR_MASK) != 0) {
			actuatorUsers = mot_actuator_put(ACTUATOR_CLIENT_VIBRATOR);
			start = ktime_get();
			if (mot_actuator_state >= MOT_ACTUATOR_INITED && mot_actuator_state < MOT_ACTUATOR_RELEASED) {
				mot_actuator_power_off(0);
				mot_actuator_release_cci(0);
				if (mot_dev_list[mot_device_index].actuator_info[0].has_ois) {
					mot_ois_stop_protection(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev);
				}
			}
			mot_actuator_state = MOT_ACTUATOR_RELEASED;
			end = ktime_get();
			duration = ktime_sub(end, start);
			duration /= 1000;
			CAM_WARN(CAM_ACTUATOR, "reset lens delay: %dus", duration);
		}
		mot_actuator_uninit_runtime();
		if (mot_dev_list[mot_device_index].actuator_info[0].has_ois) {
			mot_ois_handle_shut_down(&mot_actuator_fctrl.v4l2_dev_str.pdev->dev);
		}
		mutex_unlock(&mot_actuator_fctrl.actuator_lock);
	}

	mot_actuator_driver_probed = false;
	return;
}

static const struct of_device_id mot_actuator_driver_dt_match[] = {
	{.compatible = "mot,actuator"},
	{}
};

static int32_t mot_actuator_driver_platform_probe(
	struct platform_device *pdev)
{
	int rc = 0;

	CAM_DBG(CAM_ACTUATOR, "Adding sensor actuator component");
	rc = component_add(&pdev->dev, &mot_actuator_component_ops);
	if (rc) {
		mot_actuator_driver_probed = false;
		CAM_ERR(CAM_ICP, "failed to add component rc: %d", rc);
	} else {
		mot_actuator_driver_probed = true;
	}

	return rc;
}

MODULE_DEVICE_TABLE(of, mot_actuator_driver_dt_match);

struct platform_driver mot_actuator_platform_driver = {
	.probe = mot_actuator_driver_platform_probe,
	.driver = {
		.name = "mot,actuator",
		.owner = THIS_MODULE,
		.of_match_table = mot_actuator_driver_dt_match,
		.suppress_bind_attrs = true,
	},
	.remove = mot_actuator_platform_remove,
	.shutdown = mot_actuator_platform_shutdown,
};

int mot_actuator_driver_init(void)
{
	int32_t rc = 0;

	rc = platform_driver_register(&mot_actuator_platform_driver);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR,
			"platform_driver_register failed rc = %d", rc);
		return rc;
	}

	return rc;
}

void mot_actuator_driver_exit(void)
{
	platform_driver_unregister(&mot_actuator_platform_driver);
}

MODULE_DESCRIPTION("mot_actuator_driver");
MODULE_LICENSE("GPL v2");

