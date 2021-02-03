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
			
Note: 目前该目录下的 demo 程序基于 Android 7.1 平台开发，如果要在高于 Android 7.1 平台上移植
使用需要更新 MediaCodec 使用头文件，建议参考 SDK 目录 frameworks/av/cmds 下的程式头文件使用。