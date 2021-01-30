#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/timer.h>
#include <asm/current.h>
#include <asm/uaccess.h>

#define DRIVER_NAME "beep"

static const unsigned int MINOR_BASE = 0; // udev minor番号の始まり
static const unsigned int MINOR_NUM = 1;  // udev minorの個数
// デバイス全域で使用する変数達
// beep_probeでメモリ確保する。
struct beep_device_info {
    unsigned int major; // udev major番号
    struct cdev cdev;
    struct class *class;
    struct gpio_desc *gpio;
    struct timer_list ringing_timer;
    unsigned long ringing_time_jiffies; // 鳴動時間(単位:jiffies) 0で永遠
};

// /dev/beep0配下のアクセス関数

// デバイス情報を取得し、file構造体に保存する。
static int beep_open(struct inode *inode, struct file *file) {
    pr_devel("%s:beep open\n", __func__);
    struct beep_device_info *bdev = container_of(inode->i_cdev, struct beep_device_info, cdev);
    if (bdev==NULL) {
        pr_err("%s:デバイス情報取得失敗\n", __func__);
        return -EFAULT;
    }
    file->private_data = bdev;
    return 0;
}

static int beep_close(struct inode *inode, struct file *file) {
    //実質何もしない
    pr_devel("%s:beep closed\n", __func__);
    return 0;
}

static ssize_t beep_read(struct file *fp, char __user *buf, size_t count, loff_t *f_pos) {
    struct beep_device_info *bdev = fp->private_data;
    if (!bdev) {
        pr_err("%s:デバイス情報取得失敗\n", __func__);
        return -EFAULT;
    }
    put_user(gpiod_get_value(bdev->gpio)+'0', &buf[0]);
    return 1;
}

static ssize_t beep_write(struct file *fp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct beep_device_info *bdev = fp->private_data;
    char outValue;
    unsigned long expires;
    int result;

    get_user(outValue, &buf[0]);
    if (outValue=='0' || outValue=='1') {
        gpiod_set_value(bdev->gpio , outValue - '0');
        if ( outValue == '1' ) {
            expires = jiffies + bdev->ringing_time_jiffies;
            result = mod_timer(&bdev->ringing_timer, expires);
            pr_devel("%s: timer_start!(%lu jiffies)(active=%d)\n", __func__, expires, result);
        }

        pr_devel("%s: writed [%c] \n", __func__, outValue);
    } else {
        printk(KERN_INFO "%s: no writed. arg=\"%c\"\n", __func__ , outValue);
    }

    return count;
}

void beep_off_when_timeup(struct timer_list *timer) {
    struct beep_device_info *bdev = container_of(timer, struct beep_device_info, ringing_timer);
    if (!bdev) {
        pr_err("%s:デバイス情報取得失敗\n", __func__);
        return;
    }
    gpiod_set_value(bdev->gpio, 0);
}


/* ハンドラ　テーブル */
struct file_operations beep_fops = {
    .open     = beep_open,
    .release  = beep_close,
    .read     = beep_read,
    .write    = beep_write,
};

static int make_udev(struct beep_device_info *bdev, const char* name) { 
    int ret = 0;
    dev_t dev;

    /* メジャー番号取得 */
    ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, name);
    if (ret != 0) {
        printk(KERN_ALERT "%s: メジャー番号取得失敗(%d)\n", __func__, ret);
        goto err;
    }
    bdev->major = MAJOR(dev);

    /* カーネルへのキャラクタデバイスドライバ登録 */
    cdev_init(&bdev->cdev, &beep_fops);
    bdev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&bdev->cdev, dev, MINOR_NUM);
    if (ret != 0) {
        printk(KERN_ALERT "%s: キャラクタデバイス登録失敗(%d)\n", __func__, ret);
        goto err_cdev_add;
    }

    /* カーネルクラス登録 */
    bdev->class = class_create(THIS_MODULE, name);
    if (IS_ERR(bdev->class)) {
        pr_alert("%s: カーネルクラス登録失敗\n", __func__);
        ret =  -PTR_ERR(bdev->class);
        goto err_class_create;
    }

    /* /sys/class/mygpio の生成 */
    for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
        device_create(bdev->class, NULL, MKDEV(bdev->major, minor), NULL, "beep%d", minor);
    }

    return 0;

err_class_create:
    cdev_del(&bdev->cdev);
err_cdev_add:
    unregister_chrdev_region(dev, MINOR_NUM);
err:
    return ret;
}

static void remove_udev(struct beep_device_info *bdev) {
    dev_t dev = MKDEV(bdev->major, MINOR_BASE);
    for (int minor=MINOR_BASE; minor<MINOR_BASE+MINOR_NUM; minor++) {
        /* /sys/class/mygpio の削除 */
        device_destroy(bdev->class, MKDEV(bdev->major, minor));
    }
    class_destroy(bdev->class); /* クラス登録解除 */
    cdev_del(&bdev->cdev); /* デバイス除去 */
    unregister_chrdev_region(dev, MINOR_NUM); /* メジャー番号除去 */
}

static const struct of_device_id of_beep_ids[] = {
    { .compatible = "crowpi2-beep" } ,
    { },
};

MODULE_DEVICE_TABLE(of, of_beep_ids);

static int beep_probe(struct platform_device *p_dev) {
    struct device *dev = &p_dev->dev;
    struct beep_device_info *bdev;
    int result;

    if (!dev->of_node) {
        printk(KERN_ERR "%s:Not Exist of_node for BEEP DRIVER. Check DTB\n", __func__);
        return -ENODEV;
    }

    // デバイス情報のメモリ確保と初期化
    bdev = (struct beep_device_info*)devm_kzalloc(dev, sizeof(struct beep_device_info), GFP_KERNEL);
    if (!bdev) {
        pr_alert("%s: デバイス情報メモリ確保失敗\n", __func__);
        return -ENOMEM;
    }
    dev_set_drvdata(dev, bdev);

    // gpioの確保と初期化
    bdev->gpio = devm_gpiod_get(dev, NULL, GPIOD_OUT_LOW);
    if (IS_ERR(bdev->gpio)) {
        result = PTR_ERR(bdev->gpio);
        printk(KERN_ERR "%s: can not get GPIO.ERR(%d)\n", __func__, result);
        return -EIO;
    }

    // udevの生成
    result = make_udev(bdev, p_dev->name);
    if (result != 0) {
        printk(KERN_ALERT "%s:Fail make udev. gpio desc dispose!!!\n", __func__);
        gpiod_put(bdev->gpio);
    }
    
    // timerの生成
    timer_setup(&bdev->ringing_timer, beep_off_when_timeup, 0);
    bdev->ringing_time_jiffies = msecs_to_jiffies(3000);

    printk(KERN_INFO "%s:beep driver init\n",__func__);
    return result;
}

static int beep_remove(struct platform_device *p_dev) {
    struct beep_device_info *bdev = dev_get_drvdata(&p_dev->dev);
    remove_udev(bdev);

    // gpioデバイスの開放
    if (bdev->gpio) {
        gpiod_put(bdev->gpio);
    }

    del_timer(&bdev->ringing_timer);

    printk(KERN_INFO "%s:beep driver unloaded\n",__func__);
    return 0;
} 
            

static struct platform_driver beep_driver = { 
    .probe = beep_probe,
    .remove = beep_remove,
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_beep_ids,
    },
};

module_platform_driver(beep_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is beep driver for crowpi2");
MODULE_AUTHOR("mito");

