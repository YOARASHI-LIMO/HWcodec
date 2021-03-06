//**************************************************************************
//* 版权所有
//*
//* 文件名称：CQsv264_Encoder
//* 内容摘要：Intel Qsv硬编码封装
//* 当前版本：V1.0
//*	修	  改：		
//* 作    者：mediapro
//* 完成日期：2018-4-12
//***************************************************************************/
#include "SDQsv264Encoder.h"
#include "SDCodecCommon.h"
#include "SDLog.h"
#include "SDMutex.h"


//判断当前环境是否支持QSV硬编码
bool CQsv264_Encoder::IsHwAcclSupported(void)
{
#if 0
	mfxIMPL impl = MFX_IMPL_AUTO;
	mfxSession session = NULL;
	mfxVersion ver = {3, 1};
	MFXInit( MFX_IMPL_AUTO, &ver, &session );
	MFXQueryIMPL( session, &impl );
	MFXClose( session );
	SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_INFO, "IsHwAcclSupported check return:%d!", impl);
	if (impl & MFX_IMPL_HARDWARE)
	{
		return true;
	}
	else
	{
		return false;
	}
#else
	//Open-Close方式测试是否支持
	AVCodec* pH264Codec = avcodec_find_encoder_by_name("h264_qsv");
	if(NULL == pH264Codec)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_INFO, "Qsv Hardware Enc is not support!!");
		return  false;
	}

	AVCodecContext*	pEncContext = avcodec_alloc_context3(pH264Codec);
	if (NULL == pEncContext)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_INFO, "Qsv Hardware Enc is not support!!");
		return false;
	}
	pEncContext->codec_id				= pH264Codec->id;
	pEncContext->time_base.num			= 1;
	pEncContext->time_base.den			= 25;
	pEncContext->pix_fmt				= *pH264Codec->pix_fmts;
	pEncContext->width					= 1280;
	pEncContext->height					= 720;
	pEncContext->bit_rate				= 1024*1000;
	pEncContext->rc_max_rate			= 0; 

	AVDictionary *options = NULL;

	av_opt_set(pEncContext->priv_data,"async_depth","1",0);
	av_opt_set(pEncContext->priv_data,"max_dec_frame_buffering","1",0);
	av_opt_set(pEncContext->priv_data,"look_ahead","0",0);
	av_opt_set(pEncContext->priv_data,"bitrate_limit","1",0);
	av_opt_set(pEncContext->priv_data, "profile", "baseline", 0);

	int ret = avcodec_open2(pEncContext, pH264Codec, NULL);
	if (ret < 0)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_INFO, "Qsv Hardware Enc is not support!!");
		avcodec_free_context(&pEncContext);
		pEncContext = NULL;
		return false;
	}

	SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_INFO, "Qsv Hardware Enc is support!!");
	avcodec_close(pEncContext);
	avcodec_free_context(&pEncContext);
	pEncContext = NULL;
	return true;

#endif
}

CQsv264_Encoder::CQsv264_Encoder()
{
	m_bClosed = true;
	m_pClosedCs = CSDMutex::CreateObject();
	m_nKeyFrameIntervalTime = 10;

	m_tExternParams.nLevel = 40;
	m_tExternParams.nProfile = QSV264_HIGH_PROFILE;
	m_tExternParams.bEnableVbv = 1;
	strcpy(m_tExternParams.acPreset, "slow");
	m_bRequestIdr = false;
	
	m_ptEncContext = NULL;
	m_nReqFormat = AV_PIX_FMT_NONE;
	m_nReqWidth = 0;
	m_nReqHeight = 0;

	m_nPtsBaseNum = 0;
	m_nPtsStartTime = 0;
}

CQsv264_Encoder::~CQsv264_Encoder()
{
	CSDMutex::RealseObject(m_pClosedCs);
	m_pClosedCs = NULL;
}

//打开编码器接口
bool CQsv264_Encoder::Open(int nWidth, int nHeight, int nFrameRate, int nBitrate, int nKeyFrameInterval, Qsv264ExternParams *pExternParams)
{
	CSDMutex cs(m_pClosedCs);
	if(m_bClosed == false)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Already opened then call open, should closed first!");
		return false;
	}

	if (pExternParams == NULL)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "pExternParams is invalid NULL!");
		return false;
	}

	m_nPtsStartTime = 0;

	return mfOpen(nWidth, nHeight, nFrameRate, nBitrate, nKeyFrameInterval, pExternParams);

}

bool CQsv264_Encoder::mfOpen(int nWidth, int nHeight, int nFrameRate, int nBitrate, int nKeyFrameInterval, Qsv264ExternParams *pExternParams)
{	

	int nKeyInt = nKeyFrameInterval*nFrameRate;
	m_nKeyFrameIntervalTime = nKeyFrameInterval;

	AVCodec* pH264Codec = avcodec_find_encoder_by_name("h264_qsv");
	if(NULL == pH264Codec)
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "avcodec_find_encoder_by_name find qsv failed!");
		return  false;
	}

	m_ptEncContext = avcodec_alloc_context3(pH264Codec);
	m_ptEncContext->gop_size				= nKeyInt;

	m_ptEncContext->has_b_frames			= 0;
	m_ptEncContext->max_b_frames			= 0;

	m_ptEncContext->codec_id				= pH264Codec->id;
	m_ptEncContext->time_base.num			= 1;
	m_ptEncContext->time_base.den			= nFrameRate;
	m_ptEncContext->pix_fmt					= *pH264Codec->pix_fmts;
	m_ptEncContext->width					= nWidth;
	m_ptEncContext->height					= nHeight;
	m_ptEncContext->bit_rate				= nBitrate*1000;
	m_ptEncContext->rc_max_rate				= 0; //ABR

	av_opt_set(m_ptEncContext->priv_data,"async_depth","1",0);
	av_opt_set(m_ptEncContext->priv_data,"max_dec_frame_buffering","1",0);
	av_opt_set(m_ptEncContext->priv_data,"look_ahead","0",0);
	av_opt_set(m_ptEncContext->priv_data,"bitrate_limit","1",0);


	int nAvgFrameSizeBytes = nBitrate*1000 / (nFrameRate * 8);
	char acMaxFrameSizeBytes[128];
	memset(acMaxFrameSizeBytes, 0x0, sizeof(acMaxFrameSizeBytes));
	sprintf(acMaxFrameSizeBytes, "%d", nAvgFrameSizeBytes*6);
	av_opt_set(m_ptEncContext->priv_data,"max_frame_size", acMaxFrameSizeBytes, 0);
	
	if (pExternParams->nProfile == QSV264_HIGH_PROFILE)
	{
		av_opt_set(m_ptEncContext->priv_data, "profile", "high", 0);
	}
	else
	{
		av_opt_set(m_ptEncContext->priv_data, "profile", "baseline", 0);
	}
	av_opt_set(m_ptEncContext->priv_data,"trellis", "off", 0);
	av_opt_set(m_ptEncContext->priv_data,"preset", pExternParams->acPreset, 0);

	int ret = avcodec_open2(m_ptEncContext, pH264Codec, NULL);//&options);
	if (ret < 0)
	{
		char strErrorInfo[512];
		memset(strErrorInfo, 0x0, sizeof(strErrorInfo));
		av_strerror(ret, strErrorInfo, 511);
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "avcodec_open2 for qsv failed, return:%d %s!", ret, strErrorInfo);
		avcodec_free_context(&m_ptEncContext);
		m_ptEncContext = NULL;
		return  false;
	}	
	
	//要求外层输入的AVFrame属性
	m_nReqFormat = m_ptEncContext->pix_fmt;
	m_nReqWidth = nWidth;
	m_nReqHeight = nHeight;

	//存储外层传入参数
	m_tExternParams.nLevel = pExternParams->nLevel;
	m_tExternParams.nProfile = pExternParams->nProfile;
	m_tExternParams.bEnableVbv = pExternParams->bEnableVbv;
	strcpy(m_tExternParams.acPreset, pExternParams->acPreset);
	
	m_nPrevIdrTime = 0;
	m_bRequestIdr = false;

	m_bClosed = false;
	return(true);
}

bool CQsv264_Encoder::GetInputFormat(int *pnFormat, int *pnWidth, int *pnHeight)
{
	CSDMutex cs(m_pClosedCs);
	if( m_bClosed )
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Already closed then call GetInputFormat!");
		return false;
	}

	*pnFormat = m_nReqFormat;
	*pnWidth = m_nReqWidth;
	*pnHeight = m_nReqHeight;

	return true;
}

bool CQsv264_Encoder::Control(int nFrameRate, int bitrate, int nIdrInterval, int nWidth, int nHeight)
{
	CSDMutex cs(m_pClosedCs);
	if( m_bClosed )
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Already closed then call Control!");
		return false;
	}

	mfClose();

	return mfOpen(nWidth, nHeight, nFrameRate, bitrate, nIdrInterval, &m_tExternParams);

}

void CQsv264_Encoder::Close()
{
	CSDMutex cs(m_pClosedCs);
	mfClose();
}

void CQsv264_Encoder::mfClose()
{
	m_bClosed = true;
	if (m_ptEncContext)
	{
		avcodec_close(m_ptEncContext);
		avcodec_free_context(&m_ptEncContext);
		m_ptEncContext = NULL;
	}
}

//返回大于0---当前码流帧大小
//返回0-------当前无码流输出（初次）
//小于0-------编码失败
int CQsv264_Encoder::Encode(AVFrame* ptEncFrame, BYTE* pucBitstreamData, int nMaxSupportLen, int64_t* pPts, int64_t* pDts)
{
	int ret = -1;
	int got_output = 0;
	CSDMutex cs(m_pClosedCs);
	if( m_bClosed )
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Already closed then call encode!");
		return(-1);
	}
	
	if((ptEncFrame == NULL) || (pucBitstreamData == NULL))
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "(ptEncFrame == %x) || (pRetData == %x)", ptEncFrame, pucBitstreamData);
		return(-1);
	}

	if ((ptEncFrame->width != m_ptEncContext->width) ||
		(ptEncFrame->height != m_ptEncContext->height) ||
		(ptEncFrame->format != m_ptEncContext->pix_fmt))
	{
		SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Input format %dx%d-%d is invalid(request: %dx%d-%d)!", 
					ptEncFrame->width, ptEncFrame->height, ptEncFrame->format, 
					m_ptEncContext->width, m_ptEncContext->height, m_ptEncContext->pix_fmt);
		return(-1);
	}

	ptEncFrame->pts = mfGeneratePts();

	//无B帧，PTS=DTS
	if (pPts)
	{
		*pPts = ptEncFrame->pts;
	}
	if (pDts)
	{
		*pDts = ptEncFrame->pts;
	}


	//上一次编码IDR帧的时间，用于避免由于实际输入帧频不足配置的帧率，
	//而导致长时间（大于指定的最大I帧间隔）无IDR帧的情况
	long nCurrTime = SD_GetTickCount();
	if ((nCurrTime - m_nPrevIdrTime >= m_nKeyFrameIntervalTime*1000) || (m_bRequestIdr == true))
	{
		m_bRequestIdr = false;
		m_nPrevIdrTime = SD_GetTickCount();
		//TBD:设置IDR帧
	}

	AVPacket tBitstreamPkt;
	av_init_packet(&tBitstreamPkt);
	tBitstreamPkt.data = NULL;
	tBitstreamPkt.size = 0;					
	ret = avcodec_encode_video2(m_ptEncContext, &tBitstreamPkt, ptEncFrame, &got_output);					
	if(ret == 0 && got_output)
	{		
		//返回编码码流大小
		if (tBitstreamPkt.size < nMaxSupportLen)
		{
			bool bRet = AVCFilterNalUnits((const char*)tBitstreamPkt.data, tBitstreamPkt.size, (char*)pucBitstreamData, &ret);
			if(bRet == false)
			{
				SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "AVCFilterNalUnits failed!!");	
				ret = 0;
			}
			else
			{
				//bitstream_save(pucBitstreamData, ret, "D:\\test.h264");
			}

		}
		else
		{
			SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "Output stream buf is too small:%d (%d)!", nMaxSupportLen, tBitstreamPkt.size);	
			ret = -1;
		}
	}
	else
	{
		if (ret < 0)
		{
			char strErrorInfo[512];
			memset(strErrorInfo, 0x0, sizeof(strErrorInfo));
			av_strerror(ret, strErrorInfo, 511);
			SDLOG_PRINTF("CQsv264_Encoder", SD_LOG_LEVEL_ERROR, "avcodec_encode_video2 failed, return:%d %s!", ret, strErrorInfo);
		}
	}
	av_packet_unref(&tBitstreamPkt);
	return ret;
}


//请求即刻编码IDR帧
bool CQsv264_Encoder::IdrRequest()
{
	m_bRequestIdr = true;
	return true;
}

//请求即刻编码IDR帧并且重新初始化时间戳
void CQsv264_Encoder::ReInit()
{
	m_bRequestIdr = true;
	m_nPtsStartTime = 0;
}

int64_t CQsv264_Encoder::mfGeneratePts()
{
	int64_t nNow = (int64_t)SD_GetTickCount();

	if (m_nPtsStartTime == 0)
	{
		m_nPtsStartTime = nNow;
	}

	//1KHZ时基
	return (nNow - m_nPtsStartTime)*1 + m_nPtsBaseNum;
}



