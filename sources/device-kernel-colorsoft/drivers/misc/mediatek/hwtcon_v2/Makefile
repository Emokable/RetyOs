ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwtcon_v2/hal

ifdef CONFIG_PMIC_FITI_V2
	ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/fiti_high
else
	ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/fiti
endif
ccflags-y += -I$(srctree)/include/soc
ccflags-y += -I$(srctree)/include/linux/
ccflags-y += -I$(srctree)/lib/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v3
ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v3/mt8512
ccflags-y += -I$(srctree)/drivers/media/platform/mtk-mdp
ccflags-y += -I$(srctree)/drivers/media/platform/mtk-vcu
ccflags-y += -I$(srctree)/drivers/misc/mediatek/
ccflags-y += -I$(srctree)/drivers/media/platform/mtk-imgrz
ccflags-y += -I$(srctree)/drivers/regulator/

ccflags-y += -DHWTCON_M4U_SUPPORT
# ccflags-y += -DFPGA_EARLY_PORTING
# ccflags-y += -DMARKER_V2_ENABLE
ccflags-y += -DCMDQ_RELEASE_LUT

MODULE_NAME = hwtcon_v2

$(MODULE_NAME)-objs := hwtcon_epd.o
$(MODULE_NAME)-objs += hwtcon_driver.o
$(MODULE_NAME)-objs += hwtcon_fb.o
$(MODULE_NAME)-objs += hwtcon_rect.o
$(MODULE_NAME)-objs += hwtcon_core.o
$(MODULE_NAME)-objs += hwtcon_debug.o
$(MODULE_NAME)-objs += hwtcon_mdp.o
$(MODULE_NAME)-objs += hwtcon_file.o
$(MODULE_NAME)-objs += hwtcon_lightbox.o
$(MODULE_NAME)-objs += decompress_inflate.o
$(MODULE_NAME)-objs += hwtcon_extra_feature.o
$(MODULE_NAME)-objs += hwtcon_cmdq.o

#HAL code
$(MODULE_NAME)-objs += hal/hwtcon_hal.o
$(MODULE_NAME)-objs += hal/hwtcon_dpi_config.o
$(MODULE_NAME)-objs += hal/hwtcon_tcon_config.o
$(MODULE_NAME)-objs += hal/hwtcon_pipeline_config.o
$(MODULE_NAME)-objs += hal/hwtcon_regal_config.o
$(MODULE_NAME)-objs += hal/hwtcon_wf_lut_config.o
$(MODULE_NAME)-objs += hal/hwtcon_wf_lut_rdma_config.o

obj-$(CONFIG_MTK_HWTCON_V2) += $(MODULE_NAME).o
