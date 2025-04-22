#include<linux/init.h>
#include<linux/module.h>
#include<linux/fs.h>
#include<linux/uaccess.h>
#include<linux/tty.h>
#include<linux/serial.h>
#include<linux/delay.h>
#include<linux/tty_driver.h>
#include<linux/usb.h>
#include<linux/cdev.h>
#include<linux/kernel.h>
#include<linux/kprobes.h>
#include <linux/kallsyms.h>

#include "klookuper/lookuper.h"

MODULE_LICENSE("Dual BSD/GPL");
//EXPORT_SYMBOL(acm_write_done);


static int kprobe_init(void);
static void kprobe_exit(void);

static struct kprobe kp_open;
static struct kprobe kp_read;
static struct kprobe kp_write;
static struct kprobe kp_close;

static int pre_handler_open(struct kprobe *kp, struct pt_regs *regs)
{
  pr_info("cdc_acm: open function interrupt");
  return 0;
}

static int pre_handler_read(struct kprobe *kp, struct pt_regs *regs)
{
  pr_info("cdc_acm: read function interrupt");
  return 0;
}

static int pre_handler_write(struct kprobe *kp, struct pt_regs *regs)
{
  pr_info("cdc_acm: write function interrupt");
  return 0;
}

static int pre_handler_close(struct kprobe *kp, struct pt_regs *regs)
{
  pr_info("cdc_acm: close function interrupt");
  return 0;
}

static int __init kprobe_init(void)
{
  pr_info("cdc_acm: kprobe register");
  int retval;
  size_t addr;
//  unsigned long addr;
  //  open
  kp_open.symbol_name = "acm_tty_open";
  kp_open.pre_handler = pre_handler_open;
  retval = register_kprobe(&kp_open);
  if(retval < 0) {
    pr_err("Kprobe registration for open function failed. Error number: %d", retval);
    return retval;
  }

  //  read
  //kp_read.symbol_name = "acm_submit_read_urb";
#ifdef CONFIG_KALLSYMS
  retval = kallsyms_addr_lookup("acm_read_bulk_callback", &addr, NULL, NULL);
  if (addr == 0) {
      pr_err("Failed to find acm_read_bulk_callback symbol address");
      return -EINVAL;
  }
  kp_read.addr = (void*)addr;//0xffffffffc32ea9a0;
  kp_read.pre_handler = pre_handler_read;
  retval = register_kprobe(&kp_read);
  if(retval < 0) {
    pr_err("Kprobe registration for open function failed. Error number: %d", retval);
    unregister_kprobe(&kp_open);
    return retval;
  }
#else
  pr_err("kallsyms_lookup_name is not available in this kernel configuration");
#endif
  //  write
  kp_write.symbol_name = "acm_tty_write";
  kp_write.pre_handler = pre_handler_write;
  retval = register_kprobe(&kp_write);
  if(retval < 0) {
    pr_err("Kprobe registration for write function failed. Error number: %d", retval);
    unregister_kprobe(&kp_open);
    unregister_kprobe(&kp_read);
    return retval;
  }

  //  close
  kp_close.symbol_name = "acm_tty_close";
  kp_close.pre_handler = pre_handler_close;
  retval = register_kprobe(&kp_close);
  if(retval < 0) {
    pr_err("Kprobe registration for close function failed. Error number: %d", retval);
    unregister_kprobe(&kp_open);
    unregister_kprobe(&kp_read);
    unregister_kprobe(&kp_write);
    return retval;
  }

  return 0;
}

static void __exit kprobe_exit(void)
{
  pr_info("cdc_acm: krpobe unregister");
  unregister_kprobe(&kp_open);
  unregister_kprobe(&kp_read);
  unregister_kprobe(&kp_write);
  unregister_kprobe(&kp_close);
}

module_init(kprobe_init);
module_exit(kprobe_exit);
