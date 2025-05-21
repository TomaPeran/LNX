#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/usb/serial.h>
#include <linux/tty_port.h>

#define DRIVER_NAME "my_usb_serial"
#define MY_TTY_MAJOR 240
#define MY_TTY_MINORS 1
#define VENDOR_ID  0x0525  // Replace with your actual VID
#define PRODUCT_ID 0xa4a7  // Replace with your actual PID

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
    struct tty_port tty_port;
    struct tty_struct *tty;
    spinlock_t lock;
};

static struct tty_driver *my_tty_driver;
static struct usb_driver my_usb_driver;  // Declare the usb_driver earlier

static void my_usb_read_bulk_callback(struct urb *urb)
{
    struct my_usb_device *dev = urb->context;

    if (urb->status == 0 && urb->actual_length > 0) {
        tty_insert_flip_string(&dev->tty_port, urb->transfer_buffer, urb->actual_length);
        tty_flip_buffer_push(&dev->tty_port);
    }

    usb_submit_urb(urb, GFP_ATOMIC);
}

static int my_tty_open(struct tty_struct *tty, struct file *file)
{
    struct my_usb_device *dev;
    struct usb_interface *interface;

    // Retrieve the interface from tty->index (you only support one minor = index 0)
    interface = usb_find_interface(&my_usb_driver, tty->index);
    if (!interface) {
        pr_err(DRIVER_NAME ": Could not find USB interface for tty index %d\n", tty->index);
        return -ENODEV;
    }

    dev = usb_get_intfdata(interface);
    if (!dev) {
        pr_err(DRIVER_NAME ": Device data not available\n");
        return -ENODEV;
    }

    tty->driver_data = dev;
    dev->tty = tty;
    return tty_port_open(&dev->tty_port, tty, file);
}

static void my_tty_close(struct tty_struct *tty, struct file *file)
{
    struct my_usb_device *dev = tty->driver_data;
    if (dev) {
        tty_port_close(&dev->tty_port, tty, file);
        dev->tty = NULL;
    }
}

static ssize_t my_tty_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
    struct my_usb_device *dev = tty->driver_data;
    struct urb *urb;
    unsigned char *buffer;
    int retval;

    if (!dev || !dev->udev)
        return -ENODEV;

    buffer = kmemdup(buf, count, GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb) {
        kfree(buffer);
        return -ENOMEM;
    }

    usb_fill_bulk_urb(urb,
                      dev->udev,
                      usb_sndbulkpipe(dev->udev, dev->bulk_out->bEndpointAddress),
                      buffer,
                      count,
                      NULL,
                      buffer);

    usb_anchor_urb(urb, &dev->submitted);
    retval = usb_submit_urb(urb, GFP_KERNEL);
    if (retval)
        usb_unanchor_urb(urb);
    else
        retval = count;

    usb_free_urb(urb);
    return retval;
}

static const struct tty_operations my_tty_ops = {
    .open = my_tty_open,
    .close = my_tty_close,
    .write = my_tty_write,
};

static int my_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct my_usb_device *dev;
    int i;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;
    usb_set_intfdata(interface, dev);
    spin_lock_init(&dev->lock);
    init_usb_anchor(&dev->submitted);

    iface_desc = interface->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        struct usb_endpoint_descriptor *ep = &iface_desc->endpoint[i].desc;

        if (usb_endpoint_is_bulk_in(ep)) {
            pr_info(DRIVER_NAME ": Found bulk IN endpoint\n");
            dev->bulk_in = ep;
        }
        else if (usb_endpoint_is_bulk_out(ep)) {
            pr_info(DRIVER_NAME ": Found bulk OUT endpoint\n");
            dev->bulk_out = ep;
        }
    }

    if (!dev->bulk_in || !dev->bulk_out) {
        pr_err(DRIVER_NAME ": Could not find bulk endpoints\n");
        kfree(dev);
        return -ENODEV;
    }

    dev->bulk_in_size = usb_endpoint_maxp(dev->bulk_in);
    dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
    if (!dev->bulk_in_buffer) {
        kfree(dev);
        return -ENOMEM;
    }

    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb) {
        kfree(dev->bulk_in_buffer);
        kfree(dev);
        return -ENOMEM;
    }

    usb_fill_bulk_urb(dev->bulk_in_urb,
                      dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in->bEndpointAddress),
                      dev->bulk_in_buffer,
                      dev->bulk_in_size,
                      my_usb_read_bulk_callback,
                      dev);

    usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);

    tty_port_init(&dev->tty_port);
    int retval = tty_port_register_device(&dev->tty_port, my_tty_driver, 0, &interface->dev);

    if(retval < 0) {
      pr_err(DRIVER_NAME ": couldn't register tty port\n");
      return retval;
    }
    pr_info(DRIVER_NAME ": USB device connected\n");
    return 0;
}

static void my_usb_disconnect(struct usb_interface *interface)
{
    struct my_usb_device *dev = usb_get_intfdata(interface);

    usb_kill_urb(dev->bulk_in_urb);
    usb_free_urb(dev->bulk_in_urb);
    kfree(dev->bulk_in_buffer);

    tty_port_unregister_device(&dev->tty_port, my_tty_driver, 0);
    tty_port_destroy(&dev->tty_port);

    usb_put_dev(dev->udev);
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
    int ret;

    // Allocate the tty driver with minor count
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

    // Set terminal configuration for serial communication
    my_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL; // 9600 baud, 8 data bits, enable receiver, local mode
    my_tty_driver->init_termios.c_iflag = IGNPAR | ICRNL; // Ignore parity errors, map CR to NL
    my_tty_driver->init_termios.c_oflag = 0; // No output processing
    my_tty_driver->init_termios.c_lflag = 0; // Raw mode (no line processing)
    my_tty_driver->init_termios.c_cc[VTIME] = 0;  // No timeout
    my_tty_driver->init_termios.c_cc[VMIN] = 1;   // At least one character

    // Disable echo
    my_tty_driver->init_termios.c_lflag &= ~ECHO; // Disable echo flag

    // Set up tty operations
    tty_set_operations(my_tty_driver, &my_tty_ops);

    ret = tty_register_driver(my_tty_driver);
    if (ret) {
        pr_err(DRIVER_NAME ": couldn't register tty driver\n");
        tty_driver_kref_put(my_tty_driver);  // Use kref_put instead of put_tty_driver
        return ret;
    }

    // Register the USB driver
    return usb_register(&my_usb_driver);
}

static void __exit my_usb_exit(void)
{
    usb_deregister(&my_usb_driver);
    tty_unregister_driver(my_tty_driver);
    tty_driver_kref_put(my_tty_driver);  // Use kref_put instead of put_tty_driver
}

module_init(my_usb_init);
module_exit(my_usb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Minimal USB TTY driver with bulk URB support, echo disabled");

