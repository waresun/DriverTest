#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <asm/uaccess.h>
 
#include "hello.h"
 
// * Master and slave devices number variables
static int hello_major = 0;
static int hello_minor = 0;
 
// * device types and device variables
static struct class* hello_class = NULL;
static struct hello_android_dev* hello_dev = NULL;
 
// * traditional method of operation of the device file
static int hello_open(struct inode* inode, struct file* flip);
static int hello_release(struct inode* inode, struct file* flip);
static ssize_t hello_read(struct file* flip, char __user* buf, size_t count, loff_t* f_pos);
static ssize_t hello_write(struct file* flip, const char __user* buf, size_t count, loff_t* f_pos);
 
// * the method of operation of the device file table
static struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.open = hello_open,
	.release = hello_release,
	.read = hello_read,
	.write = hello_write,
};
 
 
// * access to set property methods
static ssize_t hello_val_show(struct device* dev, struct device_attribute* attr, char* buf);
static ssize_t hello_val_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t count);
 
// * define the device properties
static DEVICE_ATTR(val, S_IRUGO | S_IWUSR, hello_val_show, hello_val_store);
 
 
// * open the device methods
static int hello_open(struct inode* inode, struct file* flip)
{
	struct hello_android_dev* dev;
 
	// save the device struct to the private area
	dev = container_of(inode->i_cdev, struct hello_android_dev, dev);
	flip->private_data = dev;
 
	return 0;
}
 
// * release
static int hello_release(struct inode* inode, struct file* filp)
{
	return 0;
}
 
// * read
static ssize_t hello_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos)
{
	ssize_t err = 0;
	struct hello_android_dev* dev = filp->private_data;
 
	// async access
	if(down_interruptible( &(dev->sem) )){
		return -ERESTARTSYS;
	}
 
	if(count < sizeof(dev->val) ){
		goto out;
	}
 
	// 
	if(copy_to_user(buf, &(dev->val), sizeof(dev->val) )){
		err = -EFAULT;
		goto out;
	}
 
	err = sizeof(dev->val);
 
out:
	up(&(dev->sem));
 
	return err;
}
 
// * write
static ssize_t hello_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos)
{
	struct hello_android_dev* dev = filp->private_data;
	ssize_t err = 0;
	
	// async access
	if(down_interruptible( &(dev->sem) )){
		return -ERESTARTSYS;
	}
	
	if(count != sizeof(dev->val) ){
		goto out;
	}
	
	// save the buffer value to device registers
	if( copy_from_user( &(dev->val), buf, count) ){
		err = -EFAULT;
		goto out;
	}
	
	err = sizeof(dev->val);
 
out:
	up(&(dev->sem));
	return err;
}
 
// * read the registers value val to the buffer buf, inner
static ssize_t __hello_get_val(struct hello_android_dev* dev, char* buf)
{
	int val = 0;
	
	// async access
	if(down_interruptible( &(dev->sem) )){
		return -ERESTARTSYS;
	}
	
	val = dev->val;
	up( &(dev->sem) );
	
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}
 
// * write the buffer value buf to the device registers val, inner
static ssize_t __hello_set_val(struct hello_android_dev* dev, const char* buf, size_t count)
{
	int val = 0;
	
	// translate the string to number
	val = simple_strtol(buf, NULL, 10);
	
	// async access
	if(down_interruptible( &(dev->sem) )){
		return -ERESTARTSYS;
	}
	
	dev->val = val;
	up( &(dev->sem));
	
	return count;
}
 
// * read the device properties val
static ssize_t hello_val_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	struct hello_android_dev* hdev = (struct hello_android_dev*)dev_get_drvdata(dev);
	
	return __hello_get_val(hdev, buf);
}
 
// * write the device properties val
static ssize_t hello_val_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	struct hello_android_dev* hdev = (struct hello_android_dev*)dev_get_drvdata(dev);
	
	return __hello_set_val(hdev, buf, count);
}
 
// * read the device registers val, and save to the page buffer
static ssize_t hello_proc_read(char* page, char** start, off_t off, int count, int* eof, void* data)
{
	if(off > 0){
		*eof = 1;
		return 0;
	}
	
	return __hello_get_val(hello_dev, page);
}
 
// * save the buffer value buff to the device registers val
static ssize_t hello_proc_write(struct file* filp, const char __user* buff, unsigned long len, void* data)
{
	int err = 0;
	char* page = NULL;
	
	if(len > PAGE_SIZE){
		printk(KERN_ALERT"The buff is too large:%lu.\n", len);
		return -EFAULT;
	}
	
	page = (char*)__get_free_page(GFP_KERNEL);
	if(!page){
		printk(KERN_ALERT"Failed to alloc page.\n");
		return -ENOMEM;
	}
	
	// copy the user buffer value to kernel buffer
	if(copy_from_user(page, buff, len) ){
		printk(KERN_ALERT"Failed to copy buff from user.\n");
		err = -EFAULT;
		goto out;
	}
	
	err = __hello_set_val(hello_dev, page, len);
	
out:
	free_page( (unsigned long)page);
	return err;
}
 
// * create /proc/hello file
static void hello_create_proc(void)
{
	struct proc_dir_entry* entry;
	entry = create_proc_entry(HELLO_DEVICE_PROC_NAME, 0, NULL);
	if(entry){
		entry->owner = THIS_MODULE;
		entry->read_proc = hello_proc_read;
		entry->write_proc = hello_proc_write;
	}
}
 
// * delete /proc/hello file
static void hello_remove_proc(void)
{
	remove_proc_entry(HELLO_DEVICE_PROC_NAME, NULL);
}
 
// * init device
static int __hello_setup_dev(struct hello_android_dev* dev)
{
	int err;
	dev_t devno = MKDEV(hello_major, hello_minor);
	
	memset(dev, 0, sizeof(struct hello_android_dev) );
	
	cdev_init( &(dev->dev), &hello_fops);
	dev->dev.owner = THIS_MODULE;
	dev->dev.ops = &hello_fops;
	
	// registe charater device
	err = cdev_add( &(dev->dev), devno, 1);
	if(err){
		return err;
	}
	
	// init single and registers value val
	init_MUTEX(&(dev->sem));
	dev->val = 0;
	
	return 0;
}
 
// * load module
static int __init hello_init(void)
{
	int err = -1;
	dev_t dev = 0;
	struct device* temp = NULL;
	
	printk(KERN_ALERT"Initializing hello device.\n");
	
	// malloc master and slave device number
	err = alloc_chrdev_region( &dev, 0, 1, HELLO_DEVICE_NODE_NAME);
	if(err < 0){
		printk(KERN_ALERT"Failed to alloc char dev region.\n");
		goto fail;
	}
	
	hello_major = MAJOR(dev);
	hello_minor = MINOR(dev);
	
	// alloc hello device struct valiriable
	hello_dev = kmalloc( sizeof(struct hello_android_dev), GFP_KERNEL);
	if(!hello_dev){
		err = -ENOMEM;
		printk(KERN_ALERT"Failed to alloc hello_dev.\n");
		goto unregister;
	}
	
	// init device
	err = __hello_setup_dev(hello_dev);
	if(err){
		printk(KERN_ALERT"Failed to setup dev:%d.\n", err);
		goto cleanup;
	}
	
	// create device type directory hello on /sys/class/
	hello_class = class_create(THIS_MODULE, HELLO_DEVICE_CLASS_NAME);
	if(IS_ERR(hello_class)){
		err = PTR_ERR(hello_class);
		printk(KERN_ALERT"Failed to create hello class.\n");
		goto destroy_cdev;
	}
	
	// create device file hello on /dev/ and /sys/class/hello
	temp = device_create(hello_class, NULL, dev, "%s", HELLO_DEVICE_FILE_NAME);
	if(IS_ERR(temp)){
		err = PTR_ERR(temp);
		printk(KERN_ALERT"Failed to create hello device.\n");
		goto destroy_class;
	}
	
	// create property file val on /sys/class/hello/hello
	err = device_create_file(temp, &dev_attr_val);
	if(err < 0){
		printk(KERN_ALERT"Failed to create attribute val.\n");
		goto destroy_device;
	}
	
	dev_set_drvdata(temp, hello_dev);
	
	// create /proc/hello file
	hello_create_proc();
	
	printk(KERN_ALERT"Successed to initialize hello device.\n");
	return 0;
	
destroy_device:
	device_destroy(hello_class, dev);
 
destroy_class:
	class_destroy(hello_class);
 
destroy_cdev:
	cdev_del(&hello_dev->dev);
 
cleanup:
	kfree(hello_dev);
 
unregister:
	unregister_chrdev_region(MKDEV(hello_major, hello_minor), 1);
 
fail:
	return err;
}
 
// * unload module
static void __exit hello_exit(void)
{
	dev_t devno = MKDEV(hello_major, hello_minor);
	
	printk(KERN_ALERT"Destroy hello device.\n");
	
	// delete /proc/hello file
	hello_remove_proc();
	
	// destroy device type and device
	if(hello_class){
		device_destroy(hello_class, MKDEV(hello_major, hello_minor) );
		class_destroy(hello_class);
	}
	
	// delete character device and release device memory
	if(hello_dev){
		cdev_del(&(hello_dev->dev) );
		kfree(hello_dev);
	}
	
	// destroy device number
	unregister_chrdev_region(devno, 1);
}
 
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("First Android Device");
 
module_init(hello_init);
module_exit(hello_exit);
