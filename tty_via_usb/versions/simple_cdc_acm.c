#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/uaccess.h>
#include <linux/serial_core.h>
#include <linux/fs.h>

#define USB_VENDOR_ID 0x0525
#define USB_PRODUCT_ID 0xa4a7



#define PRINT_USB_INTERFACE_DESCRIPTOR( i )                         \
{                                                                   \
    pr_info("USB_INTERFACE_DESCRIPTOR:\n");                         \
    pr_info("-----------------------------\n");                     \
    pr_info("bLength: 0x%x\n", i.bLength);                          \
    pr_info("bDescriptorType: 0x%x\n", i.bDescriptorType);          \
    pr_info("bInterfaceNumber: 0x%x\n", i.bInterfaceNumber);        \
    pr_info("bAlternateSetting: 0x%x\n", i.bAlternateSetting);      \
    pr_info("bNumEndpoints: 0x%x\n", i.bNumEndpoints);              \
    pr_info("bInterfaceClass: 0x%x\n", i.bInterfaceClass);          \
    pr_info("bInterfaceSubClass: 0x%x\n", i.bInterfaceSubClass);    \
    pr_info("bInterfaceProtocol: 0x%x\n", i.bInterfaceProtocol);    \
    pr_info("iInterface: 0x%x\n", i.iInterface);                    \
    pr_info("\n");                                                  \
}
#define PRINT_USB_ENDPOINT_DESCRIPTOR( e )                          \
{                                                                   \
    pr_info("USB_ENDPOINT_DESCRIPTOR:\n");                          \
    pr_info("------------------------\n");                          \
    pr_info("bLength: 0x%x\n", e.bLength);                          \
    pr_info("bDescriptorType: 0x%x\n", e.bDescriptorType);          \
    pr_info("bEndPointAddress: 0x%x\n", e.bEndpointAddress);        \
    pr_info("bmAttributes: 0x%x\n", e.bmAttributes);                \
    pr_info("wMaxPacketSize: 0x%x\n", e.wMaxPacketSize);            \
    pr_info("bInterval: 0x%x\n", e.bInterval);                      \
    pr_info("\n");                                                  \
}

static struct tty_driver *tty_driver;


static int pi_tty_open(struct tty_struct *tty, struct file *file);
static void pi_tty_close(struct tty_struct *tty, struct file *file);

static int pi_usb_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void pi_usb_disconnect(struct usb_interface *interface);

const struct usb_device_id usb_device_table[] = {
  { USB_DEVICE( USB_VENDOR_ID, USB_PRODUCT_ID) },
  {}
};

MODULE_DEVICE_TABLE(usb, usb_device_table);

static struct usb_driver usb_device_driver = {
  .name = "pi_usb_serial_driver",
  .id_table = usb_device_table,
  .probe = pi_usb_probe,
  .disconnect = pi_usb_disconnect
};

static const struct tty_operations tty_ops = {
  .open = pi_tty_open,
  .close = pi_tty_close
};

static int pi_tty_open(struct tty_struct *tty, struct file *file)
{
  pr_info("Custom USB TTY: Open\n");
  return 0;
}

static void pi_tty_close(struct tty_struct *tty, struct file *file)
{
  pr_info("Custom USB TTY: Close\n");
}

static int pi_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
  unsigned int i;
  unsigned int endpoints_count;
  struct usb_host_interface *iface_desc = interface->cur_altsetting;
  dev_info(&interface->dev, "USB Driver Probed: Vendor ID : 0x%02x,\t"
           "Product ID : 0x%02x\n", id->idVendor, id->idProduct);

  endpoints_count = iface_desc->desc.bNumEndpoints;
  PRINT_USB_INTERFACE_DESCRIPTOR(iface_desc->desc);

  for ( i = 0; i < endpoints_count; i++ ) {
       PRINT_USB_ENDPOINT_DESCRIPTOR(iface_desc->endpoint[i].desc);
  }

  return 0;
}

static void pi_usb_disconnect(struct usb_interface *interface)
{
  pr_info("USB device disconnect\n");
}

static int __init usb_init(void)
{
  int result;

  tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW );  //
  if(IS_ERR(tty_driver)) {
    pr_err("Failed to initialize TTY driver!\n");
    return PTR_ERR(tty_driver);
  }

  tty_driver->owner=THIS_MODULE;
  tty_driver->driver_name="Custom TTY driver";
  tty_driver->name="TTY_cdc";
  tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
  tty_driver->major = 227;
  tty_driver->minor_start = 0;
  tty_driver->init_termios = tty_std_termios;
  tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
//  tty_driver->init_termios.c_iflag &= ~BRKINT & ~IMAXBEL;
//  tty_driver->init_termios.c_lflag &= ~ECHO;
  tty_set_operations(tty_driver, &tty_ops);

  result = tty_register_driver(tty_driver);
  if(result) {
    pr_err("Failed to register TTY driver! Error number: %d\n", result);
    tty_driver_kref_put(tty_driver);
    return result;
  }
  pr_info("TTY driver registered succesfully!");

  result = usb_register(&usb_device_driver);
  if (result) {
    pr_err("Failed to initialize USB driver! Exiting with error number: %d", result);
    tty_unregister_driver(tty_driver);
    tty_driver_kref_put(tty_driver);
    return result;
  }
  pr_info("USB driver registered successfully!");
  
  return result;
}

static void __exit usb_exit(void)
{
  tty_unregister_driver(tty_driver);
  tty_driver_kref_put(tty_driver);
  usb_deregister(&usb_device_driver);
}

module_init(usb_init);
module_exit(usb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB Driver with TTY protocol for serial communication");
