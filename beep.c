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
MODULE_DESCRIPTION("This is mygpio driver.");
MODULE_AUTHOR("mito");

#define DRIVER_NAME "mygpio"

static const unsigned int MINOR_BASE = 0;
static const unsigned int MINOR_NUM = 1;
static unsigned int mygpio_major;
static struct cdev mygpio_cdev;
static struct class *mygpio_class = NULL;

struct mygpio_driver_t {
    struct device_driver driver;
};

static struct mygpio_driver_t mygpio_driver = { 
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = NULL,
    },
};

static int mygpio_open(struct inode *inode, struct file *file) {
    printk(KERN_ALERT "mygpio open\n");
    return 0;
}

static int mygpio_close(struct inode *inode, struct file *file) {
    printk(KERN_ALERT "mygpio closed\n");
    return 0;
}

static ssize_t mygpio_read(struct file *fp, char __user *buf, size_t count, loff_t *f_pos) {
    printk(KERN_ALERT "mygpio read\n");
    if (count > 0) {
        buf[0] = 'A';
    }
    return 1;
}

static ssize_t mygpio_write(struct file *fp, const char __user *buf, size_t count, loff_t *f_pos) {
    printk(KERN_ALERT "mygpio write\n");
    return 1;
}

/* ハンドラ　テーブル */
struct file_operations mygpio_fops = {
    .open     = mygpio_open,
    .release  = mygpio_close,
    .read     = mygpio_read,
    .write    = mygpio_write,
};

static int mygpio_init(struct mygpio_driver_t *drv) {
    int ret = 0;
    dev_t dev;

    printk(KERN_INFO "mygpio driver init\n");

    /* メジャー番号取得 */
    ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, drv->driver.name);
    if (ret != 0) {
        printk(KERN_ALERT "My_gpio: メジャー番号取得失敗(%d)\n", ret);
        return -1;
    }
    mygpio_major = MAJOR(dev);

    /* cdev初期化 */
    cdev_init(&mygpio_cdev, &mygpio_fops);
    mygpio_cdev.owner = THIS_MODULE;

    /* カーネルへのドライバ登録 */
    ret = cdev_add(&mygpio_cdev, dev, MINOR_NUM);
    if (ret != 0) {
        printk(KERN_ALERT "My_gpio: カーネル登録失敗(%d)\n", ret);
        return -1;
    }

    /* カーネルクラス登録 */
    mygpio_class = class_create(THIS_MODULE, drv->driver.name);
    if (IS_ERR(mygpio_class)) {
        printk(KERN_ALERT "mygpio: カーネルクラス登録失敗\n");
        cdev_del(&mygpio_cdev);
        unregister_chrdev_region(dev, MINOR_NUM);
        return -1;
    }

    /* /sys/class/mygpio の生成 */
    for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
        device_create(mygpio_class, NULL, MKDEV(mygpio_major, minor), NULL, "gygpio%d", minor);
    }

    return 0;
}

static void mygpio_exit(struct mygpio_driver_t *drv) {
    dev_t dev = MKDEV(mygpio_major, MINOR_BASE);
    for (int minor=MINOR_BASE; minor<MINOR_BASE+MINOR_NUM; minor++) {
        /* /sys/class/mygpio の削除 */
        device_destroy(mygpio_class, MKDEV(mygpio_major, minor));
    }
    class_destroy(mygpio_class); /* クラス登録解除 */
    cdev_del(&mygpio_cdev); /* デバイス除去 */
    unregister_chrdev_region(dev, MINOR_NUM); /* メジャー番号除去 */
    printk(KERN_INFO "mygpio driver unloaded\n");
} 
            
module_driver(mygpio_driver, mygpio_init, mygpio_exit);
