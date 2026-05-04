#include <linux/module.h>
#include <linux/of.h>
#include <linux/workqueue.h>

#include "xiaomi_touch.h"
#include "pipa-pen.h"

/*
 * The legacy userspace workaround was started from boot_completed, so keep a
 * bounded retry window until the vendor touchscreen backend registers mode
 * handlers and is ready to accept the pen-mode update.
 */
#define PIPA_PEN_INIT_DELAY_MS	1000
#define PIPA_PEN_RETRY_DELAY_MS	500
#define PIPA_PEN_MAX_RETRIES	120
#define PIPA_PEN_MODE_VALUE	1

static struct xiaomi_touch_pdata *pipa_pen_pdata;
static unsigned int pipa_pen_retry_count;
static bool pipa_pen_enabled;

static void pipa_pen_enable_worker(struct work_struct *work);
static DECLARE_DELAYED_WORK(pipa_pen_enable_work, pipa_pen_enable_worker);

static bool pipa_pen_is_target(void)
{
	return of_machine_is_compatible("xiaomi,pipa");
}

static void pipa_pen_schedule(unsigned int delay_ms)
{
	schedule_delayed_work(&pipa_pen_enable_work,
			      msecs_to_jiffies(delay_ms));
}

static void pipa_pen_retry(struct xiaomi_touch *touch_dev, int ret)
{
	if (pipa_pen_retry_count >= PIPA_PEN_MAX_RETRIES) {
		if (touch_dev && touch_dev->dev) {
			dev_warn(touch_dev->dev,
				 "pipa pen enable timed out after %u retries: %d\n",
				 pipa_pen_retry_count, ret);
		}
		return;
	}

	pipa_pen_retry_count++;
	pipa_pen_schedule(PIPA_PEN_RETRY_DELAY_MS);
}

static void pipa_pen_enable_worker(struct work_struct *work)
{
	struct xiaomi_touch_pdata *pdata = pipa_pen_pdata;
	struct xiaomi_touch_interface *touch_data;
	struct xiaomi_touch *touch_dev;
	int ret;

	(void)work;

	if (!pipa_pen_is_target() || pipa_pen_enabled || !pdata)
		return;

	touch_dev = pdata->device;
	if (!touch_dev || !pdata->touch_data) {
		pipa_pen_retry(touch_dev, -ENODEV);
		return;
	}

	mutex_lock(&touch_dev->mutex);
	touch_data = pdata->touch_data;
	if (!touch_data || !touch_data->setModeValue) {
		mutex_unlock(&touch_dev->mutex);
		pipa_pen_retry(touch_dev, -EAGAIN);
		return;
	}

	ret = touch_data->setModeValue(Touch_Pen_ENABLE, PIPA_PEN_MODE_VALUE);
	mutex_unlock(&touch_dev->mutex);
	if (ret < 0) {
		pipa_pen_retry(touch_dev, ret);
		return;
	}

	pipa_pen_enabled = true;
	if (touch_dev->dev)
		dev_info(touch_dev->dev, "pipa pen mode enabled\n");
}

void pipa_pen_init(struct xiaomi_touch_pdata *pdata)
{
	if (!pipa_pen_is_target())
		return;

	cancel_delayed_work_sync(&pipa_pen_enable_work);

	pipa_pen_pdata = pdata;
	pipa_pen_retry_count = 0;
	pipa_pen_enabled = false;

	pipa_pen_schedule(PIPA_PEN_INIT_DELAY_MS);
}
EXPORT_SYMBOL_GPL(pipa_pen_init);

void pipa_pen_remove(void)
{
	cancel_delayed_work_sync(&pipa_pen_enable_work);

	pipa_pen_pdata = NULL;
	pipa_pen_retry_count = 0;
	pipa_pen_enabled = false;
}
EXPORT_SYMBOL_GPL(pipa_pen_remove);

MODULE_DESCRIPTION("Xiaomi Pad 6 pen initialization hook");
MODULE_LICENSE("GPL");
