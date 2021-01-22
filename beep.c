#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <asm/current.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is beep driver for crowpi2");
MODULE_AUTHOR("mito");

#define DRIVER_NAME "beep"

static const unsigned int MINOR_BASE = 0;
static const unsigned int MINOR_NUM = 1;
static unsigned int beep_major;
static struct cdev beep_cdev;
static struct class *beep_class = NULL;

struct beep_driver_t {
    struct device_driver driver;
};

static struct beep_driver_t beep_driver = { 
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = NULL,
    },
};

static int beep_open(struct inode *inode, struct file *file) {
    printk(KERN_ALERT "mygpio open\n");
    return 0;
}

static int beep_close(struct inode *inode, struct file *file) {
    printk(KERN_ALERT "mygpio closed\n");
    return 0;
}

static ssize_t beep_read(struct file *fp, char __user *buf, size_t count, loff_t *f_pos) {
    printk(KERN_ALERT "mygpio read\n");
    if (count > 0) {
        buf[0] = 'A';
    }
    return 1;
}

static ssize_t beep_write(struct file *fp, const char __user *buf, size_t count, loff_t *f_pos) {
    printk(KERN_ALERT "mygpio write\n");
    return 1;
}

/* ハンドラ　テーブル */
struct file_operations beep_fops = {
    .open     = beep_open,
    .release  = beep_close,
    .read     = beep_read,
    .write    = beep_write,
};

static int beep_init(struct beep_driver_t *drv) {
    int ret = 0;
    dev_t dev;

    printk(KERN_INFO "mygpio driver init\n");

    /* メジャー番号取得 */
    ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, drv->driver.name);
    if (ret != 0) {
        printk(KERN_ALERT "beep: メジャー番号取得失敗(%d)\n", ret);
        return -1;
    }
    beep_major = MAJOR(dev);

    /* cdev初期化 */
    cdev_init(&beep_cdev, &beep_fops);
    beep_cdev.owner = THIS_MODULE;

    /* カーネルへのドライバ登録 */
    ret = cdev_add(&beep_cdev, dev, MINOR_NUM);
    if (ret != 0) {
        printk(KERN_ALERT "beep: カーネル登録失敗(%d)\n", ret);
        return -1;
    }

    /* カーネルクラス登録 */
    beep_class = class_create(THIS_MODULE, drv->driver.name);
    if (IS_ERR(beep_class)) {
        printk(KERN_ALERT "beep: カーネルクラス登録失敗\n");
        cdev_del(&beep_cdev);
        unregister_chrdev_region(dev, MINOR_NUM);
        return -1;
    }

    /* /sys/class/mygpio の生成 */
    for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
        device_create(beep_class, NULL, MKDEV(beep_major, minor), NULL, "beep%d", minor);
    }

    return 0;
}

static void beep_exit(struct beep_driver_t *drv) {
    dev_t dev = MKDEV(beep_major, MINOR_BASE);
    for (int minor=MINOR_BASE; minor<MINOR_BASE+MINOR_NUM; minor++) {
        /* /sys/class/mygpio の削除 */
        device_destroy(beep_class, MKDEV(beep_major, minor));
    }
    class_destroy(beep_class); /* クラス登録解除 */
    cdev_del(&beep_cdev); /* デバイス除去 */
    unregister_chrdev_region(dev, MINOR_NUM); /* メジャー番号除去 */
    printk(KERN_INFO "beep driver unloaded\n");
} 
            
module_driver(beep_driver, beep_init, beep_exit);
