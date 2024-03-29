#if TSP_SEC_FACTORY
#include <linux/uaccess.h>

#ifdef CONFIG_INPUT_BOOSTER
#include <linux/input/input_booster.h>
#endif

static void set_default_result(struct mxt_fac_data *data)
{
	char delim = ':';

	memset(data->cmd_result, 0x00, ARRAY_SIZE(data->cmd_result));
	memcpy(data->cmd_result, data->cmd, strlen(data->cmd));
	strncat(data->cmd_result, &delim, 1);
}

static void set_cmd_result(struct mxt_fac_data *data, char *buff, int len)
{
	strncat(data->cmd_result, buff, len);
}

static void not_support_cmd(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};

	set_default_result(fdata);
	sprintf(buff, "%s", "NA");
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_NOT_APPLICABLE;
	tsp_debug_info(true, &client->dev, "%s: %s \"%s(%d)\"\n",
		__func__, fdata->cmd, buff, strnlen(buff, sizeof(buff)));

	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = false;
	mutex_unlock(&fdata->cmd_lock);
}

static bool mxt_check_xy_range(struct mxt_data *data, u16 node)
{
	u8 x_line = node / data->info.matrix_ysize;
	u8 y_line = node % data->info.matrix_ysize;
	return (y_line < data->fdata->num_ynode) ?
		(x_line < data->fdata->num_xnode) : false;
}

/* +  Vendor specific helper functions */
static int mxt_xy_to_node(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};
	int node;

	/* cmd_param[0][1] : [x][y] */
	if (fdata->cmd_param[0] < 0
		|| fdata->cmd_param[0] >= data->fdata->num_xnode
		|| fdata->cmd_param[1] < 0
		|| fdata->cmd_param[1] >= data->fdata->num_ynode) {
		snprintf(buff, sizeof(buff) , "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;

		tsp_debug_info(true, &client->dev, "%s: parameter error: %u,%u\n",
			__func__, fdata->cmd_param[0], fdata->cmd_param[1]);
		return -EINVAL;
	}

	/*
	 * maybe need to consider orient.
	 *   --> y number
	 *  |(0,0) (0,1)
	 *  |(1,0) (1,1)
	 *  v
	 *  x number
	 */
	node = fdata->cmd_param[0] * data->fdata->num_ynode
			+ fdata->cmd_param[1];

	tsp_debug_info(true, &client->dev, "%s: node = %d\n", __func__, node);
	return node;
}

static void mxt_node_to_xy(struct mxt_data *data, u16 *x, u16 *y)
{
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	*x = fdata->delta_max_node / data->fdata->num_ynode;
	*y = fdata->delta_max_node % data->fdata->num_ynode;

	tsp_debug_info(true, &client->dev, "%s: node[%d] is X,Y=%d,%d\n",
		__func__, fdata->delta_max_node, *x, *y);
}

static int mxt_set_diagnostic_mode(struct mxt_data *data, u8 dbg_mode)
{
	struct i2c_client *client = data->client;
	u8 cur_mode;
	int ret;

	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
			MXT_COMMAND_DIAGNOSTIC, dbg_mode);

	if (ret) {
		tsp_debug_err(true, &client->dev, "Failed change diagnositc mode to %d\n",
			 dbg_mode);
		goto out;
	}

	if (dbg_mode & MXT_DIAG_MODE_MASK) {
		do {
			ret = mxt_read_object(data, MXT_DEBUG_DIAGNOSTIC_T37,
				MXT_DIAGNOSTIC_MODE, &cur_mode);
			if (ret) {
				tsp_debug_err(true, &client->dev, "Failed getting diagnositc mode\n");
				goto out;
			}
			msleep(20);
		} while (cur_mode != dbg_mode);
		tsp_debug_dbg(false, &client->dev, "current dianostic chip mode is %d\n",
			 cur_mode);
	}

out:
	return ret;
}

static int mxt_read_diagnostic_data(struct mxt_data *data,
	u8 dbg_mode, u16 node, u16 *dbg_data)
{
	struct i2c_client *client = data->client;
	struct mxt_object *dbg_object;
	u8 read_page, read_point;
	u8 cur_page, cnt_page;
	u8 data_buf[DATA_PER_NODE] = { 0 };
	int ret = 0;

	/* calculate the read page and point */
	read_page = node / NODE_PER_PAGE;
	node %= NODE_PER_PAGE;
	read_point = (node * DATA_PER_NODE) + 2;

	/* to make the Page Num to 0 */
	ret = mxt_set_diagnostic_mode(data, MXT_DIAG_CTE_MODE);
	if (ret)
		goto out;

	/* change the debug mode */
	ret = mxt_set_diagnostic_mode(data, dbg_mode);
	if (ret)
		goto out;

	/* get object info for diagnostic */
	dbg_object = mxt_get_object(data, MXT_DEBUG_DIAGNOSTIC_T37);
	if (!dbg_object) {
		tsp_debug_err(true, &client->dev, "fail to get object_info\n");
		ret = -EINVAL;
		goto out;
	}

	/* move to the proper page */
	for (cnt_page = 1; cnt_page <= read_page; cnt_page++) {
		ret = mxt_set_diagnostic_mode(data, MXT_DIAG_PAGE_UP);
		if (ret)
			goto out;
		do {
			msleep(20);
			ret = mxt_read_mem(data,
				dbg_object->start_address + MXT_DIAGNOSTIC_PAGE,
				1, &cur_page);
			if (ret) {
				tsp_debug_err(true, &client->dev, "%s Read fail page\n",
					__func__);
				goto out;
			}
		} while (cur_page != cnt_page);
	}

	/* read the dbg data */
	ret = mxt_read_mem(data, dbg_object->start_address + read_point,
		DATA_PER_NODE, data_buf);
	if (ret)
		goto out;

	*dbg_data = ((u16)data_buf[1] << 8) + (u16)data_buf[0];

	tsp_debug_info(true, &client->dev, "dbg_mode[%d]: dbg data[%d] = %d\n",
		dbg_mode, (read_page * NODE_PER_PAGE) + node,
		dbg_mode == MXT_DIAG_DELTA_MODE ? (s16)(*dbg_data) : *dbg_data);
out:
	return ret;
}

static void mxt_treat_dbg_data(struct mxt_data *data,
	struct mxt_object *dbg_object, u8 dbg_mode, u8 read_point, u16 num)
{
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	u8 data_buffer[DATA_PER_NODE] = { 0 };
	bool reference_compare = true;
	int x_num = num / data->fdata->num_ynode;
	int y_num = num % data->fdata->num_ynode;
	u16 reference_temp = 0;

	if (dbg_mode == MXT_DIAG_DELTA_MODE) {
		/* read delta data */
		mxt_read_mem(data, dbg_object->start_address + read_point,
			DATA_PER_NODE, data_buffer);

		fdata->delta[num] =
			((u16)data_buffer[1]<<8) + (u16)data_buffer[0];

		tsp_debug_dbg(false, &client->dev, "delta[%d] = %d\n",
			num, fdata->delta[num]);

		if (abs(fdata->delta[num])
			> abs(fdata->delta_max_data)) {
			fdata->delta_max_node = num;
			fdata->delta_max_data = fdata->delta[num];
		}
	} else if (dbg_mode == MXT_DIAG_REFERENCE_MODE) {
		/* read reference data */
		mxt_read_mem(data, dbg_object->start_address + read_point,
			DATA_PER_NODE, data_buffer);

		reference_temp = ((u16)data_buffer[1] << 8) + (u16)data_buffer[0];
		if (reference_temp < REF_OFFSET_VALUE ) {
			msleep(10);
			tsp_debug_err(true, &client->dev, "reference[%d] is out of range = %d(%d,%d) & retry!\n",
				num, reference_temp, x_num, y_num);
			mxt_read_mem(data, dbg_object->start_address + read_point,
				DATA_PER_NODE, data_buffer);
			reference_temp = ((u16)data_buffer[1] << 8) + (u16)data_buffer[0];
			tsp_debug_err(true, &client->dev, "reference[%d] = %d(%d,%d)\n",
				num, reference_temp, x_num, y_num);
		}

		reference_temp = reference_temp < REF_OFFSET_VALUE ? 0 : reference_temp;
		fdata->reference[num] = reference_temp ? reference_temp - REF_OFFSET_VALUE : 0;

		/* check that reference is in spec or not */
		if (fdata->reference[num] < REF_MIN_VALUE
			|| fdata->reference[num] > REF_MAX_VALUE) {
			tsp_debug_err(true, &client->dev, "reference[%d] is out of range = %d(%d,%d)\n",
				num, fdata->reference[num], x_num, y_num);
		}

#if ENABLE_TOUCH_KEY
		/* Y-node of touch-key reference value does not compared. */
		if (y_num == (data->pdata->touchkey[0].ynode)) {
			reference_compare = false;
		}

		if (reference_compare)
#endif
		{
			if (fdata->reference[num] > fdata->ref_max_data)
				fdata->ref_max_data = fdata->reference[num];
			if (fdata->reference[num] < fdata->ref_min_data)
				fdata->ref_min_data =	fdata->reference[num];
		}
#if ENABLE_TOUCH_KEY
		reference_compare = true;
#endif
		tsp_debug_dbg(false, &client->dev, "reference[%d] = %d\n",
				num, fdata->reference[num]);
	}
}

static int mxt_read_all_diagnostic_data(struct mxt_data *data, u8 dbg_mode)
{
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_object *dbg_object;
	u8 read_page, cur_page, end_page, read_point;
	u16 node, num = 0,  cnt = 0;
	int ret = 0;

	/* to make the Page Num to 0 */
	ret = mxt_set_diagnostic_mode(data, MXT_DIAG_CTE_MODE);
	if (ret)
		goto out;

	/* change the debug mode */
	ret = mxt_set_diagnostic_mode(data, dbg_mode);
	if (ret)
		goto out;

	/* get object info for diagnostic */
	dbg_object = mxt_get_object(data, MXT_DEBUG_DIAGNOSTIC_T37);
	if (!dbg_object) {
		tsp_debug_err(true, &client->dev, "fail to get object_info\n");
		ret = -EINVAL;
		goto out;
	}
	fdata->ref_min_data = REF_MAX_VALUE;
	fdata->ref_max_data = REF_MIN_VALUE;
	fdata->delta_max_data = 0;
	fdata->delta_max_node = 0;
	end_page = (data->info.matrix_xsize * data->info.matrix_ysize)
				/ NODE_PER_PAGE;

	/* read the dbg data */
	for (read_page = 0 ; read_page < end_page; read_page++) {
		for (node = 0; node < NODE_PER_PAGE; node++) {
			read_point = (node * DATA_PER_NODE) + 2;

			if (mxt_check_xy_range(data, cnt)) {
				mxt_treat_dbg_data(data, dbg_object, dbg_mode,
					read_point, num);
				num++;
			}
			cnt++;
		}
		ret = mxt_set_diagnostic_mode(data, MXT_DIAG_PAGE_UP);
		if (ret)
			goto out;
		do {
			msleep(20);
			ret = mxt_read_mem(data,
				dbg_object->start_address + MXT_DIAGNOSTIC_PAGE,
				1, &cur_page);
			if (ret) {
				tsp_debug_err(true, &client->dev,
					"%s Read fail page\n", __func__);
				goto out;
			}
		} while (cur_page != read_page + 1);
	}

	if (dbg_mode == MXT_DIAG_REFERENCE_MODE) {
		tsp_debug_info(true, &client->dev, "min/max reference is [%d/%d]\n",
			fdata->ref_min_data, fdata->ref_max_data);
	} else if (dbg_mode == MXT_DIAG_DELTA_MODE) {
		tsp_debug_info(true, &client->dev, "max delta node %d=[%d]\n",
			fdata->delta_max_node, fdata->delta_max_data);
	}
out:
	return ret;
}

/*
 * find the x,y position to use maximum delta.
 * it is diffult to map the orientation and caculate the node number
 * because layout is always different according to device
 */
static void find_delta_node(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct mxt_fac_data *fdata = data->fdata;
	char buff[16] = {0};
	u16 x, y;
	int ret;

	set_default_result(fdata);

	/* read all delta to get the maximum delta value */
	ret = mxt_read_all_diagnostic_data(data,
			MXT_DIAG_DELTA_MODE);
	if (ret) {
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
		mxt_node_to_xy(data, &x, &y);
		snprintf(buff, sizeof(buff), "%d,%d", x, y);
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));

		fdata->cmd_state = CMD_STATUS_OK;
	}
}

/* -  Vendor specific helper functions */

/* + function realted samsung factory test */
static int mxt_load_fw_from_ums(struct mxt_fw_info *fw_info,
		const u8 *fw_data)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	struct file *filp = NULL;
	struct firmware fw;
	mm_segment_t old_fs = {0};
	const char *firmware_name =
		data->pdata->firmware_name ?: MXT_DEFAULT_FIRMWARE_NAME;
	char *fw_path;
	int ret = 0;

	memset(&fw, 0, sizeof(struct firmware));

	fw_path = kzalloc(MXT_MAX_FW_PATH, GFP_KERNEL);
	if (fw_path == NULL) {
		tsp_debug_err(true, dev, "Failed to allocate firmware path.\n");
		return -ENOMEM;
	}

	snprintf(fw_path, MXT_MAX_FW_PATH, "/sdcard/%s", firmware_name);

	old_fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		tsp_debug_err(true, dev, "Could not open firmware: %s,%d\n",
			fw_path, (s32)filp);
		ret = -ENOENT;
		goto err_open;
	}

	fw.size = filp->f_path.dentry->d_inode->i_size;

	fw_data = kzalloc(fw.size, GFP_KERNEL);
	if (!fw_data) {
		tsp_debug_err(true, dev, "Failed to alloc buffer for fw\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	ret = vfs_read(filp, (char __user *)fw_data, fw.size, &filp->f_pos);
	if (ret != fw.size) {
		tsp_debug_err(true, dev, "Failed to read file %s (ret = %d)\n",
			fw_path, ret);
		ret = -EINVAL;
		goto err_alloc;
	}
	fw.data = fw_data;

	ret = mxt_verify_fw(fw_info, &fw);

err_alloc:
	filp_close(filp, current->files);
err_open:
	set_fs(old_fs);
	kfree(fw_path);

	return ret;
}

static int mxt_load_fw_from_req_fw(struct mxt_fw_info *fw_info,
		const struct firmware *fw)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	const char *firmware_name =
		data->pdata->firmware_name ?: MXT_DEFAULT_FIRMWARE_NAME;
	int ret = 0;

	if (MXT_FIRMWARE_UPDATE_TYPE) {
		char fw_path[MXT_MAX_FW_PATH];

		memset(&fw_path, 0, MXT_MAX_FW_PATH);

		snprintf(fw_path, MXT_MAX_FW_PATH, "%s%s",
			MXT_FIRMWARE_INKERNEL_PATH, firmware_name);

		tsp_debug_err(true, dev, "%s\n", fw_path);

		ret = request_firmware(&fw, fw_path, dev);
		if (ret) {
			tsp_debug_err(true, dev,
				"Could not request firmware %s\n", fw_path);
			goto out;
		}
	} else {
		ret = request_firmware_nowait(THIS_MODULE, true, firmware_name,
					dev, GFP_KERNEL,
					data, mxt_request_firmware_work);
		if (ret) {
			tsp_debug_err(true, dev,
				"Could not request firmware %s\n",
				firmware_name);
			goto out;
		}
	}

	ret = mxt_verify_fw(fw_info, fw);
out:
	return ret;
}

static void fw_update(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct device *dev = &data->client->dev;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_fw_info fw_info;
	const u8 *fw_data = NULL;
	const struct firmware *fw = NULL;
	int ret = 0;
	char buff[16] = {0};

	memset(&fw_info, 0, sizeof(struct mxt_fw_info));
	fw_info.data = data;

	set_default_result(fdata);
	switch (fdata->cmd_param[0]) {

	case MXT_FW_FROM_UMS:
		ret = mxt_load_fw_from_ums(&fw_info, fw_data);
		if (ret)
			goto out;
	break;

	case MXT_FW_FROM_BUILT_IN:
	case MXT_FW_FROM_REQ_FW:
		ret = mxt_load_fw_from_req_fw(&fw_info, fw);
		if (ret)
			goto out;
	break;

	default:
		tsp_debug_err(true, dev, "invalid fw file type!!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = wait_for_completion_interruptible_timeout(&data->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));

	if (ret <= 0) {
		tsp_debug_err(true, dev, "error while waiting for device to init (%d)\n",
			ret);
		ret = -EBUSY;
		goto out;
	}

	mutex_lock(&data->input_dev->mutex);
	disable_irq(data->client->irq);

	/* Change to the bootloader mode */
	ret = mxt_command_reset(data, MXT_BOOT_VALUE);
	if (ret)
		goto irq_enable;

	ret = mxt_flash_fw(&fw_info);
	if (ret) {
		tsp_debug_err(true, dev, "The firmware update failed(%d)\n", ret);
	} else {
		tsp_debug_info(true, dev, "The firmware update succeeded\n");
		kfree(data->objects);
		data->objects = NULL;

		ret = mxt_initialize(data);
		if (ret) {
			tsp_debug_err(true, dev, "Failed to initialize\n");
			goto irq_enable;
		}

		ret = mxt_rest_initialize(&fw_info);
		if (ret) {
			tsp_debug_err(true, dev, "Failed to rest init\n");
			goto irq_enable;
		}
	}
	ret = mxt_make_highchg(data);

irq_enable:
	enable_irq(data->client->irq);
	mutex_unlock(&data->input_dev->mutex);
out:
	release_firmware(fw);
	kfree(fw_data);

	if (ret) {
		snprintf(buff, sizeof(buff), "FAIL");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
		snprintf(buff, sizeof(buff), "OK");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_OK;
	}

	return;
}

#if TSP_PATCH

static int mxt_load_patch_from_ums(struct mxt_data *data,
			u8 *patch_data)
{
	struct device *dev = &data->client->dev;
	struct file *filp = NULL;
	struct firmware fw;
	mm_segment_t old_fs = {0};
	const char *firmware_name = "patch.bin";
	char *fw_path;
	int ret = 0;

	memset(&fw, 0, sizeof(struct firmware));

	fw_path = kzalloc(MXT_MAX_FW_PATH, GFP_KERNEL);
	if (fw_path == NULL) {
		tsp_debug_err(true, dev, "Failed to allocate firmware path.\n");
		return -ENOMEM;
	}

	snprintf(fw_path, MXT_MAX_FW_PATH, "/sdcard/%s", firmware_name);

	old_fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		tsp_debug_err(true, dev, "Could not open firmware: %s,%d\n",
			fw_path, (s32)filp);
		ret = -ENOENT;
		goto err_open;
	}

	fw.size = filp->f_path.dentry->d_inode->i_size;

	patch_data = kzalloc(fw.size, GFP_KERNEL);
	if (!patch_data) {
		tsp_debug_err(true, dev, "Failed to alloc buffer for fw\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	ret = vfs_read(filp, (char __user *)patch_data, fw.size, &filp->f_pos);
	if (ret != fw.size) {
		tsp_debug_err(true, dev, "Failed to read file %s (ret = %d)\n",
			fw_path, ret);
		ret = -EINVAL;
		goto err_alloc;
	}
	fw.data = patch_data;
	data->patch.patch = patch_data;

	tsp_debug_info(true, dev, "%s patch file size:%d\n", __func__, fw.size);

#if 0
	print_hex_dump(KERN_INFO, "PATCH FILE:", DUMP_PREFIX_NONE, 16, 1,
		patch_data, fw.size, false);
#endif
	//ret = mxt_verify_fw(fw_info, &fw);
	ret = 0;

err_alloc:
	filp_close(filp, current->files);
err_open:
	set_fs(old_fs);
	kfree(fw_path);

	return ret;
}

static void patch_update(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct device *dev = &data->client->dev;
	struct mxt_fac_data *fdata = data->fdata;
	u8 *patch_data = NULL;
	int ret = 0;
	char buff[16] = {0};

	set_default_result(fdata);
	switch (fdata->cmd_param[0]) {

	case 1:
		ret = mxt_load_patch_from_ums(data, patch_data);
		if (ret)
			goto out;
	break;

	default:
		tsp_debug_err(true, dev, "invalid patch file type!!\n");
		ret = -EINVAL;
		goto out;
	}

	tsp_debug_info(true, &data->client->dev, "%s ppatch:%p %p\n", __func__,
		patch_data, data->patch.patch);
	ret = mxt_patch_init(data, data->patch.patch);

out:
	kfree(patch_data);

	if (ret) {
		snprintf(buff, sizeof(buff), "FAIL");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
		snprintf(buff, sizeof(buff), "OK");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_OK;
	}
	return;
}
#endif

static void get_fw_ver_bin(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct device *dev = &data->client->dev;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[40] = {0};

	set_default_result(fdata);

	if (!fdata->fw_ver && !fdata->build_ver) {
		snprintf(buff, sizeof(buff), "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
		/* Format : IC vendor + H/W verion of IC + F/W version
		 * ex) Atmel : AT00201B
		 */
			snprintf(buff, sizeof(buff), "AT00%02x%02x",
			fdata->fw_ver, fdata->build_ver);
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_OK;
	}
	tsp_debug_info(true, dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_fw_ver_ic(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[40] = {0};
	int ver, build;

	set_default_result(fdata);

	ver = data->info.version;
	build = data->info.build;

	/* Format : IC vendor + H/W verion of IC + F/W version
	 * ex) Atmel : AT00201B
	 */
	snprintf(buff, sizeof(buff), "AT00%02x%02x", ver, build);

	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_config_ver(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_object *user_object;

	char buff[50] = {0};
	char date[10] = {0};
	int error = 0;
	u32 current_crc = 0;

	set_default_result(fdata);

	/* Get the config version from userdata */
	user_object = mxt_get_object(data, MXT_SPT_USERDATA_T38);
	if (!user_object) {
		tsp_debug_err(true, &client->dev, "fail to get object_info\n");
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}

	mxt_read_mem(data, user_object->start_address,
			MXT_CONFIG_VERSION_LENGTH, date);

	disable_irq(data->client->irq);
	/* Get config CRC from device */
	error = mxt_read_config_crc(data, &current_crc);
	if (error)
		tsp_debug_err(true, &client->dev, "%s Error getting configuration CRC:%d\n",
			__func__, error);
	enable_irq(data->client->irq);

	/* Model number_vendor_date_CRC value */
#if defined(CONFIG_V1A) || defined(CONFIG_V2A)|| defined(CONFIG_N1A)|| defined(CONFIG_N2A)
	snprintf(buff, sizeof(buff), "%s_AT_%s",
		data->pdata->project_name, date);
#else
	snprintf(buff, sizeof(buff), "%s_AT_1121_0x%06X",
		data->pdata->project_name, current_crc);
#endif

	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_threshold(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	int error;

	char buff[16] = {0};
	u8 threshold = 0;

	set_default_result(fdata);

	if (data->pdata->revision == MXT_REVISION_I)
		error = mxt_read_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T100,
			 30, &threshold);
	else
		error = mxt_read_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T9,
			 7, &threshold);

	if (error) {
		tsp_debug_err(true, &client->dev, "Failed get the threshold\n");
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}

	snprintf(buff, sizeof(buff), "%d", threshold);

	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void module_off_master(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[3] = {0};

	mutex_lock(&data->input_dev->mutex);

	if (mxt_stop(data))
		snprintf(buff, sizeof(buff), "%s", "NG");
	else
		snprintf(buff, sizeof(buff), "%s", "OK");

	mutex_unlock(&data->input_dev->mutex);

	set_default_result(fdata);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));

	if (strncmp(buff, "OK", 2) == 0)
		fdata->cmd_state = CMD_STATUS_OK;
	else
		fdata->cmd_state = CMD_STATUS_FAIL;

	tsp_debug_info(true, &client->dev, "%s: %s\n", __func__, buff);
}

static void module_on_master(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[3] = {0};

	mutex_lock(&data->input_dev->mutex);

	if (mxt_start(data))
		snprintf(buff, sizeof(buff), "%s", "NG");
	else
		snprintf(buff, sizeof(buff), "%s", "OK");

	mutex_unlock(&data->input_dev->mutex);

	set_default_result(fdata);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));

	if (strncmp(buff, "OK", 2) == 0)
		fdata->cmd_state = CMD_STATUS_OK;
	else
		fdata->cmd_state = CMD_STATUS_FAIL;

	tsp_debug_info(true, &client->dev, "%s: %s\n", __func__, buff);

}

static void get_chip_vendor(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};

	set_default_result(fdata);

	snprintf(buff, sizeof(buff), "%s", "ATMEL");
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_chip_name(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};

	set_default_result(fdata);

	snprintf(buff, sizeof(buff), "%s", MXT_DEV_NAME);

	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_x_num(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};
	int val;

	set_default_result(fdata);
	val = fdata->num_xnode;
	if (val < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;

		tsp_debug_info(true, &client->dev, "%s: fail to read num of x (%d).\n",
			__func__, val);

		return;
	}
	snprintf(buff, sizeof(buff), "%u", val);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_y_num(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};
	int val;

	set_default_result(fdata);
	val = fdata->num_ynode;
	if (val < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;

		tsp_debug_info(true, &client->dev, "%s: fail to read num of y (%d).\n",
			__func__, val);

		return;
	}
	snprintf(buff, sizeof(buff), "%u", val);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void run_reference_read(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct mxt_fac_data *fdata = data->fdata;
	int ret;
	char buff[16] = {0};

	set_default_result(fdata);

#if TSP_PATCH
	if(data->patch.event_cnt)
		mxt_patch_test_event(data, 2);
#endif

	ret = mxt_read_all_diagnostic_data(data,
			MXT_DIAG_REFERENCE_MODE);
	if (ret)
		fdata->cmd_state = CMD_STATUS_FAIL;
	else {
		snprintf(buff, sizeof(buff), "%d,%d",
			fdata->ref_min_data, fdata->ref_max_data);
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));

		fdata->cmd_state = CMD_STATUS_OK;
	}
}

static void get_reference(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	char buff[16] = {0};
	int node;

	set_default_result(fdata);
	/* add read function */

	node = mxt_xy_to_node(data);
	if (node < 0) {
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}
	snprintf(buff, sizeof(buff), "%u", fdata->reference[node]);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void run_delta_read(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct mxt_fac_data *fdata = data->fdata;
	int ret;

	set_default_result(fdata);

	ret = mxt_read_all_diagnostic_data(data,
			MXT_DIAG_DELTA_MODE);
	if (ret)
		fdata->cmd_state = CMD_STATUS_FAIL;
	else
		fdata->cmd_state = CMD_STATUS_OK;
}

static void get_delta(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	char buff[16] = {0};
	int node;

	set_default_result(fdata);
	/* add read function */

	node = mxt_xy_to_node(data);
	if (node < 0) {
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}
	snprintf(buff, sizeof(buff), "%d", fdata->delta[node]);
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static int mxt_check_fcal_status(struct mxt_data *data, u8 status)
{
	struct device *dev = &data->client->dev;
	int error = -EINVAL;

	tsp_debug_info(true, dev, "FCAL : state[%s]\n",
		 MXT_FCALSTATE(status) ? (MXT_FCALSTATE(status) == 1 ?
		 "PRIMED" : "GENERATED") : "IDLE");

	switch (MXT_FCALSTATE(status)) {
	case MXT_FCALSTATE_IDLE:
		if (status & MXT_FCALSTATUS_SEQTO) {
			tsp_debug_err(true, dev, "Sequence Timeout:[0x%x]\n", status);
			goto out;
		}
		if (status & MXT_FCALSTATUS_SEQERR) {
			tsp_debug_err(true, dev, "Sequence Error:[0x%x]\n", status);
			goto out;
		}
		if (status & MXT_FCALSTATUS_SEQDONE) {
			tsp_debug_info(true, dev, "Sequence Done:[0x%x]\n", status);
			error = FCALSEQDONE_MAGIC;
			goto out;
		}
		break;
	case MXT_FCALSTATE_PRIMED:
		error = mxt_write_object(data, MXT_SPT_GOLDENREFERENCES_T66,
				0, MXT_FCALCMD(MXT_FCALCMD_GENERATE) | 0x03);
		if (error) {
			tsp_debug_err(true, dev, "Failed to write FALCMD_GENERATE\n");
			goto out;
		}
		break;
	case MXT_FCALSTATE_GENERATED:
		if (status & MXT_FCALSTATUS_FAIL) {
			tsp_debug_err(true, dev, "Calibration Failed:[0x%x]\n", status);
			goto out;
		}
		if (status & MXT_FCALSTATUS_PASS) {
			tsp_debug_info(true, dev, "Calibration Passed:[0x%x]\n", status);
			error = mxt_write_object(data,
					 MXT_SPT_GOLDENREFERENCES_T66, 0,
					 MXT_FCALCMD(MXT_FCALCMD_STORE) | 0x03);
			if (error) {
				tsp_debug_err(true, dev, "Failed to write FCALCMD_STORE\n");
				goto out;
			}
		}
		break;
	default:
		tsp_debug_err(true, dev, "Invaild Factory Calibration status[0x%x]\n",
				 status);
	}

out:
	return error;
}

static int mxt_read_message_on_fcal(struct mxt_data *data,
	struct mxt_message *message)
{
	struct device *dev = &data->client->dev;
	u8 report_id = 0, type = 0, fcal_status = 0;
	int error = 0;

	do {
		if (mxt_read_message(data, message)) {
			tsp_debug_err(true, dev, "Failed to read message\n");
			return -EIO;
		}

#if TSP_USE_ATMELDBG
		if (data->atmeldbg.display_log) {
			print_hex_dump(KERN_INFO, "MXT MSG:",
				DUMP_PREFIX_NONE, 16, 1,
				message,
				sizeof(struct mxt_message), false);
		}
#endif
		report_id = message->reportid;
		if (report_id > data->max_reportid)
			goto end;

		type = data->reportids[report_id].type;
		/* check message is avaiable on Factory calibration */
		switch (type) {
		case MXT_SPT_GOLDENREFERENCES_T66:
			fcal_status = message->message[0];

			tsp_debug_dbg(false, dev, "T%d:\t[0x%x][0x%x][0x%x][0x%x][0x%x]\n",
				type, message->reportid,
				message->message[0], message->message[1],
				message->message[2], message->message[3]);

			error = mxt_check_fcal_status(data, fcal_status);
			if (error)
				return error;
			break;

		/* need to add exception case such as T9 touch message.
		 * I think T9 message represent that user touch the device
		 * during Factory calibration
		 */
		case MXT_TOUCH_MULTITOUCHSCREEN_T9:
		case MXT_PROCI_TOUCHSUPPRESSION_T42:
			tsp_debug_err(true, dev, "Object detected on screen[T%d]\n",
				type);
#if 0	/* TODO : Need to review..... */
			/* Store the fail cause in cmd_result */
			snprintf(buff, sizeof(buff), "%s",
						 "Object detected on screen");
			set_cmd_result(data->fdata, buff,
						 strnlen(buff, sizeof(buff)));

			/* Send the PRIME commnad to make sequence error */
			error = mxt_write_object(data,
					MXT_SPT_GOLDENREFERENCES_T66, 0,
					MXT_FCALCMD(MXT_FCALCMD_PRIME) | 0x03);

			if (error) {
				tsp_debug_err(true, &client->dev, "Failed to write FCALCMD_PRIME\n");
				return error;
			}
#endif
			break;

		default:
			tsp_debug_dbg(false, dev, "T%d:\t[0x%x][0x%x][0x%x][0x%x][0x%x][0x%x][0x%x][0x%x][0x%x]\n",
				type, message->reportid,
				message->message[0], message->message[1],
				message->message[2], message->message[3],
				message->message[4], message->message[5],
				message->message[6], message->message[7]);
		}
	} while (!data->pdata->read_chg());

end:
	return 0;
}

static void mxt_run_factory_cal(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_message message;
	int error = 0;

	set_default_result(fdata);

	/* To ensure that previous touchs are released */
	msleep(500);

	/* CTRL : enable RPTEN and ENABLE */
	error = mxt_write_object(data, MXT_SPT_GOLDENREFERENCES_T66,
				0, MXT_FCALCMD(MXT_FCALCMD_PRIME) | 0x03);

	if (error) {
		tsp_debug_err(true, &client->dev, "Failed to write FCALCMD_PRIME\n");
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}

	disable_irq(client->irq);

	do {
		error = mxt_read_message_on_fcal(data, &message);

		if (!error)
			msleep(1000);
	} while (!error);

	enable_irq(client->irq);

	if (error == FCALSEQDONE_MAGIC) {
		tsp_debug_info(true, &client->dev, "Sucessed Factory Calibration\n");
		fdata->cmd_state = CMD_STATUS_OK;
	} else {
		tsp_debug_err(true, &client->dev, "Failed Factory Calibration [%d]", error);
		fdata->cmd_state = CMD_STATUS_FAIL;
	}
}
static void set_jitter_level(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_message message;
	int error = 0;
	char buff[16] = {0};

	set_default_result(fdata);

	if (!data->mxt_enabled) {
		tsp_debug_err(true, &client->dev, "%s failed, TSP IC is not ready!\n",
					__func__);
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else if (fdata->cmd_param[0] < 1 || fdata->cmd_param[0] > 8) {
		tsp_debug_err(true, &client->dev, "%s failed, the range of jitter level is 1~8[%d]\n",
					__func__, fdata->cmd_param[0]);
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
		int retval = 0, level = 0;
		u8 cfg_val;

		level = fdata->cmd_param[0];
		retval = mxt_read_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T9, MXT_TOUCH_MOVFILTER2, &cfg_val);

		if (!retval) {
			cfg_val = ((cfg_val & 0xf0 )| level);

			/* TODO: Write config value for changing jitter value  on tsp ic */
			retval = mxt_write_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T9, MXT_TOUCH_MOVFILTER2, cfg_val);
		} else {
			tsp_debug_err(true, &data->client->dev, "%s: failed to read T9 object.\n", __func__);
			retval = -EINVAL;
		}

		if (retval < 0) {
			tsp_debug_err(true, &client->dev, "%s: failed, retval=%d,level=%d,cfg_val=%d\n",
						__func__, retval, level, cfg_val);
			snprintf(buff, sizeof(buff), "%s", "NG");
			set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
			fdata->cmd_state = CMD_STATUS_FAIL;
		} else {
			tsp_debug_info(true, &client->dev, "%s: success write level[%d],cfg_val=[%d]\n",
						__func__, level, cfg_val);
			snprintf(buff, sizeof(buff), "%s", "OK");
			set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
			fdata->cmd_state = CMD_STATUS_OK;
		}
	}

	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = false;
	mutex_unlock(&fdata->cmd_lock);

	fdata->cmd_state = CMD_STATUS_WAITING;

	return;
}

#if ENABLE_TOUCH_KEY
static void get_tk_threshold(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	int error;

	char buff[16] = {0};
	u8 threshold = 0;

	set_default_result(fdata);

	error = mxt_read_object(data, MXT_TOUCH_KEYARRAY_T15, 7, &threshold);

	if (error) {
		tsp_debug_err(true, &client->dev, "Failed get the tk_threshold\n");
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}

	snprintf(buff, sizeof(buff), "%d", threshold);

	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;
	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}

static void get_tk_delta(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	char buff[16] = {0};
	int node = -1 ,i = 0;
	u16 keydelta = 0;

	set_default_result(fdata);
	/* add read function */

	for (i=0 ; i < data->pdata->num_touchkey ; i++) {
		if (!strcmp(fdata->cmd_param_str[0], data->pdata->touchkey[i].name))
			break;
	}

	if (i != data->pdata->num_touchkey) {
		tsp_debug_info(true, &client->dev, "%s: before touchkey ref read[%s]\n",
			__func__, fdata->cmd_param_str[0]);
		mxt_read_diagnostic_data(data, MXT_DIAG_KEYDELTA_MODE,
			data->pdata->touchkey[i].deltaobj, &keydelta);
		tsp_debug_info(true, &client->dev, "%s: after touchkey ref read[%d]\n",
			__func__, (s16)keydelta);
	} else {
		tsp_debug_info(true, &client->dev, "%s: find touchkey name fail![%s]\n",
			__func__, fdata->cmd_param_str[0]);
		fdata->cmd_state = CMD_STATUS_FAIL;
		return;
	}

	snprintf(buff, sizeof(buff), "%d", (s16)keydelta > 0 ? keydelta : 0 );
	set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
	fdata->cmd_state = CMD_STATUS_OK;

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));
}
#if defined(CONFIG_N1A_3G)
static bool set_threshold(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_message message;
	char buff[16] = {0};
	int retval = 0;
	bool command_success = true;

	if (data->setdata == 1) {
		retval = mxt_write_object(data, MXT_TOUCH_KEYARRAY_T15,	6, 75);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T15[6] BLEN = 75\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_TOUCH_KEYARRAY_T15,	7, 55);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T15[7] TCHTHR = 55\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_TOUCH_KEYARRAY_T15,	8, 5);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T15[8] TCHDI = 5\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_GEN_POWERCONFIG_T7,	1, 255);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T7[1] ACTVACQINT =255\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_SPT_CTECONFIG_T46, 3, 63);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T46[3] ACTVSYNCSPERX = 63\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_PROCI_SHIELDLESS_T56, 0, 3);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T56[0] CTRL = 3\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_SPT_CTESCANCONFIG_T77, 0, 5);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T77[0] CTRL = 5\n");
			command_success = false;
		}

		retval = mxt_write_object(data, MXT_SPT_CTESCANCONFIG_T77, 1, 5);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed T77[1] ACTVPRCSSCNEXT = 5\n");
			command_success = false;
		}

		tsp_debug_info(true, &client->dev, "success confing write\n");
	} else {
		retval = mxt_command_reset(data, MXT_RESET_VALUE);
		if (retval) {
			tsp_debug_err(true, &client->dev, "Failed Reset IC\n");
			command_success = false;
		} else
			tsp_debug_info(true, &client->dev, "success SW Reset IC\n");
	}
	return command_success;
}

static void set_tk_threshold(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	struct mxt_message message;
	char buff[16] = {0};
	int retval = 0;
	int threshold;
	bool command_success;

	set_default_result(fdata);

	if (system_rev >= 5) {
		tsp_debug_err(true, &client->dev, "%s failed, not suppport[%d]!\n",
			__func__, system_rev);

		mutex_lock(&fdata->cmd_lock);
		fdata->cmd_is_running = false;
		mutex_unlock(&fdata->cmd_lock);

		fdata->cmd_state = CMD_STATUS_WAITING;

		return;
	}

	if (data->setdata == fdata->cmd_param[0]) {
		tsp_debug_err(true, &client->dev, "%s ,same state : nothing to do[%s], TSP %s!\n",
			__func__, fdata->cmd_param[0] == 1 ? "ON" : "OFF",
			data->mxt_enabled ? "ON" : "OFF");

		mutex_lock(&fdata->cmd_lock);
		fdata->cmd_is_running = false;
		mutex_unlock(&fdata->cmd_lock);

		fdata->cmd_state = CMD_STATUS_WAITING;

		return;
	}

	data->setdata = fdata->cmd_param[0];

	if (!data->mxt_enabled) {
		tsp_debug_err(true, &client->dev, "%s failed, TSP IC is not ready!\n",
					__func__);
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;

		mutex_lock(&fdata->cmd_lock);
		fdata->cmd_is_running = false;
		mutex_unlock(&fdata->cmd_lock);

		return;
	}

	command_success = set_threshold(data);

	if (command_success) {
		tsp_debug_info(true, &client->dev, "%s: success write configs\n",
					__func__);
		snprintf(buff, sizeof(buff), "%s", "OK");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_OK;
	} else {
		tsp_debug_err(true, &client->dev, "%s: fail write configs\n",
					__func__);
		snprintf(buff, sizeof(buff), "%s", "NG");
		set_cmd_result(fdata, buff, strnlen(buff, sizeof(buff)));
		fdata->cmd_state = CMD_STATUS_FAIL;
	}

	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = false;
	mutex_unlock(&fdata->cmd_lock);

	fdata->cmd_state = CMD_STATUS_WAITING;

	return;
}
#endif
#endif

#if defined(TSP_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
static void boost_level(void *device_data)
{
	struct mxt_data *data = (struct mxt_data *)device_data;
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	char buff[16] = {0};
	int node = -1 ,i = 0;
	u16 keydelta = 0;
	u8 max_level = 3;

#ifdef CONFIG_INPUT_BOOSTER
	max_level = BOOSTER_LEVEL_MAX;
#elif TSP_BOOSTER
	max_level = TSP_BOOSTER_LEVEL_MAX;
#endif

	set_default_result(fdata);

	if (fdata->cmd_param[0] < 0 || fdata->cmd_param[0] >= max_level) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		fdata->cmd_state = CMD_STATUS_FAIL;
	} else {
#ifdef CONFIG_INPUT_BOOSTER
		change_tsp_level(fdata->cmd_param[0]);
#elif TSP_BOOSTER
		data->booster.boost_level = fdata->cmd_param[0];
#endif
		tsp_debug_dbg(false, &client->dev, "%s %d\n", __func__, fdata->cmd_param[0]);
		snprintf(buff, sizeof(buff), "%s", "OK");
		fdata->cmd_state = CMD_STATUS_OK;
	}

	tsp_debug_info(true, &client->dev, "%s: %s(%d)\n",
		__func__, buff, strnlen(buff, sizeof(buff)));

	return;
}
#endif
/* - function realted samsung factory test */

#define TSP_CMD(name, func) .cmd_name = name, .cmd_func = func

struct tsp_cmd {
	struct list_head	list;
	const char		*cmd_name;
	void			(*cmd_func)(void *device_data);
};

static struct tsp_cmd tsp_cmds[] = {
	{TSP_CMD("fw_update", fw_update),},
	{TSP_CMD("get_fw_ver_bin", get_fw_ver_bin),},
	{TSP_CMD("get_fw_ver_ic", get_fw_ver_ic),},
	{TSP_CMD("get_config_ver", get_config_ver),},
	{TSP_CMD("get_threshold", get_threshold),},
	{TSP_CMD("module_off_master", module_off_master),},
	{TSP_CMD("module_on_master", module_on_master),},
	{TSP_CMD("module_off_slave", not_support_cmd),},
	{TSP_CMD("module_on_slave", not_support_cmd),},
	{TSP_CMD("get_chip_vendor", get_chip_vendor),},
	{TSP_CMD("get_chip_name", get_chip_name),},
	{TSP_CMD("get_x_num", get_x_num),},
	{TSP_CMD("get_y_num", get_y_num),},
	{TSP_CMD("run_reference_read", run_reference_read),},
	{TSP_CMD("get_reference", get_reference),},
	{TSP_CMD("run_delta_read", run_delta_read),},
	{TSP_CMD("get_delta", get_delta),},
	{TSP_CMD("find_delta", find_delta_node),},
	{TSP_CMD("run_factory_cal", mxt_run_factory_cal),},
	{TSP_CMD("set_jitter_level", set_jitter_level),},
#if ENABLE_TOUCH_KEY
	{TSP_CMD("get_tk_threshold", get_tk_threshold),},
	{TSP_CMD("get_tk_delta", get_tk_delta),},
#if defined(CONFIG_N1A_3G)
	{TSP_CMD("set_tk_threshold", set_tk_threshold),},
#endif
#endif
#if TSP_PATCH
	{TSP_CMD("patch_update", patch_update),},
#endif
#if defined(TSP_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
	{TSP_CMD("boost_level", boost_level),},
#endif

	{TSP_CMD("not_support_cmd", not_support_cmd),},
};

/* Functions related to basic interface */
static ssize_t store_cmd(struct device *dev, struct device_attribute
				  *devattr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;

	char *cur, *start, *end;
	char buff[TSP_CMD_STR_LEN] = {0};
	int len, i;
	struct tsp_cmd *tsp_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;
	int ret;

	if (count >= TSP_CMD_STR_LEN) {
		printk(KERN_ERR "%s: overflow command length\n", __func__);
		return -EINVAL;
	}

	if (fdata->cmd_is_running == true) {
		tsp_debug_err(true, &client->dev, "tsp_cmd: other cmd is running.\n");
		goto err_out;
	}

	/* check lock  */
	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = true;
	mutex_unlock(&fdata->cmd_lock);

	fdata->cmd_state = CMD_STATUS_RUNNING;

	for (i = 0; i < ARRAY_SIZE(fdata->cmd_param); i++)
		fdata->cmd_param[i] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;
	memset(fdata->cmd, 0x00, ARRAY_SIZE(fdata->cmd));
	memcpy(fdata->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	/* find command */
	list_for_each_entry(tsp_cmd_ptr, &fdata->cmd_list_head, list) {
		if (!strcmp(buff, tsp_cmd_ptr->cmd_name)) {
			cmd_found = true;
			break;
		}
	}

	/* set not_support_cmd */
	if (!cmd_found) {
		list_for_each_entry(tsp_cmd_ptr,
			&fdata->cmd_list_head, list) {
			if (!strcmp("not_support_cmd", tsp_cmd_ptr->cmd_name))
				break;
		}
	}

	/* parsing parameters */
	if (cur && cmd_found) {
		cur++;
		start = cur;
		memset(buff, 0x00, ARRAY_SIZE(buff));
		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(buff, start, end - start);
				*(buff + strlen(buff)) = '\0';
				memcpy(fdata->cmd_param_str + param_cnt, buff,\
					end - start + 1);
				ret = kstrtoint(buff, 10,\
					fdata->cmd_param + param_cnt);
				start = cur + 1;
				memset(buff, 0x00, ARRAY_SIZE(buff));
				param_cnt++;
			}
			cur++;
		} while ((cur - buf <= len) && (param_cnt < TSP_CMD_PARAM_NUM));
	}

	tsp_debug_info(true, &client->dev, "cmd = %s\n", tsp_cmd_ptr->cmd_name);
	for (i = 0; i < param_cnt; i++) {
		tsp_debug_info(true, &client->dev, "cmd param %d= %d, cmd param str = %s\n",
			i, fdata->cmd_param[i], fdata->cmd_param_str[i]);
	}

	tsp_cmd_ptr->cmd_func(data);
err_out:
	return count;
}

static ssize_t show_cmd_status(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_fac_data *fdata = data->fdata;

	char buff[16] = {0};

	tsp_debug_info(true, &data->client->dev, "tsp cmd: status:%d\n",
			fdata->cmd_state);

	if (fdata->cmd_state == CMD_STATUS_WAITING)
		snprintf(buff, sizeof(buff), "WAITING");

	else if (fdata->cmd_state == CMD_STATUS_RUNNING)
		snprintf(buff, sizeof(buff), "RUNNING");

	else if (fdata->cmd_state == CMD_STATUS_OK)
		snprintf(buff, sizeof(buff), "OK");

	else if (fdata->cmd_state == CMD_STATUS_FAIL)
		snprintf(buff, sizeof(buff), "FAIL");

	else if (fdata->cmd_state == CMD_STATUS_NOT_APPLICABLE)
		snprintf(buff, sizeof(buff), "NOT_APPLICABLE");

	return snprintf(buf, TSP_BUF_SIZE, "%s\n", buff);
}

static ssize_t show_cmd_result(struct device *dev, struct device_attribute
				    *devattr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_fac_data *fdata = data->fdata;

	tsp_debug_info(true, &data->client->dev, "tsp cmd: result: %s\n",
		fdata->cmd_result);

	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = false;
	mutex_unlock(&fdata->cmd_lock);

	fdata->cmd_state = CMD_STATUS_WAITING;

	return snprintf(buf, TSP_BUF_SIZE, "%s\n", fdata->cmd_result);
}

#if TSP_CHANGE_CONFIG_FOR_INPUT
static ssize_t set_tsp_for_inputmethod_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
	     data->is_inputmethod);
}

static ssize_t set_tsp_for_inputmethod_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	u8 jump_limit = 0;
	struct mxt_data *data = dev_get_drvdata(dev);

	printk(KERN_ERR "[TSP]set_tsp_for_inputmethod_store. call");

	if (!data->mxt_enabled) {
		printk(KERN_ERR
			   "[TSP]set_tsp_for_inputmethod_store. "
			   "mxt_enabled is 0\n");
		return 1;
	}

	if (*buf == '1' && (!data->is_inputmethod)) {
	    dev_err(&(data->input_dev->dev),
	        "%s: Set TSP inputmethod IN\n",
	        __func__);
		jump_limit = 30;
	    data->is_inputmethod = true;

	} else if (*buf == '0' && (data->is_inputmethod)) {
	    dev_err(&(data->input_dev->dev),
	        "%s: Set TSP inputmethod OUT\n",
	        __func__);
		jump_limit = 60;
		data->is_inputmethod = false;
	}
	else {
	    dev_err(&(data->input_dev->dev),
	        "%s: err in argument for inputmethod\n",
	        __func__);
		return 1;
	}

	mxt_write_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T9, 30, jump_limit);

	 dev_info(&(data->input_dev->dev),
		 "%s: read T9,jump_limit(%d)\n",
		 __func__,jump_limit);

	return 1;
}
#else	/* TSP_CHANGE_CONFIG_FOR_INPUT */
#define set_tsp_for_inputmethod_show	NULL
#define set_tsp_for_inputmethod_store	NULL
#endif

#if defined(CONFIG_INPUT_BOOSTER) || defined(TSP_BOOSTER)
static ssize_t tsp_booster_enabled_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", tsp_booster_enabled);
}

static ssize_t tsp_booster_enabled_store(struct device *dev,
						struct device_attribute *attr, const char *buf,
						size_t count)
{
	int enabled;

	if (sscanf(buf, "%u", &enabled) != 1)
		return -EINVAL;

	if (enabled == 1) {
		tsp_booster_enabled = 1;
	}
	else if (enabled == 0) {
		tsp_booster_enabled = 0;
	}
	else {
		return -EINVAL;
	}

	return count;
}
#endif

static DEVICE_ATTR(cmd, S_IWUSR | S_IWGRP, NULL, store_cmd);
static DEVICE_ATTR(cmd_status, S_IRUGO, show_cmd_status, NULL);
static DEVICE_ATTR(cmd_result, S_IRUGO, show_cmd_result, NULL);
static DEVICE_ATTR(set_tsp_for_inputmethod, S_IRUGO | S_IWUSR | S_IWGRP,
	set_tsp_for_inputmethod_show, set_tsp_for_inputmethod_store);
#if defined(CONFIG_INPUT_BOOSTER) || defined(TSP_BOOSTER)
static DEVICE_ATTR(tsp_booster_enabled, S_IRUGO | S_IWUSR | S_IWGRP,
		   tsp_booster_enabled_show, tsp_booster_enabled_store);
#endif

static struct attribute *touchscreen_factory_attributes[] = {
	&dev_attr_cmd.attr,
	&dev_attr_cmd_status.attr,
	&dev_attr_cmd_result.attr,
#if defined(CONFIG_INPUT_BOOSTER) || defined(TSP_BOOSTER)
	&dev_attr_tsp_booster_enabled.attr,
#endif
	NULL,
};

static struct attribute_group touchscreen_factory_attr_group = {
	.attrs = touchscreen_factory_attributes,
};

#if ENABLE_TOUCH_KEY
static int touchkey_delta_show(struct mxt_data  *data, char *key_name)
{

	struct i2c_client *client = data->client;
	struct mxt_fac_data *fdata = data->fdata;
	int i;
	u16 keydelta = 0;

	/* check lock  */
	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = true;
	fdata->cmd_state = CMD_STATUS_RUNNING;
	mutex_unlock(&fdata->cmd_lock);


	/* find touch key data */
	for (i=0 ; i < data->pdata->num_touchkey ; i++) {
		if (!strcmp(key_name, data->pdata->touchkey[i].name))
			break;
	}

	tsp_debug_info(true, &client->dev, "%s: %s touchkey delta read\n",
			__func__, key_name);

	if (i != data->pdata->num_touchkey) {
		mxt_read_diagnostic_data(data, MXT_DIAG_KEYDELTA_MODE,
			data->pdata->touchkey[i].deltaobj, &keydelta);
	} else {
		tsp_debug_info(true, &client->dev, "%s: Can't find %s touchkey!\n",
			__func__, key_name);
		return 0;
	}

	tsp_debug_info(true, &client->dev, "%s: %s touchkey delta read : %d\n",
			__func__, key_name, (s16)keydelta);

	/* check lock  */
	mutex_lock(&fdata->cmd_lock);
	fdata->cmd_is_running = false;
	fdata->cmd_state = CMD_STATUS_WAITING;
	mutex_unlock(&fdata->cmd_lock);

	return ((s16)keydelta > 0 ? (keydelta / 8) : 0);

}

static ssize_t touchkey_d_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "d_menu"));
}

static ssize_t touchkey_d_home1_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "d_home1"));
}

static ssize_t touchkey_d_home2_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "d_home2"));
}

static ssize_t touchkey_d_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "d_back"));
}

/* Depend on OS. (~JBP : ues menu key, KK~ : use recent key */
static ssize_t touchkey_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "menu"));
}
static ssize_t touchkey_recent_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "recent"));
}
/* Depend on OS. */

static ssize_t touchkey_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", touchkey_delta_show(data, "back"));
}

static ssize_t get_touchkey_threshold(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	u8 threshold;
	int error;

	error = mxt_read_object(data, MXT_TOUCH_KEYARRAY_T15, 7, &threshold);

	if (error) {
		printk(KERN_ERR "[TouchKey] Failed get the touchkey threshold\n");
		return sprintf(buf, "0\n");
	}

	printk(KERN_DEBUG "[TouchKey] %s [%d]\n", __func__, threshold);
	return sprintf(buf, "%d\n", threshold);
}

static ssize_t touchkey_led_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		printk(KERN_DEBUG "[TouchKey] %s, %d err\n",
			__func__, __LINE__);
		return size;
	}

#ifdef LED_LDO_WITH_REGULATOR
	if (input > 1 && tsp_keys_enabled) {
		update_touchkey_brightness(input);
	}
	input = input ? 1 : 0;
#endif

	if (input != 0 && input != 1) {
		printk(KERN_DEBUG "[TouchKey] %s wrong cmd %x\n",
			__func__, input);
		return size;
	}

	/* control led */
	if (input == 1)
		data->pdata->led_power_on();
	else
		data->pdata->led_power_off();

	return size;
}

static ssize_t touchkey_report_dummy_key_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->report_dummy_key ? "True" : "False");
}

static ssize_t touchkey_report_dummy_key_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		tsp_debug_info(true, &data->client->dev, "%s: %d err\n",
			__func__, ret);
		return size;
	}

	if (input)
		data->report_dummy_key = true;
	else
		data->report_dummy_key = false;

	return size;
}

#if defined(TOUCHKEY_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
static ssize_t touchkey_boost_level(struct device *dev,
						struct device_attribute *attr, const char *buf,
						size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int level;

	sscanf(buf, "%d", &level);

	if (level < 0 || level > 2) {
		tsp_debug_err(true, &data->client->dev, "err to set boost_level %d\n", level);
		return count;
	}

#ifdef CONFIG_INPUT_BOOSTER
	change_touchkey_level(level);
#else
	data->tsk_booster.boost_level = level;
#endif
	tsp_debug_info(true, &data->client->dev, "%s %d\n", __func__, level);

	return count;
}

static ssize_t tk_booster_enabled_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", tk_booster_enabled);
}

static ssize_t tk_booster_enabled_store(struct device *dev,
						struct device_attribute *attr, const char *buf,
						size_t count)
{
	int enabled;

	if (sscanf(buf, "%u", &enabled) != 1)
		return -EINVAL;

	if (enabled == 1) {
		tk_booster_enabled = 1;
	}
	else if (enabled == 0) {
		tk_booster_enabled = 0;
	}
	else {
		return -EINVAL;
	}

	return count;
}
#endif

static ssize_t show_tsp_keys_enabled(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", tsp_keys_enabled);
}

static ssize_t tsp_keys_enabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	tsp_keys_enabled = (input != 0);

	return count;
}

static DEVICE_ATTR(touchkey_d_menu, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_d_menu_show, NULL);
static DEVICE_ATTR(touchkey_d_home1, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_d_home1_show, NULL);
static DEVICE_ATTR(touchkey_d_home2, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_d_home2_show, NULL);
static DEVICE_ATTR(touchkey_d_back, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_d_back_show, NULL);
#if defined(CONFIG_N1A) /* N1A : JBP */
static DEVICE_ATTR(touchkey_menu, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_menu_show, NULL);
#else
static DEVICE_ATTR(touchkey_recent, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_recent_show, NULL);
#endif
static DEVICE_ATTR(touchkey_back, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_back_show, NULL);
static DEVICE_ATTR(touchkey_threshold, S_IRUGO | S_IWUSR | S_IWGRP, get_touchkey_threshold, NULL);
static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touchkey_led_control);
static DEVICE_ATTR(extra_button_event, S_IRUGO | S_IWUSR | S_IWGRP,
					touchkey_report_dummy_key_show, touchkey_report_dummy_key_store);
#if defined(TOUCHKEY_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
static DEVICE_ATTR(boost_level, S_IWUSR | S_IWGRP, NULL, touchkey_boost_level);
static DEVICE_ATTR(touchkey_booster_enabled, S_IRUGO | S_IWUSR | S_IWGRP,
		   tk_booster_enabled_show, tk_booster_enabled_store);
#endif
static DEVICE_ATTR(touchkey_enabled, S_IRUGO | S_IWUSR | S_IWGRP,
			show_tsp_keys_enabled, tsp_keys_enabled_store);

static struct attribute *touchkey_attributes[] = {
	&dev_attr_touchkey_d_menu.attr,
	&dev_attr_touchkey_d_home1.attr,
	&dev_attr_touchkey_d_home2.attr,
	&dev_attr_touchkey_d_back.attr,
#if defined(CONFIG_N1A) /* N1A : JBP */
	&dev_attr_touchkey_menu.attr,
#else
	&dev_attr_touchkey_recent.attr,
#endif
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_brightness.attr,
	&dev_attr_extra_button_event.attr,
#if defined(TOUCHKEY_BOOSTER) || defined(CONFIG_INPUT_BOOSTER)
	&dev_attr_boost_level.attr,
	&dev_attr_touchkey_booster_enabled.attr,
#endif
	&dev_attr_touchkey_enabled.attr,
	NULL,
};

static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};

#endif

#endif	/* TSP_SEC_FACTORY*/

#if TSP_USE_ATMELDBG
static int mxt_atmeldbg_read_block(struct i2c_client *client,
	   u16 addr, u16 length, u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];
	__le16 le_addr;
	struct mxt_data *data = i2c_get_clientdata(client);
	struct mxt_object *object;

	if (data == NULL) {
		tsp_debug_err(true, &client->dev, "%s Data is Null\n", __func__);
		return -EINVAL;
	}

	object = mxt_get_object(data, MXT_GEN_MESSAGEPROCESSOR_T5);

	if ((data->atmeldbg.last_read_addr == addr) &&
		(addr == object->start_address)) {
		if  (i2c_master_recv(client, value, length) == length) {
			if (data->atmeldbg.display_log)
				print_hex_dump(KERN_INFO, "MXT RX:",
					DUMP_PREFIX_NONE, 16, 1,
					value, length, false);
			return 0;
		} else
			return -EIO;
	} else {
		data->atmeldbg.last_read_addr = addr;
	}

	le_addr = cpu_to_le16(addr);
	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &le_addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;
	if (i2c_transfer(adapter, msg, 2) == 2) {
		if (data->atmeldbg.display_log) {
			print_hex_dump(KERN_INFO, "MXT TX:", DUMP_PREFIX_NONE,
				16, 1, msg[0].buf, msg[0].len, false);
			print_hex_dump(KERN_INFO, "MXT RX:", DUMP_PREFIX_NONE,
				16, 1, msg[1].buf, msg[1].len, false);
		}
		return 0;
	} else
		return -EIO;
}

/* Writes a block of bytes (max 256) to given address in mXT chip. */
static int mxt_atmeldbg_write_block(struct i2c_client *client,
	    u16 addr, u16 length, u8 *value)
{
	int i;
	struct {
		__le16	le_addr;
		u8	data[256];

	} i2c_block_transfer;

	struct mxt_data *data = i2c_get_clientdata(client);

	if (length > 256)
		return -EINVAL;

	if (data == NULL) {
		tsp_debug_err(true, &client->dev, "%s Data is Null\n", __func__);
		return -EINVAL;
	}

	data->atmeldbg.last_read_addr = -1;

	for (i = 0; i < length; i++)
		i2c_block_transfer.data[i] = *value++;

	i2c_block_transfer.le_addr = cpu_to_le16(addr);

	i = i2c_master_send(client, (u8 *) &i2c_block_transfer, length + 2);

	if (i == (length + 2)) {
		if (data->atmeldbg.display_log)
			print_hex_dump(KERN_INFO, "MXT TX:", DUMP_PREFIX_NONE,
				16, 1, &i2c_block_transfer, length+2, false);
		return length;
	} else
		return -EIO;
}

static ssize_t mem_atmeldbg_access_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	int ret = 0;
	struct i2c_client *client =
		to_i2c_client(container_of(kobj, struct device, kobj));

	tsp_debug_info(true, &client->dev, "%s p=%p off=%lli c=%zi\n",
			__func__, buf, off, count);

	if (off >= 32768)
		return -EIO;

	if (off + count > 32768)
		count = 32768 - off;

	if (count > 256)
		count = 256;

	if (count > 0)
		ret = mxt_atmeldbg_read_block(client, off, count, buf);

	return ret >= 0 ? count : ret;
}

static ssize_t mem_atmeldbg_access_write(struct file *filp,
	struct kobject *kobj, struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	int ret = 0;
	struct i2c_client *client =
		to_i2c_client(container_of(kobj, struct device, kobj));

	tsp_debug_info(true, &client->dev, "%s p=%p off=%lli c=%zi\n",
			__func__, buf, off, count);

	if (off >= 32768)
		return -EIO;

	if (off + count > 32768)
		count = 32768 - off;

	if (count > 256)
		count = 256;

	if (count > 0)
		ret = mxt_atmeldbg_write_block(client, off, count, buf);

	return ret >= 0 ? count : 0;
}

static ssize_t mxt_atmeldbg_pause_driver_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int count = 0;

	count += sprintf(buf + count, "%d", data->atmeldbg.stop_sync);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t mxt_atmeldbg_pause_driver_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->atmeldbg.stop_sync = i;

		tsp_debug_info(true, &client->dev, "%s input sync for Atmel dbug App\n",
			i ? "Stop" : "Start");
	} else {
		tsp_debug_info(true, &client->dev, "Error %s\n", __func__);
	}

	return count;
}

static ssize_t mxt_atmeldbg_debug_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int count = 0;

	count += sprintf(buf + count, "%d", data->atmeldbg.display_log);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t mxt_atmeldbg_debug_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	int i;
	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->atmeldbg.display_log = i;
		if (i)
			dmesg_restrict = 0;
		else
			dmesg_restrict = 1;

		tsp_debug_info(true, &client->dev, "%s, dmsesg_restricted :%d\n",
			i ? "debug enabled" : "debug disabled", dmesg_restrict);
		tsp_debug_info(true, &client->dev, "%s raw data message for Atmel debug App\n",
			i ? "Display" : "Undisplay");
	} else {
		tsp_debug_info(true, &client->dev, "Error %s\n", __func__);
	}

	return count;
}

static ssize_t mxt_atmeldbg_command_off_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->atmeldbg.block_access = i;

		if (data->atmeldbg.block_access)
			disable_irq(client->irq);
		else
			enable_irq(client->irq);

		tsp_debug_info(true, &client->dev, "%s host driver access IC for Atmel dbug App\n",
			i ? "Stop" : "Start");
	} else
		tsp_debug_info(true, &client->dev, "Error %s\n", __func__);

	return count;
}


static ssize_t mxt_atmeldbg_cmd_calibrate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret;

	/* send calibration command to the chip */
	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
		MXT_COMMAND_CALIBRATE, 1);

	return (ret < 0) ? ret : count;
}

static ssize_t mxt_atmeldbg_cmd_reset_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret;

	/* send reset command to the chip */
	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
		MXT_COMMAND_RESET, MXT_RESET_VALUE);

	return (ret < 0) ? ret : count;
}

static ssize_t mxt_atmeldbg_cmd_backup_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret;

	/* send backup command to the chip */
	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
		MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

	return (ret < 0) ? ret : count;
}
#endif

static ssize_t mxt_object_setting(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	unsigned int type;
	unsigned int offset;
	unsigned int value;
	u8 val;
	int ret;
	sscanf(buf, "%u%u%u", &type, &offset, &value);
	tsp_debug_info(true, &client->dev, "object type T%d", type);
	tsp_debug_info(true, &client->dev, "data offset %d\n", offset);
	tsp_debug_info(true, &client->dev, "data value %d\n", value);

	ret = mxt_write_object(data,
		(u8)type, (u8)offset, (u8)value);

	if (ret) {
		tsp_debug_err(true, &client->dev, "fail to write T%d index:%d, value:%d\n",
			type, offset, value);
		goto out;
	} else {
		ret = mxt_read_object(data,
				(u8)type, (u8)offset, &val);

		if (ret) {
			tsp_debug_err(true, &client->dev, "fail to read T%d\n",
				type);
			goto out;
		} else
			tsp_debug_info(true, &client->dev, "T%d Byte%d is %d\n",
				type, offset, val);
	}
out:
	return count;
}

static ssize_t mxt_object_show(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct mxt_object *object;
	unsigned int type;
	u8 val;
	u16 i;

	sscanf(buf, "%u", &type);
	tsp_debug_info(true, &client->dev, "object type T%d\n", type);

	object = mxt_get_object(data, type);
	if (!object) {
		tsp_debug_err(true, &client->dev, "fail to get object_info\n");
		return -EINVAL;
	} else {
		for (i = 0; i < object->size; i++) {
			mxt_read_mem(data, object->start_address + i,
				1, &val);
			tsp_debug_info(true, &client->dev, "Byte %u --> %u\n", i, val);
		}
	}

	return count;
}

#if TSP_USE_ATMELDBG
/* Functions for mem_access interface */
static struct bin_attribute mem_access_attr;

/* Sysfs files for libmaxtouch interface */
static DEVICE_ATTR(pause_driver, S_IRUGO | S_IWUSR | S_IWGRP,
	mxt_atmeldbg_pause_driver_show, mxt_atmeldbg_pause_driver_store);
static DEVICE_ATTR(debug_enable, S_IRUGO | S_IWUSR | S_IWGRP,
	mxt_atmeldbg_debug_enable_show, mxt_atmeldbg_debug_enable_store);
static DEVICE_ATTR(command_calibrate, S_IRUGO | S_IWUSR | S_IWGRP,
	NULL, mxt_atmeldbg_cmd_calibrate_store);
static DEVICE_ATTR(command_reset, S_IRUGO | S_IWUSR | S_IWGRP,
	NULL, mxt_atmeldbg_cmd_reset_store);
static DEVICE_ATTR(command_backup, S_IRUGO | S_IWUSR | S_IWGRP,
	NULL, mxt_atmeldbg_cmd_backup_store);
static DEVICE_ATTR(command_off, S_IRUGO | S_IWUSR | S_IWGRP,
	NULL, mxt_atmeldbg_command_off_store);
#endif
static DEVICE_ATTR(object_show, S_IWUSR | S_IWGRP, NULL,
	mxt_object_show);
static DEVICE_ATTR(object_write, S_IWUSR | S_IWGRP, NULL,
	mxt_object_setting);

static struct attribute *libmaxtouch_attributes[] = {
#if TSP_USE_ATMELDBG
	&dev_attr_pause_driver.attr,
	&dev_attr_debug_enable.attr,
	&dev_attr_command_calibrate.attr,
	&dev_attr_command_reset.attr,
	&dev_attr_command_backup.attr,
	&dev_attr_command_off.attr,
#endif
	&dev_attr_object_show.attr,
	&dev_attr_object_write.attr,
	NULL,
};

static struct attribute_group libmaxtouch_attr_group = {
	.attrs = libmaxtouch_attributes,
};

#if TSP_SEC_FACTORY
static void mxt_deallocate_factory(struct mxt_data *data)
{
	struct mxt_fac_data *fdata = data->fdata;

	kfree(fdata->delta);
	kfree(fdata->reference);
	kfree(fdata);
}

static int mxt_allocate_factory(struct mxt_data *data)
{
	const struct mxt_platform_data *pdata = data->pdata;
	struct mxt_fac_data *fdata = NULL;
	struct device *dev = &data->client->dev;
	int error = 0;
	int i;

	fdata = kzalloc(sizeof(struct mxt_fac_data), GFP_KERNEL);
	if (fdata == NULL) {
		tsp_debug_err(true, dev, "Fail to allocate sysfs data.\n");
		return -ENOMEM;
	}

	if (!pdata->num_xnode || !pdata->num_ynode) {
		tsp_debug_err(true, dev, "%s num of x,y is 0\n", __func__);
		error =  -EINVAL;
		goto err_get_num_node;
	}

	fdata->num_xnode = pdata->num_xnode;
	fdata->num_ynode = pdata->num_ynode;
	fdata->num_nodes = fdata->num_xnode * fdata->num_ynode;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	tsp_debug_info(true, dev, "%s: x=%d, y=%d, total=%d\n",
		__func__, fdata->num_xnode,
		fdata->num_ynode, fdata->num_nodes);
#endif

	fdata->reference = kzalloc(fdata->num_nodes * sizeof(u16),
				   GFP_KERNEL);
	if (!fdata->reference) {
		tsp_debug_err(true, dev, "Fail to alloc reference of fdata\n");
		error = -ENOMEM;
		goto err_alloc_reference;
	}

	fdata->delta = kzalloc(fdata->num_nodes * sizeof(s16), GFP_KERNEL);
	if (!fdata->delta) {
		tsp_debug_err(true, dev, "Fail to alloc delta of fdata\n");
		error = -ENOMEM;
		goto err_alloc_delta;
	}

	INIT_LIST_HEAD(&fdata->cmd_list_head);
	for (i = 0; i < ARRAY_SIZE(tsp_cmds); i++)
		list_add_tail(&tsp_cmds[i].list, &fdata->cmd_list_head);

	mutex_init(&fdata->cmd_lock);
	fdata->cmd_is_running = false;

	data->fdata = fdata;

	return error;

err_alloc_delta:
	kfree(fdata->reference);
err_alloc_reference:
err_get_num_node:
	kfree(fdata);

	return error;
}

static int mxt_init_factory(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;

	error = mxt_allocate_factory(data);
	if (error)
		return -ENOMEM;

	data->fdata->fac_dev_ts = device_create(sec_class,
			NULL, 0, data, "tsp");
	if (IS_ERR(data->fdata->fac_dev_ts)) {
		tsp_debug_err(true, dev, "Failed to create device for the sysfs\n");
		error = IS_ERR(data->fdata->fac_dev_ts);
		goto err_create_device;
	}
	error = sysfs_create_group(&data->fdata->fac_dev_ts->kobj,
		&touchscreen_factory_attr_group);
	if (error) {
		tsp_debug_err(true, dev, "Failed to create touchscreen sysfs group\n");
		goto err_create_group;
	}
	error = sysfs_create_link(&data->fdata->fac_dev_ts->kobj,
		&data->input_dev->dev.kobj, "input");
	if (error < 0) {
		dev_err(dev, "%s: Failed to create input symbolic link\n",
			__func__);
	}

	return 0;

err_create_group:
err_create_device:
	mxt_deallocate_factory(data);

	return error;
}

#if ENABLE_TOUCH_KEY
static int mxt_sysfs_init_for_touchkeyled(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;

	data->fdata->touchkey_dev_ts = device_create(sec_class,
			NULL, 0, data, "sec_touchkey");
	if (IS_ERR(data->fdata->touchkey_dev_ts)) {
		tsp_debug_err(true, dev, "Failed to create device for the sysfs\n");
		error = IS_ERR(data->fdata->touchkey_dev_ts);
		return 0;
	}
	error = sysfs_create_group(&data->fdata->touchkey_dev_ts->kobj,
		&touchkey_attr_group);
	if (error)
		tsp_debug_err(true, dev, "Failed to create touchscreen sysfs group\n");

	return 0;
}
#endif

static void mxt_exit_factory(struct mxt_data *data)
{
	struct mxt_fac_data *fdata = data->fdata;

	sysfs_remove_group(&fdata->fac_dev_ts->kobj,
		&touchscreen_factory_attr_group);
	mxt_deallocate_factory(data);
}
#endif

#if TSP_USE_ATMELDBG
static int __devinit mxt_sysfs_init_for_ATMELDBG(struct mxt_data *data)
{
	struct i2c_client *client = data->client;

	sysfs_bin_attr_init(&mem_access_attr);
	mem_access_attr.attr.name = "mem_access";
	mem_access_attr.attr.mode = S_IRUGO | S_IWUGO;
	mem_access_attr.read = mem_atmeldbg_access_read;
	mem_access_attr.write = mem_atmeldbg_access_write;
	mem_access_attr.size = 65535;
	data->atmeldbg.display_log = 0;

	if (sysfs_create_bin_file(&client->dev.kobj, &mem_access_attr) < 0) {
		tsp_debug_err(true, &client->dev, "Failed to create device file(%s)!\n",
				mem_access_attr.attr.name);
		return -EINVAL;
	}

	return 0;
}
#endif

extern struct class *sec_class;

static int  __devinit mxt_sysfs_init(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);
	int error;
	struct device *sec_touchscreen;

	error = sysfs_create_group(&client->dev.kobj, &libmaxtouch_attr_group);
	if (error) {
		tsp_debug_err(true, &client->dev, "Failed to create libmaxtouch sysfs group\n");
		return -EINVAL;
	}

	sec_touchscreen = device_create(sec_class, NULL, 0, data, "sec_touchscreen");
	if (IS_ERR(sec_touchscreen))
		dev_err(&client->dev,
				"%s: Failed to create device(sec_touchscreen)!\n",
				__func__);
	if (device_create_file(sec_touchscreen, &dev_attr_set_tsp_for_inputmethod) < 0)
		dev_err(&client->dev,
				"%s: Failed to create device file(%s)\n",
				__func__, dev_attr_set_tsp_for_inputmethod.attr.name);

#if TSP_USE_ATMELDBG
	error = mxt_sysfs_init_for_ATMELDBG(data);
#endif
	if (error)
		goto err_sysfs_init_for_ATMELDBG;

#if TSP_SEC_FACTORY
	error = mxt_init_factory(data);
#endif
	if (error)
		goto err_mxt_init_factory;

#if ENABLE_TOUCH_KEY
	mxt_sysfs_init_for_touchkeyled(data);
#endif

	return 0;

err_mxt_init_factory:
#if TSP_USE_ATMELDBG
	sysfs_remove_bin_file(&client->dev.kobj, &mem_access_attr);
#endif

err_sysfs_init_for_ATMELDBG:
	sysfs_remove_group(&client->dev.kobj, &libmaxtouch_attr_group);

	return error;
}

static void  mxt_sysfs_remove(struct mxt_data *data)
{
	sysfs_remove_group(&data->client->dev.kobj, &libmaxtouch_attr_group);

#if TSP_USE_ATMELDBG
	sysfs_remove_bin_file(&data->client->dev.kobj, &mem_access_attr);
#endif
#if TSP_SEC_FACTORY
	mxt_exit_factory(data);
#endif
}

#if TSP_BOOSTER
static void mxt_change_dvfs_lock(struct work_struct *work)
{
	struct mxt_data *data =
		container_of(work, struct mxt_data, booster.work_dvfs_chg.work);

	mutex_lock(&data->booster.dvfs_lock);

	switch (data->booster.boost_level) {
	case TSP_BOOSTER_LEVEL1:
	case TSP_BOOSTER_LEVEL3:
		if (pm_qos_request_active(&data->booster.tsp_cpu_qos))
			pm_qos_remove_request(&data->booster.tsp_cpu_qos);

		if (pm_qos_request_active(&data->booster.tsp_mif_qos))
			pm_qos_remove_request(&data->booster.tsp_mif_qos);

		if (pm_qos_request_active(&data->booster.tsp_int_qos))
			pm_qos_remove_request(&data->booster.tsp_int_qos);
		tsp_debug_dbg(false, &data->client->dev, "[TSP_DVFS] level[%d] : DVFS OFF\n", data->booster.boost_level);
		break;
	case TSP_BOOSTER_LEVEL2:
		if (pm_qos_request_active(&data->booster.tsp_cpu_qos))
			pm_qos_update_request(&data->booster.tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_2);

		if (pm_qos_request_active(&data->booster.tsp_mif_qos))
			pm_qos_update_request(&data->booster.tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_2);

		if (pm_qos_request_active(&data->booster.tsp_int_qos))
			pm_qos_update_request(&data->booster.tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_2);
		tsp_debug_dbg(false, &data->client->dev, "[TSP_DVFS] level[%d] : DVFS CHANGED\n", data->booster.boost_level);
		break;
	default:
		tsp_debug_err(true, &data->client->dev, "[TSP_DVFS] Undefined type passed %d\n", data->booster.boost_level);
		break;
	}

	mutex_unlock(&data->booster.dvfs_lock);
}

static void mxt_set_dvfs_off(struct work_struct *work)
{
	struct mxt_data *data =
		container_of(work, struct mxt_data, booster.work_dvfs_off.work);

	mutex_lock(&data->booster.dvfs_lock);

	if (pm_qos_request_active(&data->booster.tsp_cpu_qos))
		pm_qos_remove_request(&data->booster.tsp_cpu_qos);

	if (pm_qos_request_active(&data->booster.tsp_mif_qos))
		pm_qos_remove_request(&data->booster.tsp_mif_qos);

	if (pm_qos_request_active(&data->booster.tsp_int_qos))
		pm_qos_remove_request(&data->booster.tsp_int_qos);

	data->booster.dvfs_lock_status = false;
	mutex_unlock(&data->booster.dvfs_lock);

	tsp_debug_dbg(false, &data->client->dev, "[TSP_DVFS] level[%d] : DVFS OFF\n", data->booster.boost_level);
}

static void mxt_init_dvfs_level(struct mxt_data *data)
{
	/* DVFS level is depend on booster_level which is writed by sysfs
	 * booster_level : 1	(press)CPU 1600000, MIF 667000, INT 333000 -> after 130msec -> OFF
	 * booster_level : 2	(press)CPU 1600000, MIF 667000, INT 333000 -> after 130msec
	 *							-> CPU 650000, MIF 400000, INT 111000 -> after 500msec -> OFF
	 * booster_level : 3	(press)CPU 650000, MIF 667000, INT 333000 -> after 130msec -> OFF
	 */
	unsigned int level = data->booster.boost_level;

	switch (level) {
	case TSP_BOOSTER_LEVEL1:
	case TSP_BOOSTER_LEVEL2:
		if (pm_qos_request_active(&data->booster.tsp_cpu_qos))
			pm_qos_update_request(&data->booster.tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_1);
		else
			pm_qos_add_request(&data->booster.tsp_cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCH_BOOSTER_CPU_FRQ_1);

		if (pm_qos_request_active(&data->booster.tsp_mif_qos))
			pm_qos_update_request(&data->booster.tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_1);
		else
			pm_qos_add_request(&data->booster.tsp_mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCH_BOOSTER_MIF_FRQ_1);

		if (pm_qos_request_active(&data->booster.tsp_int_qos))
			pm_qos_update_request(&data->booster.tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_1);
		else
			pm_qos_add_request(&data->booster.tsp_int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCH_BOOSTER_INT_FRQ_1);
		break;
	case TSP_BOOSTER_LEVEL3:
		if (pm_qos_request_active(&data->booster.tsp_cpu_qos))
			pm_qos_update_request(&data->booster.tsp_cpu_qos, TOUCH_BOOSTER_CPU_FRQ_3);
		else
			pm_qos_add_request(&data->booster.tsp_cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCH_BOOSTER_CPU_FRQ_3);

		if (pm_qos_request_active(&data->booster.tsp_mif_qos))
			pm_qos_update_request(&data->booster.tsp_mif_qos, TOUCH_BOOSTER_MIF_FRQ_3);
		else
			pm_qos_add_request(&data->booster.tsp_mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCH_BOOSTER_MIF_FRQ_3);

		if (pm_qos_request_active(&data->booster.tsp_int_qos))
			pm_qos_update_request(&data->booster.tsp_int_qos, TOUCH_BOOSTER_INT_FRQ_3);
		else
			pm_qos_add_request(&data->booster.tsp_int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCH_BOOSTER_INT_FRQ_3);
		break;
	default:
		tsp_debug_err(true, &data->client->dev, "[TSP_DVFS] Undefined type passed %d", level);
		break;
	}
}

static void mxt_set_dvfs_lock(struct mxt_data *data, unsigned int on, bool booster_restart)
{
	if (data->booster.boost_level == TSP_BOOSTER_DISABLE || tsp_booster_enabled == 0)
		return;

	mutex_lock(&data->booster.dvfs_lock);

	switch (on) {
	case TSP_BOOSTER_OFF:
		if (data->booster.dvfs_lock_status) {
			schedule_delayed_work(&data->booster.work_dvfs_off,
				msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME));
		}
		break;
	case TSP_BOOSTER_ON:
		cancel_delayed_work(&data->booster.work_dvfs_off);

		if (!data->booster.dvfs_lock_status || booster_restart) {
			cancel_delayed_work(&data->booster.work_dvfs_chg);
			mxt_init_dvfs_level(data);

			schedule_delayed_work(&data->booster.work_dvfs_chg,
							msecs_to_jiffies(TOUCH_BOOSTER_CHG_TIME));

			tsp_debug_dbg(false, &data->client->dev, "[TSP_DVFS] level[%d] : DVFS ON\n", data->booster.boost_level);
			data->booster.dvfs_lock_status = true;
		}
		break;
	case TSP_BOOSTER_FORCE_OFF:
		if (data->booster.dvfs_lock_status) {
			cancel_delayed_work(&data->booster.work_dvfs_off);
			cancel_delayed_work(&data->booster.work_dvfs_chg);
			schedule_work(&data->booster.work_dvfs_off.work);
		}
		break;
	default:
		break;
	}
	mutex_unlock(&data->booster.dvfs_lock);

}

static int mxt_init_dvfs(struct mxt_data *data)
{

	mutex_init(&data->booster.dvfs_lock);

	INIT_DELAYED_WORK(&data->booster.work_dvfs_off, mxt_set_dvfs_off);
	INIT_DELAYED_WORK(&data->booster.work_dvfs_chg, mxt_change_dvfs_lock);

	data->booster.dvfs_lock_status = false;

	tsp_debug_info(true, &data->client->dev, "%s[%d]", __func__, data->booster.boost_level);
	return 0;
}
#endif /* - TOUCH_BOOSTER */

#if TOUCHKEY_BOOSTER
#define set_qos(req, pm_qos_class, value) { \
	if (pm_qos_request_active(req)) \
		pm_qos_update_request(req, value); \
	else \
		pm_qos_add_request(req, pm_qos_class, value); \
}

#define remove_qos(req) { \
	if (pm_qos_request_active(req)) \
	pm_qos_remove_request(req); \
}

static void touchkey_change_dvfs_lock(struct work_struct *work)
{
	struct mxt_data *data =
		container_of(work, struct mxt_data, tsk_booster.tsk_work_dvfs_chg.work);

	mutex_lock(&data->tsk_booster.tsk_dvfs_lock);

	if (TOUCHKEY_BOOSTER_LEVEL1 == data->tsk_booster.boost_level) {
		set_qos(&data->tsk_booster.cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCHKEY_BOOSTER_CPU_FRQ_1);
		set_qos(&data->tsk_booster.mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCHKEY_BOOSTER_MIF_FRQ_1);
		set_qos(&data->tsk_booster.int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCHKEY_BOOSTER_INT_FRQ_1);
	} else if (TOUCHKEY_BOOSTER_LEVEL2 == data->tsk_booster.boost_level) {
		set_qos(&data->tsk_booster.cpu_qos, PM_QOS_CPU_FREQ_MIN, TOUCHKEY_BOOSTER_CPU_FRQ_2);
		set_qos(&data->tsk_booster.mif_qos, PM_QOS_BUS_THROUGHPUT, TOUCHKEY_BOOSTER_MIF_FRQ_2);
		set_qos(&data->tsk_booster.int_qos, PM_QOS_DEVICE_THROUGHPUT, TOUCHKEY_BOOSTER_INT_FRQ_2);
	} else {
		tsp_debug_err(true, &data->client->dev, "[TSK_DVFS] boost_level error[], %d\n", data->tsk_booster.boost_level);
	}

	tsp_debug_dbg(false, &data->client->dev, "[TSK_DVFS] DVFS ON, %d\n", data->tsk_booster.boost_level);
	data->tsk_booster.tsk_dvfs_lock_status = true;
	data->tsk_booster.dvfs_signal = false;
	mutex_unlock(&data->tsk_booster.tsk_dvfs_lock);
}

static void touchkey_set_dvfs_off(struct work_struct *work)
{
	struct mxt_data *data =
		container_of(work, struct mxt_data, tsk_booster.tsk_work_dvfs_off.work);

	mutex_lock(&data->tsk_booster.tsk_dvfs_lock);

	remove_qos(&data->tsk_booster.cpu_qos);
	remove_qos(&data->tsk_booster.mif_qos);
	remove_qos(&data->tsk_booster.int_qos);

	data->tsk_booster.tsk_dvfs_lock_status = false;
	data->tsk_booster.dvfs_signal = false;
	mutex_unlock(&data->tsk_booster.tsk_dvfs_lock);

	tsp_debug_dbg(false, &data->client->dev, "[TSK_DVFS] DVFS OFF, %d\n", data->tsk_booster.boost_level);
}

static void touchkey_set_dvfs_lock(struct mxt_data *data, uint32_t on)
{
	if (TOUCHKEY_BOOSTER_DISABLE == data->tsk_booster.boost_level || tk_booster_enabled == 0)
		return;

	mutex_lock(&data->tsk_booster.tsk_dvfs_lock);
	if (on == TOUCHKEY_BOOSTER_OFF) {
		if (data->tsk_booster.dvfs_signal) {
			cancel_delayed_work(&data->tsk_booster.tsk_work_dvfs_chg);
			schedule_delayed_work(&data->tsk_booster.tsk_work_dvfs_chg, 0);
			schedule_delayed_work(&data->tsk_booster.tsk_work_dvfs_off,
				msecs_to_jiffies(TOUCHKEY_BOOSTER_OFF_TIME));
		} else if (data->tsk_booster.tsk_dvfs_lock_status) {
			schedule_delayed_work(&data->tsk_booster.tsk_work_dvfs_off,
				msecs_to_jiffies(TOUCHKEY_BOOSTER_OFF_TIME));
		}
	} else if (on == TOUCHKEY_BOOSTER_ON) {
		cancel_delayed_work(&data->tsk_booster.tsk_work_dvfs_off);
		if (!data->tsk_booster.tsk_dvfs_lock_status && !data->tsk_booster.dvfs_signal) {
			schedule_delayed_work(&data->tsk_booster.tsk_work_dvfs_chg,
							msecs_to_jiffies(TOUCHKEY_BOOSTER_ON_TIME));
			data->tsk_booster.dvfs_signal = true;
		}
	} else if (on == TOUCHKEY_BOOSTER_FORCE_OFF) {
		if (data->tsk_booster.tsk_dvfs_lock_status) {
			cancel_delayed_work(&data->tsk_booster.tsk_work_dvfs_off);
			cancel_delayed_work(&data->tsk_booster.tsk_work_dvfs_chg);
			schedule_work(&data->tsk_booster.tsk_work_dvfs_off.work);
		}
	}
	mutex_unlock(&data->tsk_booster.tsk_dvfs_lock);
}


static int touchkey_init_dvfs(struct mxt_data *data)
{
	mutex_init(&data->tsk_booster.tsk_dvfs_lock);

	INIT_DELAYED_WORK(&data->tsk_booster.tsk_work_dvfs_off, touchkey_set_dvfs_off);
	INIT_DELAYED_WORK(&data->tsk_booster.tsk_work_dvfs_chg, touchkey_change_dvfs_lock);

	data->tsk_booster.tsk_dvfs_lock_status = false;

	tsp_debug_info(true, &data->client->dev, "%s[%d]", __func__, data->booster.boost_level);
	return 0;
}
#endif /* - TOUCHKEY_BOOSTER */

