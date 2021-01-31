#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/vermagic.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/kernel.h>      
#include <linux/interrupt.h>
#include <linux/kobject.h> 
#include <linux/timer.h>

/* warning! need write-all permission so overriding check */ 
#undef VERIFY_OCTAL_PERMISSIONS
#define VERIFY_OCTAL_PERMISSIONS(perms) (perms)

// module specific vars
static int major;
static int minor = 0;
static int n_devices = 1; 
static char* name = "bme280";
static struct cdev* cdev1 = NULL;
static int i2c_bus_number = 1;
struct class *my_class;

// vars for updating period
// (in the Interrupt func)
static const int period_start_ms = 2000;
static const int period_divisor = 2;
static const int max_step_count = 4;
static int period_ms = 2000;
static int button_pressed = 0;
static struct timespec ts_last, ts_current, ts_diff; 

static int irqNumber;     

// For bme280 access over i2c: bme280_read function 
#define DATA_SIZE 40
#define BME280_I2C_BUS_ADDRESS 0x76
#define BME280_DATA_ADDRESS 0xF7 
#define BME280_CAL1_ADDRESS 0x88
#define BME280_CAL2_ADDRESS 0xA1
#define BME280_CAL3_ADDRESS 0xE1
#define BME280_CTRL_HUMIDITY 0xF2
#define BME280_CTRL 0xF4
#define CTRL_HUM_DATA 2
#define CTRL_DATA 73

// ################ MODULE #####################################################

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mariia Fortova");
MODULE_DESCRIPTION("BME280 Weather Station Driver");

static unsigned int gpioButton = 24;       
module_param(gpioButton, uint, S_IRUGO);    // Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioButton, " GPIO Button number (default=24)");  

static unsigned int gpioLED = 23;          
module_param(gpioLED, uint, S_IRUGO);       // Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED, " GPIO LED number (default=23)");    

// ################ MODULE FUNC DEFS ###########################################
/**
 * @brief Module's Initialization method 
 * @details registers chdev numbers, cdev struct, creates chdev file,
 * setup GPIO ports, creates class, kobject, attributes' group in sysfs,
 * added interrupt handler to gpio  
 **/
static int __init bme280_init(void);
/**
 * @brief Module's Exit method 
 * @details removes interrupt, free numbers, removes group, class from file system,
 * unregister Gpios, remove cdev, chdev file, dealloc major, minor numbers
 **/
static void bme280_exit(void);

// ################# FOPS  FUNC DEFS ###########################################
/**
 * @brief File Operation's function, called on opening the chdev file
 * setup i2c_client, put it into filp, write to/configure bme280
 * @param inode inode pointer (here: for chdev numbers & cdev struct)
 * @param filp file pointer holding private data
 * @return 0 on success
 **/
int 
bme280_open(struct inode *inode, struct file *filp);
/**
 * @brief File Operation's function, called on reading the chdev file
 * @param filp file pointer holding private data
 * @param buf user buffer pointer
 * @param buf_length buffer length
 * @param f_pos user defined offset of buf's start
 * @return bytes' count, that was read
 **/
ssize_t 
bme280_read (struct file *filp, char __user *buf, size_t buf_length, loff_t *f_pos);
/**
 * @brief File Operation's function, called on closing the chdev file
 * unregister i2c_client
 * @param inode inode pointer (here: for chdev numbers & cdev struct)
 * @param filp file pointer holding private data
 * @return 0 on success
 **/
int 
bme280_release(struct inode *inode, struct file *filp);

// ################# ATTR & INTERRUPT FUNC DEFS ################################

/**
 * is bound automatically to attr defined at line 111
 * @param kobj corresponding kobject
 * @param attr attribute pointer
 * @param buf data to be read
 **/
static ssize_t 
period_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
/**
 * @brief Interrupt handler function. Is bound automatically to attr 
 * defined at line 111
 * @return specific flag to signalize if Interrupt was handles or not
 **/
static irq_handler_t  
period_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

// ################## STRUCTS & GLOBAL VARS ####################################

// custom structure to hold all nesessary 
struct bme280_dev {
	struct cdev cdev;
	struct i2c_adapter i2c_adap;
	struct i2c_client *i2c_client;
    int x;
};

// fops to cdev struct (handling for char device)
struct file_operations bme280_fops = {
    .owner = THIS_MODULE,
    .read = bme280_read,
    .open = bme280_open,
    .release = bme280_release,
};

// board info in order to setup i2c_client
const struct i2c_board_info bme280_i2c_board_info = {
    I2C_BOARD_INFO("bme280", BME280_I2C_BUS_ADDRESS),
};  

// Sysfs attribute 
static struct kobj_attribute update_period_attr = __ATTR_RO(period_ms);

// Sysfs KObject
static struct kobject *bme280_kobj;

// Attributes in the sysfs for USER/Kernel communication
static struct attribute *bme280_attrs[] = {
    &update_period_attr.attr, // period attribute
    NULL,
};

//  The attribute group uses the attribute array and a name, which is exposed on sysfs 
static struct attribute_group attr_group = {
      .name  = "WeatherAttrGroup",              
      .attrs = bme280_attrs,                
};

// ##################### IMPLEMENTATION ATTR FUNC ################################

static ssize_t 
period_ms_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   return sprintf(buf, "%d\n", period_ms);
}

static irq_handler_t 
period_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{

   // Determine the time difference between last 2 interrupt calls
   getnstimeofday(&ts_current);         
   ts_diff = timespec_sub(ts_current, ts_last); 

   // if interrupt was call less then 0.25 sec ago 
   if ((ts_diff.tv_sec*1000 + ts_diff.tv_nsec/1000000) < 250) 
   {
       // -> do not handle the call 
       return (irq_handler_t) IRQ_NONE;
   }

   ts_last = ts_current; 
   button_pressed++;

   // if rest != 0 -> decrease the period
   if(button_pressed % max_step_count) 
   {
       period_ms /= period_divisor;
   }else // if rest == 0 -> reset period                            
   {
       period_ms = period_start_ms;
   }

   return (irq_handler_t) IRQ_HANDLED;  
}

// ##################### IMPLEMENTATION ###########################################

int 
bme280_open(struct inode *inode, struct file *filp) 
{
    struct bme280_dev *dev;
    struct i2c_client *i2c_client;
    int err;

    printk(KERN_INFO "BME280: OPEN called");

    /* setup i2c client */
    i2c_client = i2c_new_device(i2c_get_adapter(i2c_bus_number), &bme280_i2c_board_info);

    // Create an instance of a bme280_dev and add the cdev putting everything
    // into private_data so we can access it from the read later 
    dev = container_of(inode->i_cdev, struct bme280_dev, cdev);
    dev->i2c_client = i2c_client;
    filp->private_data = dev;
    
    //write to register:
    err = i2c_smbus_write_byte_data(dev->i2c_client, BME280_CTRL_HUMIDITY, CTRL_HUM_DATA);
    if(err < 0)
    {
       printk(KERN_WARNING "BME280: Failed to write: configuration Humidity Oversampling");
    }

    //write to register:
    err = i2c_smbus_write_byte_data(dev->i2c_client, BME280_CTRL, CTRL_DATA);
    if(err < 0)
    {
       printk(KERN_WARNING "BME280: Failed to write: configuration Temp, Pres Oversampling + CTRL");
    }


    return 0;
}

int 
bme280_release(struct inode *inode, struct file *filp) 
{
    struct bme280_dev *dev = filp->private_data;
    printk(KERN_INFO "BME280: RELEASE called");

    if (dev->i2c_client) 
    {
        printk(KERN_INFO "BME280: unregistering i2c_client");
        i2c_unregister_device(dev->i2c_client);
    }

    return 0;
}

ssize_t 
bme280_read (struct file *filp, char __user *buf, size_t buf_length, loff_t *f_pos) 
{
    struct bme280_dev* dev = filp->private_data;
    int bytes_read = 0;
    int not_copied_bytes = 0; 
        
    u8   data[DATA_SIZE] = {0};
    u8*  dataCal1 = &data[0];    // first 24 bytes
    u8*  dataCal2 = &data[24];   // next 1 byte
    u8*  dataCal3 = &data[25];   // next 7 bytes
    u8*  dataMesure = &data[32];  // last 8 bytes


    // signalize reading start
    gpio_set_value(gpioLED, 1);    
    
    // READ  cal1 data
    bytes_read = i2c_smbus_read_i2c_block_data(dev->i2c_client, BME280_CAL1_ADDRESS, 24, dataCal1);
    if(bytes_read < 0)
    {
        printk(KERN_ERR "BME280: read - failed read cal from 0x88");
    }

    // READ  cal2 data
    bytes_read = i2c_smbus_read_i2c_block_data(dev->i2c_client, BME280_CAL2_ADDRESS, 1, dataCal2);
    if(bytes_read < 0)
    {
        printk(KERN_ERR "BME280: read - failed read cal from 0xA1");
    }

    // READ  cal3 data
    bytes_read = i2c_smbus_read_i2c_block_data(dev->i2c_client, BME280_CAL3_ADDRESS, 7, dataCal3);
    if(bytes_read < 0)
    {
        printk(KERN_ERR "BME280: read - failed read cal from 0xE1");
    }

    // CREAD Mesurement Data
    bytes_read = i2c_smbus_read_i2c_block_data(dev->i2c_client, BME280_DATA_ADDRESS, 8, dataMesure);
    if(bytes_read < 0)
    {
        printk(KERN_ALERT "BME280: read - failed read data from 0xF7");
    }

    // waiting for mesurements + waiting LED is ON
    // (Datasheet Appendix B: Measurement time and current calculation)
    schedule_timeout(16);
    
    // COPY TO USER
    not_copied_bytes = copy_to_user(buf, data, DATA_SIZE);
    if (not_copied_bytes) 
    {
        printk(KERN_ALERT "BME280: `copy_to_user` failed not copied bytes: %d", not_copied_bytes);
        return 0;
    }else
    {
       printk(KERN_INFO "BME280: succeded copy data to user space");
    }

    // signalize reading end
    gpio_set_value(gpioLED, 0); 

    return DATA_SIZE;
}

// #################################################################

static int __init bme280_init(void) 
{
    unsigned long IRQflags = IRQF_TRIGGER_FALLING; 
    dev_t dev1;
    int err;
    //init rime for IRQ Handler
    getnstimeofday(&ts_last);  

    printk(KERN_INFO "BME280: Initializing device driver.");

    /* Register char device major number dynamically */
    err = alloc_chrdev_region(&dev1, minor, n_devices, name);
    if (err < 0)  
    {
        printk(KERN_ERR "BME280: Can't get major number\n");
        goto exit;
    }
    major = MAJOR(dev1);
    printk(KERN_INFO "BME280: Assigned major number=%d\n", major);
    
    /* Setup cdev struct and add to system */
    printk(KERN_INFO "BME280: Allocating cdev");
    cdev1 = cdev_alloc();
    cdev_init(cdev1, &bme280_fops);
    cdev1->owner = THIS_MODULE;
    err = cdev_add(cdev1, dev1, 1);
    if (err < 0) 
    {
        printk(KERN_ALERT "BME280: Error while adding char device to system");
        goto exit;
    }
    
    // create device file
    my_class = class_create(THIS_MODULE, "MyClass");
                // params:(class, parent device, devt, data, name)
    device_create(my_class, NULL, MKDEV(MAJOR(dev1), 0), NULL, "bme280");

    // Setup LED & Button
    gpio_request(gpioLED, "sysfs");          // request gpio port
    gpio_direction_output(gpioLED, 0);       // Set the gpio to be in output mode and off
    gpio_export(gpioLED, false);             // Causes gpio to appear in /sys/class/gpio
                                             // false - direction cannot be changed
    gpio_request(gpioButton, "sysfs");       
    gpio_direction_input(gpioButton);             // Set the button GPIO to be an input
    gpio_export(gpioButton, false);          


    // create the kobject sysfs entry at /sys/kernel/WeatherStation 
    bme280_kobj = kobject_create_and_add("WeatherKObject", kernel_kobj); // kernel_kobj points to /sys/kernel
    if(!bme280_kobj)
    {
        printk(KERN_ERR "BME280: Failed to create kobject mapping\n");
        return -ENOMEM;
    }

    // add the attributes to /sys/kernel/WeatherStation 
    err = sysfs_create_group(bme280_kobj, &attr_group);
    if(err) 
    {
        printk(KERN_ERR "BME280: Failed to create sysfs group\n");
        kobject_put(bme280_kobj);     // remove kObject                    
        return err;
    }    
 
    // create Interrupt  
    irqNumber = gpio_to_irq(gpioButton);        // generate IRQ (mapping)
    printk(KERN_INFO "BME280: The button is mapped to IRQ: %d\n", irqNumber);
    err = request_irq(irqNumber,             // The interrupt number requested
                     (irq_handler_t) period_irq_handler, 
                      IRQflags,               // Use the custom kernel param to set interrupt type
                      "BME280_button_handler",   // Used in /proc/interrupts to identify the owner
                      NULL);                  // The *dev_id for shared interrupt lines, NULL is okay
    if(err) 
    {
        printk(KERN_ERR "BME280: Failed to request IRQ!\n");
        free_irq(irqNumber, NULL); 
        return err;
    }  
    
    printk(KERN_INFO "BME280: Initialized successfully");
    return 0;

exit:
    bme280_exit();
    return err;
}

static void bme280_exit(void) 
{
    dev_t dev1 = MKDEV(major, minor);
    printk(KERN_WARNING "BME280: Stopping device driver\n");

    free_irq(irqNumber, NULL);  // Free the IRQ number, no *dev_id required in this case
    kobject_put(bme280_kobj); //// remove kObject 
    printk(KERN_WARNING "BME280: Unexport GPIOs\n");
    gpio_set_value(gpioLED, 0);              
    gpio_unexport(gpioLED);                  
    gpio_unexport(gpioButton);               
    gpio_free(gpioLED);                      
    gpio_free(gpioButton); 

    if(my_class) 
    {
        printk(KERN_WARNING "BME280: Deleting sysfs entries");
        device_destroy(my_class, dev1);
        printk(KERN_WARNING "BME280: Destroying my_class");
        class_destroy(my_class);
    }

    if (cdev1) 
    {
        printk(KERN_WARNING "BME280: Removing cdev1 from system");
        cdev_del(cdev1);
    }

    if (major > 0) 
    {
        printk(KERN_WARNING "BME280: Unregistering char device %d\n", major);
        unregister_chrdev_region(dev1, n_devices);
    }

}

module_init(bme280_init);
module_exit(bme280_exit);
