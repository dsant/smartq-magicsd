/********************************************************************
* $ID: cmd_hhtech.c     ��, 26 10�� 2007 15:13:25 +0800  whg        *
*                                                                   *
* Description:                                                      *
*                                                                   *
* Maintainer:  ����(WangGang)  <wanggang@hhcn.com>                  *
*                                                                   *
* CopyRight (c)  2007  HHTech                                       *
*   www.hhcn.com, www.hhcn.org                                      *
*   All rights reserved.                                            *
*                                                                   *
* This file is free software;                                       *
*   you are free to modify and/or redistribute it                   *
*   under the terms of the GNU General Public Licence (GPL).        *
*                                                                   *
* Last modified: Wed, 06 May 2009 13:52:44 +0800       by root #
*                                                                   *
* No warranty, no liability, use this at your own risk!             *
********************************************************************/
#include <common.h>
#include <command.h>
#include <s3c6410.h>
#include "hhtech.h"
#include "asm/mach-types.h"

//#define PASSWD
#ifdef PASSWD
#include "lib_Data.h"
#include "lib_Crypto.h"
#endif

#ifdef CONFIG_HHTECH_MINIPMP

#if 1
  #define  PFUNC(fmt, args...) \
	do{	printf("< Enter In:%s():%4d> ", __FUNCTION__, __LINE__); \
		printf(fmt, ##args); \
	}while(0)
#else
  #define PFUNC(fmt, args...)
#endif				// PRINT_FUNC


#define INAND_RW_DIRECT    1  // write inand block, no filesystem
  #define INAND_DEV           (1)
  #define INAND_MAX_READ_BLK  (16<<11) // kernel and initramfs max size
  #define INAND_BLOCK_SIZE    (512)
  #define INAND_KERNEL0_BEND  (( 8<<20)/INAND_BLOCK_SIZE) // 8M*2 block from end
  #define INAND_KERNEL1_BEND  ((16<<20)/INAND_BLOCK_SIZE)

static const char Q5[] =                "/SmartQ5";
static const char Q7[] =                "/SmartQ7";
static char *firmware = Q7;

static int machType = MACH_TYPE_SMARTQ7;

#define UBOOTNAME               "/u-boot.bin"
#ifdef CONFIG_ENABLE_MMU
  #define MEM_KERNEL_START        0xC0008000	/* read card file to here */
  #define MEM_INITRAMFS           0xC1000000

  #define MEM_READ_FILE           0xC4008000	/* read card file to here */
  #define MEM_RDTEST              0xC2000000      /* read you write to spiflash */

#else
  #define MEM_KERNEL_START        0x50008000	/* read card file to here */
  #define MEM_INITRAMFS           0x51000000

  #define MEM_READ_FILE           0x54008000	/* read card file to here */
  #define MEM_RDTEST              0x52000000      /* read you write to spiflash */
#endif

#define ENCRYPT_OFFSET      	0x10000		/* firm is encrypted */
#define ENCRYPT_LEN         	0x1000		/* firm encrypt size */

#define DEBUG
#undef	DEBUG

#if defined(CONFIG_NAND)
#include <nand.h>
extern int nand_curr_device;
extern nand_info_t nand_info[];
static int nand_read_to_mem(u32 mem_addr, u32 offset, u32 size);
static int nand_write_from_mem(u32 mem_addr, u32 offset, u32 size);

#elif defined(CONFIG_BOOT_NOR)
#include <flash.h>
extern flash_info_t flash_info[CFG_MAX_FLASH_BANKS];
extern int read_buff (flash_info_t * info, uchar *dst, ulong addr, ulong cnt);
extern int write_buff (flash_info_t * info, uchar * src, ulong addr, ulong cnt);
#endif

extern int do_fat_fsload (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
extern int do_bootm (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);

static void key_init(void);
static u32 key_read(void);
static int boot_image(u32 addr, u32 addr2);
static int load_sd_file(int dev, char *file, u32 addr, int size);
static int load_nand_image(u32 mem_addr);
static int load_boot_kernel(int flag, int param);

static int key_factory = 0;

#ifdef PASSWD
static void mdelay(int ms_count)
{
	udelay(ms_count * 1000);
}

static void set_value_8(unsigned int value1, unsigned int value2, uchar val[])
{
	val[0] = (uchar)(value1 >> 24); val[1] = (uchar)(value1 >> 16);
	val[2] = (uchar)(value1 >> 8); val[3] = (uchar)(value1);
	val[4] = (uchar)(value2 >> 24); val[5] = (uchar)(value2 >> 16);
	val[6] = (uchar)(value2 >> 8); val[7] = (uchar)(value2);
}
#endif

static int file_check_sum(void *addr, int len)
{
    unsigned long check = 0, i, ret = -1;
    unsigned long *pp = (unsigned long*)addr;

    if(len & 0x3 != 0)        {
		printf("u-boot.ldr or firmware.bin size error(%d)\n", len);
		return -1;
    }

    for(i = 0; i < ((len/4)-1); i++)
		check += pp[i];

    if(check == pp[i])
		ret = 0;

    printf("Check Sum %s:len=%d, check=0x%x : 0x%x\n", ((ret) ? "Fail" : "SUCESS"),
	    len, check, pp[i]);
    return ret;
}

#define GPIO_DCIN   (('L'-'A')*16 + 13)
#define GPIO_GPN13   (('N'-'A')*16 + 13)
#define GPIO_GPN14   (('N'-'A')*16 + 14)

unsigned get_mach_type(void)
{
   return machType;
}

static int get_dc_status(void)
{
    if (machType == MACH_TYPE_SMARTQ5)
       return gpio_get_value(GPIO_DCIN);
    else
       return !gpio_get_value(GPIO_DCIN);

}

// LED is on GPN8 and GPN9 (on Q5, reverse on Q7)
#define GPIO_LED_0  (('N'-'A')*16 + 8)
#define GPIO_LED_1  (('N'-'A')*16 + 9)

static int set_led(int flag)
{
    flag = flag & 0x3;

    unsigned led0 = (machType == MACH_TYPE_SMARTQ5) ? GPIO_LED_0 : GPIO_LED_1;
    unsigned led1 = (machType == MACH_TYPE_SMARTQ5) ? GPIO_LED_1 : GPIO_LED_0;

    if(flag & 0x01) gpio_direction_output(led0, 0);
    else            gpio_direction_output(led0, 1);

    if(flag & 0x02) gpio_direction_output(led1, 0);
    else            gpio_direction_output(led1, 1);

    return 0;
}

// LCD backlight on or off
#define GPIO_LCD_BACKLIGHT  (('F'-'A')*16 + 15)
static int set_lcd_backlight(int flag)
{
    if(flag)
	gpio_direction_output(GPIO_LCD_BACKLIGHT, 1);
    else
	gpio_direction_output(GPIO_LCD_BACKLIGHT, 0);

    return 0;
}


// high poweroff, low start
#define GPIO_POWEROFF (('K'-'A')*16 + 15)
static int do_poweroff(u32 flag)
{
//	int usb = 0, key;
	PFUNC("flag=0x%x\n", flag);
#ifdef DEBUG
	set_led(0); udelay(0x300000);
	set_led(1); udelay(0x300000);
	set_led(2); udelay(0x300000);
	set_led(3); udelay(0x300000);
#endif
	set_led(0);

	gpio_direction_output(GPIO_POWEROFF, 1);

	return 0;
}

// start firmware error, this is check by WatchDog
static int do_firmware_error(int flag, int param)
{
#if 0     /* comment by whg HHTECH */
	if(1 != card_plugin()) {	// No Card
		PFUNC("Card Not Found\n");
		show_bitmap(B_NOSD);
		rtc_delay(3);
		do_poweroff(0x8FFF);
	}
	
	if(card_usb_plug_out()) {	// Card + USB connect
		PFUNC("Usb Found\n");
		show_bitmap(B_USB);
		do_card_connect_pc(0, 0);
		udelay(1000); do_poweroff(0x8FFF);
	}
	
	PFUNC("error\n");
	show_bitmap(B_ERROR);
	rtc_delay(3);
#endif    /* comment by WangGang   */
	do_poweroff(0x8FFF);
	return 0;
}

#ifdef INAND_RW_DIRECT
#include <part.h>
#define  HEAD_MAGIC     0x39000032 // 2009

/* hardcoded pro-tem */
#include "/usr/src/smartQ/smartq-initramfs/fw-utils/firmware_header.h"

extern block_dev_desc_t * mmc_get_dev(int dev);
extern void mmc_release_dev(int dev);

static int do_read_inand(int dev, ulong offset_end_inand, int flag)
{
    ulong blk_addr, blk_num;
    firmware_fileheader  *fh = (firmware_fileheader*)MEM_READ_FILE;
    block_dev_desc_t *desc = NULL;
    
    if(!(desc = mmc_get_dev(dev))) return -1;

    blk_addr = desc->lba - offset_end_inand;
    memset(fh, 0, sizeof(firmware_fileheader));
    desc->block_read(dev, blk_addr, 1, (ulong*)fh);
    PFUNC("vendor = %s\n", fh->vendor);
    
    if(fh->magic != HEAD_MAGIC) {
	printf("magic error:0x%x\n", fh->magic);
	return -2;
    }

    // kernel
    blk_addr = desc->lba - offset_end_inand + fh->zimage.nand.offset;
    blk_num = (fh->zimage.nand.size / INAND_BLOCK_SIZE) + 1;
    if(blk_num > (16<<11)) return -3; // mux is 16M
    desc->block_read(dev, blk_addr, blk_num, (ulong*)MEM_KERNEL_START);

    if(flag) { // initramfs
	blk_addr = desc->lba - offset_end_inand + fh->initramfs.nand.offset;
	blk_num = (fh->initramfs.nand.size / INAND_BLOCK_SIZE) + 1;
	if(blk_num > (16<<11)) return -4; // mux is 16M
	desc->block_read(dev, blk_addr, blk_num, (ulong*)MEM_INITRAMFS);
    }

    return 0;
}

static int do_readsd_upgrade(int dev, char *file)
{
    int size;
    unsigned char *ptr, *ptrsrc;
    firmware_fileheader  *fh = (firmware_fileheader*)MEM_READ_FILE;

#ifdef DEBUG
    set_led(3);
#endif
    memset(fh, 0, sizeof(firmware_fileheader));
    size = load_sd_file(dev, file, (u32)fh, INAND_BLOCK_SIZE);

    PFUNC("vendor = %s\n", fh->vendor);
    if(size > 0 && fh->magic != HEAD_MAGIC) {
	printf("magic error\n");
	return -2;
    }

    if (fh->machType == 0)  /* older software */
      fh->machType = MACH_TYPE_SMDK6410;
    else
      machType = fh->machType; /* make a file-local copy */

    if (fh->machType == MACH_TYPE_SMARTQ5)
      firmware = Q5;

    if(fh->initramfs.file.offset > fh->zimage.file.offset) 
	size = fh->initramfs.file.offset + fh->initramfs.file.size;
    else
	size = fh->zimage.file.offset + fh->zimage.file.size;
    size = load_sd_file(dev, file, (u32)fh, size);
    if(size > 0 && fh->magic != HEAD_MAGIC) {
	printf("magic error\n");
	return -2;
    }

    ptrsrc = (unsigned char *)fh;
    ptr = (unsigned char *)MEM_KERNEL_START;
    memcpy(ptr, (ptrsrc + fh->zimage.file.offset), fh->zimage.file.size);
    ptr = (unsigned char *)MEM_INITRAMFS;
    memcpy(ptr, (ptrsrc + fh->initramfs.file.offset), fh->initramfs.file.size);

    boot_image(MEM_KERNEL_START, MEM_INITRAMFS);

    return 0;
}
#endif

int do_start_firmware(int flag, char *file)
{
    int ret = 0;
    char *s;

    if((s = getenv("stop")) && s[0] == 'y') {
	printf("Get env <stop=%s>\n", s);
	return 0;
    }

#ifdef INAND_RW_DIRECT
#ifdef DEBUG
    set_led(2);
#endif
    if((do_read_inand(INAND_DEV, INAND_KERNEL1_BEND, 0)) 
	|| boot_image(MEM_KERNEL_START, 0)) {
	printf("Error for iNAND START 1\n");

	if((do_read_inand(INAND_DEV, INAND_KERNEL0_BEND, 0)) 
	    || boot_image(MEM_KERNEL_START, 0)) {
	    printf("Error for iNAND START 2\n");
	    ret = -1;
	}
    }

    if(flag) {
	printf("\nLoad upgrade image from SD(%s)\n", firmware);
	do_readsd_upgrade(0, firmware);
    }

#else
  #if defined(CONFIG_NAND)
    printf("Load firm from Nand\n");
    ret = load_nand_image(MEM_READ_FILE);
    if(ret >= 0 ) {
	ret = boot_image(MEM_READ_FILE, 0);
    }

  #elif defined(CONFIG_BOOT_NOR)
    printf("Load firm from SD file\n");
    if(!file)	file = firmware;
    ret = load_sd_file(file, MEM_READ_FILE, 0);
    if(ret >= 0 ) {
	ret = boot_image(MEM_READ_FILE, 0);
    }

  #endif

#endif  // INAND_RW_DIRECT

    if(ret) do_firmware_error(0, 0);

    return ret;
}

static int do_upgrade(int flag, int param)
{
#ifdef INAND_RW_DIRECT
    if(0 == flag) goto up_from_sd;

#ifdef DEBUG
    set_led(2);
#endif
    if((do_read_inand(INAND_DEV, INAND_KERNEL1_BEND, 1)) 
	|| boot_image(MEM_KERNEL_START, MEM_INITRAMFS)) {
	printf("Error iNAND START 1\n");

	if((do_read_inand(INAND_DEV, INAND_KERNEL0_BEND, 1)) 
	    || boot_image(MEM_KERNEL_START, MEM_INITRAMFS)) {
	    printf("Error iNAND START 2\n");
	}
    }

up_from_sd:
    printf("Load upgrade IMG from SD(%s)\n", firmware);
    do_readsd_upgrade(0, firmware);

#else
    load_boot_kernel(0, 0);
    boot_image(MEM_READ_FILE, 0);
#endif
    return 0;
}

#define BOOT_NAND_OFFSET       (0x0000000)
static int do_boot_upgrade(int flag, int param)
{
    int size = 0, err = 0, sects;

    size = load_sd_file(param, UBOOTNAME, MEM_READ_FILE, 0);
    if( size < 0) {
    	printf("Load file(%s) error\n", UBOOTNAME);
		return -1;
	}

	if(file_check_sum((u8*)MEM_READ_FILE, size) != 0) return -1;

next:
#if defined(CONFIG_BOOT_NAND)
	nand_write_from_mem(MEM_READ_FILE, BOOT_NAND_OFFSET, size);
	nand_read_to_mem(MEM_RDTEST, BOOT_NAND_OFFSET, size);

#elif defined(CONFIG_BOOT_NOR)
	sects = (size < 0x10000) ? 4 : ((size / 0x10000) + 4);
	// erase it
	flash_erase(&flash_info[0], 0, sects);
	// write to no flash
	write_buff(&flash_info[0], MEM_READ_FILE, 0, size);
	// read for checksum
	read_buff(&flash_info[0], MEM_RDTEST, 0, size);
#endif
	
	if(file_check_sum((u8*)MEM_RDTEST, size) == 0) goto out;
	if( err++ < 3) goto next;

out:

    return 0;
}


static int press_key(void)
{
    u32 key = key_read();
    u32 start_tick = get_ticks(), cur_tick = 0;

ReRead:
    key_factory = 0;
    key = key_read();

#ifdef PASSWD
    unsigned char a[8],b[8];
    unsigned long int i_NC, i_C2, i_G2;
    unsigned char buf[20];
    unsigned char obuf[2];
    int i, ret;

    buf[0]=0x55;
    buf[1]=0xaa;

    ll_PowerOn();
    mdelay(20);

    if(SUCCESS != cm_Init()) printf("err init cm\n");
    ret = cm_WriteConfigZone(0xb,0xa,buf,2,0x0);
    if(ret!=SUCCESS) {printf("err write to mtz,return 0x%x\n",ret);
	return -1;
    }
    mdelay(20);
    ret = cm_ReadConfigZone(0xb,0xa,obuf,2);
    if(ret!=SUCCESS) {printf("err read from mtz,return 0x%x\n",ret);
	printf("read mtz 0x%x 0x%x\n",obuf[0],obuf[1]);
	return -1;
    }
    if((obuf[0]!=0x55)||(obuf[1]!=0xaa)) return -1;
    mdelay(20);
    printf("mtz test success\n");

    /* read NC */
    ret = cm_ReadConfigZone(0xb,0x19,buf,7);
    if(ret!=SUCCESS) {
	printf("err read from NC,return 0x%x\n",ret);
	return -1;
    } else {
	printf("NC is: ");
	for(i = 0; i < 7; i++)
	    printf("%02x", buf[i]);
	printf("\n");
    }
    i_NC = (unsigned int)(buf[0] << 24) | (unsigned int)(buf[1] << 16) | (unsigned int)(buf[2] << 8) | (unsigned int)buf[3]; 
    printf("i_NC = %x\n", i_NC);

    i_G2 = 2 * i_NC + 100;
    printf("i_G2 = %0x\n", i_G2);
    set_value_8(i_G2, i_G2, G2);
    printf("G2 is: ");
    for(i = 0; i < 8; i++)
	printf("%02x", G2[i]);
    printf("\n");

    mdelay(20);
    for(i=0;i<8;i++){
	a[i] = G2[i];
	b[i] = G2[i];
    }
    ret = cm_VerifyCrypto(0xb, 2, a, NULL, 0x0);
    if (ret != 0x00) {printf("fail 0x%x\n",ret);
	return -1;
    }
    ret = cm_VerifyCrypto(0xb, 2, b, NULL, 0x1);
    if (ret != 0x00) {printf("fail 2 0x%x\n",ret);
	return -1;
    }
#endif

    switch(key)	{
	case KEY_UPGRADE: // upgrade from SD
	case KEY_UPGRADE3:
	    set_led(3);
	    do_upgrade(0, 0);
	    do_poweroff(0x8FFF);
	    key = 0;
	    break;
	case KEY_UPGRADE2:  // upgrade from INAND
	    set_led(3);
	    do_upgrade(1, 0);
	    do_poweroff(0x8FFF);
	    key = 0;
	    break;
	case KEY_FACTORY:
	    key_factory = 1;
	    set_led(1);
	    break;
	case KEY_POWER_ON:
	    set_led(1);
	    key = 1;
	    break;
	case KEY_NONE:	// no key
	    key = 0; 
	    if(0 == get_dc_status()) {
		PFUNC("other key and no usb,poweroff\n");
		do_poweroff(0x8FFF);
	    }
	    set_led(1);
	    break;
	default:	// muilt key perssed
	    if(key & KEY_LOCK) {
		rtc_delay(3);
		do_poweroff(0x8FFF);
	    }
	    else {
		do_poweroff(0x8FFF);
	    }
	    key = 0;
	    break;
    }

    cur_tick = get_ticks();
    if(cur_tick - start_tick < 1000)	// delay 1 sec for press error
	goto ReRead;

    return key;
}

#if 0     /* comment by whg HHTECH */
static int check_battery(void)
{
	int val = 0, loops = 0;
	
	printf("check battery\n");
	val = get_battery_cap();
	if( val >= POWEROFF_BAT) return 0;	//not Low Battery

ReCheckUsb:		// some usb not be checked
	val = card_usb_plug_out();
	if(1 == val) return 1; 		// Connect USB
	udelay(400); if(++loops < 30) goto ReCheckUsb;
	
	// LowBattery
	printf("Low Bat and No Usb(%d), Poweroff\n", val);
	set_led_blink(3, 50000);

	set_led(0);
	do_poweroff(0x8000);
	return -1;
}
#endif    /* comment by WangGang   */

static void set_boot_env(int pow_key, int flag)
{
    char *s1, *s2, *s3, arg[256];

    s1 = getenv("bootargs");
    s2 = getenv("mem");
    s3 = getenv("max_mem");

    memset(arg, 0, sizeof(arg));
#ifdef CONFIG_TVOUT_NONE
    sprintf(arg, "%s powkey=%d mem=%s max_mem=%s tvout=no", 
	    s1, pow_key, s2, s3);
#else
    sprintf(arg, "%s powkey=%d mem=%s max_mem=%s", 
	    s1, pow_key, s2, s3);
#endif

    setenv("bootargs", arg);
}

static int battery_probe(void)
{
    S3C2410_ADC * const adc = S3C2410_GetBase_ADC();
    S3C64XX_GPIO * const gpio = S3C64XX_GetBase_GPIO();
    unsigned int i, val, data0, powerflag;

    if(get_dc_status()) {
	printf("dc on, don't need check battery!\n");
	return 0;
    }

    adc->ADCDLY = 50000 & 0xffff;
    val = S3C6410_ADCCON_RESSEL_12BIT | S3C2410_ADCCON_PRSCEN | S3C2410_ADCCON_PRSCVL(49);
    adc->ADCCON = val | S3C2410_ADCCON_SELMUX(0);   // using AN0
    udelay(10);

    for(i=0; i<5; i++){
	adc->ADCCON |= S3C2410_ADCCON_ENABLE_START; // start ADC conversion
	do {
	    val = adc->ADCCON;
	} while(!(val & S3C2410_ADCCON_ECFLG));	// wait for the end of ADC conversion

	// read data
	// ref voltage:2.4V, battery max:4.2V, battery low:3.45V
	data0 = adc->ADCDAT0 & S3C_ADCDAT0_XPDATA_MASK_12BIT;
	if(data0 < 2192)	// (1525 * 345) / 240 = 2192
	    powerflag = 1;
	else
	    powerflag = 0;
    }
    if(powerflag){
	printf("The battery too low.\n\n");
	set_led(2); udelay(0x300000);
	set_led(0); udelay(0x300000);
	set_led(2); udelay(0x300000);
	set_led(0); udelay(0x300000);
	set_led(2); udelay(0x300000);
	set_led(0); udelay(0x300000);
	set_led(2); udelay(0x300000);
	set_led(0);
	do_poweroff(0x8FFF);
    }
    return 0;
}

int init_hard_last(int flag, int param)
{
//	u32 addr = 0, ddrque;
	u32 key;
	char *s;

#ifdef DEBUG
	set_led(1);
#endif
	/* get_dma addr, DmaMemory = memAddr - 0x200000 
	 * DmaLinkAddr = No Need */
#if 0     /* comment by whg HHTECH */
	if(get_env_mem("mem", &addr) == 0) 
		gLcdInfo.dma_base = ((addr<<20) - gLcdInfo.fb_dma_size);	/* 720*576*2 */
	gLcdInfo.dmasg = (dmasg_t*)(gLcdInfo.dma_base + gLcdInfo.fb_size);
	memset((void*)gLcdInfo.dma_base, 0x00, gLcdInfo.fb_size);
	gLcdInfo.fb_addr = gLcdInfo.dma_base - (1<<20);
	gLcdInfo.zip_addr = gLcdInfo.fb_addr - (1<<20);

	printf("this init,spi st=0x%x\n", spi_protect(1, 0));
	hhbf_lcd_init(000);

	unzip_logo_bmp();
	show_bitmap(B_LOGO);
	printf("show logo end\n");
	init_card_gpio();
#endif    /* comment by WangGang   */
	gpio_direction_input(GPIO_DCIN);
	gpio_direction_input(GPIO_GPN13);
	gpio_direction_input(GPIO_GPN14);
	key_init();
	udelay(12000);

	if(1 == flag && 1 == param)	{
		do_upgrade(0, 0);
		return 0;
	}
	else if(2 == flag) {
	    do_boot_upgrade(0, param);
	    return 0;
	}

	if((s = getenv("stop")) && s[0] == 'y') return 0;

	battery_probe();
	key = press_key();
	if(key == -1)
	    return -1;
//	set_boot_env(key, 0);
	PFUNC("BOOTARGS=%s\n", getenv("bootargs"));
	return 0;
}



///////////////////////////////////////////////////////////////////////////
/* SOME mixer FUNCTION */
///////////////////////////////////////////////////////////////////////////
inline unsigned short genX(void)
{
	static unsigned short fbnc[]={1,1};
	unsigned short x = fbnc[0] + fbnc[1];
	fbnc[0] = fbnc[1];
	fbnc[1] = x;
	return (x%16);
}   

inline unsigned short genY(void)
{
	static unsigned short fbnc[]={1,2};
	unsigned short y = fbnc[0] + fbnc[1];
	fbnc[0] = fbnc[1];
	fbnc[1] = y;
	return ((y<<8) | (y>>8));
}

inline void encrypt(unsigned short *data)
{
	unsigned short x,y,t1,t2;

	x = genX();
	y = genY();
	t1 = *data ^ y;
	t2 = (t1 >> x) | (t1 << (16-x));
	*data = t2;
}

int do_encrypt_data(void *src, int len)
{
	int i, size = len;
	unsigned short *p = (unsigned short *)src;

	PFUNC("start decrypt file from 0x%x,size=%d\n", src,size); 
	for(i = 0; i < size; i += 2)	{
		encrypt(p);
		p++;
	}
	printf("over decrypt\n");
	return 0;
}

///////////////////////////////////////////////////////////////////////////
#if 0     /* comment by whg HHTECH */
/* Write one memory data to spi flash
 * rd_addr is use for read spi flash data to check
 */
int do_write_spi_flash(u32 spi_addr, u32 mem_addr, u32 size, u32 rd_addr)
{
	int ret = 0, times = 0;
	unsigned long crc1, crc2;
	
	if( file_check_sum((u8*)mem_addr, size) != 0)
		return -6;		// file check sum error

	/* unprotect spi flash */
	spi_protect(-1, 0);

	/* check for write last sector */
	eeprom_read(CFG_DEF_EEPROM_ADDR, 0x70000, (uchar*)rd_addr, 0x10000);
	*(unsigned long*)(rd_addr + 0x10000 - 4) = 0xABAB1234; // change last byte
	crc1 = crc32(0, (u8*)rd_addr, 0x10000);
	eeprom_write(CFG_DEF_EEPROM_ADDR, 0x70000, (uchar*)rd_addr, 0x10000);
	eeprom_read(CFG_DEF_EEPROM_ADDR, 0x70000, (uchar*)rd_addr, 0x10000);
	crc2 = crc32(0, (u8*)rd_addr, 0x10000);
	printf("\tWrite Check sector %s\n", (crc1 == crc2 ) ? "SUCCESS" : "FAIL");
	if(crc1 != crc2) {
	    spi_protect(1, 0);
	    return -1;
	}

	crc1 = crc32(0, (u8*)mem_addr, size);

loop_write:
	if(eeprom_write(CFG_DEF_EEPROM_ADDR, spi_addr,
		(uchar*)mem_addr, size))
		printf("Write SPI Error, addr=0x%x,size=%d\n", spi_addr, size);

	if(eeprom_read(CFG_DEF_EEPROM_ADDR, spi_addr,
		(uchar*)rd_addr, size))
		printf("Read SPI Error, addr=0x%x,size=%d\n", spi_addr, size);
	
	crc2 = crc32(0, (u8*)rd_addr, size);

	if(crc1 != crc2  && ++times < 3)	{
		printf("\nWrite and Check spi flash Error:%d\n", times);
		goto loop_write;
	}

	spi_protect(1, 0);  // protect spi flash
	ret = (crc1 == crc2) ? 0 : -1;
	printf("\nWrite and Check spi flash %s\n", 
		(ret == 0) ? "SUCCESS" : "FAILE");

	return ret;
}
#endif    /* comment by WangGang   */

char *cmd_argv[6], params[6][24];
static void init_cmd_argv(void)
{
	int i;
	memset(params, 0, sizeof(params));
	for(i = 0; i < 6; i++)
		cmd_argv[i] = &params[i][0];
}

static int load_sd_file(int dev, char *file, u32 addr, int size)
{
#ifdef CONFIG_SUPPORT_VFAT
	unsigned long long ticks;
	int ret = 0, argc = 6;
	char *s;

	init_cmd_argv();
	sprintf(cmd_argv[0], "fatload");
	sprintf(cmd_argv[1], "mmc");
	sprintf(cmd_argv[2], "%d:1", dev);
	sprintf(cmd_argv[3], "0x%x", addr);
	sprintf(cmd_argv[4], "%s", file);
	sprintf(cmd_argv[5], "0x%x", size);
	
	ticks = get_ticks();
	if( (ret = do_fat_fsload(NULL, 0, argc, cmd_argv)))
	{
	    printf("Error, fatload 0:1\n");
	    sprintf(cmd_argv[2], "%d:0", dev);
	    if( (ret = do_fat_fsload(NULL, 0, argc, cmd_argv))) {
		printf("Error, fatload 0:0\n");
		ret = -2;
		goto Err;
	    }
	}
	
	s = getenv("filesize");
	ret = simple_strtoul(s, (char**)0, 16);
	printf("Load file success:%s,len=%d, time=%d\n",
		file, ret, (get_ticks() - ticks));

Err:
	return ret;
#else 

    return -1;
#endif
}

#ifdef CONFIG_NAND
static int nand_read_to_mem(u32 mem_addr, u32 offset, u32 size)
{
    int ret; ulong oret = size;
    nand_info_t *nand;

    if(nand_curr_device < 0) nand = &nand_info[0];
    else                     nand = &nand_info[nand_curr_device];

    ret = nand_read(nand, offset, &oret, (u_char*)mem_addr);

    return ret;
}

#define NAND_SIZE_PAGE    (2<<10)      // 2K
#define NAND_SIZE_BLOCK   (NAND_SIZE_PAGE * 128)  // 256k
static int nand_write_from_mem(u32 mem_addr, u32 offset, u32 size)
{
    int ret; ulong len;
    nand_info_t *nand;

    if(nand_curr_device < 0) nand = &nand_info[0];
    else                     nand = &nand_info[nand_curr_device];

    len = ((size + (NAND_SIZE_BLOCK-1 ) ) / NAND_SIZE_BLOCK) * NAND_SIZE_BLOCK;
    nand_erase(nand, offset, len);
	
    len = ((size + (NAND_SIZE_PAGE-1)) / NAND_SIZE_PAGE) * NAND_SIZE_PAGE;
    ret = nand_write(nand, offset, &len, (u_char*)mem_addr);

    return ret;
}

#define   NAND_IMG_OFFSET      (0x200000)
#define   MAX_IMG_SIZE         (0xA00000)
static int load_nand_image(u32 mem_addr)
{
    int ret;
    
    ret = nand_read_to_mem(mem_addr, NAND_IMG_OFFSET, MAX_IMG_SIZE);
    return ret;
}
#endif

static int boot_image(u32 addr, u32 addr2)
{
#ifdef CONFIG_SUPPORT_VFAT
  extern void mmc_release_dev(int dev);
#endif
	int argc = 2;
	init_cmd_argv();

#ifdef DEBUG
	set_led(3);
#endif
	set_lcd_backlight(0);	// turn off the LCD backlight
	if(key_factory)
	    setenv("bootargs", "console=ttySAC0,115200n8 root=/dev/mmcblk0p1 rootwait splash factory");
	else
	    setenv("bootargs", "console=ttySAC0,115200n8 root=/dev/mmcblk0p1 rootwait splash");
	sprintf(cmd_argv[0], "bootm");
	sprintf(cmd_argv[1], "0x%x", addr);
	if(addr2) {
	    argc += 1;
	    sprintf(cmd_argv[2], "0x%x", addr2);
	    setenv("bootargs", "console=ttySAC0,115200n8 rdinit=/sbin/init");
	}

#ifdef CONFIG_SUPPORT_VFAT
	mmc_release_dev(1);
#endif
	return do_bootm(NULL, 0, argc, cmd_argv);
}

static void key_init(void)
{
    int i = 0;

    while(1) {
	if(0xFFFF == gkeys[i].key) break;
	gpio_direction_input(gkeys[i].gpio);

	i += 1;
    }
    return;
}


static u32 key_read(void)
{
    int i = 0; u32 key = 0;

    while(1) {
	if(0xFFFF == gkeys[i].key) break;

	if((gkeys[i].vtg && (1 == gpio_get_value(gkeys[i].gpio)))
		|| (!gkeys[i].vtg && (0 == gpio_get_value(gkeys[i].gpio))))
	    key |= gkeys[i].key;
	i += 1;
    }

    return key;
}



///////////////////////////////////////////////////////////////////////////
/* COMMAND start FUNCTION */
//////////////////////////////////////////////////////////////////////////
void show_help(cmd_tbl_t *cmdtp)
{
    if(cmdtp)	{
#ifdef	CFG_LONGHELP
	printf ("Usage:\n%s%s\n", cmdtp->usage, cmdtp->help);
#else
	printf ("Usage:\n%s\n", cmdtp->usage);
#endif
    }
}

#include <regs.h>
#include <asm/io.h>
int do_start (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
    int rcode = 0, param = 0;
    char *file = NULL;

    if (argc < 2) {
	show_help(cmdtp);
	return 1;
    }

    if(strncmp(argv[1], "upgrade", 7) == 0) {
	init_hard_last(1, 1);
	rcode = -1;
    }
    else if(strncmp(argv[1], "upboot", 6) == 0) {
	init_hard_last(2, 0);
	rcode = -1;
    }
    else if(strncmp(argv[1], "runfirm", 7) == 0) {
	if(argc > 2)	file = argv[2];
	do_start_firmware(0, file);
	rcode = -1;
    }
    else if(strncmp(argv[1], "blockr", 6) == 0) {
#ifdef INAND_RW_DIRECT
	block_dev_desc_t *desc = mmc_get_dev(1);
	memset((void*)0x52000000, 0, 128);
	if(!desc) {
	    printf("init error\n");
	    return -1;
	}
	desc->block_read(1, 32*32, 1, (ulong*)0x52000000);

	printf("Read:\n\t");
	for(rcode = 0; rcode < 8; rcode++)
	    printf("%08x ", *(ulong*)(0x52000000 + rcode*4));

	printf("\n");
#endif
	rcode = -1;
    }
    else if(strncmp(argv[1], "blockw", 6) == 0) {
	mmc_get_dev(1);

	for(rcode = 0; rcode < 10; rcode++)
	    *(ulong*)(0x52000000 + rcode*4) = (0x12345678+rcode);
	
	mmc_write_block(1, 32*32, 1, (ulong*)0x52000000);

	rcode = -1;
    }
    else if(strncmp(argv[1], "test", 4) == 0) {
	static int high = 0;
	writel(0x00111111, GPHCON0);
	if(high == 0) {
	    printf("high \n");
	    writel(0x0000003F, GPHDAT);
	    high = 1;
	}
	else  {
	    printf("low\n");
	    writel(0x00000000, GPHDAT);
	    high = 0;
	}

	printf("CON:0x%x, dat:0x%x,pud:0x%x,sonslp=0x%x,pudslp=0x%x\n",
	    readl(GPHCON0), readl(GPHDAT), readl(GPHPUD),
	    readl(GPHCONSLP), readl(GPHPUDSLP));

    }
#if 0     /* comment by whg HHTECH */
	else if(strncmp(argv[1], "bat", 3) == 0) {
		param = get_battery_cap();
		printf("Get Battery=%dmV\n", param);
	}
#endif    /* comment by WangGang   */
	else if(strncmp(argv[1], "led", 3) == 0) {
		param = simple_strtoul(argv[2], NULL, 16);
		set_led(param);
	}
	else if(strncmp(argv[1], "key", 3) == 0) {
		gpio_direction_input(GPIO_DCIN);
		key_init();
		param = key_read();
		printf("Read Key=0x%x,dc=%d\n", param, get_dc_status());
	}
	else if(strncmp(argv[1], "poweroff", 8) == 0) {
		do_poweroff(0x8FFFF);
	}
#if 0     /* comment by whg HHTECH */
	else if(strncmp(argv[1], "lcd", 3) == 0) {
		param = simple_strtoul(argv[2], NULL, 16);
		rcode = simple_strtoul(argv[3], NULL, 16);
		gLcdInfo.reg_set(param, rcode);
		rcode = 0;
	}
	else if(strncmp(argv[1], "protect", 7) == 0) {
		if(memcmp(argv[2], "on", 2) == 0) {
		    rcode = spi_protect(1, 0);
		    printf("Flash proected, ret=0x%x\n", rcode);
		}
		else if(memcmp(argv[2], "off", 3) == 0) {
		    rcode = spi_protect(-1, 0);
		    printf("Flash Unproected, ret=0x%x\n", rcode);
		}
		else  {
		    rcode = spi_protect(0, 0);
		    printf("Flash status is: 0x%0x\n", rcode);
		}
		rcode = 0;
	}
	else if(strncmp(argv[1], "cardinit", 8) == 0) {
	    init_card_gpio();
	}
	else if(strncmp(argv[1], "freq", 4) == 0) {
	extern void change_cpu_freq(int level);
	extern u_long get_cclk(void);
		u_long msel;	u_long vco; int lcd_set = 0; 
		if(argc > 2) {
	//		lcd_set = 1; hhbf_lcd_close(0); udelay(10000);
			param = simple_strtoul(argv[2], NULL, 10);
			change_cpu_freq(param);
		}

		udelay(10000);
		msel = (*pPLL_CTL >> 9) & 0x3F;
		if (0 == msel) msel = 64;

		vco = CONFIG_CLKIN_HZ;
		vco >>= (1 & *pPLL_CTL);	/* DF bit */
		vco = msel * vco;
	    printf("  VCO: %lu MHz, CCLK: %lu MHz, SCLK: %lu MHz, Vot=%dmV\n",
           vco / 1000000, get_cclk() / 1000000, get_sclk() / 1000000,
		   (((*pVR_CTL>>4)&0xF)-6) * 50 + 850);

		if(1 == lcd_set) {
			hhbf_lcd_init(0);
			gLcdInfo.setbl_brightness(4);
		}
	}
	else ;
#endif    /* comment by WangGang   */

#if 0     /* comment by whg HHTECH */
	param = simple_strtoul(argv[1], NULL, 16);
	printf ("Start command, param=0x%x\n", param);
	hhbf_lcd_init(param);
#endif    /* comment by WangGang   */
	return rcode;
}

U_BOOT_CMD(
	start, CFG_MAXARGS, 1,	do_start,
	"start   - start application\n",
	"param     - start COMMAND param\n"
	"  upgrade      - load BOOT LDR form sd, write to flash\n"
	"  upboot       - upgrade u-boot.bin\n"
	"  runfirm      - load FRIM BIN, them runing\n"
	"  connect      - connect USB and wait disconnect\n"
	"  bat          - Read Battery Votage\n"
	"  led          - Set Led on/off\n"
	"  protect      - Set SPI flash protect on/off\n"
);

#define IMAGE_FLAG       "WANGGANG"

#if defined(CONFIG_NAND)
//#define READ_SIZE       (PHYS_FLASH_SIZE)
#define READ_SIZE       0x180000
static int load_boot_kernel(int flag, int param)
{
    int ret, i;
    unsigned char *ptr;

    ret = nand_read_to_mem(MEM_READ_FILE, 0, READ_SIZE);
    ptr = (unsigned char*)MEM_READ_FILE;

    for(i = 0; i < READ_SIZE; i += 1024) {
	if(0 == memcmp(ptr, IMAGE_FLAG, strlen(IMAGE_FLAG)))
	    break;
	ptr += 1024;
    }

    if(i >= READ_SIZE) {
	printf("No Found Image Flag For Boot on Nand\n");
	return -2;
    }
    ptr += ((strlen(IMAGE_FLAG) + 3) / 4) * 4;
    memcpy((void*)MEM_READ_FILE, ptr, READ_SIZE);

    return 0;
}

//#elif defined(CONFIG_BOOT_NOR)
//static int load_boot_kernel(int flag, int param)
//{
//    int i, kernel_size = PHYS_FLASH_SIZE;
//    unsigned char *ptr;
//
//    memcpy((void*)MEM_READ_FILE, (void*)CFG_FLASH_BASE, kernel_size);
//    ptr = (unsigned char*)MEM_READ_FILE;
//
//    for(i = 0; i < PHYS_FLASH_SIZE; i += 1024) {
//	if(0 == memcpy(ptr, IMAGE_FLAG, strlen(IMAGE_FLAG)))
//	    break;
//	ptr += 1024;
//    }
//
//    if(i >= PHYS_FLASH_SIZE) {
//	printf("No Found Image Flag For Boot on Nor\n");
//	return -2;
//    }
//    ptr += ((strlen(IMAGE_FLAG) + 3) / 4) * 4;
//    memcpy((void*)MEM_READ_FILE, ptr, PHYS_FLASH_SIZE);
//
//    return 0;
//}
//
#elif defined(CONFIG_XXX)
static int load_boot_kernel(int flag, int param)
{


    return 0;
}
#endif

#endif // CONFIG_HHTECH_MINIPMP
/****************** End Of File: cmd_hhtech.c ******************/
