#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/usb/serial.h>
#include <linux/tty_port.h>
#include <linux/version.h>

#define DRIVER_NAME "my_usb_serial"
#define MY_TTY_MAJOR 240
#define MY_TTY_MINORS 1

// Change these to match your Raspberry Pi 4B USB device
// You can find these values using 'lsusb' command
#define VENDOR_ID  0x0525  // Likely value for RPi 4B
#define PRODUCT_ID 0xa4a7  // Likely value for RPi 4B

static struct usb_device_id my_usb_table[] = {
    { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    {}
};
MODULE_DEVICE_TABLE(usb, my_usb_table);

struct my_usb_device {
    struct usb_device *udev;
    struct usb_interface *interface;
    struct usb_endpoint_descriptor *bulk_in;
    struct usb_endpoint_descriptor *bulk_out;
    unsigned char *bulk_in_buffer;
    size_t bulk_in_size;
    struct urb *bulk_in_urb;
    struct usb_anchor submitted;
    struct tty_port port;
    int open_count;
    spinlock_t lock;
    bool ongoing_read;
    bool read_urb_busy;
    struct mutex write_lock;     // Added for write serialization
};

static struct tty_driver *my_tty_driver;
static struct my_usb_device *g_dev = NULL;

static void my_usb_read_bulk_callback(struct urb *urb)
{
    pr_info(DRIVER_NAME ": Called read() function\n");
    struct my_usb_device *dev = urb->context;
    unsigned long flags;
    int status;

    spin_lock_irqsave(&dev->lock, flags);

    if (urb->status) {
        if (urb->status == -ENOENT ||
            urb->status == -ECONNRESET ||
            urb->status == -ESHUTDOWN) {
            spin_unlock_irqrestore(&dev->lock, flags);
            return;
        }
        dev->read_urb_busy = false;
        spin_unlock_irqrestore(&dev->lock, flags);
        return;
    }

    if (urb->actual_length > 0) {
        tty_insert_flip_string(&dev->port, dev->bulk_in_buffer, urb->actual_length);
        tty_flip_buffer_push(&dev->port);
    }

    if (dev->ongoing_read) {
        usb_fill_bulk_urb(dev->bulk_in_urb,
                          dev->udev,
                          usb_rcvbulkpipe(dev->udev, dev->bulk_in->bEndpointAddress),
                          dev->bulk_in_buffer,
                          dev->bulk_in_size,
                          my_usb_read_bulk_callback,
                          dev);

        status = usb_submit_urb(dev->bulk_in_urb, GFP_ATOMIC);
        if (status) {
            dev->read_urb_busy = false;
        }
    } else {
        dev->read_urb_busy = false;
    }

    spin_unlock_irqrestore(&dev->lock, flags);
}

static int my_usb_start_read(struct my_usb_device *dev)
{
    unsigned long flags;
    int rv = 0;

    spin_lock_irqsave(&dev->lock, flags);

    if (!dev->ongoing_read) {
        dev->ongoing_read = true;
        dev->read_urb_busy = true;

        usb_fill_bulk_urb(dev->bulk_in_urb,
                        dev->udev,
                        usb_rcvbulkpipe(dev->udev, dev->bulk_in->bEndpointAddress),
                        dev->bulk_in_buffer,
                        dev->bulk_in_size,
                        my_usb_read_bulk_callback,
                        dev);

        rv = usb_submit_urb(dev->bulk_in_urb, GFP_ATOMIC);
        if (rv) {
            dev->ongoing_read = false;
            dev->read_urb_busy = false;
            pr_err(DRIVER_NAME ": Failed to submit read URB: %d\n", rv);
        }
    }

    spin_unlock_irqrestore(&dev->lock, flags);
    return rv;
}

static int my_usb_stop_read(struct my_usb_device *dev)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->lock, flags);
    dev->ongoing_read = false;
    spin_unlock_irqrestore(&dev->lock, flags);

    usb_kill_urb(dev->bulk_in_urb);
    return 0;
}

static int my_port_activate(struct tty_port *port, struct tty_struct *tty)
{
    struct my_usb_device *dev = container_of(port, struct my_usb_device, port);
    return my_usb_start_read(dev);
}

static void my_port_shutdown(struct tty_port *port)
{
    struct my_usb_device *dev = container_of(port, struct my_usb_device, port);
    my_usb_stop_read(dev);
}

static const struct tty_port_operations my_port_ops = {
    .activate = my_port_activate,
    .shutdown = my_port_shutdown,
};

static int my_tty_open(struct tty_struct *tty, struct file *file)
{
    pr_info(DRIVER_NAME ": Called open() function\n");
    if (!g_dev)
        return -ENODEV;

    return tty_port_open(&g_dev->port, tty, file);
}

static void my_tty_close(struct tty_struct *tty, struct file *file)
{
    pr_info(DRIVER_NAME ": Called close() function\n");
    if (g_dev)
        tty_port_close(&g_dev->port, tty, file);
}

/* Write URB completion callback */
static void my_write_bulk_callback(struct urb *urb)
{
    struct my_usb_device *dev = urb->context;
    unsigned char *buf = urb->transfer_buffer;
    
    /* Free the transfer buffer allocated in write */
    kfree(buf);
    
    /* If urb has an error, log it */
    if (urb->status)
        dev_err(&dev->interface->dev, "Write URB returned status %d\n", urb->status);
    
    /* Decrement the counter of active writes */
    usb_autopm_put_interface_async(dev->interface);
}

static ssize_t my_tty_write(struct tty_struct *tty,
                      const unsigned char *buffer, size_t count)
{
    pr_info(DRIVER_NAME ": Called write() function\n");
    struct my_usb_device *dev = g_dev;
    struct urb *urb;
    unsigned char *buf;
    int retval;

    if (!dev || !dev->udev)
        return -ENODEV;

    if (count == 0)
        return 0;

    /* Serialize writes for thread safety */
    mutex_lock(&dev->write_lock);

    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb) {
        retval = -ENOMEM;
        goto error_no_urb;
    }

    /* Allocate a buffer for this write */
    buf = kmalloc(count, GFP_KERNEL);
    if (!buf) {
        retval = -ENOMEM;
        goto error_no_buf;
    }

    /* Copy the data from user space to our buffer */
    memcpy(buf, buffer, count);

    /* Indicate we are using the interface */
    usb_autopm_get_interface(dev->interface);

    /* Set up the URB */
    usb_fill_bulk_urb(urb, dev->udev,
                      usb_sndbulkpipe(dev->udev, dev->bulk_out->bEndpointAddress),
                      buf, count, my_write_bulk_callback, dev);

    /* Enable zero length packet if we're sending a multiple of the endpoint size */
    if ((count % usb_endpoint_maxp(dev->bulk_out)) == 0)
        urb->transfer_flags |= URB_ZERO_PACKET;

    /* Send the data */
    usb_anchor_urb(urb, &dev->submitted);
    retval = usb_submit_urb(urb, GFP_KERNEL);
    if (retval) {
        dev_err(&dev->interface->dev, "Failed to submit write URB, error %d\n", retval);
        usb_unanchor_urb(urb);
        usb_autopm_put_interface(dev->interface);
        kfree(buf);
        goto error_unanchor;
    }

    /* URB submitted successfully */
    retval = count;

error_unanchor:
    /* urb is freed in the completion handler */
    usb_free_urb(urb);
    mutex_unlock(&dev->write_lock);
    return retval;

error_no_buf:
    usb_free_urb(urb);
error_no_urb:
    mutex_unlock(&dev->write_lock);
    return retval;
}

/* Implement the missing write_room method */
static unsigned int my_tty_write_room(struct tty_struct *tty)
{
    struct my_usb_device *dev = g_dev;
    
    if (!dev)
        return 0;
    
    /* Return the maximum amount we can accept at once */
    return dev->bulk_out ? usb_endpoint_maxp(dev->bulk_out) * 16 : 0;
}

/* Add flush_buffer for completeness */
static void my_tty_flush_buffer(struct tty_struct *tty)
{
    struct my_usb_device *dev = g_dev;

    if (!dev)
        return;

    usb_kill_anchored_urbs(&dev->submitted);
}

/* Add throttle and unthrottle for flow control */
static void my_tty_throttle(struct tty_struct *tty)
{
    struct my_usb_device *dev = g_dev;

    if (!dev)
        return;

    spin_lock_irq(&dev->lock);
    dev->ongoing_read = false;
    spin_unlock_irq(&dev->lock);
}

static void my_tty_unthrottle(struct tty_struct *tty)
{
    struct my_usb_device *dev = g_dev;

    if (!dev)
        return;

    spin_lock_irq(&dev->lock);
    if (!dev->read_urb_busy) {
        dev->ongoing_read = true;
        my_usb_start_read(dev);
    } else {
        dev->ongoing_read = true;
    }
    spin_unlock_irq(&dev->lock);
}

static const struct tty_operations my_tty_ops = {
    .open = my_tty_open,
    .close = my_tty_close,
    .write = my_tty_write,
    .write_room = my_tty_write_room,     /* Added missing write_room */
    .flush_buffer = my_tty_flush_buffer, /* Added flush_buffer */
    .throttle = my_tty_throttle,         /* Added throttle */
    .unthrottle = my_tty_unthrottle,     /* Added unthrottle */
};

static int my_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    struct my_usb_device *dev;
    int i, retval = -ENOMEM;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    spin_lock_init(&dev->lock);
    mutex_init(&dev->write_lock);  /* Initialize the mutex */
    init_usb_anchor(&dev->submitted);
    tty_port_init(&dev->port);
    dev->port.ops = &my_port_ops;

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    // Debug: Print device and interface information
    pr_info(DRIVER_NAME ": Probing USB device VID=%04x, PID=%04x\n", 
            dev->udev->descriptor.idVendor, dev->udev->descriptor.idProduct);
    
    iface_desc = interface->cur_altsetting;
    pr_info(DRIVER_NAME ": Interface has %d endpoints\n", iface_desc->desc.bNumEndpoints);

    // Look for bulk endpoints
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        
        pr_info(DRIVER_NAME ": Endpoint %d: type=%02x, direction=%s, address=%02x\n", 
                i,
                endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK,
                usb_endpoint_dir_in(endpoint) ? "IN" : "OUT",
                endpoint->bEndpointAddress);

        if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
            if (usb_endpoint_dir_in(endpoint)) {
                pr_info(DRIVER_NAME ": Found bulk IN endpoint at address %02x\n", 
                        endpoint->bEndpointAddress);
                dev->bulk_in = endpoint;
            } else {
                pr_info(DRIVER_NAME ": Found bulk OUT endpoint at address %02x\n", 
                        endpoint->bEndpointAddress);
                dev->bulk_out = endpoint;
            }
        }
    }

    if (!dev->bulk_in || !dev->bulk_out) {
        pr_err(DRIVER_NAME ": Could not find bulk endpoints\n");
        goto error;
    }

    dev->bulk_in_size = usb_endpoint_maxp(dev->bulk_in);
    dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
    if (!dev->bulk_in_buffer)
        goto error;

    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb)
        goto error;

    g_dev = dev;
    usb_set_intfdata(interface, dev);

    struct device *tty_dev = tty_port_register_device(&dev->port, my_tty_driver, 0, &interface->dev);
    if (IS_ERR(tty_dev)) {
        retval = PTR_ERR(tty_dev);
        pr_err(DRIVER_NAME ": could not register tty port, error %d\n", retval);
        goto error;
    }

    pr_info(DRIVER_NAME ": USB device connected\n");
    return 0;

error:
    if (dev) {
        if (dev->bulk_in_urb)
            usb_free_urb(dev->bulk_in_urb);
        kfree(dev->bulk_in_buffer);
        tty_port_destroy(&dev->port);
        if (dev->udev)
            usb_put_dev(dev->udev);
        kfree(dev);
        g_dev = NULL;
    }
    return retval;
}

static void my_usb_disconnect(struct usb_interface *interface)
{
    struct my_usb_device *dev = usb_get_intfdata(interface);

    if (!dev)
        return;

    tty_unregister_device(my_tty_driver, 0);
    
    /* Make sure open() doesn't try to use the device anymore */
    g_dev = NULL;
    
    /* Kill any pending URBs */
    usb_kill_anchored_urbs(&dev->submitted);
    usb_kill_urb(dev->bulk_in_urb);
    
    /* Free allocated resources */
    usb_free_urb(dev->bulk_in_urb);
    kfree(dev->bulk_in_buffer);
    usb_put_dev(dev->udev);
    tty_port_destroy(&dev->port);
    kfree(dev);

    pr_info(DRIVER_NAME ": USB device disconnected\n");
}

static struct usb_driver my_usb_driver = {
    .name = DRIVER_NAME,
    .id_table = my_usb_table,
    .probe = my_usb_probe,
    .disconnect = my_usb_disconnect,
};

static int __init my_usb_init(void)
{
    int retval;

    pr_info(DRIVER_NAME ": Initializing USB TTY driver\n");

    my_tty_driver = tty_alloc_driver(MY_TTY_MINORS, 
                                 TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
    if (IS_ERR(my_tty_driver))
        return PTR_ERR(my_tty_driver);

    my_tty_driver->driver_name = DRIVER_NAME;
    my_tty_driver->name = "ttyMYUSB";
    my_tty_driver->major = MY_TTY_MAJOR;
    my_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
    my_tty_driver->subtype = SERIAL_TYPE_NORMAL;
    my_tty_driver->init_termios = tty_std_termios;
    my_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
    my_tty_driver->init_termios.c_iflag = IGNPAR | ICRNL;
    my_tty_driver->init_termios.c_oflag = 0;
    my_tty_driver->init_termios.c_lflag = 0;
    my_tty_driver->init_termios.c_lflag &= ~ECHO;

    tty_set_operations(my_tty_driver, &my_tty_ops);

    retval = tty_register_driver(my_tty_driver);
    if (retval) {
        pr_err(DRIVER_NAME ": failed to register tty driver\n");
        tty_driver_kref_put(my_tty_driver);
        return retval;
    }

    retval = usb_register(&my_usb_driver);
    if (retval) {
        pr_err(DRIVER_NAME ": failed to register USB driver\n");
        tty_unregister_driver(my_tty_driver);
        tty_driver_kref_put(my_tty_driver);
        return retval;
    }

    pr_info(DRIVER_NAME ": USB TTY driver initialized successfully\n");
    return 0;
}

static void __exit my_usb_exit(void)
{
    usb_deregister(&my_usb_driver);
    tty_unregister_driver(my_tty_driver);
    tty_driver_kref_put(my_tty_driver);
}

module_init(my_usb_init);
module_exit(my_usb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB TTY driver for Raspberry Pi 4B communication");
