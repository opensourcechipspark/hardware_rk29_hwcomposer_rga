/****************************************************************************
*
*    Copyright (c) 2005 - 2011 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/13/2011. Do not edit!!!
*
*****************************************************************************/




#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "rk_hwcomposer.h"

#include <hardware/hardware.h>


#include <stdlib.h>
#include <errno.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <sync/sync.h>
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#include  <hardware/hal_public.h>
#include  <linux/fb.h>
#else
#include "../libgralloc_ump/gralloc_priv.h"
#endif
#include <time.h>
#include <poll.h>
#include "rk_hwcomposer_hdmi.h"
#include <ui/PixelFormat.h>
#include <sys/stat.h>
#include "hwc_ipp.h"
#include "hwc_rga.h"
#define MAX_DO_SPECIAL_COUNT        5
#define RK_FBIOSET_ROTATE            0x5003 
#define FPS_NAME                    "com.aatt.fpsm"
#define BOTTOM_LAYER_NAME           "NavigationBar"
#define TOP_LAYER_NAME              "StatusBar"
#define WALLPAPER                   "ImageWallpaper"
//#define ENABLE_HDMI_APP_LANDSCAP_TO_PORTRAIT
static int SkipFrameCount = 0;

static hwcContext * _contextAnchor = NULL;
static int bootanimFinish = 0;
static hwbkupmanage bkupmanage;
#undef LOGV
#define LOGV(...)


static int
hwc_blank(
            struct hwc_composer_device_1 *dev,
            int dpy,
            int blank);
static int
hwc_query(
        struct hwc_composer_device_1* dev,
        int what,
        int* value);

static int hwc_event_control(
                struct hwc_composer_device_1* dev,
                int dpy,
                int event,
                int enabled);

static int
hwc_prepare(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
    );


static int
hwc_set(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t  ** displays
    );

static int
hwc_device_close(
    struct hw_device_t * dev
    );

static int
hwc_device_open(
    const struct hw_module_t * module,
    const char * name,
    struct hw_device_t ** device
    );

static struct hw_module_methods_t hwc_module_methods =
{
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM =
{
    common:
    {
        tag:           HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 2,
        id:            HWC_HARDWARE_MODULE_ID,
        name:          "Hardware Composer Module",
        author:        "Vivante Corporation",
        methods:       &hwc_module_methods,
        dso:           NULL,
        reserved:      {0, }
    }
};

static int LayerZoneCheck( hwc_layer_1_t * Layer)
{
    hwc_region_t * Region = &(Layer->visibleRegionScreen);
    hwc_rect_t const * rects = Region->rects;
    int i;
    for (i = 0; i < (unsigned int) Region->numRects ;i++)
    {
        LOGV("checkzone=%s,[%d,%d,%d,%d]", \
            Layer->LayerName,rects[i].left,rects[i].top,rects[i].right,rects[i].bottom );
        if(rects[i].left < 0 || rects[i].top < 0
           || rects[i].right > _contextAnchor->fbWidth
           || rects[i].bottom > _contextAnchor->fbHeight)
        {
            return -1;
        }
    }

    return 0;
}

void hwc_sync(hwc_display_contents_1_t  *list)
{
  if (list == NULL)
  {
    return ;
  }
  
  for (int i=0; i<list->numHwLayers; i++)
  {
     if (list->hwLayers[i].acquireFenceFd>0)
     {
       sync_wait(list->hwLayers[i].acquireFenceFd,-1);
       ALOGV("fenceFd=%d,name=%s",list->hwLayers[i].acquireFenceFd,list->hwLayers[i].layerName);
     }

  }
}

void hwc_sync_release(hwc_display_contents_1_t  *list)
{
  for (int i=0; i<list->numHwLayers; i++)
  {
    hwc_layer_1_t* layer = &list->hwLayers[i];
    if (layer == NULL)
    {
      return ;
    }
    if (layer->acquireFenceFd>0)
    {
      ALOGV(">>>close acquireFenceFd:%d,layername=%s",layer->acquireFenceFd,layer->LayerName);
      close(layer->acquireFenceFd);
      list->hwLayers[i].acquireFenceFd = -1;
    }
  }

  if (list->outbufAcquireFenceFd>0)
  {
    ALOGV(">>>close outbufAcquireFenceFd:%d",list->outbufAcquireFenceFd);
    close(list->outbufAcquireFenceFd);
    list->outbufAcquireFenceFd = -1;
  }
  
}
#if 1
static int layer_seq = 0;

int rga_video_copybit(struct private_handle_t *src_handle,
			 struct private_handle_t *dst_handle)
{
    struct tVPU_FRAME *pFrame  = NULL;
    struct rga_req  rga_cfg;
    int   rga_fd = _contextAnchor->engine_fd;
    if (!rga_fd)
    {
       return -1;
    }
    if (!src_handle || !dst_handle)
    {
      return -1;
    }
    struct private_handle_t *handle = src_handle;
    pFrame = (tVPU_FRAME *)GPU_BASE;

    //backup video
    memset(&_contextAnchor->video_frame, 0, sizeof(_contextAnchor->video_frame));
    _contextAnchor->video_frame.vpu_handle = (void*)GPU_BASE;
    memcpy(&_contextAnchor->video_frame.vpu_frame,(void*)GPU_BASE,sizeof(tVPU_FRAME));
    
    memset(&rga_cfg,0x0,sizeof(rga_req));
    ALOGV("videopFrame addr=%x,FrameWidth=%d,FrameHeight=%d",pFrame->FrameBusAddr[0],pFrame->FrameWidth,pFrame->FrameHeight);
    rga_cfg.src.yrgb_addr =  (int)pFrame->FrameBusAddr[0] + 0x60000000;
    rga_cfg.src.uv_addr  = rga_cfg.src.yrgb_addr + (( pFrame->FrameWidth + 15) & ~15) * ((pFrame->FrameHeight + 15) & ~15);
    rga_cfg.src.v_addr   =  rga_cfg.src.uv_addr;
    rga_cfg.src.vir_w =  ((pFrame->FrameWidth + 15) & ~15);
    rga_cfg.src.vir_h = ((pFrame->FrameHeight + 15) & ~15);
    rga_cfg.src.format = RK_FORMAT_YCbCr_420_SP;

    rga_cfg.src.act_w = pFrame->FrameWidth;
    rga_cfg.src.act_h = pFrame->FrameHeight;
    rga_cfg.src.x_offset = 0;
    rga_cfg.src.y_offset = 0;

 	ALOGD_IF(0, "copybit src info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
          "vir_w=%d,vir_h=%d,format=%d,"
          "act_x_y_w_h [%d,%d,%d,%d] ",
          rga_cfg.src.yrgb_addr, rga_cfg.src.uv_addr ,rga_cfg.src.v_addr,
          rga_cfg.src.vir_w ,rga_cfg.src.vir_h ,rga_cfg.src.format ,
          rga_cfg.src.x_offset ,
          rga_cfg.src.y_offset,
          rga_cfg.src.act_w ,
          rga_cfg.src.act_h
        );


#ifdef TARGET_BOARD_PLATFORM_RK30XXB
    rga_cfg.dst.yrgb_addr = GPU_BASE; //dsthandle->base;//(int)(fixInfo.smem_start + dsthandle->offset);
    rga_cfg.dst.vir_w =   (GPU_WIDTH + 31) & ~31;//((srcandle->iWidth*2 + (8-1)) & ~(8-1))/2 ;  /* 2:RK_FORMAT_RGB_565 ,8:????*///srcandle->width;
#else
    rga_cfg.dst.yrgb_addr = GPU_BASE; //dsthandle->base;//(int)(fixInfo.smem_start + dsthandle->offset);
    rga_cfg.dst.vir_w =   ((GPU_WIDTH*2 + (8-1)) & ~(8-1))/2 ;  
#endif
    rga_cfg.dst.vir_h =  GPU_HEIGHT;
    rga_cfg.dst.act_w = GPU_WIDTH;//Rga_Request.dst.vir_w;
    rga_cfg.dst.act_h = GPU_HEIGHT;//Rga_Request.dst.vir_h;
    rga_cfg.dst.uv_addr  = 0;//Rga_Request.dst.yrgb_addr + (( srcandle->width + 15) & ~15) * ((srcandle->height + 15) & ~15);
    rga_cfg.dst.v_addr   = rga_cfg.dst.uv_addr;
    //Rga_Request.dst.format = RK_FORMAT_RGB_565;
    rga_cfg.clip.xmin = 0;
    rga_cfg.clip.xmax = rga_cfg.dst.vir_w - 1;
    rga_cfg.clip.ymin = 0;
    rga_cfg.clip.ymax = rga_cfg.dst.vir_h - 1;
    rga_cfg.dst.x_offset = 0;
    rga_cfg.dst.y_offset = 0;

    rga_cfg.sina = 0;
    rga_cfg.cosa = 0x10000;

	char property[PROPERTY_VALUE_MAX];
	int gpuformat = HAL_PIXEL_FORMAT_RGB_565;
	if (property_get("sys.yuv.rgb.format", property, NULL) > 0) 
    {
	    gpuformat = atoi(property);
	}
	if(gpuformat == HAL_PIXEL_FORMAT_RGBA_8888)
    {
    	rga_cfg.dst.format = RK_FORMAT_RGBA_8888;//RK_FORMAT_RGB_565;
	}
    else if(gpuformat == HAL_PIXEL_FORMAT_RGBX_8888)
    {
    	rga_cfg.dst.format = RK_FORMAT_RGBX_8888;
    }
    else if(gpuformat == HAL_PIXEL_FORMAT_RGB_565)
    {
    	rga_cfg.dst.format = RK_FORMAT_RGB_565;
    }
 	ALOGD_IF(0,"copybit dst info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
           "vir_w=%d,vir_h=%d,format=%d,"
           "clip[%d,%d,%d,%d], "
           "act_x_y_w_h [%d,%d,%d,%d] ",
			rga_cfg.dst.yrgb_addr, rga_cfg.dst.uv_addr ,rga_cfg.dst.v_addr,
			rga_cfg.dst.vir_w ,rga_cfg.dst.vir_h ,rga_cfg.dst.format,
			rga_cfg.clip.xmin,
			rga_cfg.clip.xmax,
			rga_cfg.clip.ymin,
			rga_cfg.clip.ymax,
			rga_cfg.dst.x_offset ,
			rga_cfg.dst.y_offset,
			rga_cfg.dst.act_w ,
			rga_cfg.dst.act_h

        );

    //Rga_Request.render_mode = pre_scaling_mode;
    rga_cfg.alpha_rop_flag |= (1 << 5);
   	rga_cfg.mmu_info.mmu_en    = 1;
   	rga_cfg.mmu_info.mmu_flag  = ((2 & 0x3) << 4) | 1;


	//gettimeofday(&tpend1,NULL);
	if(ioctl(rga_fd, RGA_BLIT_SYNC, &rga_cfg) != 0)
	{

	 	ALOGE("ERROR:src info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
	         "vir_w=%d,vir_h=%d,format=%d,"
	         "act_x_y_w_h [%d,%d,%d,%d] ",
				rga_cfg.src.yrgb_addr, rga_cfg.src.uv_addr ,rga_cfg.src.v_addr,
				rga_cfg.src.vir_w ,rga_cfg.src.vir_h ,rga_cfg.src.format ,
				rga_cfg.src.x_offset ,
				rga_cfg.src.y_offset,
				rga_cfg.src.act_w ,
				rga_cfg.src.act_h

	        );

	 	ALOGE("ERROR dst info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
	         "vir_w=%d,vir_h=%d,format=%d,"
	         "clip[%d,%d,%d,%d], "
	         "act_x_y_w_h [%d,%d,%d,%d] ",
				rga_cfg.dst.yrgb_addr, rga_cfg.dst.uv_addr ,rga_cfg.dst.v_addr,
				rga_cfg.dst.vir_w ,rga_cfg.dst.vir_h ,rga_cfg.dst.format,
				rga_cfg.clip.xmin,
				rga_cfg.clip.xmax,
				rga_cfg.clip.ymin,
				rga_cfg.clip.ymax,
				rga_cfg.dst.x_offset ,
				rga_cfg.dst.y_offset,
				rga_cfg.dst.act_w ,
				rga_cfg.dst.act_h
	        );
        return -1;
	}
   #if 0
   FILE * pfile = NULL;
   int srcStride = android::bytesPerPixel(src_handle->format);
   char layername[100];
   memset(layername,0,sizeof(layername));
   system("mkdir /data/dumplayer/ && chmod /data/dumplayer/ 777 ");
   sprintf(layername,"/data/dumplayer/dmlayer%d_%d_%d_%d.bin",\
           layer_seq,src_handle->stride,src_handle->height,srcStride);

   pfile = fopen(layername,"wb");
   if(pfile)
   {
    fwrite((const void *)src_handle->base,(size_t)(2 * src_handle->stride*src_handle->height),1,pfile);
    fclose(pfile);
   }
   layer_seq++;
   #endif
    return 0;
}


int rga_video_reset()
{
  if (_contextAnchor->video_frame.vpu_handle)
  {
    ALOGV(" rga_video_reset,%x",_contextAnchor->video_frame.vpu_handle);
    memcpy((void*)_contextAnchor->video_frame.vpu_handle, 
            (void*)&_contextAnchor->video_frame.vpu_frame,sizeof(tVPU_FRAME));
    tVPU_FRAME* p = (tVPU_FRAME*)_contextAnchor->video_frame.vpu_handle;
 // ALOGD("vpu,w=%d,h=%d",p->FrameWidth,p->FrameHeight);
    _contextAnchor->video_frame.vpu_handle = 0;
  }

  return 0;
}
#endif
static PFNEGLGETRENDERBUFFERANDROIDPROC _eglGetRenderBufferANDROID;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
static PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC _eglRenderBufferModifiedANDROID;
#endif
static int skip_count = 0;
static uint32_t
_CheckLayer(
    hwcContext * Context,
    uint32_t Count,
    uint32_t Index,
    hwc_layer_1_t * Layer,
    hwc_display_contents_1_t * list,
    int videoflag
    )
{
    struct private_handle_t * handle =
        (struct private_handle_t *) Layer->handle;

    float hfactor = 1;
    float vfactor = 1;
    char pro_value[PROPERTY_VALUE_MAX];
    (void) Context;
    (void) Count;
    (void) Index;

    if(!videoflag)
    {

        hfactor = (float) (Layer->sourceCrop.right - Layer->sourceCrop.left)
                / (Layer->displayFrame.right - Layer->displayFrame.left);

        vfactor = (float) (Layer->sourceCrop.bottom - Layer->sourceCrop.top)
                / (Layer->displayFrame.bottom - Layer->displayFrame.top);

/* ----for 3066b support only in video mode in hwc module ----*/
/* --------------- this code is  for a short time----*/
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
           // Layer->compositionType = HWC_FRAMEBUFFER;
           // return HWC_FRAMEBUFFER;
#endif
/* ----------------------end--------------------------------*/

    }
    /* Check whether this layer is forced skipped. */

    if ((Layer->flags & HWC_SKIP_LAYER)
        //||(Layer->transform == (HAL_TRANSFORM_FLIP_V  | HAL_TRANSFORM_ROT_90))
        //||(Layer->transform == (HAL_TRANSFORM_FLIP_H  | HAL_TRANSFORM_ROT_90))
        ||(hfactor >1.0f)  // because rga scale down too slowly,so return to opengl  ,huangds modify
        ||(vfactor >1.0f)  // because rga scale down too slowly,so return to opengl ,huangds modify
        ||((hfactor <1.0f || vfactor <1.0f) &&(handle && GPU_FORMAT == HAL_PIXEL_FORMAT_RGBA_8888)) // because rga scale up RGBA foramt not support
        ||((Layer->transform != 0)&&(!videoflag))
        ||(Context->IsRk3188 && !(videoflag && Count <=2))
        || skip_count<5
        )
    {
        /* We are forbidden to handle this layer. */
        LOGV("%s(%d):Will not handle layer %s: SKIP_LAYER,Layer->transform=%d,hfactor=%f,vfactor=%f,Layer->flags=%d",
             __FUNCTION__, __LINE__, Layer->LayerName,Layer->transform,hfactor,vfactor,Layer->flags);
        Layer->compositionType = HWC_FRAMEBUFFER;
        if (skip_count<5)
        {
         skip_count++;
        }
        return HWC_FRAMEBUFFER;
    }

    /* Check whether this layer can be handled by Vivante 2D. */
    do
    {
        RgaSURF_FORMAT format = RK_FORMAT_UNKNOWN;
        /* Check for dim layer. */
        if ((Layer->blending & 0xFFFF) == HWC_BLENDING_DIM )
        {
            Layer->compositionType = HWC_DIM;
            Layer->flags           = 0;
            break;
        }

        if (Layer->handle == NULL)
        {
            LOGE("%s(%d):No layer surface at %d.",
                 __FUNCTION__,
                 __LINE__,
                 Index);
            /* TODO: I BELIEVE YOU CAN HANDLE SUCH LAYER!. */
            if( SkipFrameCount == 0)
            {
                Layer->compositionType = HWC_FRAMEBUFFER;
                SkipFrameCount = 1;
            }
            else
            {
                Layer->compositionType = HWC_CLEAR_HOLE;
                Layer->flags           = 0;
            }

            break;
        }

        /* At least surfaceflinger can handle this layer. */
        Layer->compositionType = HWC_FRAMEBUFFER;

        /* Get format. */
        if( hwcGetFormat(handle, &format) != hwcSTATUS_OK
            || (LayerZoneCheck(Layer) != 0))
        {
             return HWC_FRAMEBUFFER;
        }

        LOGV("name[%d]=%s,phy_addr=%x",Index,list->hwLayers[Index].LayerName,handle->phy_addr);

     
        if( (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO && Count <= 2 && getHdmiMode()==0 && Context->wfdOptimize==0) 
           || (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO && Count == 3 && getHdmiMode()==0 \
		      && strstr(list->hwLayers[Count - 2].LayerName, "SystemBar") && Context->wfdOptimize==0)
          )
        {
           if (Layer->transform==0 || (Context->ippDev!=NULL && Layer->transform!=0 && Context->ippDev->ipp_is_enable()>0))
           {
            Layer->compositionType = HWC_TOWIN0;
            Layer->flags = 0;
            Context->flag = 1;
			break;
           }
          //}
        }

        if( (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
           )
        //if( handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO )
        {
            Layer->compositionType = HWC_FRAMEBUFFER;
            return HWC_FRAMEBUFFER;

        }
        

        //if( videoflag)
        ///{

           // Layer->compositionType = HWC_FRAMEBUFFER;
           // return HWC_FRAMEBUFFER;
        //}


        //if((!strcmp(Layer->LayerName,"Starting com.android.camera"))
            //||(!strcmp(Layer->LayerName,"com.android.camera/com.android.camera.Camera"))
            //)
        /*
        if(strstr(Layer->LayerName,"com.android.camera"))
        {
            Layer->compositionType = HWC_FRAMEBUFFER;
            return HWC_FRAMEBUFFER;

        }
        */

        /* Normal 2D blit can be use. */
        Layer->compositionType = HWC_BLITTER;

        /* Stupid, disable alpha blending for the first layer. */
        if (Index == 0)
        {
            Layer->blending = HWC_BLENDING_NONE;
        }
    }
    while (0);

    /* Return last composition type. */
    return Layer->compositionType;
}

/*****************************************************************************/

#if hwcDEBUG
static void
_Dump(
    hwc_display_contents_1_t* list
    )
{
    size_t i, j;

    for (i = 0; list && (i < list->(numHwLayers - 1)); i++)
    {
        hwc_layer_1_t const * l = &list->hwLayers[i];

        if(l->flags & HWC_SKIP_LAYER)
        {
            LOGD("layer %p skipped", l);
        }
        else
        {
            LOGD("layer=%p, "
                 "type=%d, "
                 "hints=%08x, "
                 "flags=%08x, "
                 "handle=%p, "
                 "tr=%02x, "
                 "blend=%04x, "
                 "{%d,%d,%d,%d}, "
                 "{%d,%d,%d,%d}",
                 l,
                 l->compositionType,
                 l->hints,
                 l->flags,
                 l->handle,
                 l->transform,
                 l->blending,
                 l->sourceCrop.left,
                 l->sourceCrop.top,
                 l->sourceCrop.right,
                 l->sourceCrop.bottom,
                 l->displayFrame.left,
                 l->displayFrame.top,
                 l->displayFrame.right,
                 l->displayFrame.bottom);

            for (j = 0; j < l->visibleRegionScreen.numRects; j++)
            {
                LOGD("\trect%d: {%d,%d,%d,%d}", j,
                     l->visibleRegionScreen.rects[j].left,
                     l->visibleRegionScreen.rects[j].top,
                     l->visibleRegionScreen.rects[j].right,
                     l->visibleRegionScreen.rects[j].bottom);
            }
        }
    }
}
#endif

#if hwcDumpSurface
static void
_DumpSurface(
    hwc_display_contents_1_t* list
    )
{
    size_t i;
    static int DumpSurfaceCount = 0;

    char pro_value[PROPERTY_VALUE_MAX];
    property_get("sys.dump",pro_value,0);
    //LOGI(" sys.dump value :%s",pro_value);
    if(!strcmp(pro_value,"true"))
    {
        for (i = 0; list && (i < (list->numHwLayers - 1)); i++)
        {
            hwc_layer_1_t const * l = &list->hwLayers[i];

            if(l->flags & HWC_SKIP_LAYER)
            {
                LOGI("layer %p skipped", l);
            }
            else
            {
                struct private_handle_t * handle_pre = (struct private_handle_t *) l->handle;
                int32_t SrcStride ;
                FILE * pfile = NULL;
                char layername[100] ;


                if( handle_pre == NULL)
                    continue;

                SrcStride = android::bytesPerPixel(handle_pre->format);
                memset(layername,0,sizeof(layername));
                system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
                //mkdir( "/data/dump/",777);
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                sprintf(layername,"/data/dump/dmlayer%d_%d_%d_%d.bin",DumpSurfaceCount,handle_pre->stride,handle_pre->height,SrcStride);
#else
                sprintf(layername,"/data/dump/dmlayer%d_%d_%d_%d.bin",DumpSurfaceCount,handle_pre->width,handle_pre->height,SrcStride);
#endif

                DumpSurfaceCount ++;
                pfile = fopen(layername,"wb");
                if(pfile)
                {
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                    fwrite((const void *)handle_pre->base,(size_t)(SrcStride * handle_pre->stride*handle_pre->height),1,pfile);
#else
                    fwrite((const void *)handle_pre->iBase,(size_t)(SrcStride * handle_pre->width*handle_pre->height),1,pfile);

#endif
                    fclose(pfile);
                    LOGI(" dump surface layername %s,w:%d,h:%d,formatsize :%d",layername,handle_pre->width,handle_pre->height,SrcStride);
                }
            }
        }

    }
    property_set("sys.dump","false");


}
#endif

void
hwcDumpArea(
    IN hwcArea * Area
    )
{
    hwcArea * area = Area;

    while (area != NULL)
    {
        char buf[128];
        char digit[8];
        bool first = true;

        sprintf(buf,
                "Area[%d,%d,%d,%d] owners=%08x:",
                area->rect.left,
                area->rect.top,
                area->rect.right,
                area->rect.bottom,
                area->owners);

        for (int i = 0; i < 32; i++)
        {
            /* Build decimal layer indices. */
            if (area->owners & (1U << i))
            {
                if (first)
                {
                    sprintf(digit, " %d", i);
                    strcat(buf, digit);
                    first = false;
                }
                else
                {
                    sprintf(digit, ",%d", i);
                    strcat(buf, digit);
                }
            }

            if (area->owners < (1U << i))
            {
                break;
            }
        }

        LOGD("%s", buf);

        /* Advance to next area. */
        area = area->next;
    }
}
#include <ui/PixelFormat.h>

 
//extern "C" void *blend(uint8_t *dst, uint8_t *src, int dst_w, int src_w, int src_h);


int hwc_prepare_virtual(hwc_composer_device_1_t * dev, hwc_display_contents_1_t  *contents)
{
	if (contents==NULL)
	{
		return -1;
	}
	hwcContext * context = _contextAnchor;
	for (int j = 0; j <(contents->numHwLayers - 1); j++)
	{
		struct private_handle_t * handle = (struct private_handle_t *)contents->hwLayers[j].handle;

		if (handle && GPU_FORMAT==HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
		{
			ALOGD("WFD rga_video_copybit,%x,w=%d,h=%d",\
				GPU_BASE,GPU_WIDTH,GPU_HEIGHT);
			if (context->wfdOptimize==0)
			{
				if (!_contextAnchor->video_frame.vpu_handle)
				{
					rga_video_copybit(handle,handle);
				}
			}
		}
	}
	return 0;
}

int
hwc_prepare(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
    )
{
    size_t i;
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;
    int videoflag = 0;
    hwcContext * context = _contextAnchor;


    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    /* Check device handle. */
    if (context == NULL
    || &context->device.common != (hw_device_t *) dev
    )
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);
        return HWC_EGL_ERROR;
    }

#if hwcDumpSurface
    _DumpSurface(list);
#endif

    /* Check layer list. */
    if ((list == NULL)
    ||  (list->numHwLayers == 0)
    //||  !(list->flags & HWC_GEOMETRY_CHANGED)
    )
    {
        return 0;
    }

    LOGV("%s(%d):>>> Preparing %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

    property_get("sys.hwc.compose_policy", value, "0");
    new_value = atoi(value);
    /* Roll back to FRAMEBUFFER if any layer can not be handled. */
    if(new_value <= 0 )
    {
        for (i = 0; i < (list->numHwLayers - 1); i++)
        {
            list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
        }
        return 0;
    }
#if hwcDEBUG
    LOGD("%s(%d):Layers to prepare:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif

    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        struct private_handle_t * handle = (struct private_handle_t *) list->hwLayers[i].handle;

        if( ( list->hwLayers[i].flags & HWC_SKIP_LAYER)
            ||(handle == NULL)
           )
            break;

        if( GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
        {
            videoflag = 1;
            break;
        }

    }
    /* Check all layers: tag with different compositionType. */
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];

        uint32_t compositionType =
             _CheckLayer(context, list->numHwLayers - 1, i, layer,list,videoflag);

        /* TODO: Viviante limitation:
         * If any layer can not be handled by hwcomposer, fail back to
         * 3D composer for all layers. We then need to tag all layers
         * with FRAMEBUFFER in that case. */
        if (compositionType == HWC_FRAMEBUFFER)
        {
            ALOGV("line=%d back to gpu", __LINE__);           
            break;
        }
    }

    /* Roll back to FRAMEBUFFER if any layer can not be handled. */
    if (i != (list->numHwLayers - 1))
    {
        size_t j;
        //if(new_value == 1)  // print log
        //LOGD("%s(%d):Fail back to 3D composition path,i=%x,list->numHwLayers=%d", __FUNCTION__, __LINE__,i,list->numHwLayers);

        for (j = 0; j <(list->numHwLayers - 1); j++)
        {
            list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;

            /*  // move to exit
            struct private_handle_t * handle = (struct private_handle_t *)list->hwLayers[j].handle;
            if (handle && GPU_FORMAT==HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
            {
               ALOGV("rga_video_copybit,%x,w=%d,h=%d",\
                      GPU_BASE,GPU_WIDTH,GPU_HEIGHT);
               if (!_contextAnchor->video_frame.vpu_handle)
               {
                  rga_video_copybit(handle,handle);
               }
            }
            */
        }

        if(context->fbFd1 > 0  )
        {
            context->fb1_cflag = true;
        }


    }
    if(list->numHwLayers > 1 && 
        list->hwLayers[0].compositionType == HWC_FRAMEBUFFER ) // GPU handle it ,so recover
    {
        size_t j;
        for (j = 0; j <(list->numHwLayers - 1); j++)
        {
            struct private_handle_t * handle = (struct private_handle_t *)list->hwLayers[j].handle;
            if (handle && GPU_FORMAT==HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
            {
               ALOGV("rga_video_copybit,%x,w=%d,h=%d",\
                      GPU_BASE,GPU_WIDTH,GPU_HEIGHT);
               if (!_contextAnchor->video_frame.vpu_handle)
               {
                  rga_video_copybit(handle,handle);
               }
            }
        }
    }

    hwc_display_contents_1_t* list_wfd = displays[HWC_DISPLAY_VIRTUAL];
    if (list_wfd)
    {
      hwc_prepare_virtual(dev, list);
    }
    return 0;
}

int hwc_blank(struct hwc_composer_device_1 *dev, int dpy, int blank)
{
    // We're using an older method of screen blanking based on
    // early_suspend in the kernel.  No need to do anything here.
    hwcContext * context = _contextAnchor;

    return 0;
    switch (dpy) {
    case HWC_DISPLAY_PRIMARY: {
        int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
        int err = ioctl(context->fbFd, FBIOBLANK, fb_blank);
        if (err < 0) {
            if (errno == EBUSY)
                ALOGD("%sblank ioctl failed (display already %sblanked)",
                        blank ? "" : "un", blank ? "" : "un");
            else
                ALOGE("%sblank ioctl failed: %s", blank ? "" : "un",
                        strerror(errno));
            return -errno;
        }
        break;
    }

    case HWC_DISPLAY_EXTERNAL:
        /*
        if (pdev->hdmi_hpd) {
            if (blank && !pdev->hdmi_blanked)
                hdmi_disable(pdev);
            pdev->hdmi_blanked = !!blank;
        }
        */
        break;

    default:
        return -EINVAL;

    }

    return 0;
}

int hwc_query(struct hwc_composer_device_1* dev,int what, int* value)
{

    hwcContext * context = _contextAnchor;

    switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // we support the background layer
        value[0] = 1;
        break;
    case HWC_VSYNC_PERIOD:
        // vsync period in nanosecond
        value[0] = 1e9 / context->fb_fps;
        break;
    default:
        // unsupported query
        return -EINVAL;
    }
    return 0;
}

static int display_commit( int dpy, private_handle_t*  handle)
{
    unsigned int videodata[2];
    int ret = 0;
    hwcContext * context = _contextAnchor;
 
    struct fb_var_screeninfo info;                    
    info = context->info;
    if (!handle)
    {
      return -1;
    }
    info.activate |= FB_ACTIVATE_FORCE;
    videodata[0] = videodata[1] = context->hwc_ion.pion->phys + context->hwc_ion.offset;
    ALOGV("hwc fbPost:%x,offset=%x,fd=%d,videodata=%x",\
           videodata[0],context->hwc_ion.offset,context->dpyAttr[0].fd,videodata[0]);
    if (ioctl(context->dpyAttr[0].fd, FB1_IOCTL_SET_YUV_ADDR, videodata) == -1)
    {
        ALOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__,context->dpyAttr[dpy].fd,videodata[0]);
        return -1;
    } 
    int sync = 0;
    ioctl(context->dpyAttr[0].fd, FBIOPUT_VSCREENINFO, &info);
    ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &sync);
    context->hwc_ion.last_offset = context->hwc_ion.offset;
    if ((context->hwc_ion.offset+context->lcdSize) >= context->fbSize)
    {
     context->hwc_ion.offset = 0; 
    }
    else
    {
     context->hwc_ion.offset = context->hwc_ion.offset+context->lcdSize;
    }  
#if 0
    videodata[0] = videodata[1] = context->fbPhysical;
    if (ioctl(context->dpyAttr[dpy].fd, FB1_IOCTL_SET_YUV_ADDR, videodata) == -1)
    {
        ALOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__,context->dpyAttr[dpy].fd,videodata[0]);
        return -1;
    } 

    if (handle != NULL)
    {
        unsigned int offset = handle->offset;
        info.yoffset = offset/context->fbStride;
       
        ALOGV(" display_commit set ioctl handle=%x offset =%d,adr=%x",handle,offset,handle->base);      
        if (ioctl(context->dpyAttr[dpy].fd, FBIOPUT_VSCREENINFO, &info) == -1)
        {
            ALOGE("FBIOPAN_DISPLAY display error, displayid=%d,fd=%d,yoffset=%x,offset=%x,finfo.line_length=%d,err=%s",\
                   dpy,context->dpyAttr[dpy].fd,info.yoffset,handle->offset,context->fbStride,strerror(errno));
         }
        else
        {
            int sync = 0;
            ioctl(context->dpyAttr[dpy].fd, RK_FBIOSET_CONFIG_DONE, &sync);
        }
    }
    else
    {
        //ALOGE("hwc12:fb handle is null.");
    }
#endif
    return 0;
}

static int hwc_fbPost(hwc_composer_device_1_t * dev, size_t numDisplays, hwc_display_contents_1_t** displays)
{
  hwcContext * context = _contextAnchor;
  unsigned int videodata[2];
  int ret = 0;
  for (size_t i=0; i<numDisplays-1; i++)
  {
   if (context->dpyAttr[i].connected && context->dpyAttr[i].fd>0)
   {
    hwc_display_contents_1_t *list = displays[i];
    if (list == NULL)
    {
       return -1;
    }

    struct fb_var_screeninfo info;
    int numLayers = list->numHwLayers;
    hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];
    hwc_sync(list);
    if (fbLayer == NULL)
    {
        return -1;
    }
    info = context->info;
    struct private_handle_t*  handle = (struct private_handle_t*)fbLayer->handle;
    if (!handle)
    {
      return -1;
    }
    void * vaddr = NULL;
    videodata[0] = videodata[1] = context->fbPhysical;

    if (ioctl(context->dpyAttr[i].fd, FB1_IOCTL_SET_YUV_ADDR, videodata) == -1)
    {
        //ALOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__,pdev->dpyAttr[i].fd,videodata[0]);
        return -1;
    }

   // ioctl(pdev->dpyAttr[i].fd, FBIOPUT_VSCREENINFO, &info);    
   // ioctl(pdev->dpyAttr[i].fd, RK_FBIOSET_CONFIG_DONE, &sync);

    if (handle != NULL)
    {

        unsigned int offset = handle->offset;
        info.yoffset = offset/context->fbStride;
        if (ioctl(context->dpyAttr[i].fd, FBIOPUT_VSCREENINFO, &info) == -1)
        {

        }
        else
        {
            int sync = 0;
            ioctl(context->dpyAttr[i].fd, RK_FBIOSET_CONFIG_DONE, &sync);
            hwc_sync_release(list);
        }
    }
    else
    {
        //ALOGE("hwc12:fb handle is null.");
    }
   }
 }
  return 0;
}

int hwc_set_virtual(hwc_composer_device_1_t * dev, hwc_display_contents_1_t  **contents, unsigned int rga_fb_addr)
{
	hwc_display_contents_1_t* list_pri = contents[0];
	hwc_display_contents_1_t* list_wfd = contents[2];
	hwc_layer_1_t *  fbLayer = &list_pri->hwLayers[list_pri->numHwLayers - 1];
	hwc_layer_1_t *  wfdLayer = &list_wfd->hwLayers[list_wfd->numHwLayers - 1];
	hwcContext * context = _contextAnchor;
	struct timeval tpend1, tpend2;
	long usec1 = 0;
	gettimeofday(&tpend1,NULL);
	if (list_wfd)
	{
		hwc_sync(list_wfd);
	}
	if (fbLayer==NULL || wfdLayer==NULL)
	{
		return -1;
	}

	if ((context->wfdOptimize>0) && wfdLayer->handle)
	{
		hwc_cfg_t cfg;
		memset(&cfg, 0, sizeof(hwc_cfg_t));
		cfg.src_handle = (struct private_handle_t *)fbLayer->handle;
		cfg.transform = fbLayer->realtransform;
		ALOGD("++++transform=%d",cfg.transform);
		cfg.dst_handle = (struct private_handle_t *)wfdLayer->handle;
		cfg.src_rect.left = (int)fbLayer->displayFrame.left;
		cfg.src_rect.top = (int)fbLayer->displayFrame.top;
		cfg.src_rect.right = (int)fbLayer->displayFrame.right;
		cfg.src_rect.bottom = (int)fbLayer->displayFrame.bottom;
		cfg.src_format = cfg.src_handle->format;

		cfg.rga_fbAddr = rga_fb_addr;
		cfg.dst_rect.left = (int)wfdLayer->displayFrame.left;
		cfg.dst_rect.top = (int)wfdLayer->displayFrame.top;
		cfg.dst_rect.right = (int)wfdLayer->displayFrame.right;
		cfg.dst_rect.bottom = (int)wfdLayer->displayFrame.bottom;
		cfg.dst_format = cfg.dst_handle->format;
		set_rga_cfg(&cfg);
		do_rga_transform_and_scale();
	}
	
	if (list_wfd)
	{
		hwc_sync_release(list_wfd);
	}

	gettimeofday(&tpend2,NULL);
	usec1 = 1000*(tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec- tpend1.tv_usec)/1000;
	ALOGD("hwc use time=%ld ms",usec1);
	return 0;
}

int
hwc_set(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t  ** displays
    )
{
    hwcContext * context = _contextAnchor;
    hwcSTATUS status = hwcSTATUS_OK;
    unsigned int i;

    struct private_handle_t * handle     = NULL;

    int needSwap = false;
    EGLBoolean success = EGL_FALSE;
#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif
#if hwcBlitUseTime
    struct timeval tpendblit1, tpendblit2;
    long usec2 = 0;
#endif
    int sync = 1;

    hwc_display_t dpy = NULL;
    hwc_surface_t surf = NULL;
    android_native_buffer_t * fbBuffer = NULL;
    struct private_handle_t * fbhandle = NULL;

    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    hwc_display_contents_1_t* list_wfd = displays[HWC_DISPLAY_VIRTUAL];
    hwc_sync(list);
    rga_video_reset();
    if (list != NULL) {
        //dpy = list->dpy;
        //surf = list->sur;
        dpy = eglGetCurrentDisplay();
        surf = eglGetCurrentSurface(EGL_DRAW);
        
    }
    /* Check device handle. */
    if (context == NULL
    || &context->device.common != (hw_device_t *) dev
    )
    {
        LOGE("%s(%d): Invalid device!", __FUNCTION__, __LINE__);
        return HWC_EGL_ERROR;
    }

    /* Check layer list. */
    if (list == NULL 
        || list->numHwLayers == 0)
    {
        /* Reset swap rectangles. */
        return 0;
    }

    if(list->skipflag)
    {
        hwc_sync_release(list);
        return 0;
    }

    LOGV("%s(%d):>>> Set start %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);



#if hwcDEBUG
    LOGD("%s(%d):Layers to set:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif
    #if hwcUseTime
    gettimeofday(&tpend1,NULL);
    #endif


    /* Prepare. */
    for (i = 0; i < (list->numHwLayers-1); i++)
    {
        /* Check whether this composition can be handled by hwcomposer. */
        if (list->hwLayers[i].compositionType >= HWC_BLITTER)
        {
            #if ENABLE_HWC_WORMHOLE
            hwcRECT FbRect;
            hwcArea * area;
            hwc_region_t holeregion;
            #endif


            /* Will be hangled by hwcomposer, get back buffer. */
            fbBuffer = (android_native_buffer_t *)
                _eglGetRenderBufferANDROID((EGLDisplay) dpy, (EGLSurface) surf);

            /* Check back buffer. */
            if (fbBuffer == NULL)
            {
                LOGE("%s(%d):Get back buffer Failed.", __FUNCTION__, __LINE__);
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
            }

            /* Get gc buffer handle. */
            fbhandle = (struct private_handle_t *) fbBuffer->handle;
            if (fbhandle == NULL)
            {
                LOGE("%s(%d):Get back buffer handle =NULL.", __FUNCTION__, __LINE__);
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
            }
            struct private_handle_t *handle = fbhandle;
#if ENABLE_HWC_WORMHOLE
            /* Reset allocated areas. */
            if (context->compositionArea != NULL)
            {
                _FreeArea(context, context->compositionArea);

                context->compositionArea = NULL;
            }

            FbRect.left = 0;
            FbRect.top = 0;
            FbRect.right = GPU_WIDTH;
            FbRect.bottom = GPU_HEIGHT;

            /* Generate new areas. */
            /* Put a no-owner area with screen size, this is for worm hole,
             * and is needed for clipping. */
            context->compositionArea = _AllocateArea(context,
                                                     NULL,
                                                     &FbRect,
                                                     0U);

            /* Split areas: go through all regions. */
            for (int i = 0; i < list->numHwLayers-1; i++)
            {
                int owner = 1U << i;
                hwc_layer_1_t *  hwLayer = &list->hwLayers[i];
                hwc_region_t * region  = &hwLayer->visibleRegionScreen;

                /* Now go through all rectangles to split areas. */
                for (int j = 0; j < region->numRects; j++)
                {
                    /* Assume the region will never go out of dest surface. */
                    _SplitArea(context,
                               context->compositionArea,
                               (hwcRECT *) &region->rects[j],
                               owner);

                }

            }
#if DUMP_SPLIT_AREA
                LOGD("SPLITED AREA:");
                hwcDumpArea(context->compositionArea);
#endif

            area = context->compositionArea;

            while (area != NULL)
            {
                /* Check worm hole first. */
                if (area->owners == 0U)
                {

                    holeregion.numRects = 1;
                    holeregion.rects = (hwc_rect_t const*)&area->rect;
                    /* Setup worm hole source. */
                    LOGV(" WormHole [%d,%d,%d,%d]",
                            area->rect.left,
                            area->rect.top,
                            area->rect.right,
                            area->rect.bottom
                        );

                    hwcClear(context,
                                 0xFF000000,
                                 fbhandle,
                                 (hwc_rect_t *)&area->rect,
                                 &holeregion
                                 );

                    /* Advance to next area. */


                }
                 area = area->next;
            }

#endif

            /* Done. */
            needSwap = true;
            break;
        }
        else if (list->hwLayers[i].compositionType == HWC_FRAMEBUFFER)
        {
            /* Previous swap rectangle is gone. */
            needSwap = true;
            break;

        }
    }

    /* Go through the layer list one-by-one blitting each onto the FB */
    for (i = 0; i < list->numHwLayers; i++)
    {
        switch (list->hwLayers[i].compositionType)
        {
        case HWC_BLITTER:
            LOGV("%s(%d):Layer %d is BLIITER", __FUNCTION__, __LINE__, i);
            /* Do the blit. */
#if hwcBlitUseTime
            gettimeofday(&tpendblit1,NULL);
#endif

            hwcONERROR(
                hwcBlit(context,
                        &list->hwLayers[i],
                        fbhandle,
                        &list->hwLayers[i].sourceCrop,
                        &list->hwLayers[i].displayFrame,
                        &list->hwLayers[i].visibleRegionScreen));
#if hwcBlitUseTime
            gettimeofday(&tpendblit2,NULL);
            usec2 = 1000*(tpendblit2.tv_sec - tpendblit1.tv_sec) + (tpendblit2.tv_usec- tpendblit1.tv_usec)/1000;
            LOGD("hwcBlit compositer %d layers=%s use time=%ld ms",i,list->hwLayers[i].LayerName,usec2);
#endif

            break;

        case HWC_CLEAR_HOLE:
            LOGV("%s(%d):Layer %d is CLEAR_HOLE", __FUNCTION__, __LINE__, i);
            /* Do the clear, color = (0, 0, 0, 1). */
            /* TODO: Only clear holes on screen.
             * See Layer::onDraw() of surfaceflinger. */
            if (i != 0) break;

            hwcONERROR(
                hwcClear(context,
                         0xFF000000,
                         fbhandle,
                         &list->hwLayers[i].displayFrame,
                         &list->hwLayers[i].visibleRegionScreen));
            break;

        case HWC_DIM:
            LOGV("%s(%d):Layer %d is DIM", __FUNCTION__, __LINE__, i);
            if (i == 0)
            {
                /* Use clear instead of dim for the first layer. */
                hwcONERROR(
                    hwcClear(context,
                             ((list->hwLayers[0].blending & 0xFF0000) << 8),
                             fbhandle,
                             &list->hwLayers[i].displayFrame,
                             &list->hwLayers[i].visibleRegionScreen));
            }
            else
            {
                /* Do the dim. */
                hwcONERROR(
                    hwcDim(context,
                           &list->hwLayers[i],
                           fbhandle,
                           &list->hwLayers[i].displayFrame,
                           &list->hwLayers[i].visibleRegionScreen));
            }
            break;

        case HWC_OVERLAY:
            /* TODO: HANDLE OVERLAY LAYERS HERE. */
            LOGV("%s(%d):Layer %d is OVERLAY", __FUNCTION__, __LINE__, i);
            break;

        case HWC_TOWIN0:
            {
                struct private_handle_t * handle = NULL;
                LOGV("%s(%d):Layer %d is HWC_TOWIN0", __FUNCTION__, __LINE__, i);

                fbBuffer = (android_native_buffer_t *)
                    _eglGetRenderBufferANDROID((EGLDisplay) dpy, (EGLSurface) surf);

                if (fbBuffer == NULL)
                {
                    LOGE("%s(%d):Get fb buffer Failed.", __FUNCTION__, __LINE__);
                    hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                }


                /* Get gc buffer handle. */
                fbhandle = (struct private_handle_t *)fbBuffer->handle;
                handle = (struct private_handle_t *)list->hwLayers[i].handle;
                hwcONERROR(
                    hwcLayerToWin(context,
                            &list->hwLayers[i],
                            fbhandle,
                            &list->hwLayers[i].sourceCrop,
                            &list->hwLayers[i].displayFrame,
                            &list->hwLayers[i].visibleRegionScreen,
                            i,
                            0
                            ));

                if((list->numHwLayers - 1) == 1 || i == 1 
                    || GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO
                  )
                {
                    ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &sync);
                    if( context->fbFd1 > 0 )
                    {
                        LOGD(" close fb1 in video");
                        //close(Context->fbFd1);
                        if(closeFb(context->fbFd1) == 0)
                        {
                            context->fbFd1 = 0;
                            context->fb1_cflag = false;
                        }
                    }
                    hwc_sync_release(list);
                    if (list_wfd)
                    {
                      hwc_sync_release(list_wfd);
                    }
                    return hwcSTATUS_OK;
                }

            }

            break;

        case HWC_TOWIN1:
            {



                LOGV("%s(%d):Layer %d is HWC_TOWIN1", __FUNCTION__, __LINE__, i);


                fbBuffer = (android_native_buffer_t *)
                    _eglGetRenderBufferANDROID((EGLDisplay) dpy, (EGLSurface) surf);

                if (fbBuffer == NULL)
                {
                    LOGE("%s(%d):Get fb buffer Failed.", __FUNCTION__, __LINE__);
                    hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                }


                /* Get gc buffer handle. */
                fbhandle = (struct private_handle_t *) fbBuffer->handle;


                if( context->fbFd1 == 0)
                {
                    context->fbFd1 = open("/dev/graphics/fb1", O_RDWR, 0);
                    if(context->fbFd1 <= 0)
                    {
                        LOGE(" fb1 open fail,return to opengl composer");
                        hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                    }
                    else
                    {
                        ALOGD("fb1 open!");
                    }

                }

                //gettimeofday(&tpendblit1,NULL);

                  hwcONERROR(
                    hwcLayerToWin(context,
                            &list->hwLayers[i],
                            fbhandle,
                            &list->hwLayers[i].sourceCrop,
                            &list->hwLayers[i].displayFrame,
                            &list->hwLayers[i].visibleRegionScreen,
                            i,
                            1
                            ));
                //gettimeofday(&tpendblit2,NULL);
                //usec2 = 1000*(tpendblit2.tv_sec - tpendblit1.tv_sec) + (tpendblit2.tv_usec- tpendblit1.tv_usec)/1000;
                //LOGD("hwcBlit hwcVideo2LCDC %d layers=%s use time=%ld ms",i,list->hwLayers[i].LayerName,usec2);
            }
            if((list->numHwLayers == 1) || i == 1)
            {
                sync = 1; 
                ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &sync);

                /*
                for (int layerid = 0; layerid < list->numHwLayers; layerid++) {
                    if (!strcmp(FPS_NAME,list->hwLayers[layerid].LayerName)) {
                        eglSwapBuffers((EGLDisplay) dpy, (EGLSurface) surf);
                    }
                }*/
 		hwc_sync_release(list);
				
		if (list_wfd)
                {
                  hwc_sync_release(list_wfd);
                }
                return hwcSTATUS_OK;
            }

        default:
            LOGV("%s(%d):Layer %d is FRAMEBUFFER", __FUNCTION__, __LINE__, i);
            break;
        }
    }

    if (fbBuffer != NULL)
    {
        if(ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
        {
            LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
        }
        else
        {
         success =  EGL_TRUE ;
        }
         display_commit(0,(struct private_handle_t *) fbBuffer->handle); 
    }
    else
    {
        hwc_fbPost(dev,numDisplays,displays);
    }

#if hwcUseTime
    gettimeofday(&tpend2,NULL);
    usec1 = 1000*(tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec- tpend1.tv_usec)/1000;
    LOGD("hwcBlit compositer %d layers use time=%ld ms",list->numHwLayers,usec1);
#endif

#ifndef TARGET_BOARD_PLATFORM_RK30XXB
    //_eglRenderBufferModifiedANDROID((EGLDisplay) dpy, (EGLSurface) surf);
#endif

#ifdef TARGET_BOARD_PLATFORM_RK30XXB
       if (context->flag > 0)
	{
	    int videodata[2];
	    videodata[1] = videodata[0] = context->fbPhysical;
	    ioctl(context->fbFd, 0x5002, videodata);
	    context->flag = 0;
	}
#endif


        //close(Context->fbFd1);
#ifdef ENABLE_HDMI_APP_LANDSCAP_TO_PORTRAIT            
    if (list != NULL && getHdmiMode()>0) 
    {
      if (bootanimFinish==0)
      {
	       char pro_value[16];
	       property_get("service.bootanim.exit",pro_value,0);
		   bootanimFinish = atoi(pro_value);
		   if (bootanimFinish > 0)
		   {
			 usleep(1000000);
		   }
      }
	  else
	  {
	      if (strstr(list->hwLayers[list->numHwLayers-1].LayerName,"FreezeSurface")<=0)
	      {

			  if (list->hwLayers[0].transform==HAL_TRANSFORM_ROT_90 || list->hwLayers[0].transform==HAL_TRANSFORM_ROT_270)
			  {
			    int rotation = list->hwLayers[0].transform;
				if (ioctl(_contextAnchor->fbFd, RK_FBIOSET_ROTATE, &rotation)!=0)
				{
				  LOGE("%s(%d):RK_FBIOSET_ROTATE error!", __FUNCTION__, __LINE__);
				}
			  }
			  else
			  {
				int rotation = 0;
				if (ioctl(_contextAnchor->fbFd, RK_FBIOSET_ROTATE, &rotation)!=0)
				{
				  LOGE("%s(%d):RK_FBIOSET_ROTATE error!", __FUNCTION__, __LINE__);
				}
			  }
	      }
	  }
    }
#endif
    /* Swap buffers.
     * Case 1: framebuffer layers, call eglSwapBuffers immediately.
     * Case 2: blitter layers, backBuffer is not NULL, call eglSwapBuffers
     *         to post backBuffer.
     * Do NOT need swap if no framebuffer layers and no blitter layers. */
   // if (needSwap)
    //{
       // success = eglSwapBuffers((EGLDisplay) dpy, (EGLSurface) surf); 

    hwc_sync_release(list);
    {
      if (list_wfd)
      {
        int mode = -1;
        unsigned int fb_addr = 0;
        if (fbBuffer != NULL)
        {
          fb_addr = context->hwc_ion.pion->phys + context->hwc_ion.last_offset;
	}
        hwc_set_virtual(dev, displays,fb_addr);
      }
    }
    return 0; //? 0 : HWC_EGL_ERROR;
OnError:
    /* Error rollback */
    /* Case 1: get back buffer failed, backBuffer is NULL in this case, so we
     *         skip this frame.
     * Case 2: get back buffer succeeded, backBuffer is not NULL, but some 2D
     *         callings failed, so we call swap buffers to post backBuffer. */
    if (fbBuffer != NULL)
    {
        //eglSwapBuffers((EGLDisplay) dpy, (EGLSurface) surf);
    }

    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);
    rga_video_reset();
    return HWC_EGL_ERROR;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
                                    hwc_procs_t const* procs)
{
    hwcContext * context = _contextAnchor;

    context->procs =  (hwc_procs_t *)procs;
}


static int hwc_event_control(struct hwc_composer_device_1* dev,
        int dpy, int event, int enabled)
{

    hwcContext * context = _contextAnchor;

    switch (event) {
    case HWC_EVENT_VSYNC:
    {
        int val = !!enabled;
        int err;

        err = ioctl(context->fbFd, RK_FBIOSET_VSYNC_ENABLE, &val);
        if (err < 0)
        {
            LOGE(" RK_FBIOSET_VSYNC_ENABLE err=%d",err);
            return -1;
        }
        return 0;
    }
    default:
        return -1;
    }
}

static void handle_vsync_event(hwcContext * context )
{

    if (!context->procs)
        return;

    int err = lseek(context->vsync_fd, 0, SEEK_SET);
    if (err < 0) {
        ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        return;
    }

    char buf[4096];
    err = read(context->vsync_fd, buf, sizeof(buf));
    if (err < 0) {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return;
    }
    buf[sizeof(buf) - 1] = '\0';

  //  errno = 0;
    uint64_t timestamp = strtoull(buf, NULL, 0) ;/*+ (uint64_t)(1e9 / context->fb_fps)  ;*/

    uint64_t mNextFakeVSync = timestamp + (uint64_t)(1e9 / context->fb_fps);


    struct timespec spec;
    spec.tv_sec  = mNextFakeVSync / 1000000000;
    spec.tv_nsec = mNextFakeVSync % 1000000000;

    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);


    if (err==0)
    {
        context->procs->vsync(context->procs, 0, mNextFakeVSync );
        //ALOGD(" timestamp=%lld ms,preid=%lld us",mNextFakeVSync/1000000,(uint64_t)(1e6 / context->fb_fps) );
    }
    else
    {
        ALOGE(" clock_nanosleep ERR!!!");
    }
}

static void *hwc_thread(void *data)
{
    hwcContext * context = _contextAnchor;



#if 0
    uint64_t timestamp = 0;
    nsecs_t now = 0;
    nsecs_t next_vsync = 0;
    nsecs_t sleep;
    const nsecs_t period = nsecs_t(1e9 / 50.0);
    struct timespec spec;
   // int err;
    do
    {

        now = systemTime(CLOCK_MONOTONIC);
        next_vsync = context->mNextFakeVSync;

        sleep = next_vsync - now;
        if (sleep < 0) {
            // we missed, find where the next vsync should be
            sleep = (period - ((now - next_vsync) % period));
            next_vsync = now + sleep;
        }
        context->mNextFakeVSync = next_vsync + period;

        spec.tv_sec  = next_vsync / 1000000000;
        spec.tv_nsec = next_vsync % 1000000000;

        do
        {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err<0 && errno == EINTR);

        if (err == 0)
        {
            if (context->procs && context->procs->vsync)
            {
                context->procs->vsync(context->procs, 0, next_vsync);

                ALOGD(" hwc_thread next_vsync=%lld ",next_vsync);
            }

        }

    } while (1);
#endif

  //    char uevent_desc[4096];
   // memset(uevent_desc, 0, sizeof(uevent_desc));



    char temp[4096];

    int err = read(context->vsync_fd, temp, sizeof(temp));
    if (err < 0) {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return NULL;
    }

    struct pollfd fds[1];
    fds[0].fd = context->vsync_fd;
    fds[0].events = POLLPRI;
    //fds[1].fd = uevent_get_fd();
    //fds[1].events = POLLIN;

    while (true) {
        int err = poll(fds, 1, -1);

        if (err > 0) {
            if (fds[0].revents & POLLPRI) {
                handle_vsync_event(context);
            }

        }
        else if (err == -1) {
            if (errno == EINTR)
                break;
            ALOGE("error in vsync thread: %s", strerror(errno));
        }
    }

    return NULL;
}



int
hwc_device_close(
    struct hw_device_t *dev
    )
{
    hwcContext * context = _contextAnchor;

    LOGV("%s(%d):Close hwc device in thread=%d",
         __FUNCTION__, __LINE__, gettid());

    /* Check device. */
    if (context == NULL
    || &context->device.common != (hw_device_t *) dev
    )
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);

        return -EINVAL;
    }

    if (--context->reference > 0)
    {
        /* Dereferenced only. */
        return 0;
    }

    if(context->engine_fd)
        close(context->engine_fd);
    /* Clean context. */
    if(context->vsync_fd > 0)
        close(context->vsync_fd);
    if(context->fbFd > 0)
    {
        close(context->fbFd );

    }
    if(context->fbFd1 > 0)
    {
        close(context->fbFd1 );
    }

	if (context->ippDev)
    {
      ipp_close(context->ippDev);
	}
    if (context->hwc_ion.ion_device)
    {
      context->hwc_ion.ion_device->free(context->hwc_ion.ion_device, context->hwc_ion.pion);
      ion_close(context->hwc_ion.ion_device);
      context->hwc_ion.ion_device = NULL;
    }
    pthread_mutex_destroy(&context->lock);
    free(context);

    _contextAnchor = NULL;

    return 0;
}

static int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
            			uint32_t* configs, size_t* numConfigs)
{
   int ret = 0;
   hwcContext * pdev = ( hwcContext  *)dev; 
    //in 1.1 there is no way to choose a config, report as config id # 0
    //This config is passed to getDisplayAttributes. Ignore for now.
    switch(disp) {
        
        case HWC_DISPLAY_PRIMARY:
            if(*numConfigs > 0) {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0; //NO_ERROR
            break;
        case HWC_DISPLAY_EXTERNAL:
            ret = -1; //Not connected
            if(pdev->dpyAttr[HWC_DISPLAY_EXTERNAL].connected) {
                ret = 0; //NO_ERROR
                if(*numConfigs > 0) {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            break;
    }
   return 0;
}

static int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
            			 uint32_t config, const uint32_t* attributes, int32_t* values)
{

    hwcContext  *pdev = (hwcContext  *)dev; 
    //If hotpluggable displays are inactive return error
    if(disp == HWC_DISPLAY_EXTERNAL && !pdev->dpyAttr[disp].connected) {
        return -1;
    }
    static  uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
     };
    //From HWComposer

    const int NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES)/sizeof(DISPLAY_ATTRIBUTES)[0]);

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] = pdev->dpyAttr[disp].vsync_period;
            break;
        case HWC_DISPLAY_WIDTH:
            values[i] = pdev->dpyAttr[disp].xres;
            ALOGD("%s disp = %d, width = %d",__FUNCTION__, disp,
                    pdev->dpyAttr[disp].xres);
            break;
        case HWC_DISPLAY_HEIGHT:
            values[i] = pdev->dpyAttr[disp].yres;
            ALOGD("%s disp = %d, height = %d",__FUNCTION__, disp,
                    pdev->dpyAttr[disp].yres);
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = (int32_t) (pdev->dpyAttr[disp].xdpi*1000.0);
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = (int32_t) (pdev->dpyAttr[disp].ydpi*1000.0);
            break;
        default:
            ALOGE("Unknown display attribute %d",
                    attributes[i]);
            return -EINVAL;
        }
    }

   return 0;
}

int is_surport_wfd_optimize()
{
   char value[PROPERTY_VALUE_MAX];
   memset(value,0,PROPERTY_VALUE_MAX);
   property_get("drm.service.enabled", value, "false");
   if (!strcmp(value,"false"))
   {
     return false;
   }
   else
   {
     return true;
   }
}

int hwc_copybit(struct hwc_composer_device_1 *dev,
             hwc_layer_1_t *src_layer,
			 hwc_layer_1_t *dst_layer,
			 int flag
			 )
{
 

    return 0;
}

static void hwc_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len)
{
  // return 0;
}
int
hwc_device_open(
    const struct hw_module_t * module,
    const char * name,
    struct hw_device_t ** device
    )
{
    int  status    = 0;
    int rel;
    hwcContext * context = NULL;
    struct fb_fix_screeninfo fixInfo;
    struct fb_var_screeninfo info;
    int refreshRate = 0;
    int cpu_fd = 0;
    float xdpi;
    float ydpi;
    uint32_t vsync_period; 
    unsigned int fbPhy_end;
    char pro_value[PROPERTY_VALUE_MAX];
    LOGD("%s(%d):Open hwc device in thread=%d",
         __FUNCTION__, __LINE__, gettid());

    *device = NULL;

    if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0)
    {
        LOGE("%s(%d):Invalid device name!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    /* Get context. */
    context = _contextAnchor;

    /* Return if already initialized. */
    if (context != NULL)
    {
        /* Increament reference count. */
        context->reference++;

        *device = &context->device.common;
        return 0;
    }

    /* TODO: Find a better way instead of EGL_ANDROID_get_render_buffer. */
    _eglGetRenderBufferANDROID = (PFNEGLGETRENDERBUFFERANDROIDPROC)
                                 eglGetProcAddress("eglGetRenderBufferANDROID");

#ifndef TARGET_BOARD_PLATFORM_RK30XXB

    _eglRenderBufferModifiedANDROID = (PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC)
                                    eglGetProcAddress("eglRenderBufferModifiedANDROID");
#endif

    if (_eglGetRenderBufferANDROID == NULL)
    {
        LOGE("EGL_ANDROID_get_render_buffer extension "
             "Not Found for hwcomposer");

        return HWC_EGL_ERROR;
    }
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
    if(_eglRenderBufferModifiedANDROID == NULL)
    {
        LOGE("EGL_ANDROID_buffer_modifyed extension "
             "Not Found for hwcomposer");

            return HWC_EGL_ERROR;

    }
#endif
    /* Allocate memory. */
    context = (hwcContext *) malloc(sizeof (hwcContext));
    
    if(context == NULL)
    {
        LOGE("%s(%d):malloc Failed!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }
    memset(context, 0, sizeof (hwcContext));

    context->fbFd = open("/dev/graphics/fb0", O_RDWR, 0);
    if(context->fbFd < 0)
    {
         hwcONERROR(hwcSTATUS_IO_ERR);
    }

    rel = ioctl(context->fbFd, FBIOGET_FSCREENINFO, &fixInfo);
    if (rel != 0)
    {
         hwcONERROR(hwcSTATUS_IO_ERR);
    }



    if (ioctl(context->fbFd, FBIOGET_VSCREENINFO, &info) == -1)
    {
         hwcONERROR(hwcSTATUS_IO_ERR);
    }
    
    xdpi = 1000 * (info.xres * 25.4f) / info.width;
    ydpi = 1000 * (info.yres * 25.4f) / info.height;

    refreshRate = 1000000000000LLU /
    (
       uint64_t( info.upper_margin + info.lower_margin + info.yres )
       * ( info.left_margin  + info.right_margin + info.xres )
       * info.pixclock
     );
    
    if (refreshRate == 0) {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60*1000;
    }

    
    vsync_period  = 1000000000 / refreshRate;

    context->dpyAttr[HWC_DISPLAY_PRIMARY].fd = context->fbFd;
    //xres, yres may not be 32 aligned
    context->dpyAttr[HWC_DISPLAY_PRIMARY].stride = fixInfo.line_length /(info.xres/8);
    context->dpyAttr[HWC_DISPLAY_PRIMARY].xres = info.xres;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].yres = info.yres;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].xdpi = xdpi;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].ydpi = ydpi;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period = vsync_period;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;
    context->info = info;

    /* Initialize variables. */
   
    context->device.common.tag = HARDWARE_DEVICE_TAG;
    context->device.common.version = HWC_DEVICE_API_VERSION_1_3;
    
    context->device.common.module  = (hw_module_t *) module;

    /* initialize the procs */
    context->device.common.close   = hwc_device_close;
    context->device.prepare        = hwc_prepare;
    context->device.set            = hwc_set;
   // context->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    context->device.blank          = hwc_blank;
    context->device.query          = hwc_query;
    context->device.eventControl   = hwc_event_control;

    context->device.registerProcs  = hwc_registerProcs;

    context->device.getDisplayConfigs = hwc_getDisplayConfigs;
    context->device.getDisplayAttributes = hwc_getDisplayAttributes;
    context->device.rkCopybit = hwc_copybit;

    context->device.fbPost2 = hwc_fbPost;
    context->device.dump = hwc_dump;

    /* Get gco2D object pointer. */
    context->engine_fd = open("/dev/rga",O_RDWR,0);
    if( context->engine_fd < 0)
    {
        hwcONERROR(hwcRGA_OPEN_ERR);

    }

#if ENABLE_WFD_OPTIMIZE
	 property_set("sys.enable.wfd.optimize","1");
#endif
    {
	char value[PROPERTY_VALUE_MAX];
	memset(value,0,PROPERTY_VALUE_MAX);
	property_get("sys.enable.wfd.optimize", value, "0");
	int type = atoi(value);
        context->wfdOptimize = type;
        init_rga_cfg(context->engine_fd);
        if (type>0 && !is_surport_wfd_optimize())
        {
           property_set("sys.enable.wfd.optimize","0");
        }
    }

    /* Initialize pmem and frameubffer stuff. */
   // context->fbFd         = 0;
   // context->fbPhysical   = ~0U;
   // context->fbStride     = 0;



    if ( info.pixclock > 0 )
    {
        refreshRate = 1000000000000000LLU /
        (
            uint64_t( info.vsync_len + info.upper_margin + info.lower_margin + info.yres )
            * ( info.hsync_len + info.left_margin  + info.right_margin + info.xres )
            * info.pixclock
        );
    }
    else
    {
        ALOGW("fbdev pixclock is zero");
    }

    if (refreshRate == 0)
    {
        refreshRate = 60*1000;  // 60 Hz
    }

    context->fb_fps = refreshRate / 1000.0f;

    context->fbPhysical = fixInfo.smem_start;
    context->fbStride   = fixInfo.line_length;
    context->fbWidth = info.xres;
	context->fbHeight = info.yres;
    context->pmemPhysical = ~0U;
    context->pmemLength   = 0;
	property_get("ro.rk.soc",pro_value,"0");
    context->IsRk3188 = !strcmp(pro_value,"rk3188");
    context->fbSize = info.xres*info.yres*4*3;
    context->lcdSize = info.xres*info.yres*4; 
    {
     ion_open(context->fbSize, ION_MODULE_UI, &context->hwc_ion.ion_device);
     int err = context->hwc_ion.ion_device->alloc(context->hwc_ion.ion_device, context->fbSize,\
                                              _ION_HEAP_RESERVE, &context->hwc_ion.pion); 
     if (!err)
     {
        ALOGD("Ion alloc succ.");  
     }
     else
     {
        ALOGE("Ion alloc fail.");  
     }
    context->hwc_ion.offset=0;
    }

    /* Increment reference count. */
    context->reference++;
    _contextAnchor = context;
    if (context->fbWidth > context->fbHeight)
    {
      property_set("sys.display.oritation","0");
    }
    else
    {
      property_set("sys.display.oritation","2");
    }

#if USE_HW_VSYNC

    context->vsync_fd = open("/sys/class/graphics/fb0/vsync", O_RDONLY, 0);
    //context->vsync_fd = open("/sys/devices/platform/rk30-lcdc.0/vsync", O_RDONLY);
    if (context->vsync_fd < 0) {
         hwcONERROR(hwcSTATUS_IO_ERR);
    }


    if (pthread_mutex_init(&context->lock, NULL))
    {
         hwcONERROR(hwcMutex_ERR);
    }

    if (pthread_create(&context->hdmi_thread, NULL, hwc_thread, context))
    {
         hwcONERROR(hwcTHREAD_ERR);
    }
#endif

    /* Return device handle. */
    *device = &context->device.common;

    LOGD("RGA HWComposer verison%s\n"
         "Device:               %p\n"
         "fb_fps=%f",
         "1.0.0",
         context,
         context->fb_fps);

#ifdef TARGET_BOARD_PLATFORM_RK30XXB
    property_set("sys.ghwc.version","1.025");
#else
    property_set("sys.ghwc.version","1.025");
#endif

    char Version[32];

    memset(Version,0,sizeof(Version));
    if(ioctl(context->engine_fd, RGA_GET_VERSION, Version) == 0)
    {
        property_set("sys.grga.version",Version);
        LOGD(" rga version =%s",Version);

    }
	 context->ippDev = new ipp_device_t();
	 rel = ipp_open(context->ippDev);
     if (rel < 0)
     {
		ALOGE("Open ipp device fail.");
	 }
     init_hdmi_mode();
     pthread_t t;
     if (pthread_create(&t, NULL, rk_hwc_hdmi_thread, NULL))
     {
         LOGD("Create readHdmiMode thread error .");
     }
    return 0;

OnError:

    if (context->vsync_fd > 0)
    {
        close(context->vsync_fd);
    }
    if(context->fbFd > 0)
    {
        close(context->fbFd );

    }
    if(context->fbFd1 > 0)
    {
        close(context->fbFd1 );
    }

    pthread_mutex_destroy(&context->lock);

    /* Error roll back. */
    if (context != NULL)
    {
        if (context->engine_fd != NULL)
        {
            close(context->engine_fd);
        }
        free(context);

    }

    *device = NULL;

    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);

    return -EINVAL;
}

int  getHdmiMode()
{
   #if 0
    char pro_value[16];
    property_get("sys.hdmi.mode",pro_value,0);
        int mode = atoi(pro_value);
    return mode;
   #else
      // LOGD("g_hdmi_mode=%d",g_hdmi_mode);
   #endif
    return g_hdmi_mode;
}

void init_hdmi_mode()
{

        int fd = open("/sys/devices/virtual/switch/hdmi/state", O_RDONLY);
		
        if (fd > 0)
        {
                 char statebuf[100];
                 memset(statebuf, 0, sizeof(statebuf));
                 int err = read(fd, statebuf, sizeof(statebuf));

                if (err < 0)
                 {
                        ALOGE("error reading vsync timestamp: %s", strerror(errno));
                        return;
                 }
                close(fd);
                g_hdmi_mode = atoi(statebuf);
               /* if (g_hdmi_mode==0)
                {
                    property_set("sys.hdmi.mode", "0");
                }
                else
                {
                    property_set("sys.hdmi.mode", "1");
                }*/

        }
        else
        {
           LOGE("Open hdmi mode error.");
        }

}
int closeFb(int fd)
{
    if (fd > 0)
    {
        int disable = 0;

        if (ioctl(fd, 0x5019, &disable) == -1)
        {
            LOGE("close fb[%d] fail.",fd);
            return -1;
        }
        ALOGD("fb1 realy close!");
        return (close(fd));
    }
    return -1;
}
