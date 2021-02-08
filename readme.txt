1. 概述
    mediaExample 为 rockchip Andorid 平台硬编解码 demo 仓库，提供了几种不同层次的硬编解码
    使用接口，给 media 工程师设计使用参考。包括:
      - /native-codec/ - Android native 层 MediaCodec 使用范例
      - /rkvpu-codec/ - 基于 vpu 直接调用 rockchip 硬件编解码库使用范例

2. native-codec
    native-codec 为使用 Android 平台 native MediaCodec 设计的编解码接口使用范例，主要包括
      - native_enc_test: h264 硬编码器接口使用，目前支持文件输入与输出
        测试命令: $ native_enc_test --i input.yuv --o out.h264 --w 1280 --h 720
      - native_dec_test: 硬解码器接口使用，目前支持解码带封装格式(mp4\mkv)或 raw
        数据(h264\h265)输入
        1) 默认为 raw 数据，如果要指定封装格式输入请带参数 (--t 2)
        2) 默认将解码输出渲染到屏幕，可以直接通过 --o 参数指定输出文件将解码输出保存到文件
          测试命令:
            $ native_dec_test --i input.264 --o out.yuv --w 1280 --h 720 --t 1
            $ native_dec_test --i input.mp4 --t 2
        3) 如果指定 raw 数据输入并且不能保证 queueInputBuffer 送入完整的一帧数据，需要打开 VPU
           的 split mode，使用内部的分帧处理，否则可能出现解码错误。修改方式参考如下:

          hardware/rockchip/omx_il 仓库，文件 component/video/dec/Rkvpu_OMX_Vdec.c
          Rkvpu_SendInputData 函数 p_vpu_ctx->init 初始化前加入下面的代码:
          {
            RK_U32 need_split = 1;
            p_vpu_ctx->control(p_vpu_ctx, VPU_API_SET_PARSER_SPLIT_MODE, (void*)&need_split);
          }

    Note: 目前该目录下的 demo 程序基于 Android 7.1 平台开发，如果要在高于 7.1 平台上移植
    使用需要更新 MediaCodec 使用头文件，建议参考 SDK 目录 <frameworks/av/cmds> 下的程式头文件使用。

3. rkvpu-codec
    Rockchip 硬编解码库中设计了 VpuApiLegacy 用户接口，使用 VpuCodecContext 对象可以方便的进行
    硬编解码接口调用，/native-codec/ 目录为该用户接口的范例使用。

    Note: 该硬编解调用方式与目前 Android MediaCodec -> OMX -> libvpu 的硬解调用方式一致，omx_il 的
    设计实现代码中同样使用了该用户接口。

    1) VpuApiLegacy 一些重要概念说明
      - vpu_open_context
        获取 VpuCodecContext 对象，并完成一些回调函数的映射

      - vpu_close_context
        关闭 VpuCodecContext，释放资源

      - VpuCodecContext->init
        VpuCodecContext 初始化，宽、高、codingType 类型设置等

      - VpuCodecContext->flush
        清空缓存中的数据

      - VpuCodecContext->control
        配置一些模式，例如快速出帧模式、分帧处理模式等

      - VpuCodecContext->decode_sendstream | VpuCodecContext->decode_getframe
        往解码器中异步存取数据

      - VpuCodecContext->encoder_sendframe | VpuCodecContext->encoder_getstream
        往编码器中异步存取数据

    2) 范例说明
        rkvpu_dec_api-RKHWDecApi 为可参考的 VpuApiLegacy 接口 decoder 设计，rkvpu_dec_test.cpp
        为 RKHWDecApi 使用范例，可参考这两个文件进行硬解码器设计.

        - 解码器输出 NV12 格式
        - 平台硬解码器只能处理对齐过的 buffer，因此 RKHWDecApi 输出的 YUV buffer 也是经过对齐的，
        返回的 VPU_FRAME 句柄中，FrameWidth 为对齐过的 buffer 宽度(horizontal stride)，DisplayWidth
        为实际图像的大小，如果这两者不匹配也就是非对齐的分辨率，直接显示可能出现绿边的情况，因此
        非对齐分辨率需经过外部裁剪才能正常显示。
        - VPU_FRAME 在解码库内部循环使用，在解码显示完成之后记得使用 deinitOutFrame 解除使用状态.

        bin 测试使用:
            rkvpu_dec_test --i input.h264 --o out.yuv --w 1280 --h 720 --t 1
            (--t 1 为 h264 解码，--t 2 为 h265 解码)

4. mpp-codec
    rockchip 提供的媒体处理软件平台(Media Process Platform，简称 MPP)，是适用于所有芯片系列的
    通用媒体处理软件平台。MPP 是最底层的媒体的中间件，直接与 vpu 内核驱动交互，无论是 native-codec
    还是 rkvpu-codec 最后都需要与 mpp 交互。

    MPP 为用户提供的统一媒体接口叫 MPI(Media Process Interface)，可以用于编解码设计。具体的代码设计
    请参考 mpp-codec/MPP 开发参考_v0.5.pdf

    外部官方地址: https://github.com/rockchip-linux/mpp
    外部个人地址: https://github.com/HermanChen/mpp/

    MPI 设计参考: mpp_repository/test/mpi_dec_test.c(mpi_enc_test.c)