#ifndef OV7670_NONFIFO_H
#define OV7670_NONFIFO_H

#include <Arduino.h>
#include <Wire.h>
#include <pgmspace.h>
#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"
#include "stdlib.h"
#include "string.h"
#include "rom/lldesc.h"
#include "esp_intr_alloc.h"
#include "driver/periph_ctrl.h"
#include "esp32-hal-ledc.h"
#include "rom/gpio.h"

#pragma once



typedef struct {
	int D0;             /*!< GPIO pin for camera D0 line */
	int D1;             /*!< GPIO pin for camera D1 line */
	int D2;             /*!< GPIO pin for camera D2 line */
	int D3;             /*!< GPIO pin for camera D3 line */
	int D4;             /*!< GPIO pin for camera D4 line */
	int D5;             /*!< GPIO pin for camera D5 line */
	int D6;             /*!< GPIO pin for camera D6 line */
	int D7;             /*!< GPIO pin for camera D7 line */
	int XCLK;           /*!< GPIO pin for camera XCLK line */
	int PCLK;           /*!< GPIO pin for camera PCLK line */
	int VSYNC;          /*!< GPIO pin for camera VSYNC line */
	int xclk_freq_hz;       	/*!< Frequency of XCLK signal, in Hz */
	ledc_timer_t ledc_timer;    /*!< LEDC timer to be used for generating XCLK  */
	ledc_channel_t ledc_channel;/*!< LEDC channel to be used for generating XCLK  */
	int frame_width;
	int frame_height;
	uint8_t pixel_byte_num;

}github_camera_config_t;

esp_err_t I2S_camera_init(github_camera_config_t* config);
uint16_t* camera_getLine(uint16_t lineno);
//uint16_t* camera_getFrame(void);


// Resolution defines
#define	VGA	0	// 640 x 480
#define	QVGA	1	// 320 X 240
#define	QQVGA	2	// 160 x 120
#define CIF	3	// 352 x 288
#define QCIF	4	// 176 x 144
#define QQCIF	5	//  88 x 72

#define	YUV422		0x00	// color mode
#define	RGB565		0x04
#define	BAYER_RAW	0x01
#define PBAYER_RAW	0x05

#define OV7670_ADDR	0x21

struct regval_list{
	uint8_t reg_num;
	uint8_t value;
};

class GitHubOV7670{
	public:
		GitHubOV7670(void);
		~GitHubOV7670(void);

		void reset(void);
		esp_err_t init(const github_camera_config_t *value, uint8_t res, uint8_t colmode);

		void setResolution(uint8_t res);
		void setPCLK(uint8_t pre, uint8_t pll);
		void rewrCLKRC(void);
		void setHStart(uint16_t val);
		void setVStart(uint16_t val);
		uint16_t getHStart(void);
		uint16_t getVStart(void);

		uint16_t* getLine(uint16_t lineno);
		bool getLines(uint16_t lineno, uint8_t *buf, uint16_t n);
		bool getFrame(uint8_t *buf);

		void stop(void);
		uint16_t getMID(void);
		uint16_t getPID(void);
		void vflip( bool enable );
		void setColor(uint8_t color);
		void setGain(uint16_t val);
		uint16_t getGain(void);
		void setAGC(uint8_t val);
		bool getAGC(void);
		void setAWB(uint8_t val);
		bool getAWB(void);
		void setAEC(uint8_t val);
		bool getAEC(void);
		void setBright(int8_t val);
		int8_t getBright(void);
		void setContrast(uint8_t val);
		uint8_t getContrast(void);
		void setAWBB(uint8_t val);
		void setAWBR(uint8_t val);
		void setAWBG(uint8_t val);
		void setExposure(uint16_t val);
		void colorbar(bool on);
		void colorbar_super(bool on);

		void wrReg(uint8_t reg,uint8_t dat);
		uint8_t rdReg(uint8_t reg);

	private:
		void wrRegs(const struct regval_list *reglist);
		void conf_setFrameSize(uint8_t res);
		github_camera_config_t cam_conf;
		uint8_t _resolution;
		uint8_t _colormode;
};

// OV7670 Registers ---------------------------------------------
//
//	AEC	: Automatic Exposure(露出) Control Mode
//	AGC : Automatic Gain Control
//	AWB	: Automatic White Balance Control
//	BLC : Black Level Calibration
//	ABLC: Automatic Black Level Calibration
//	DSP	: Digital Signal Processor
//
#define REG_GAIN	0x00	// AGC[9:0] lower 8bit (higher 2bit VREF[7:6])
#define REG_BLUE	0x01	// AWB Blue gain (00-ff)
#define REG_RED		0x02	// AWB Red gain (00-ff)
#define REG_VREF	0x03	// Pieces of AGC[9:8], VSTOP[1:0], VSTART[1:0]
#define REG_COM1	0x04	/* Control 1 */
#define  COM1_CCIR656		0x40	/* CCIR656 enable */
#define REG_BAVE	0x05	/* U/B Average level */
#define REG_GbAVE	0x06	/* Y/Gb Average level */
#define REG_AECHH	0x07	/* AEC MS 5 bits */
#define REG_RAVE	0x08	/* V/R Average level */
#define REG_COM2	0x09	/* Control 2 */
#define  COM2_SSLEEP		0x10	/* Soft sleep mode */
#define  COM2_OUT_DRIVE_1x	0x00	/* Output drive capability 1x */
#define  COM2_OUT_DRIVE_2x	0x01	/* Output drive capability 2x */
#define  COM2_OUT_DRIVE_3x	0x02	/* Output drive capability 3x */
#define  COM2_OUT_DRIVE_4x	0x03	/* Output drive capability 4x */
#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */
#define REG_COM3	0x0c	/* Control 3 */
#define  COM3_SWAP		0x40	/* Byte swap */
#define  COM3_SCALEEN		0x08	/* Enable Scaling */
#define  COM3_DCWEN		0x04	/* Enable Downsampling/Dropping/Windowing */
#define REG_COM4	0x0d	/* Control 4 */
#define  COM4_AEC_FULL		0x00 /* AEC evaluate full window */
#define  COM4_AEC_1_2		0x10 /* AEC evaluate 1/2 window  */
#define  COM4_AEC_1_4		0x20 /* AEC evaluate 1/4 window  */
#define  COM4_AEC_2_3		0x30 /* AEC evaluate 2/3 window  */
#define REG_COM5	0x0e	/* All "reserved" */
#define REG_COM6	0x0f	/* Control 6 */
#define  COM6_BLK_LINE		0x80	// Enable HREF at optional black
#define  COM6_RESET_TIM		0x02	// Reset all timing when format changes
#define  COM6_RESERVE		0x41	// Reserved bit
#define REG_AECH	0x10	/* More bits of AEC value */
#define REG_CLKRC	0x11	/* Clocl control */
#define  CLK_RSVD		0x80	// Reserved
#define  CLK_EXT		0x40	/* Use external clock directly */
#define  CLK_SCALE		0x3f	/* Mask for internal clock scale */
#define REG_COM7	0x12	/* Control 7 */
#define  COM7_RESET		0x80	/* Register reset */
#define  COM7_FMT_MASK		0x38
#define  COM7_FMT_VGA		0x00
#define  COM7_FMT_CIF		0x20	/* CIF format */
#define  COM7_FMT_QVGA		0x10	/* QVGA format */
#define  COM7_FMT_QCIF		0x08	/* QCIF format */
#define  COM7_RGB		0x04	/* bits 0 and 2 - RGB format */
#define  COM7_YUV		0x00	/* YUV */
#define  COM7_BAYER		0x01	/* Bayer format */
#define  COM7_CBAR		0x02	// Color bar
#define  COM7_PBAYER		0x05	/* "Processed bayer" */
#define REG_COM8	0x13	/* Control 8 */
#define  COM8_FASTAEC		0x80	/* Enable fast AGC/AEC */
#define  COM8_AECSTEP		0x40	/* Unlimited AEC step size */
#define  COM8_BFILT		0x20	/* Band filter enable */
#define  COM8_RSVD		0x08	// Reserved fefault 1
#define  COM8_AGC		0x04	/* Auto gain enable */
#define  COM8_AWB		0x02	// Auto White Balance enable
#define  COM8_AEC		0x01	/* Auto exposure enable */
#define REG_COM9	0x14	/* Control 9- gain ceiling */
#define  COM9_AGC_GAIN_2x	0x00 /* Automatic Gain Ceiling 2x  */
#define  COM9_AGC_GAIN_4x	0x10 /* Automatic Gain Ceiling 4x  */
#define  COM9_AGC_GAIN_8x	0x20 /* Automatic Gain Ceiling 8x  */
#define  COM9_AGC_GAIN_16x	0x30 /* Automatic Gain Ceiling 16x */
#define  COM9_AGC_GAIN_32x	0x40 /* Automatic Gain Ceiling 32x */
#define  COM9_AGC_GAIN_64x	0x50 /* Automatic Gain Ceiling 64x  */
#define  COM9_AGC_GAIN_128x	0x60 /* Automatic Gain Ceiling 128x */
#define  COM9_AGC_GAIN_NOT	0x70 /* Automatic Gain Ceiling Not allowed */
#define  COM9_FREEZE_AGC_AEC	0x01
#define REG_COM10	0x15	/* Control 10 */
#define  COM10_HSYNC		0x40	/* HSYNC instead of HREF */
#define  COM10_PCLK_HB		0x20	/* PCLK does not toggle during horizontal blank */
#define  COM10_PCLK_REV 	0x10	// Reverse PCLK
#define  COM10_HREF_REV		0x08	/* Reverse HREF */
#define  COM10_VS_LEAD		0x04	/* VSYNC on clock leading edge */
#define  COM10_VS_NEG		0x02	/* VSYNC negative */
#define  COM10_HS_NEG		0x01	/* HSYNC negative */
#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */
#define REG_PSHFT	0x1b	/* Pixel delay after HREF */
#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */
#define REG_MVFP	0x1e	/* Mirror / vflip */
#define  MVFP_MIRROR		0x20	/* Mirror image */
#define  MVFP_FLIP		0x10	/* Vertical flip */
#define  MVFP_BLACK_SUN		0x04	// black sun enable
#define REG_LAEC	0x1f	// Reserved - Fine AEC Value - defines exposure value less than one row period
#define REG_ADCCTR0	0x20	// ADC range adjustment
#define REG_ADCCTR1	0x21	// Reserved
#define REG_ADCCTR2 	0x22	// Reserved
#define REG_ADCCTR3 	0x23	// Reserved
#define REG_AEW		0x24	/* AGC upper limit */
#define REG_AEB		0x25	/* AGC lower limit */
#define REG_VPT		0x26	/* AGC/AEC fast mode op region */
#define REG_BBIAS	0x27	// B Channel Signal Output Bias
#define REG_GbBIAS	0x28	// Gb Channel Signal Output Bias

#define REG_EXHCH	0x2a	// Dummy Pixel Insert MSB
#define REG_EXHCL	0x2b	// Dummy Pixel Insert LSB
#define REG_RBIAS	0x2c	// R Channel Signal Output Bias
#define REG_ADVFL	0x2d	// LSB of insert dummy rows in vertical direction (1 bit equals 1 row)
#define REG_ADVFH	0x2e	// MSB of insert dummy rows in vertical direction
#define REG_YAVE	0x2f	// Y/G Channel Average Value
#define REG_HSYST	0x30	/* HSYNC rising edge delay */
#define REG_HSYEN	0x31	/* HSYNC falling edge delay */
#define REG_HREF	0x32	/* HREF pieces */
#define REG_CHLF	0x33	// Array Current Control	 ( Reserved )
#define REG_ARBLM	0x34	// Array Referrence Control	 ( Reserved )
#define REG_ADC		0x37	// ADC Control				 ( Reserved )
#define REG_ACOM	0x38	// ADC and Analog Common MOde  Control ( Reserved )
#define REG_OFON	0x39	// Reserved - ADC Offset Control ( Reserved )
#define REG_TSLB	0x3a	// Line Buffer Test Option
#define  TSLB_NEGATE		0x20	// Negative image - see MANU & MANV
#define  TSLB_UVOUT		0x10	// Use fixed UV value
#define  TSLB_YLAST		0x08	/* UYVY or VYUY - see com13 */
#define  TSLB_AUTO		0x01	// Auto output window
#define REG_COM11	0x3b	/* Control 11 */
#define  COM11_NIGHT		0x80	/* NIght mode enable */
#define  COM11_NMFR		0x60	/* Two bit NM frame rate */
#define  COM11_FR_BY_2		0x20	// 1/2 of normal mode frame rate
#define  COM11_FR_BY_4		0x40	// 1/4 of normal mode frame rate
#define  COM11_FR_BY_8		0x60	// 1/8 of normal mode frame rate
#define  COM11_HZAUTO		0x10	/* Auto detect 50/60 Hz */
#define  COM11_50HZ		0x08	/* Manual 50Hz select */
#define  COM11_EXP		0x02	// Exposure timing can be less than limit of banding filter when light is too strong
#define REG_COM12	0x3c	/* Control 12 */
#define  COM12_HREF		0x80	/* HREF always */
#define  COM12_RSVD		0x68	// reserved bit
#define REG_COM13	0x3d	/* Control 13 */
#define  COM13_GAMMA		0x80	/* Gamma enable */
#define  COM13_UVSAT		0x40	/* UV saturation auto adjustment */
#define  COM13_UVSWAP		0x01	/* V before U - w/TSLB */
#define  COM13_RESV		0x08	// reserved bit
#define REG_COM14	0x3e	/* Control 14 */
#define  COM14_DCWEN		0x10	// DCW/PCLK-scale enable */
#define  COM14_MANUAL		0x08	// Manual scaling enable
#define  COM14_PCLKDIV_1	0x00	// PCLK Divided by 1
#define  COM14_PCLKDIV_2	0x01	// PCLK Divided by 2
#define  COM14_PCLKDIV_4	0x02	// PCLK Divided by 4
#define  COM14_PCLKDIV_8	0x03	// PCLK Divided by 8
#define  COM14_PCLKDIV_16	0x04	// PCLK Divided by 16
#define REG_EDGE	0x3f	/* Edge enhancement factor */
#define REG_COM15	0x40	/* Control 15 */
#define  COM15_R10F0		0x00	/* Data range 10 to F0 */
#define  COM15_R01FE		0x80	/*			01 to FE */
#define  COM15_R00FF		0xc0	/*			00 to FF */
#define  COM15_RGB565		0x10	/* RGB565 output */
#define  COM15_RGB555		0x30	/* RGB555 output */
#define REG_COM16	0x41	/* Control 16 */
#define  COM16_YUV_ENHANC	0x20
#define  COM16_DE_NOISE		0x10	//
#define  COM16_AWBGAIN		0x08	/* AWB gain enable */
#define REG_COM17	0x42	/* Control 17 */
#define  COM17_AECWIN		0xc0	/* AEC window - must match COM4 */
#define  COM17_AEC_FULL		0x00	/* AEC evaluate full window */
#define  COM17_AEC_1_2		0x40	/* AEC evaluate 1/2 window  */
#define  COM17_AEC_1_4		0x80	/* AEC evaluate 1/4 window  */
#define  COM17_AEC_2_3		0xC0	/* AEC evaluate 2/3 window  */
#define  COM17_CBAR			0x08	/* DSP Color bar */

#define REG_AWBC1	0x43	// AWB Control 1 (Reserved ?)
#define REG_AWBC2	0x44	// AWB Control 2 (Reserved ?)
#define REG_AWBC3	0x45	// AWB Control 3 (Reserved ?)
#define REG_AWBC4	0x46	// AWB Control 4 (Reserved ?)
#define REG_AWBC5	0x47	// AWB Control 5 (Reserved ?)
#define REG_AWBC6	0x48	// AWB Control 6 (Reserved ?)

#define REG_4B		0x4b	// UV average ebnable
#define  UV_AVR_EN 		0x01	// UV average enable
#define REG_DNSTH	0x4c	// De-noise Threshold
//#define REG_DM_POS	0x4d	// Reserved - Dummy row position
/*
#define	REG_CMATRIX_BASE 0x4f
#define   CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58
*/
#define REG_MTX1	0x4f	/* Matrix Coefficient 1 */
#define REG_MTX2	0x50	/* Matrix Coefficient 2 */
#define REG_MTX3	0x51	/* Matrix Coefficient 3 */
#define REG_MTX4	0x52	/* Matrix Coefficient 4 */
#define REG_MTX5	0x53	/* Matrix Coefficient 5 */
#define REG_MTX6	0x54	/* Matrix Coefficient 6 */

#define REG_BRIGHT	0x55	/* Brightness */
#define REG_CONTRAS	0x56	/* Contrast control */
#define REG_CONTRAS_CENTER 0x57	// Contrast Center
#define REG_MTXS	0x58	/* Matrix Coefficient Sign */
#define REG_AWBC7	0x59	// AWB Control 7
#define REG_AWBC8	0x5a	// AWB Control 8
#define REG_AWBC9	0x5b	// AWB Control 9
#define REG_AWBC10	0x5c	// AWB Control 10
#define REG_AWBC11	0x5d	// AWB Control 11
#define REG_AWBC12	0x5e	// AWB Control 12
#define REG_B_LMT	0x5f	// AWB B Gain Range
#define REG_R_LMT	0x60	// AWB R Gain Range
#define REG_G_LMT	0x61	// AWB G Gain Range
#define REG_LCC1	0x62	// Lens Correction Option 1 - X Coordinate
#define REG_LCC2	0x63	// Lens Correction Option 2 - Y Coordinate
#define REG_LCC3	0x64	// Lens Correction Option 3
#define REG_LCC4	0x65	// Lens Correction Option 4
#define REG_LCC5	0x66	// Lens Correction Control
#define REG_MANU	0x67	// Manual U Value
#define REG_MANV	0x68	// Manual V Value
#define REG_GFIX	0x69	// AWB Pre gain control
#define REG_GGAIN	0x6a	/* G Channel AWB Gain */
#define REG_DBLV	0x6b	// PLL control,Regulator control
#define  DBLV_BYPASS		0x00	// Bypass PLL
#define  DBLV_CLK_x4		0x40	// input clock x4
#define  DBLV_CLK_x6		0x80	// input clock x6
#define  DBLV_CLK_x8		0xC0	// input clock x8
#define  DBLV_RSVD		0x0A	// reserved bit
#define REG_AWBCTR3	0x6c	/* AWB Control 3 */
#define REG_AWBCTR2	0x6d	/* AWB Control 2 */
#define REG_AWBCTR1	0x6e	/* AWB Control 1 */
#define REG_AWBCTR0	0x6f	/* AWB Control 0 */
#define REG_SCALING_XSC	0x70	// test pattern, Horizontal scale factor
#define REG_SCALING_YSC	0x71	// test pattern, Vertical scale factor
#define REG_SCALING_DCWCTR 0x72	// DCW Control
#define  SCALING_DCWCTR_VDS_by_2	0x10	// Vertical Down Sampling rate by 2
#define  SCALING_DCWCTR_VDS_by_4	0x20	// Vertical Down Sampling rate by 4
#define  SCALING_DCWCTR_VDS_by_8	0x30	// Vertical Down Sampling rate by 8
#define  SCALING_DCWCTR_HDS_by_2	0x01	// Horizontal Down Sampling rate by 2
#define  SCALING_DCWCTR_HDS_by_4	0x02	// Horizontal Down Sampling rate by 2
#define  SCALING_DCWCTR_HDS_by_8	0x03	// Horizontal Down Sampling rate by 2

#define REG_SCALING_PCLK_DIV 0x73	// Clock divider control for DSP scale
#define  SCALING_PCLK_DIV_RSVD		0xf0	// Reserved
#define  SCALING_PCLK_DIV_DIS		0x08	// Bypass clock divider
#define  SCALING_PCLK_DIV_1		0x00	// Divided by 1
#define  SCALING_PCLK_DIV_2		0x01	// Divided by 2
#define  SCALING_PCLK_DIV_4		0x02	// Divided by 4
#define  SCALING_PCLK_DIV_8		0x03	// Divided by 8
#define  SCALING_PCLK_DIV_16		0x04	// Divided by 16
#define REG_REG74	0x74	// Digital gain manual control
#define REG_REG75	0x75	// Edge enhanced lower limit
#define REG_REG76	0x76	/* OV's name */
#define  R76_BLKPCOR		0x80	/* Black pixel correction enable */
#define  R76_WHTPCOR		0x40	/* White pixel correction enable */
#define REG_REG77	0x77	// Offset, de-noise range control
// 0x7a - 0x89 Ganma Curve registor
#define REG_SLOP	0x7a	// SLOP = (256-GAM15)x40/30
#define REG_GAM1	0x7b	// XREF1 4
#define REG_GAM2	0x7c	// XREF2 8
#define REG_GAM3	0x7d	// XREF3 16
#define REG_GAM4	0x7e	// XREF4 32
#define REG_GAM5	0x7f	// XREF5 40
#define REG_GAM6	0x80	// XREF6 48
#define REG_GAM7	0x81	// XREF7 56
#define REG_GAM8	0x82	// XREF8 64
#define REG_GAM9	0x83	// XREF9 72
#define REG_GAM10	0x84	// XREF10 80
#define REG_GAM11	0x85	// XREF11 96
#define REG_GAM12	0x86	// XREF12 112
#define REG_GAM13	0x87	// XREF13 144
#define REG_GAM14	0x88	// XREF14 176
#define REG_GAM15	0x89	// XREF15 208

#define REG_RGB444	0x8c	/* RGB 444 control */
#define  R444_DISABLE		0x00
#define  R444_ENABLE		0x02	/* Turn on RGB444, overrides 5x5 */
#define  R444_RGBX		0x01	/* Empty nibble at end */
#define REG_DM_LNL	0x92	// Dummy Row low 8bit
#define REG_DM_LNH	0x93	// Dummy Row high 8bit
#define REG_LCC6	0x94	// Lens Correction Optin 6
#define REG_LCC7	0x95	// Lens Correction Optin 7

#define REG_BD50ST	0x9d	// 50Hz Banding Filter Value
#define REG_BD60ST	0x9e	// 60Hz Banding Filter Value

#define REG_HAECC1	0x9f	// Hist AEC/AGC control 1
#define REG_HAECC2	0xa0	// Hist AEC/AGC control 2
/*
#define REG_HRL		0x9f	// High Reference Luminance
#define REG_LRL		0xa0	// Low Reference Luminance
#define REG_DSPC3	0xa1	// DSP Control 3
*/
#define REG_SCALING_PCLK_DELAY	0xa2	// Pixel Clock Delay

#define REG_NT_CTRL	0xa4	// Auto frame rate adjustment
#define	 NT_CTRL_ROWPF		0x08	// Auto frame rate adjust dummy row per frame
#define  NT_CTRL_DMR_2x		0x00	// insert dummy row at 2x gain
#define  NT_CTRL_DMR_4x		0x01	// insert dummy row at 4x gain
#define  NT_CTRL_DMR_8x		0x02	// insert dummy row at 28 gain

#define REG_BD50MAX	0xa5	// 50hz banding step limit
#define REG_HAECC3	0xa6	// Hist AEC/AGC control 3
#define REG_HAECC4	0xa7	// Hist AEC/AGC control 4
#define REG_HAECC5	0xa8	// Hist AEC/AGC control 5
#define REG_HAECC6	0xa9	// Hist AEC/AGC control 6
#define REG_HAECC7	0xaa	// Hist AEC/AGC control 7
#define REG_BD60MAX	0xab	// 60hz banding step limit

/*
#define REG_AECGMAX	0xa5	// Maximum Banding Filter Step
#define REG_LPH		0xa6	// Low Limit of Probability for HRL
#define REG_UPL		0xa7	// Upper Limit of Probability for LRL
#define REG_TPL		0xa8	// Probablility Threshold for LRL to control AEC/AGC speed
#define REG_TPH		0xa9	// Probablility Threshold for HRL to control AEC/AGC speed
#define REG_NALG	0xaa	// AEC Algorithm selection
*/

#define REG_STR_OPT	0xac	// R/G/B gain control
#define REG_STR_R	0xad	// R Gain for LED Output Frame
#define REG_STR_G	0xae	// G Gain for LED Output Frame
#define REG_STR_B	0xaf	// B Gain for LED Output Frame

#define REG_ABLC1	0xb1	// ABLC enable
#define  ABLC1_EN		0x04	// ABLC enable
#define REG_THL_ST	0xb3	// ABLC Target
#define REG_THL_DLT	0xb5	// ABLC Stable Range
#define REG_AD_CHB	0xbe	// Blue Channel Black Level Compensation
#define REG_AD_CHR	0xbf	// Red Channel Black Level Compensation
#define REG_AD_CHGb	0xc0	// Gb Channel Black Level Compensation
#define REG_AD_CHGr	0xc1	// Gr Channel Black Level Compensation
#define REG_SATCTR	0xc9	// Saturation Control




#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/soc.h"
#include "driver/gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"
#include <stdlib.h>
#include <string.h>
#include "rom/lldesc.h"
#include "esp_intr_alloc.h"




static const char* TAG = "camera";

static github_camera_config_t s_config;
static lldesc_t s_dma_desc[2];
static uint32_t* s_dma_buf[2];
static uint8_t* s_fb[2];
static volatile int s_fb_idx = 0;
static bool s_initialized = false;
static int s_buf_line_width;
static int s_buf_height;
static volatile int s_line_count = 0;
static volatile int s_cur_buffer = 0;
static volatile bool s_i2s_running = false;
static SemaphoreHandle_t s_data_ready;
static SemaphoreHandle_t s_line_ready;
static SemaphoreHandle_t s_vsync_catch;
static volatile bool vsync_check = false;

static intr_handle_t s_i2s_intr_handle = NULL;

static void i2s_init();
static bool i2s_frameReadStart(void);
static void i2s_readStart(int index);
static void IRAM_ATTR i2s_isr(void* arg);
static esp_err_t dma_desc_init(void);
static void line_filter_task(void *pvParameters);
static void IRAM_ATTR VSYNC_isr( void *arg );

//---------- VSYNC Interrupt --------------------------
static void IRAM_ATTR VSYNC_isr( void *arg )
{
//	uint32_t gpio_num = (uint32_t)arg;
	if( vsync_check ){
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR( s_vsync_catch, &xHigherPriorityTaskWoken);
	}
}

//---------------------------------------------------
esp_err_t I2S_camera_init(github_camera_config_t* config)
{
	memcpy(&s_config, config, sizeof(s_config));

	s_buf_line_width = s_config.frame_width * s_config.pixel_byte_num;
	s_buf_height = s_config.frame_height;

	for(int i = 0; i < 2; i++){
		if( s_fb[i] != NULL) free( s_fb[i] );
		s_fb[i] = (uint8_t*)malloc(s_buf_line_width);
		if (s_fb[i] == NULL) {
			ESP_LOGE(TAG, "Failed to allocate frame buffer");
			return ESP_ERR_NO_MEM;
		}
	}
	s_fb_idx = 0;
	s_data_ready = xSemaphoreCreateBinary();
	s_line_ready = xSemaphoreCreateBinary();
	s_vsync_catch = xSemaphoreCreateBinary();

	i2s_init();
	esp_err_t err = dma_desc_init();
	if (err != ESP_OK) {
		free(s_fb[0]);
		free(s_fb[1]);
		ESP_LOGE(TAG, "Faild to allocate dma buffer");
		return err;
	}
	xTaskCreatePinnedToCore(&line_filter_task, "line_filter", 2048, NULL, 10, NULL, 0);

	// skip at least one frame after changing camera settings (with safe timeout)
	uint32_t vsyncWaitStart = millis();
	while (gpio_get_level((gpio_num_t)s_config.VSYNC) == 1 && (millis() - vsyncWaitStart < 100)) {}
	while (gpio_get_level((gpio_num_t)s_config.VSYNC) == 0 && (millis() - vsyncWaitStart < 200)) {}
	while (gpio_get_level((gpio_num_t)s_config.VSYNC) == 1 && (millis() - vsyncWaitStart < 300)) {}

	s_initialized = true;
	return ESP_OK;
}

static bool i2s_frameReadStart(void)
{
	if (xSemaphoreTake( s_vsync_catch, pdMS_TO_TICKS(150)) != pdTRUE) return false;
	vsync_check = false;
	s_cur_buffer = 0;
	s_line_count = 0;
	s_i2s_running = true;
	i2s_readStart(s_cur_buffer);	// start RX
	return true;
}

uint16_t* camera_getLine(uint16_t lineno)
{
	if (!s_initialized) {
		return NULL;
	}
	unsigned long time = millis();
	do{
		if(!s_i2s_running){
			vsync_check = true;
			if (!i2s_frameReadStart()) return NULL;
		}
		if (xSemaphoreTake(s_line_ready, pdMS_TO_TICKS(100)) != pdTRUE) return NULL;
		if( millis() - time > 1000 ) return NULL;
	}while(lineno != s_line_count);

	return (uint16_t*)s_fb[s_fb_idx];
}

static inline void i2s_conf_reset()
{
	const uint32_t conf_reset_flags = I2S_RX_RESET_M | I2S_RX_FIFO_RESET_M | I2S_TX_RESET_M | I2S_TX_FIFO_RESET_M;
	I2S0.conf.val |= conf_reset_flags;
	I2S0.conf.val &= ~conf_reset_flags;

	while (I2S0.state.rx_fifo_reset_back) {
		;
	}
}

static void i2s_init()
{
	// Configure input GPIOs
	int pins[] = {
		s_config.D0,
		s_config.D1,
		s_config.D2,
		s_config.D3,
		s_config.D4,
		s_config.D5,
		s_config.D6,
		s_config.D7,
		s_config.PCLK,
		s_config.VSYNC,
	};

	gpio_config_t conf = {
		.pin_bit_mask = 0,
		.mode 		  = GPIO_MODE_INPUT,
		.pull_up_en   = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type	  = GPIO_INTR_DISABLE
	};

	for (int i = 0; i < 10; ++i) {
		conf.pin_bit_mask = 1ULL << pins[i];
		gpio_config(&conf);
	}

	// VSYNC Interrupt Enable
	gpio_set_intr_type( (gpio_num_t)s_config.VSYNC, GPIO_INTR_NEGEDGE );
	gpio_install_isr_service( 0 );
	gpio_isr_handler_add( (gpio_num_t)s_config.VSYNC, VSYNC_isr, (void*)(intptr_t)s_config.VSYNC );

	// Route input GPIOs to I2S peripheral using GPIO matrix
	gpio_matrix_in((gpio_num_t)s_config.D0,		I2S0I_DATA_IN0_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D1,		I2S0I_DATA_IN1_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D2,		I2S0I_DATA_IN2_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D3,		I2S0I_DATA_IN3_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D4,		I2S0I_DATA_IN4_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D5,		I2S0I_DATA_IN5_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D6,		I2S0I_DATA_IN6_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.D7,		I2S0I_DATA_IN7_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.VSYNC,	I2S0I_V_SYNC_IDX,	false);
	gpio_matrix_in(0x38,			I2S0I_H_SYNC_IDX,	false);
	gpio_matrix_in(0x38,			I2S0I_H_ENABLE_IDX, false);
	gpio_matrix_in((gpio_num_t)s_config.PCLK,	I2S0I_WS_IN_IDX,	false);

	// Enable and configure I2S peripheral
	periph_module_enable(PERIPH_I2S0_MODULE);	// I2S0 enable

	// Toggle some reset bits in LC_CONF register
	const uint32_t lc_conf_reset_flags = I2S_IN_RST_S | I2S_AHBM_RST_S | I2S_AHBM_FIFO_RST_S;

	I2S0.lc_conf.val |= lc_conf_reset_flags;
	I2S0.lc_conf.val &= ~lc_conf_reset_flags;
	
	// Toggle some reset bits in CONF register
	i2s_conf_reset();
	// Enable slave mode (sampling clock is external)
	I2S0.conf.rx_slave_mod = 1;
	// Enable parallel mode
	I2S0.conf2.lcd_en = 1;
	// Use HSYNC/VSYNC/HREF to control sampling
	I2S0.conf2.camera_en = 1;
	// Configure clock divider
	I2S0.clkm_conf.clkm_div_a = 1;
	I2S0.clkm_conf.clkm_div_b = 0;
	I2S0.clkm_conf.clkm_div_num = 2;
	// FIFO will sink data to DMA
	I2S0.fifo_conf.dscr_en = 1;
	// FIFO configuration, TBD if needed
	I2S0.fifo_conf.rx_fifo_mod_force_en = 1;	// The bit should always be set to 1.receive FIFO on
	I2S0.fifo_conf.rx_fifo_mod = 1;				// receive 16-bit single channel data
	I2S0.conf_chan.rx_chan_mod = 1;				// left channel + left channel [31:0]
	// Grab 16 samples
	I2S0.sample_rate_conf.rx_bits_mod = 16;	// Set the bits to configure the bit length of I2S receiver channel.
	// Clear flags which are used in I2S serial mode
	I2S0.conf.rx_right_first = 0;				// left fast
	I2S0.conf.rx_msb_right = 0;					// msb left
	I2S0.conf.rx_msb_shift = 0;					// Set this bit to enable receiver in Philips standard mode.
	I2S0.conf.rx_mono = 0;						// Set this bit to enable receiver’s mono mode.
	I2S0.conf.rx_short_sync = 0;				// Set this bit to enable receiver in PCM standard mode.

	// Allocate I2S interrupt, keep it disabled
	esp_intr_alloc( ETS_I2S0_INTR_SOURCE, ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
		&i2s_isr, NULL, &s_i2s_intr_handle);
}

static void i2s_readStart(int index)
{
	esp_intr_disable(s_i2s_intr_handle);

	i2s_conf_reset();
	I2S0.rx_eof_num = s_buf_line_width;
	I2S0.in_link.addr = (uint32_t) &s_dma_desc[index];
	I2S0.in_link.start = 1;
	I2S0.int_clr.val = I2S0.int_raw.val;
	I2S0.int_ena.in_done = 1;		// DMA interrupt Enable when the current txlink descriptor is handled

	esp_intr_enable(s_i2s_intr_handle);
	I2S0.conf.rx_start = 1;			// receive Start
}

static void i2s_stop()
{
	I2S0.conf.rx_start = 0;
	esp_intr_disable(s_i2s_intr_handle);
	I2S0.int_ena.in_done = 0;		// DMA interrupt Disable
	s_i2s_running = false;
	i2s_conf_reset();
}

//-------------------------------------------------
esp_err_t  dma_desc_init(void)
{
	size_t buf_size = s_buf_line_width * 2;	// 2byte --> 32bit(4bytes)

	for (int i = 0; i < 2; ++i) {
		s_dma_buf[i] = (uint32_t*) malloc(buf_size);
		if (s_dma_buf[i] == NULL){
			return ESP_ERR_NO_MEM;
		}
		s_dma_desc[i].length = buf_size;     // size of a single DMA buf
		s_dma_desc[i].size = buf_size;       // total size of the chain
		s_dma_desc[i].owner = 1;
		s_dma_desc[i].sosf = 1;
		s_dma_desc[i].buf = (uint8_t*) s_dma_buf[i];
		s_dma_desc[i].offset = i;
		s_dma_desc[i].empty = 0;
		s_dma_desc[i].eof = 1;
		s_dma_desc[i].qe.stqe_next = NULL;
	}
	return ESP_OK;
}

//============= task ===================================

static void line_filter_task(void *pvParameters)
{
	while (true) {

		xSemaphoreTake(s_data_ready, portMAX_DELAY);
		int buf_idx = !s_cur_buffer;


		s_fb_idx = (s_fb_idx + 1) % 1;
		uint8_t* pfb = s_fb[s_fb_idx];
		const uint32_t* buf = s_dma_buf[buf_idx];

		for (int i = 0; i < s_buf_line_width/2; ++i) {
			uint32_t v = *buf++;	// Get 32 bit from DMA buffer
			// 1 Pixel = (2Byte i2s overhead + 2Byte pixeldata)
			*pfb++ = (uint8_t)(v & 0x000000ff);
			*pfb++ = (uint8_t)((v & 0x00ff0000)>>16);
		}
		xSemaphoreGive(s_line_ready);
	}
}

//---------- Interrupt ------------------------------
static void IRAM_ATTR i2s_isr(void* arg)	// 1 Line read done
{
	I2S0.int_clr.val = I2S0.int_raw.val;

	s_cur_buffer = !s_cur_buffer;
	++s_line_count;
	if(s_line_count == s_buf_height) {		// 1 Frame read done
		i2s_stop();
	} else {
		i2s_readStart(s_cur_buffer);
	}
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(s_data_ready, &xHigherPriorityTaskWoken);
/*	if (xHigherPriorityTaskWoken != pdFALSE) {
		portYIELD_FROM_ISR();
	}
*/
}











//--------- Screen mode -----------------------------------------------

static const struct regval_list vga_OV7670[] PROGMEM = {	// 640 x 480
	{REG_COM3,	0},			// No scaling
	{REG_COM14,	0},
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, 0},			// Down sampling nothing
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_1},	// DSP scale control Clock divide by 1
	{REG_SCALING_PCLK_DELAY,0x02},
	{0xff, 0xff}						// END MARKER
};

static const struct regval_list cif_OV7670[] PROGMEM = {	// 352 x 288
	{REG_COM3,  COM3_DCWEN},				// Enable format scaling
	{REG_COM14, COM14_DCWEN | COM14_PCLKDIV_2},	// divide by 2
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2},	// down sampling by 2 
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2},	// DSP scale control Clock divide by 2
	{REG_SCALING_PCLK_DELAY,0x02},
	{0xff, 0xff}				// END MARKER
};

static const struct regval_list qvga_OV7670[] PROGMEM = {	// 320 x 240
	{REG_COM3,  COM3_DCWEN},				// Enable format scaling
	{REG_COM14, COM14_DCWEN | COM14_PCLKDIV_2},	// divide by 2
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2},	// down sampling by 2 
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2},	// DSP scale control Clock divide by 2
	{REG_SCALING_PCLK_DELAY,0x02},
	{0xff, 0xff}				// END MARKER
};

static const struct regval_list qqvga_OV7670[] PROGMEM = {	// 160 x 120
	{REG_COM3,  COM3_DCWEN},				// Enable format scaling
	{REG_COM14, COM14_DCWEN | COM14_MANUAL | COM14_PCLKDIV_4},	// divide by 4
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_4 | SCALING_DCWCTR_HDS_by_4},	// down sampling by 4 
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_4},	// DSP scale control Clock divide by 4
	{REG_SCALING_PCLK_DELAY,0x02},
	{0xff, 0xff}				// END MARKER
};

static const struct regval_list qcif_OV7670[] PROGMEM = {
	{REG_COM3, COM3_SCALEEN | COM3_DCWEN},	// Enable format scaling
	{REG_COM3, COM3_DCWEN},					// Enable Downsampling/Dropping/Windowing
	{REG_COM14, COM14_DCWEN | COM14_MANUAL | COM14_PCLKDIV_2},	// divide by 2
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2},		// downsample by 2
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2},	// divide by 2
	{REG_SCALING_PCLK_DELAY,0x52},
	{0xff, 0xff}	/* END MARKER */
};

static const struct regval_list qqcif_OV7670[] PROGMEM = {
	{REG_COM3,COM3_SCALEEN | COM3_DCWEN},
	{REG_COM14, COM14_DCWEN | COM14_PCLKDIV_4},	// divide by 4
	{REG_SCALING_XSC,	0x3a},		// Horizontal scale factor
	{REG_SCALING_YSC,	0x35},		// Vertical scale factor
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_4 | SCALING_DCWCTR_HDS_by_4},	// down sampling by 4 
	{REG_SCALING_PCLK_DIV,  SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_4},	// DSP scale control Clock divide by 4
	{REG_SCALING_PCLK_DELAY,0x2A},
	{0xff, 0xff}	/* END MARKER */
};

//------- Color mode --------------------------------------
static const struct regval_list yuv422_OV7670[] PROGMEM = {
	{REG_RGB444, 0},	/* No RGB444 please */
	{REG_COM1, 0},
	{REG_COM15, COM15_R00FF},
	{REG_COM9, COM9_AGC_GAIN_16x | 0x08},	/* 16x gain ceiling; 0x08 is reserved bit */
	{REG_MTX1, 0x80},		/* "matrix coefficient 1" */
	{REG_MTX2, 0x80},		/* "matrix coefficient 2" */
	{REG_MTX3,    0},		/* vb */
	{REG_MTX4, 0x22},		/* "matrix coefficient 4" */
	{REG_MTX5, 0x5e},		/* "matrix coefficient 5" */
	{REG_MTX6, 0x80},		/* "matrix coefficient 6" */
	{REG_COM13,COM13_GAMMA|COM13_UVSAT|COM13_UVSWAP},
	{0xff, 0xff}		/* END MARKER */
};

static const struct regval_list rgb565_OV7670[] PROGMEM = {
//	{REG_COM7, COM7_RGB},	// select RGB mode
	{REG_RGB444, 0},	  /* No RGB444 please */
	{REG_COM1, 0x0},
	{REG_COM15, COM15_R00FF | COM15_RGB565},	// RGB565
	{REG_TSLB, 0x04},
	{REG_COM9, COM9_AGC_GAIN_16x | 0x08},	 /* 16x gain ceiling; 0x08 is reserved bit */
	{REG_MTX1, 0xb3},		 /* "matrix coefficient 1" */
	{REG_MTX2, 0xb3},		 /* "matrix coefficient 2" */
	{REG_MTX3,    0},		 /* vb */
	{REG_MTX4, 0x3d},		 /* "matrix coefficient 4" */
	{REG_MTX5, 0xa7},		 /* "matrix coefficient 5" */
	{REG_MTX6, 0xe4},		 /* "matrix coefficient 6" */
	{REG_COM13, COM13_GAMMA | COM13_UVSAT},
	{0xff, 0xff}	/* END MARKER */
};

static const struct regval_list bayerRGB_OV7670[] PROGMEM = {
//	{REG_COM7, COM7_PBAYER},
	{REG_RGB444, R444_DISABLE},
	{REG_COM15, COM15_R00FF},
	{REG_TSLB, 0x04},
	{REG_COM13, 0x08}, /* No gamma, magic rsvd bit */
	{REG_COM16, 0x3d}, /* Edge enhancement, denoise */
	{REG_REG76, 0xe1}, /* Pix correction, magic rsvd */
	{0xff, 0xff}	/* END MARKER */
};
//-------------------------------------------------------------
/*
const struct regval_list OV7670_default_regs[] PROGMEM = {
  {0x3a,0x04},{0x40,0xd0},{0x12,0x14},{0x32,0x80},{0x17,0x16},{0x18,0x04},{0x19,0x02},{0x1a,0x7b},
  {0x03,0x06},{0x0c,0x04},{0x3e,0x19},{0x70,0x3a},{0x71,0x35},{0x72,0x11},{0x73,0xf1},{0xa2,0x02},
  {0x11,0x81},{0x7a,0x20},{0x7b,0x1c},{0x7c,0x28},{0x7d,0x3c},{0x7e,0x55},{0x7f,0x68},{0x80,0x76},
  {0x81,0x80},{0x82,0x88},{0x83,0x8f},{0x84,0x96},{0x85,0xa3},{0x86,0xaf},{0x87,0xc4},{0x88,0xd7},
  {0x89,0xe8},{0x13,0xe0},{0x00,0x00},{0x10,0x00},{0x0d,0x00},{0x14,0x28},{0xa5,0x05},{0xab,0x07},
  {0x24,0x75},{0x25,0x63},{0x26,0xA5},{0x9f,0x78},{0xa0,0x68},{0xa1,0x03},{0xa6,0xdf},{0xa7,0xdf},
  {0xa8,0xf0},{0xa9,0x90},{0xaa,0x94},{0x13,0xe5},{0x0e,0x61},{0x0f,0x4b},{0x16,0x02},{0x1e,0x37}, //{0x1e,0x17}
  {0x21,0x02},{0x22,0x91},{0x29,0x07},{0x33,0x0b},{0x35,0x0b},{0x37,0x1d},{0x38,0x71},{0x39,0x2a},
  {0x3c,0x78},{0x4d,0x40},{0x4e,0x20},{0x69,0x00},{0x6b,0x00},{0x74,0x19},{0x8d,0x4f},{0x8e,0x00},
  {0x8f,0x00},{0x90,0x00},{0x91,0x00},{0x92,0x00},{0x96,0x00},{0x9a,0x80},{0xb0,0x84},{0xb1,0x0c},
  {0xb2,0x0e},{0xb3,0x82},{0xb8,0x0a},{0x43,0x14},{0x44,0xf0},{0x45,0x34},{0x46,0x58},{0x47,0x28},
  {0x48,0x3a},{0x59,0x88},{0x5a,0x88},{0x5b,0x44},{0x5c,0x67},{0x5d,0x49},{0x5e,0x0e},{0x64,0x04},
  {0x65,0x20},{0x66,0x05},{0x94,0x04},{0x95,0x08},{0x6c,0x0a},{0x6d,0x55},{0x6e,0x11},{0x6f,0x9f},
  {0x6a,0x40},{0x01,0x40},{0x02,0x40},{0x13,0x8f},{0x15,0x22},{0x4f,0x80},{0x50,0x80},{0x51,0x00},
  {0x52,0x22},{0x53,0x5e},{0x54,0x80},{0x58,0x9e},{0x41,0x08},{0x3f,0x00},{0x75,0x05},{0x76,0xe1},
  {0x4c,0x00},{0x77,0x01},{0x3d,0xc2},{0x4b,0x09},{0xc9,0x60},{0x41,0x38},{0x56,0x40},{0x34,0x11},
  {0x3b,0x02},{0xa4,0x89},{0x96,0x00},{0x97,0x30},{0x98,0x20},{0x99,0x30},{0x9a,0x84},{0x9b,0x29},
  {0x9c,0x03},{0x9d,0x4c},{0x9e,0x3f},{0x78,0x04},{0x79,0x01},{0xc8,0xf0},{0x79,0x0f},{0xc8,0x00},
  {0x79,0x10},{0xc8,0x7e},{0x79,0x0a},{0xc8,0x80},{0x79,0x0b},{0xc8,0x01},{0x79,0x0c},{0xc8,0x0f},
  {0x79,0x0d},{0xc8,0x20},{0x79,0x09},{0xc8,0x80},{0x79,0x02},{0xc8,0xc0},{0x79,0x03},{0xc8,0x40},
  {0x79,0x05},{0xc8,0x30},{0x79,0x26},{0x09,0x03},{0x3b,0x42},{0xff,0xff},
};
*/
 const struct regval_list OV7670_default2_regs[] PROGMEM = {
	{REG_TSLB, 0x04},
	{REG_COM15, COM15_R00FF | COM15_RGB565},
	{REG_COM7, COM7_FMT_QVGA | COM7_RGB},
	{REG_HREF, 0x80},
	{REG_HSTART, 0x16},
	{REG_HSTOP, 0x04},
	{REG_VSTART, 0x02},
	{REG_VSTOP, 0x7b},
	{REG_VREF, 0x06},
	{REG_COM3, COM3_DCWEN},
	{REG_COM14, COM14_DCWEN | COM14_MANUAL | COM14_PCLKDIV_2},
	{REG_SCALING_XSC, 0x3a},
	{REG_SCALING_YSC, 0x35},
	{REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2},
	{REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2},
	{REG_SCALING_PCLK_DELAY, 0x02},
	{REG_CLKRC, CLK_RSVD | 0x01},	// clock divid 2
	// Gamma curve values
	{REG_SLOP, 0x20},	// SLOP = (256-GAM15)*40/30
	{REG_GAM1, 0x1c},
	{REG_GAM2, 0x28},
	{REG_GAM3, 0x3c},
	{REG_GAM4, 0x55},
	{REG_GAM5, 0x68},
	{REG_GAM6, 0x76},
	{REG_GAM7, 0x80},
	{REG_GAM8, 0x88},
	{REG_GAM9, 0x8f},
	{REG_GAM10,0x96},
	{REG_GAM11,0xa3},
	{REG_GAM12,0xaf},
	{REG_GAM13,0xc4},
	{REG_GAM14,0xd7},
	{REG_GAM15,0xe8},

	// AGC(Auto Gain Celling) and AEC(Auto Exposure(露出) Control) parameters.
	// AGC/AEC parameters.  Note we start by disabling those features,then turn them only after tweaking the values.
	{REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT},

	{REG_GAIN, 0x00},
	{REG_AECH, 0x00},
	{REG_COM4, COM4_AEC_FULL},
	{REG_COM9, COM9_AGC_GAIN_8x | 0x08},
	{REG_BD50MAX, 0x05},
	{REG_BD60MAX, 0x07},
	{REG_AEW, 0x75},	// AGC upper limit
	{REG_AEB, 0x63},	// AGC lower limit
	{REG_VPT, 0xA5},	// AGC/AEC fast mode op region
	{REG_HAECC1, 0x78},	// Hist AEC/AGC control 1
	{REG_HAECC2, 0x68},	// Hist AEC/AGC control 2
	{0xa1, 0x03},		// Reserved
	{REG_HAECC3, 0xdf},	// Hist AEC/AGC control 3
	{REG_HAECC4, 0xdf},	// Hist AEC/AGC control 4
	{REG_HAECC5, 0xf0},	// Hist AEC/AGC control 5
	{REG_HAECC6, 0x90},	// Hist AEC/AGC control 6
	{REG_HAECC7, 0x94},	// Hist AEC/AGC control 7

	{REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC},

	{REG_COM5, 0x61},	// Reserved
	{REG_COM6, 0x4b},	// Reset all timing when format changes
	{0x16,0x02},		// Reserved
	{REG_MVFP, 0x37},//0x07},
	{REG_ADCCTR1, 0x02},// Reserved
	{REG_ADCCTR2, 0x91},// Reserved
	{0x29,0x07},{0x33,0x0b},{0x35,0x0b},{0x37,0x1d},{0x38,0x71},{0x39,0x2a},	// All Reserved
	{REG_COM12, 0x78},
	{0x4d,0x40},{0x4e,0x20},	// Reserved

	{REG_GFIX, 0x00},	// AWB Pre gain control
	{REG_DBLV, DBLV_BYPASS},	// PLL control,Regulator control
	{REG_REG74, 0x19},	// Digital gain manual control
	{0x8d,0x4f},{0x8e,0x00},{0x8f,0x00},{0x90,0x00},{0x91,0x00},	// Reserved
	{REG_DM_LNL, 0x00},
	{0x96,0x00},{0x9a,0x80},{0xb0,0x84},
	{REG_ABLC1, 0x0c},
	{0xb2,0x0e},
	{REG_THL_ST,0x82},
	{0xb8,0x0a},

	{REG_AWBC1, 0x14},	//AWB Control 1
	{REG_AWBC2, 0xf0},	//AWB Control 2
	{REG_AWBC3, 0x34},	//AWB Control 3
	{REG_AWBC4, 0x58},	//AWB Control 4
	{REG_AWBC5, 0x28},	//AWB Control 5
	{REG_AWBC6, 0x3a},	//AWB Control 6
	{REG_AWBC7, 0x88},	//AWB Control 7
	{REG_AWBC8, 0x88},	//AWB Control 8
	{REG_AWBC9, 0x44},	//AWB Control 9
	{REG_AWBC10,0x67},	//AWB Control 10
	{REG_AWBC11,0x49},	//AWB Control 11
	{REG_AWBC12,0x0e},	//AWB Control 12

//	{REG_LCC1, 0x00},	// Lens Correction Option 1
//	{REG_LCC2, 0x00},	// Lens Correction Option 2
	{REG_LCC3, 0x04},	// Lens Correction Option 3
	{REG_LCC4, 0x20},	// Lens Correction Option 4
	{REG_LCC5, 0x05},	// Lens Correction Option 5
	{REG_LCC6, 0x04},	// Lens Correction Option 6
	{REG_LCC7, 0x08},	// Lens Correction Option 7
	{REG_AWBCTR3, 0x0a},
	{REG_AWBCTR2, 0x55},
	{REG_AWBCTR1, 0x11},
	{REG_AWBCTR0, 0x9f},
	{REG_GGAIN,	0x40},	// AWB Green gain
	{REG_BLUE,	0x40},	// AWB Blue gain (00-ff)
	{REG_RED,	0x40},	// AWB Red gain (00-ff)

	{REG_COM8, COM8_FASTAEC | COM8_RSVD | COM8_AGC | COM8_AWB | COM8_AEC},

	{REG_COM10, COM10_PCLK_HB | COM10_VS_NEG},	// PCLK does not toggle during horizontal blank & VSYNC negative

	{REG_MTX1, 0x80},	// Matrix Coefficient 1
	{REG_MTX2, 0x80},	// Matrix Coefficient 2
	{REG_MTX3, 0x00},	// Matrix Coefficient 3
	{REG_MTX4, 0x22},	// Matrix Coefficient 4
	{REG_MTX5, 0x5e},	// Matrix Coefficient 5
	{REG_MTX6, 0x80},	// Matrix Coefficient 6
	{REG_MTXS, 0x9e},	// Matrix Coefficient Sign

	{REG_COM16, COM16_AWBGAIN},	// AWB gain enable
	{REG_EDGE,	0x00},	// Edge enhancement factor
	{REG_REG75, 0x05},	// Edge enhanced lower limit
	{REG_REG76, 0xe1},	// Edge enhanced higher limit ,Black/white pixcel correction enable
	{REG_DNSTH, 0x00},	// De-noise Threshold
	{REG_REG77, 0x01},	// Offset, de-noise range control
	{REG_COM13, 0xc2},	// Gamma enable, UV saturation auto adjustment
	{0x4b, 0x09},
	{REG_SATCTR, 0x60},	// UV saturatin control min
	{REG_COM16, COM16_YUV_ENHANC | COM16_DE_NOISE | COM16_AWBGAIN},
	{REG_CONTRAS, 0x40},	// Contrast Control
	{0x34, 0x11},

	{REG_COM11, COM11_EXP},	// Exposure timing can be less than limit of banding filter when light is too strong	
 	{REG_NT_CTRL, 0x89},	// Auto frame rate adjustment dummy row selection

	// Magic setting
	{0x96,0x00},{0x97,0x30},{0x98,0x20},
	{0x99,0x30},{0x9a,0x84},{0x9b,0x29},
	{0x9c,0x03},{0x9d,0x4c},{0x9e,0x3f},
	{0x78,0x04},
	{0x79,0x01},{0xc8,0xf0},
	{0x79,0x0f},{0xc8,0x00},
	{0x79,0x10},{0xc8,0x7e},
	{0x79,0x0a},{0xc8,0x80},
	{0x79,0x0b},{0xc8,0x01},
	{0x79,0x0c},{0xc8,0x0f},
	{0x79,0x0d},{0xc8,0x20},
	{0x79,0x09},{0xc8,0x80},
	{0x79,0x02},{0xc8,0xc0},
	{0x79,0x03},{0xc8,0x40},
	{0x79,0x05},{0xc8,0x30},
	{0x79,0x26},

	{REG_COM2, COM2_OUT_DRIVE_4x},			// Output Drive Capability 4x
	{REG_COM11, COM11_FR_BY_4 | COM11_EXP},	// 1/4 normal mode frame rate, Exposure timing 

	{0xff,0xff},
};

//---------------------------------------------------------

GitHubOV7670::GitHubOV7670(void){
}

GitHubOV7670::~GitHubOV7670(void){
}

void GitHubOV7670::conf_setFrameSize(uint8_t res){
	switch(res){
		case VGA:
			cam_conf.frame_width = 640;
			cam_conf.frame_height = 480;
			break;
		case QVGA:
			cam_conf.frame_width = 320;
			cam_conf.frame_height = 240;
			break;
		case QQVGA:
			cam_conf.frame_width = 160;
			cam_conf.frame_height = 120;
			break;

		case CIF:
			cam_conf.frame_width = 352;
			cam_conf.frame_height = 288;
			break;
		case QCIF:
			cam_conf.frame_width = 176;
			cam_conf.frame_height = 144;
			break;
		case QQCIF:
			cam_conf.frame_width = 88;
			cam_conf.frame_height = 72;
			break;
	}
}

void GitHubOV7670::reset(void){
	wrReg(REG_COM7, COM7_RESET);	// All reg reset
	delay(100);

	Serial.println(F("--- Default setting -----"));
	wrRegs(OV7670_default2_regs);	// Camera Default setting
	Serial.println(F("--- Resolution setting -----"));
	setResolution( _resolution );	// 解像度設定
	Serial.println(F("--- ColorMode setting -----"));
	setColor( _colormode );			// カラーモード設定
	setPCLK(1, DBLV_CLK_x4);		// PCLK 設定 : 10MHz / (pre+1) * 4 --> 20MHz
}

esp_err_t GitHubOV7670::init(const github_camera_config_t *value, uint8_t res, uint8_t colmode){
	memcpy(&cam_conf, value, sizeof(cam_conf));
	_resolution = res;
	_colormode = colmode;

//	Wire.begin();
//	Wire.setClock(400000);
//	delay(1000);

	// XCLOK 出力
	pinMode(cam_conf.XCLK, OUTPUT);
	digitalWrite(cam_conf.XCLK, LOW);
	ledcSetup(cam_conf.ledc_channel, cam_conf.xclk_freq_hz, 2 );
	ledcAttachPin(cam_conf.XCLK, cam_conf.ledc_channel);
	ledcWrite( cam_conf.ledc_channel, 2 );

	conf_setFrameSize(res);

	switch(colmode){
		case YUV422:
		case RGB565:
			cam_conf.pixel_byte_num = 2;
			break;
		case BAYER_RAW:
			cam_conf.pixel_byte_num = 1;
			break;
		case PBAYER_RAW:
			cam_conf.pixel_byte_num = 1;	// ???
			break;
	}

	esp_err_t err = I2S_camera_init(&cam_conf);	// I2S initialize
	if (err != ESP_OK) {
        Serial.println(F(" I2S Camera init ERROR"));
		return err;
	}

	reset();

	Serial.println(F("---- Camera init ok! ----"));
	return err;
}

void GitHubOV7670::setResolution(uint8_t res){
	uint8_t temp;
	uint16_t vstart,vstop,hstart,hstop;
	uint8_t pclkdiv;

	conf_setFrameSize(res);

	temp = rdReg(REG_COM7);
	temp &= 0b01000111;

	switch(res){
		case VGA:
			wrReg(REG_COM7, temp |  COM7_FMT_VGA);	// set Resolution
			wrRegs(vga_OV7670);
			hstart = 158;
			vstart = 10;
			pclkdiv = 4;
			break;
		case QVGA:
			wrReg(REG_COM7, temp | COM7_FMT_QVGA);	// set Resolution
			wrRegs(qvga_OV7670);
			hstart = 180;
			vstart = 12;
			pclkdiv = 2;
			break;
		case QQVGA:
			wrReg(REG_COM7, temp | COM7_FMT_QVGA);	// set Resolution
			wrRegs(qqvga_OV7670);
			hstart = 190;
			vstart = 10;
			pclkdiv = 1;
			break;

		case CIF:
			wrReg(REG_COM7, temp | COM7_FMT_CIF);	// set Resolution
			wrRegs(cif_OV7670);
			hstart = 178;
			vstart = 14;
			pclkdiv = 2;
			break;
		case QCIF:
			wrReg(REG_COM7, temp | COM7_FMT_QCIF);	// set Resolution
			wrRegs(qcif_OV7670);
			hstart = 456;
			vstart = 14;
			pclkdiv = 1;
			break;
		case QQCIF:
			wrReg(REG_COM7, temp | COM7_FMT_QCIF);	// set Resolution
			wrRegs(qcif_OV7670);
			hstart = 456;
			vstart = 14;
			pclkdiv = 1;
			break;
	}
	setPCLK( pclkdiv, DBLV_CLK_x4);
	setHStart(hstart);
	setVStart(vstart);
}

void GitHubOV7670::setHStart(uint16_t hstart){
	uint16_t hstop;
	switch(_resolution){
		case VGA:
		case QVGA:
		case QQVGA:
			hstop = 640;
			break;
		case CIF:
			hstop = 704;
			break;
		case QCIF:
			hstop = 352;
			break;
		case QQCIF:
			hstop = 176;
			break;
	}
	hstop = ( hstart + hstop ) % 784;
	wrReg(REG_HSTART, (uint8_t)(hstart / 8));
	wrReg(REG_HSTOP, (uint8_t)(hstop / 8));
	wrReg(REG_HREF, 0x80 | (uint8_t)((hstop % 8) << 3) | (uint8_t)(hstart % 8));
	rewrCLKRC();
}

void GitHubOV7670::setVStart(uint16_t vstart){
	uint16_t vstop;
	vstop = vstart + 480;
	wrReg(REG_VSTART, (uint8_t)(vstart/4));
	wrReg(REG_VSTOP, (uint8_t)(vstop/4));
	wrReg(REG_VREF,	(uint8_t)((vstop%4)<<2) | (uint8_t)(vstart%4));
	rewrCLKRC();
}

uint16_t GitHubOV7670::getHStart(void){
	uint16_t hstart;
	hstart = (uint16_t)rdReg(REG_HSTART) * 8 + (uint16_t)( rdReg(REG_HREF) & 0x07);
	return hstart;
}
uint16_t GitHubOV7670::getVStart(void){
	uint16_t vstart;
	vstart = (uint16_t)rdReg(REG_VSTART) * 4 + (uint16_t)( rdReg(REG_VREF) & 0x03);
	return vstart;
}

void GitHubOV7670::stop(void){
	ledcDetachPin(cam_conf.XCLK);
}

uint16_t* GitHubOV7670::getLine(uint16_t lineno){
	uint16_t *p_buf;
	p_buf = camera_getLine(lineno);
	return p_buf;
}

bool GitHubOV7670::getLines(uint16_t lineno, uint8_t *buf, uint16_t n){
	uint16_t i,*p_buf;
	uint16_t wb = cam_conf.frame_width * cam_conf.pixel_byte_num;

	for(i=0; i<n; i++){
		p_buf = camera_getLine(lineno + i);
		if( p_buf == NULL ) return false;
		memcpy( &buf[ i * wb ], (uint8_t*)p_buf, wb);
	}
	return true;
}

bool GitHubOV7670::getFrame(uint8_t *buf){
	return getLines( 1, buf, cam_conf.frame_height );
}

void GitHubOV7670::setColor(uint8_t colormode){
	uint8_t temp;

	temp = rdReg( REG_COM7 ) & 0b01111010;
	wrReg( REG_COM7, temp | colormode );	// set colormode

	switch(colormode){
		case YUV422:
			wrRegs( yuv422_OV7670 );		// ather reg set
			cam_conf.pixel_byte_num = 2;
			break;

		case RGB565:
			wrRegs( rgb565_OV7670 );
			cam_conf.pixel_byte_num = 2;
			break;

		case BAYER_RAW:
			wrRegs( bayerRGB_OV7670 );
			cam_conf.pixel_byte_num = 1;
			break;

		case PBAYER_RAW:

			cam_conf.pixel_byte_num = 1;	// ???
			break;
	}
	rewrCLKRC();						//according to the Linux kernel driver PCLK needs rewriting
}

void GitHubOV7670::setPCLK(uint8_t pre, uint8_t pll){
	uint8_t temp;
	temp = rdReg(REG_CLKRC);
	wrReg(REG_CLKRC, (temp & 0b10000000) | pre);	// F(internal clock) = F(input clock) / (Bit[0-5] + 1)
	temp = rdReg(REG_DBLV);
	wrReg(REG_DBLV, (temp & 0b00111111) | pll);	// val=0:Bypass PLL,1:Input clock x4,2: x6,3:x8	
	rewrCLKRC();
}

void GitHubOV7670::rewrCLKRC(void){
	uint8_t temp;
	temp = rdReg(REG_CLKRC);
	wrReg(REG_CLKRC, temp);			//according to the Linux kernel driver rgb565 PCLK needs rewriting
}

void GitHubOV7670::vflip( bool enable){
	uint8_t temp;
	temp = rdReg(REG_MVFP) & 0b11001111;
	if(enable) temp |= MVFP_MIRROR |  MVFP_FLIP;
	wrReg( REG_MVFP, temp );
}

uint16_t GitHubOV7670::getMID(void){
	uint16_t id;
	id = (uint16_t)rdReg(REG_MIDH)<<8 | (uint16_t)rdReg(REG_MIDL);
	return id;
}

uint16_t GitHubOV7670::getPID(void){
	uint16_t id;
	id = (uint16_t)rdReg(REG_PID)<<8 | (uint16_t)rdReg(REG_VER);
	return id;
}

void GitHubOV7670::setGain(uint16_t val){
	if(val>1023) val = 1023;
	uint8_t temp;
	wrReg(REG_GAIN, val % 256);
	temp = rdReg(REG_VREF) & 0x3F;
	wrReg(REG_VREF, temp | ((val / 256) << 6));
}

uint16_t GitHubOV7670::getGain(void){
	uint16_t val;
	val = (uint16_t)rdReg(REG_GAIN) + (uint16_t)((rdReg(REG_VREF) & 0x3F)) * 256;
	return val;
}

void GitHubOV7670::setAGC(uint8_t val){
	uint8_t temp;
	temp = rdReg(REG_COM8) & ~(COM8_FASTAEC | COM8_AGC);
	if(val == 1) temp |= COM8_FASTAEC | COM8_AGC;
	wrReg(REG_COM8, temp);
}
bool GitHubOV7670::getAGC(void){
	uint8_t temp;
	temp = rdReg(REG_COM8);
	if( (temp & COM8_AGC) != 0) return true;
	return false;
}
void GitHubOV7670::setAWB(uint8_t val){
	uint8_t temp;
	temp = rdReg(REG_COM8) & ~COM8_AWB;
	if(val == 1) temp |= COM8_AWB;
	wrReg(REG_COM8, temp);
}
bool GitHubOV7670::getAWB(void){
	uint8_t temp;
	temp = rdReg(REG_COM8);
	if( (temp & COM8_AWB) != 0) return true;
	return false;
}
void GitHubOV7670::setAWBB(uint8_t val){
	wrReg(REG_BLUE,val);
}
void GitHubOV7670::setAWBR(uint8_t val){
	wrReg(REG_RED,val);
}
void GitHubOV7670::setAWBG(uint8_t val){
	wrReg(REG_GGAIN,val);
}
void GitHubOV7670::setAEC(uint8_t val){
	uint8_t temp;
	temp = rdReg(REG_COM8) & ~(COM8_FASTAEC | COM8_AEC);
	if(val == 1) temp |= COM8_FASTAEC | COM8_AEC;
	wrReg(REG_COM8, temp);
}
bool GitHubOV7670::getAEC(void){
	uint8_t temp;
	temp = rdReg(REG_COM8);
	if( (temp & COM8_AEC) != 0) return true;
	return false;
}
void GitHubOV7670::setBright(int8_t val){
/*	uint8_t temp;
	temp = rdReg(REG_COM8) & ~COM8_AEC;
	wrReg(REG_COM8, temp);
*/
	wrReg(REG_BRIGHT, (uint8_t)val);
}
int8_t GitHubOV7670::getBright(void){
	int8_t temp;
	temp = (int8_t)rdReg(REG_BRIGHT);
	return temp;
}
void GitHubOV7670::setContrast(uint8_t val){
	wrReg(REG_CONTRAS, val);
}
uint8_t GitHubOV7670::getContrast(void){
	uint8_t temp;
	temp = rdReg(REG_CONTRAS);
	return temp;
}
void GitHubOV7670::setExposure(uint16_t val){
	uint8_t temp;
	temp = rdReg(REG_COM1) & 0x03;
	wrReg(REG_COM1, temp | (uint8_t)(val % 4));
	wrReg(REG_AECH, (uint8_t)((val / 4) % 256));
	temp = rdReg(REG_AECHH) & 0x3F;
	wrReg(REG_AECHH, temp | (uint8_t)(val / 1024));
}

void GitHubOV7670::colorbar(bool on){
	uint8_t temp;
	temp = rdReg(REG_COM17);
	if(on)
		wrReg(REG_COM17, temp | COM17_CBAR);
	else
		wrReg(REG_COM17, temp & ~COM17_CBAR);
}

void GitHubOV7670::colorbar_super(bool on){
	uint8_t temp;
	temp = rdReg(REG_COM7);
	if(on)
		wrReg(REG_COM7, temp | COM7_CBAR);
	else
		wrReg(REG_COM7, temp & ~COM7_CBAR);
}

//----------------------------------------------
void GitHubOV7670::wrReg(uint8_t reg, uint8_t dat){
	uint8_t rdat;

	Wire.beginTransmission(OV7670_ADDR);
	Wire.write(reg);
//	delay(20);
	Wire.write(dat);
//	delay(30);
	Wire.endTransmission();
	Serial.printf("i2c write reg:%02X data:%02X\n\r",reg,dat);

//	rdat = rdReg(reg);
}

uint8_t GitHubOV7670::rdReg(uint8_t reg){
	uint8_t dat;

	Wire.beginTransmission(OV7670_ADDR);
	Wire.write(reg);
//	delay(20);
	Wire.endTransmission(true);
//	delay(20);
	Wire.requestFrom(OV7670_ADDR, 1, true);
//	delay(20);
	dat = Wire.read();
//	Wire.endTransmission();

	Serial.printf("i2c read reg:%02X data:%02X\n\r",reg,dat);

	return dat;
}

void GitHubOV7670::wrRegs(const struct regval_list *reglist){
	const struct regval_list *next = reglist;
	uint8_t val;

	for(;;){
		uint8_t reg_addr = pgm_read_byte(&next->reg_num);
		uint8_t reg_val = pgm_read_byte(&next->value);
		if((reg_addr==0xff)&&(reg_val==0xff))			// end marker
			break;
		wrReg(reg_addr, reg_val);
		next++;
	}
	delay(30);
}

#endif // OV7670_NONFIFO_H
