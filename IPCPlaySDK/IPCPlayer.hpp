#pragma once
/////////////////////////////////////////////////////////////////////////
/// @file  IPCPlayer.hpp
/// Copyright (c) 2016, xionggao.lee
/// All rights reserved.  
/// @brief IPCIPC相机播放SDK实现
/// @version 1.0  
/// @author  xionggao.lee
/// @date  2015.12.17
///   
/// 修订说明：最初版本 
/////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
//#include <vld.h>
#include "Common.h"
#include "Media.h"
#include "IPCPlaySDK.h"
#include "./DxSurface/DxSurface.h"
#include "AutoLock.h"
#include "./VideoDecoder/VideoDecoder.h"
#include "TimeUtility.h"
#include "Utility.h"
#include "DSoundPlayer.hpp"
#include "AudioDecoder.h"
#include "DirectDraw.h"
#include "TimeTrace.h"
#include "StreamFrame.h"
#include "Snapshot.h"
#include "YUVFrame.h"
#include "RenderUnit.h"
#define  Win7MajorVersion	6

/// @brief 文件帧信息，用于标识每一帧在文件的位置
struct FileFrameInfo
{
	UINT	nOffset;	///< 帧数据所在的文件偏移
	UINT	nFrameSize;	///< 包含 IPCFrameHeaderEx在内的帧数据的尺寸
	bool	bIFrame;	///< 是否I帧
	time_t	tTimeStamp;
};
// //调试开关
// struct _DebugPin
// {
// 	bool bSwitch;
// 	double dfLastTime;
// 	_DebugPin()
// 	{
// 		bSwitch = true;
// 		dfLastTime = GetExactTime();
// 	}
// 
// };
// 
// struct _DebugSwitch
// {
// 	_DebugPin InputIpcStream;	
// 	_DebugPin DecodeSucceed;
// 	_DebugPin DecodeFailure;
// 	_DebugPin RenderSucceed;
// 	_DebugPin RenderFailure;
// };
// #ifdef _DEBUG
// #define SwitchTrace(pDebugSwitch,member) { if (pDebugSwitch && pDebugSwitch->##member##.bSwitch) if (TimeSpanEx(pDebugSwitch->##member##.dfLastTime) >= 5.0f) {	DxTraceMsg("%s %d %s.\n", __FUNCTION__, __LINE__,#member);	pDebugSwitch->##member##.dfLastTime = GetExactTime();}}
// #endif 
struct _OutputTime
{
	DWORD nDecode;
	DWORD nInputStream;
	DWORD nRender;
	DWORD nQueueSize;
	_OutputTime()
	{
		// 自动初始化所有DWORD型变量，即使后续增加变量也不用再作改动
		DWORD *pThis = (DWORD *)this;
		for (int i = 0; i < sizeof(_OutputTime) / 4; i++)
			pThis[i] = timeGetTime();
		nInputStream = 0;
	}
};

/// @brief 用于码流探测的类
struct StreamProbe
{
	StreamProbe(int nBuffSize = 256 * 1024)
	{
		ZeroMemory(this, sizeof(StreamProbe));
		pProbeBuff = (byte *)_aligned_malloc(nBuffSize, 16);
		nProbeBuffSize = nBuffSize;
	}
	~StreamProbe()
	{
		_aligned_free(pProbeBuff);
	}
	bool GetProbeStream(byte *pStream, int nStreamLength)
	{
		if (!pStream || !nStreamLength)
			return false;
		if ((nProbeDataLength + nStreamLength) > nProbeBuffSize)
			return false;
		memcpy_s(&pProbeBuff[nProbeDataLength], nProbeBuffSize - nProbeDataLength, pStream, nStreamLength);
		nProbeDataLength += nStreamLength;
		nProbeDataRemained = nProbeDataLength;
		nProbeOffset = 0;
		return true;
	}

	byte		*pProbeBuff;			///< 码流控件探测缓冲区
	int			nProbeBuffSize;			///< 探测缓冲的尺寸
	int			nProbeDataLength;		///< 探测缓冲区数居的长度
	int			nProbeDataRemained;
	AVCodecID	nProbeAvCodecID;		///< 探测到的码流ID
	int			nProbeWidth;
	int			nProbeHeight;
	int			nProbeCount;			///< 探测次数
	int			nProbeOffset;			///< 码流探测时视频帧复制时的帧内偏移
};


extern volatile bool g_bThread_ClosePlayer/* = false*/;
extern list<IPC_PLAYHANDLE > g_listPlayerAsyncClose;
extern CRITICAL_SECTION  g_csListPlayertoFree;
extern double	g_dfProcessLoadTime ;
extern CDxSurface* GetDxInCache(int nWidth, int nHeight, D3DFORMAT nPixFormat);
extern void PutDxCache(CDxSurface *pDxSurface);
extern CDxSurfaceEx* g_dx;
extern HWND g_hSnapShotWnd;

/// IPCIPCPlay SDK主要功能实现类
class CIPCPlayer
{
public:
	int		nSize;
private:
	list<FrameNV12Ptr>m_listNV12;		// YUV缓存，硬解码
	list<FrameYV12Ptr>m_listYV12;		// YUV缓存，软解码
	list<StreamFramePtr>m_listAudioCache;///< 音频流播放帧缓冲
	list<StreamFramePtr>m_listVideoCache;///< 视频流播放帧缓冲
	//map<HWND, CDirectDrawPtr> m_MapDDraw;
	list<RenderUnitPtr> m_listRenderUnit;
	CRITICAL_SECTION	m_csVideoCache;		
	CRITICAL_SECTION	m_csListRenderUnit;
	/************************************************************************/
	//   ********注意 ********                                                               
	//请匆移动m_csVideoCache变量定义的位置，因为在构造函数中,为实现快速初始化，需要使用      
	//ZeroMemory对其它成员初始化，但要以此为边界避开所有STL容器类对象,因此所有容器类对象必
	//须定义在此变量之前 
	//***********************************************************************/								
	CRITICAL_SECTION	m_csAudioCache;	
	CRITICAL_SECTION	m_csListYUV;
	int					m_nMaxYUVCache;	// 允许最大的YUV缓存数量
	int					m_nMaxFrameCache;///< 最大视频缓冲数量,默认值100
		///< 当m_FrameCache中的视频帧数量超过m_nMaxFrameCache时，便无法再继续输入流数
public:
	CHAR		m_szLogFileName[512];
//#ifdef _DEBUG
	static CriticalSectionPtr m_pCSGlobalCount;
	int			m_nObjIndex;			///< 当前对象的计数
	static int	m_nGloabalCount;		///< 当前进程中CDvoPlayer对象总数
private:
	DWORD		m_nLifeTime;			///< 当前对象存在时间
	_OutputTime	m_OuputTime;
//#endif
private:
	// 视频播放相关变量
	int			m_nTotalFrames;			///< 当前文件中有效视频帧的数量,仅当播放文件时有效
	int			m_nTotalTime;			///< 当前文件中播放总时长,仅当播放文件时有效
	shared_ptr<StreamProbe>m_pStreamProbe;
	HWND		m_hRenderWnd;					///< 播放视频的窗口句柄
	volatile bool m_bEnableAsyncRender;	///< 启用异步渲染
	
	volatile bool m_bIpcStream;			///< 输入流为IPC流
	volatile DWORD m_nProbeStreamTimeout;///< 探测码流超时间，单位毫秒
	D3DFORMAT	m_nPixelFormat;

	IPC_CODEC	m_nVideoCodec;			///< 视频编码格式 @see IPC_CODEC	
	static		CAvRegister avRegister;	
	//static shared_ptr<CDSound> m_pDsPlayer;///< DirectSound播放对象指针
	shared_ptr<CDSound> m_pDsPlayer;///< DirectSound播放对象指针
	//shared_ptr<CDSound> m_pDsPlayer;	///< DirectSound播放对象指针
	CDSoundBuffer* m_pDsBuffer;
	DxSurfaceInitInfo	m_DxInitInfo;
	CDxSurfaceEx* m_pDxSurface;			///< Direct3d Surface封装类,用于显示视频
	CDirectDraw *m_pDDraw;				///< DirectDraw封装类对象，用于在xp下显示视频
	
	
	shared_ptr<ImageSpace> m_pYUVImage = NULL;
// 	bool		m_bDxReset;				///< 是否重置DxSurface
// 	HWND		m_hDxReset;
// 	CRITICAL_SECTION m_csDxSurface;
	shared_ptr<CVideoDecoder>m_pDecodec;
	//static shared_ptr<CSimpleWnd>m_pWndDxInit;///< 视频显示时，用以初始化DirectX的隐藏窗口对象
	bool		m_bRefreshWnd;			///< 停止播放时是否刷新画面
	int			m_nVideoWidth;			///< 视频宽度
	int			m_nVideoHeight;			///< 视频高度	
	AVPixelFormat	m_nDecodePixelFmt;	///< 解码后的象素格式
	int			m_nFrameEplased;		///< 已经播放帧数
	int			m_nCurVideoFrame;		///< 当前正播放的视频帧ID
	time_t		m_nFirstFrameTime;		///< 文件播放或流回放的第1帧的时间
	time_t		m_tCurFrameTimeStamp;
	time_t		m_tLastFrameTime;
	USHORT		m_nPlayFPS;				///< 实际播放时帧率
	USHORT		m_nPlayFrameInterval;	///< 播放时帧间隔	
// 	int			*m_pSkipFramesArray;	///< 快速播放时要用到的跳帧表
// 	CRITICAL_SECTION	m_csSkipFramesArray;
	HANDLE		m_hEventDecodeStart;	///< 视频解码已经开始事件
	int			m_nSkipFrames;			///< 跳帧表中的元素数量
	bool		m_bEnableD3DCache;		///< 是否启用D3D缓存
	double		m_dfLastTimeVideoPlay;	///< 前一次视频播放的时间
	double		m_dfTimesStart;			///< 开始播放的时间
	bool		m_bEnableHaccel;		///< 是否启用硬解码
	bool		m_bFitWindow;			///< 视频显示是否填满窗口
										///< 为true时，则把视频填满窗口,这样会把图像拉伸,可能会造成图像变形
										///< 为false时，则只按图像原始比例在窗口中显示,超出比例部分,则以黑色背景显示
	CRITICAL_SECTION m_csBorderRect;
	shared_ptr<RECT>m_pBorderRect;		///< 视频显示的边界，边界外的图象不予显示
public:		// 实时流播放参数
	
	bool m_bProbeStream = false;
	int  m_nProbeOffset = 0;
	
private:	// 音频播放相关变量

	IPC_CODEC	m_nAudioCodec;			///< 音频编码格式 @see IPC_CODEC
	bool		m_bEnableAudio;			///< 是否启用音频播放
	DWORD		m_dwAudioOffset;		///< 音频缓中区中的偏移地址
	HANDLE		m_hAudioFrameEvent[2];	///< 音频帧已播放事件
	DWORD		m_dwAudioBuffLength;	///< 
	int			m_nNotifyNum;			///< 音频分区段数量
	double		m_dfLastTimeAudioPlay;	///< 前一次播放音频的时间
	double		m_dfLastTimeAudioSample;///< 前一次音频采样的时间
	int			m_nAudioFrames;			///< 当前缓存中音频帧数量
	int			m_nCurAudioFrame;		///< 当前正播放的音频帧ID
	DWORD		m_nSampleFreq;			///< 音频采样频率
	WORD		m_nSampleBit;			///< 采样位宽
	WORD		m_nAudioPlayFPS;		///< 音频播放帧率

	static shared_ptr<CDSoundEnum> m_pDsoundEnum;	///< 音频设备枚举器
	static CriticalSectionPtr m_csDsoundEnum;
private:
	HANDLE		m_hThreadParser;		///< 解析IPC私有格式录像的线程
	HANDLE		m_hThreadDecode;		///< 视频解码和播放线程
	HANDLE		m_hThreadPlayAudio;		///< 音频解码和播放线程
	//HANDLE		m_hThreadGetFileSummary;///< 文件信息摘要线程
	UINT		m_nVideoCache;
	UINT		m_nAudioCache;
	//HANDLE		m_hCacheFulled;			///< 缓存在索引线程被填满的事件
	UINT		m_nHeaderFrameID;		///< 缓存中第1帧的ID
	UINT		m_nTailFrameID;			///< 级存中最后一帧的ID
	//bool		m_bThreadSummaryRun;
	bool		m_bSummaryIsReady;		///< 文件摘要信息准备完毕
	bool		m_bStopFlag;			///< 播放已停止标志，不再接收码流
	volatile bool m_bThreadParserRun;
	volatile bool m_bThreadDecodeRun;
	volatile bool m_bThreadPlayAudioRun;
	byte*		m_pParserBuffer;		///< 数据解析缓冲区
	UINT		m_nParserBufferSize;	///< 数据解析缓冲区尺寸
	DWORD		m_nParserDataLength;	///< 数据解析缓冲区中的有效数据长度
	UINT		m_nParserOffset;		///< 数据解析缓冲区尺寸当前已经解析的偏移
	CRITICAL_SECTION m_csParser;		
	//volatile bool m_bThreadFileAbstractRun;
	bool		m_bPause;				///< 是否处于暂停状态
	byte		*m_pYUV;				///< YVU捕捉专用内存
	int			m_nYUVSize ;			///<
	shared_ptr<byte>m_pYUVPtr;
	// 截图操作相关句变量
	HANDLE		m_hEvnetYUVReady;		///< YUV数据就绪事件
	HANDLE		m_hEventYUVRequire;		///< YUV数据请求事件,立即把当前解码帧进行复制m_pAvFrameSnapshot
	HANDLE		m_hEventFrameCopied;	///< 截图动作已完成复制事件
	shared_ptr<CSnapshot>m_pSnapshot;
	
private:	// 文件播放相关变量
	
	HANDLE		m_hDvoFile;				///< 正在播放的文件句柄
	INT64		m_nSummaryOffset;		///< 在读取索引时获得的文件解析偏移
#ifdef _DEBUG
	bool		m_bSeekSetDetected = false;///< 是否存在跳动帧动作
#endif
	shared_ptr<IPC_MEDIAINFO>m_pMediaHeader;/// 媒体文件头
	long		m_nSDKVersion;
	IPCFrameHeader m_FirstFrame, m_LastFrame;
	UINT		m_nFrametoRead;			///< 当前将要读取的视频帧ID
	char		*m_pszFileName;			///< 正在播放的文件名,该字段仅在文件播放时间有效
	FileFrameInfo	*m_pFrameOffsetTable;///< 视频帧ID对应文件偏移表
	volatile LONGLONG m_nSeekOffset;	///< 读文件的偏移
	CRITICAL_SECTION	m_csSeekOffset;
	float		m_fPlayRate;			///< 当前的播放的倍率,大于1时为加速播放,小于1时为减速播放，不能为0或小于0
	int			m_nMaxFrameSize;		///< 最大I帧的大小，以字节为单位,默认值256K
	int			m_nVideoFPS;					///< 视频帧的原始帧率
	USHORT		m_nFileFrameInterval;	///< 文件中,视频帧的原始帧间隔
	float		m_fPlayInterval;		///< 帧播放间隔,单位毫秒
	bool		m_bFilePlayFinished;	///< 文件播放完成标志, 为true时，播放结束，为false时，则未结束
	DWORD		m_dwStartTime;
private:
// 	CaptureFrame	m_pfnCaptureFrame;
// 	void*			m_pUserCaptureFrame;
	CaptureYUV		m_pfnCaptureYUV;
	void*			m_pUserCaptureYUV;
	CaptureYUVEx	m_pfnCaptureYUVEx;
	void*			m_pUserCaptureYUVEx;
	FilePlayProc	m_pFilePlayCallBack;
	void*			m_pUserFilePlayer;
	CaptureYUVEx	m_pfnYUVFilter;
	void*			m_pUserYUVFilter;
private:
	shared_ptr<CRunlog> m_pRunlog;	///< 运行日志
#define __countof(array) (sizeof(array)/sizeof(array[0]))
#pragma warning (disable:4996)
	void OutputMsg(char *pFormat, ...)
	{
		int nBuff;
		CHAR szBuffer[4096];
		va_list args;
		va_start(args, pFormat);		
		nBuff = _vsnprintf(szBuffer, __countof(szBuffer), pFormat, args);
		//::wvsprintf(szBuffer, pFormat, args);
		//assert(nBuff >=0);
//#ifdef _DEBUG
		OutputDebugStringA(szBuffer);
//#endif
		if (m_pRunlog)
			m_pRunlog->Runlog(szBuffer);
		va_end(args);
	}
public:
	
private:
	CIPCPlayer()
	{
		ZeroMemory(&m_csVideoCache, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_csVideoCache));
		InitializeCriticalSection(&m_csVideoCache);
		InitializeCriticalSection(&m_csAudioCache);
		InitializeCriticalSection(&m_csParser);
		InitializeCriticalSection(&m_csBorderRect);
		InitializeCriticalSection(&m_csListYUV);
		InitializeCriticalSection(&m_csListRenderUnit);
		m_nMaxFrameSize = 1024 * 256;
		nSize = sizeof(CIPCPlayer);
		m_nAudioPlayFPS = 50;
		m_nSampleFreq = 8000;
		m_nSampleBit = 16;
		m_nProbeStreamTimeout = 10000;	// 毫秒
		m_nMaxYUVCache = 10;
		m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
	}
	LONGLONG GetSeekOffset()
	{
		CAutoLock lock(&m_csSeekOffset, false,__FILE__, __FUNCTION__, __LINE__);
		return m_nSeekOffset;
	}
	void SetSeekOffset(LONGLONG nSeekOffset)
	{
		CAutoLock lock(&m_csSeekOffset, false, __FILE__, __FUNCTION__, __LINE__);
		m_nSeekOffset = nSeekOffset;
	}
	// 超大文件寻址
	__int64 LargerFileSeek(HANDLE hFile, __int64 nOffset64, DWORD MoveMethod)
	{
		LARGE_INTEGER Offset;
		Offset.QuadPart = nOffset64;
		Offset.LowPart = SetFilePointer(hFile, Offset.LowPart, &Offset.HighPart,MoveMethod);

		if (Offset.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
		{
			Offset.QuadPart = -1;
		}
		return Offset.QuadPart;
	}
	/// @brief 判断输入帧是否为IPC私有的视频帧
	/// @param[in]	pFrameHeader	IPC私有帧结构指针详见@see IPCFrameHeaderEx
	/// @param[out] bIFrame			判断输入帧是否为I帧
	/// -# true			输入帧为I帧
	///	-# false		输入帧为其它帧
	static bool IsIPCVideoFrame(IPCFrameHeader *pFrameHeader,bool &bIFrame,int nSDKVersion)
	{
		bIFrame = false;
		if (nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
		{
			switch (pFrameHeader->nType)
			{
			case FRAME_P:		// BP帧数量最多，所以前置，以减少比较次数
			case FRAME_B:
				return true;
			case 0:
			case FRAME_IDR:
			case FRAME_I:
				bIFrame = true;
				return true;
			default:
				return false;
			}
		}
		else
		{
			switch (pFrameHeader->nType)
			{// 旧版SDK中，0为bbp帧 ,1为I帧 ,2为音频帧
			case 0:
				return true;
				break;
			case 1:
				bIFrame = true;
				return true;
				break;
			default:
				return false;
				break;
			}
		}
	}

	/// @brief 取得视频文件第一个I帧或最后一个视频帧时间
	/// @param[out]	帧时间
	/// @param[in]	是否取第一个I帧时间，若为true则取第一个I帧的时间否则取最一个视频帧时间
	/// @remark 该函数只用于从旧版的IPC录像文件中取得第一帧和最后一帧
	int GetFrame(IPCFrameHeader *pFrame, bool bFirstFrame = true)
	{
		if (!m_hDvoFile)
			return IPC_Error_FileNotOpened;
		LARGE_INTEGER liFileSize;
		if (!GetFileSizeEx(m_hDvoFile, &liFileSize))
			return GetLastError();
		byte *pBuffer = _New byte[m_nMaxFrameSize];
		shared_ptr<byte>TempBuffPtr(pBuffer);
		DWORD nMoveMothod = FILE_BEGIN;
		__int64 nMoveOffset = sizeof(IPC_MEDIAINFO);

		if (!bFirstFrame)
		{
			nMoveMothod = FILE_END;
			nMoveOffset = -m_nMaxFrameSize;
		}
		
		if (liFileSize.QuadPart >= m_nMaxFrameSize &&
			LargerFileSeek(m_hDvoFile, nMoveOffset, nMoveMothod) == INVALID_SET_FILE_POINTER)
			return GetLastError();
		DWORD nBytesRead = 0;
		DWORD nDataLength = m_nMaxFrameSize;

		if (!ReadFile(m_hDvoFile, pBuffer, nDataLength, &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
		nDataLength = nBytesRead;
		char *szKey1 = "MOVD";
		char *szKey2 = "IMWH";
		int nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
		if (nOffset < 0)
		{
			nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
			if (nOffset < 0)
				return IPC_Error_MaxFrameSizeNotEnough;
		}
		nOffset -= offsetof(IPCFrameHeader, nFrameTag);	// 回退到帧头
		if (nOffset < 0)
			return IPC_Error_NotVideoFile;
		// 遍历所有最后m_nMaxFrameSize长度内的所有帧
		pBuffer += nOffset;
		nDataLength -= nOffset;
		bool bFoundVideo = false;

		FrameParser Parser;
#ifdef _DEBUG
		int nAudioFrames = 0;
		int nVideoFrames = 0;
		while (ParserFrame(&pBuffer, nDataLength, &Parser))
		{
			switch (Parser.pHeader->nType)
			{
			case 0:
			case 1:
			{
				nVideoFrames++;
				bFoundVideo = true;
				break;
			}
			case 2:      // G711 A律编码帧
			case FRAME_G711U:      // G711 U律编码帧
			{
				nAudioFrames++;
				break;
			}
			default:
			{
				assert(false);
				break;
			}
			}
			if (bFoundVideo && bFirstFrame)
				break;
			
			nOffset += Parser.nFrameSize;
		}
		OutputMsg("%s In Last %d bytes:VideoFrames:%d\tAudioFrames:%d.\n", __FUNCTION__, m_nMaxFrameSize, nVideoFrames, nAudioFrames);
#else
		while (ParserFrame(&pBuffer, nDataLength, &Parser))
		{
			if (Parser.pHeaderEx->nType == 0 ||
				Parser.pHeaderEx->nType == 1 )
			{
				bFoundVideo = true;
			}
			if (bFoundVideo && bFirstFrame)
			{
				break;
			}
			nOffset += Parser.nFrameSize;
		}
#endif
		if (SetFilePointer(m_hDvoFile, sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			return GetLastError();
		if (bFoundVideo)
		{
			memcpy_s(pFrame, sizeof(IPCFrameHeader), Parser.pHeader, sizeof(IPCFrameHeader));
			return IPC_Succeed;
		}
		else
			return IPC_Error_MaxFrameSizeNotEnough;
	}
	/// @brief 取得视频文件帧的ID和时间,相当于视频文件中包含的视频总帧数
	int GetLastFrameID(int &nLastFrameID)
	{
		if (!m_hDvoFile)
			return IPC_Error_FileNotOpened;		
		LARGE_INTEGER liFileSize;
		if (!GetFileSizeEx(m_hDvoFile, &liFileSize))
			return GetLastError();
		byte *pBuffer = _New byte[m_nMaxFrameSize];
		shared_ptr<byte>TempBuffPtr(pBuffer);
				
		if (liFileSize.QuadPart >= m_nMaxFrameSize && 
			LargerFileSeek(m_hDvoFile, -m_nMaxFrameSize, FILE_END) == INVALID_SET_FILE_POINTER)
			return GetLastError();
		DWORD nBytesRead = 0;
		DWORD nDataLength = m_nMaxFrameSize;
		
		if (!ReadFile(m_hDvoFile, pBuffer, nDataLength, &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
		nDataLength = nBytesRead;
		char *szKey1 = "MOVD";		// 新版的IPC录像文件头
		char *szKey2 = "IMWH";		// 老版本的IPC录像文件，使用了高视捷的文件头
		int nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
		if (nOffset < 0)
		{
			nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
			if (nOffset < 0)
				return IPC_Error_MaxFrameSizeNotEnough;
			else if (nOffset < offsetof(IPCFrameHeader, nFrameTag))
			{
				pBuffer += (nOffset + 4);
				nDataLength -= (nOffset + 4);
				nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
			}
		}
		else if (nOffset < offsetof(IPCFrameHeader, nFrameTag))
		{
			pBuffer += (nOffset + 4);
			nDataLength -= (nOffset + 4);
			nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
		}
		nOffset -= offsetof(IPCFrameHeader, nFrameTag);	// 回退到帧头
		if (nOffset < 0)
			return IPC_Error_NotVideoFile;
		// 遍历所有最后m_nMaxFrameSize长度内的所有帧
		pBuffer += nOffset;
		nDataLength -= nOffset;
		bool bFoundVideo = false;
		
		FrameParser Parser;
#ifdef _DEBUG
		int nAudioFrames = 0;
		int nVideoFrames = 0;
		while (ParserFrame(&pBuffer, nDataLength, &Parser))
		{
			switch (Parser.pHeaderEx->nType)
			{
			case 0:
			case FRAME_B:
			case FRAME_I:
			case FRAME_IDR:
			case FRAME_P:
			{
				nVideoFrames++;
				nLastFrameID = Parser.pHeaderEx->nFrameID;
				bFoundVideo = true;
				break;
			}
			case FRAME_G711A:      // G711 A律编码帧
			case FRAME_G711U:      // G711 U律编码帧
			case FRAME_G726:       // G726编码帧
			case FRAME_AAC:        // AAC编码帧。
			{
				nAudioFrames ++;
				break;
			}
			default:
			{
				assert(false);
				break;
			}
			}

			nOffset += Parser.nFrameSize;
		}
		OutputMsg("%s In Last %d bytes:VideoFrames:%d\tAudioFrames:%d.\n", __FUNCTION__, m_nMaxFrameSize, nVideoFrames, nAudioFrames);
#else
		while (ParserFrame(&pBuffer, nDataLength, &Parser))
		{
			if (Parser.pHeaderEx->nType == FRAME_B ||
				Parser.pHeaderEx->nType == FRAME_I ||
				Parser.pHeaderEx->nType == FRAME_P)
			{
				nLastFrameID = Parser.pHeaderEx->nFrameID;
				bFoundVideo = true;
			}
			nOffset += Parser.nFrameSize;
		}
#endif
		if (SetFilePointer(m_hDvoFile, sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			return GetLastError();
		if (bFoundVideo)
			return IPC_Succeed;
		else
			return IPC_Error_MaxFrameSizeNotEnough;
	}
	//shared_ptr<CSimpleWnd> m_pSimpleWnd /*= make_shared<CSimpleWnd>()*/;
	
	bool InitizlizeDisplay()
	{
		if (!m_hRenderWnd)
			return false;
		// 初始显示组件
		if (GetOsMajorVersion() < Win7MajorVersion)
		{
			m_pDDraw = _New CDirectDraw();
			if (m_pDDraw)
			{
				if (m_bEnableHaccel)
				{
					DDSURFACEDESC2 ddsd = { 0 };
					FormatNV12::Build(ddsd, m_nVideoWidth, m_nVideoHeight);
					m_pDDraw->Create<FormatNV12>(m_hRenderWnd, ddsd);
					m_pYUVImage = make_shared<ImageSpace>();
					m_pYUVImage->dwLineSize[0] = m_nVideoWidth;
					m_pYUVImage->dwLineSize[1] = m_nVideoWidth >> 1;
					
				}
				else
				{
					//构造DirectDraw表面  
					DDSURFACEDESC2 ddsd = { 0 };
					FormatYV12::Build(ddsd, m_nVideoWidth, m_nVideoHeight);
					m_pDDraw->Create<FormatYV12>(m_hRenderWnd, ddsd);
					m_pYUVImage = make_shared<ImageSpace>();
					m_pYUVImage->dwLineSize[0] = m_nVideoWidth;
					m_pYUVImage->dwLineSize[1] = m_nVideoWidth >> 1;
					m_pYUVImage->dwLineSize[2] = m_nVideoWidth >> 1;
				}
				Autolock(&m_csListRenderUnit);
				for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end(); it++)
					(*it)->SetVideoSize(m_nVideoWidth, m_nVideoHeight);
				
			}
			return true;
		}
		else
		{
			m_pDxSurface = _New CDxSurfaceEx;
			
			// 使用线程内CDxSurface对象显示图象
			if (m_pDxSurface)		// D3D设备尚未初始化
			{
				//m_pSimpleWnd = make_shared<CSimpleWnd>(m_nVideoWidth, m_nVideoHeight);
				DxSurfaceInitInfo &InitInfo = m_DxInitInfo;
				InitInfo.nSize = sizeof(DxSurfaceInitInfo);
				if (m_bEnableHaccel)
				{
					m_pDxSurface->SetD3DShared(m_bD3dShared);
					AVCodecID nCodecID = AV_CODEC_ID_H264;
					if (m_nVideoCodec == CODEC_H265)
						nCodecID = AV_CODEC_ID_HEVC;
					InitInfo.nFrameWidth = CVideoDecoder::GetAlignedDimension(nCodecID,m_nVideoWidth);
					InitInfo.nFrameHeight = CVideoDecoder::GetAlignedDimension(nCodecID,m_nVideoHeight);
					InitInfo.nD3DFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
				}
				else
				{
					///if (GetOsMajorVersion() < 6)
					///	InitInfo.nD3DFormat = D3DFMT_A8R8G8B8;		// 在XP环境下，某些集成显示在显示YV12象素时，会导致CPU占用率缓慢上升，这可能是D3D9或该集成显示的一个BUG
					///else
					InitInfo.nFrameWidth = m_nVideoWidth;
					InitInfo.nFrameHeight = m_nVideoHeight;
					InitInfo.nD3DFormat = m_nPixelFormat;//(D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
				}
				
				InitInfo.bWindowed = TRUE;
				//if (!pWndDxInit->GetSafeHwnd())
				InitInfo.hPresentWnd = /*m_pSimpleWnd->GetSafeHwnd()*/m_hRenderWnd;
				//else
				//	InitInfo.hPresentWnd = pWndDxInit->GetSafeHwnd();

				if (m_nRocateAngle == Rocate90 ||
					m_nRocateAngle == Rocate270 ||
					m_nRocateAngle == RocateN90)
					swap(InitInfo.nFrameWidth, InitInfo.nFrameHeight);
				
				m_pDxSurface->DisableVsync();		// 禁用垂直同步，播放帧才有可能超过显示器的刷新率，从而达到高速播放的目的
				if (!m_pDxSurface->InitD3D(InitInfo.hPresentWnd,
					InitInfo.nFrameWidth,
					InitInfo.nFrameHeight,
					InitInfo.bWindowed,
					InitInfo.nD3DFormat))
				{
					OutputMsg("%s Initialize DxSurface failed.\n", __FUNCTION__);
#ifdef _DEBUG
					OutputMsg("%s \tObject:%d DxSurface failed,Line %d Time = %d.\n", __FUNCTION__, m_nObjIndex, __LINE__, timeGetTime() - m_nLifeTime);
#endif
					return false;
				}
				return true;
			}
			else
				return false;
		}
	}
#ifdef _DEBUG
	int m_nRenderFPS = 0;
	int m_nRenderFrames = 0;
	double dfTRender = 0.0f;
#endif
	/// @brief 渲染一帧
	void RenderFrame(AVFrame *pAvFrame)
	{
		if (!m_hRenderWnd || m_bStopFlag)
			return;
		
// 		if (m_nRocateAngle)
// 		{
// 			int nRocateWidth = pAvFrame->width;
// 			int nRocateHeight = pAvFrame->height;
// 			switch (m_nRocateAngle)
// 			{
// 			case RocateNone:
// 			default:
// 				break;
// 			case Rocate90:	
// 			case Rocate270:		
// 				nRocateWidth = pAvFrame->height;
// 				nRocateHeight = pAvFrame->width;
// 			case Rocate180:			
// 			case RocateN90:
// 			{
// 				if (!m_pRacoateFrame)
// 				{
// 					m_pRacoateFrame = av_frame_alloc();
// 					int nImageSize = av_image_get_buffer_size((AVPixelFormat)pAvFrame->format, nRocateHeight, nRocateWidth, 8);
// 					if (nImageSize < 0)
// 					{
// 						char szAvError[256] = { 0 };
// 						av_strerror(nImageSize, szAvError, 1024);
// 						DxTraceMsg("%s av_image_get_buffer_size failed:%s.\n", __FUNCTION__, szAvError);
// 						assert(false);
// 					}
// 					
// 					m_pRocateImage = (byte *)_aligned_malloc(nImageSize, 8);
// 					if (!m_pRocateImage)
// 					{
// 						DxTraceMsg("%s Alloc memory failed @%s %d.\n", __FUNCTION__, __FUNCTION__, __LINE__);
// 						assert(false);
// 						return;
// 					}
// 					ZeroMemory(m_pRocateImage,nImageSize);
// 					DxTraceMsg("%s RocateImage size = %d.\n", __FUNCTION__, nImageSize);
// 					// 把显示图像与YUV帧关联
// 					av_image_fill_arrays(m_pRacoateFrame->data, m_pRacoateFrame->linesize, m_pRocateImage, (AVPixelFormat)pAvFrame->format, nRocateHeight, nRocateWidth, 8);
// 					m_pRacoateFrame->width = nRocateWidth;
// 					m_pRacoateFrame->height = nRocateHeight;
// 					m_pRacoateFrame->format = pAvFrame->format;
// 
// 				}
// 				RocateYUV420_90(pAvFrame->data, m_pRacoateFrame->data, pAvFrame->linesize, pAvFrame->width, pAvFrame->height);
// 			}
// 			break;
// 			
// 			}
// 		}
		if (m_bFitWindow)
		{
			if (m_pDxSurface)
			{
				RECT rtBorder;
				Autolock(&m_csBorderRect);
				if (m_pRacoateFrame)
				{
					if (m_pBorderRect)
					{
						CopyRect(&rtBorder, m_pBorderRect.get());
						lock.Unlock();
						m_pDxSurface->Render(m_pRacoateFrame,m_nRocateAngle);
						m_pDxSurface->Present(m_hRenderWnd, &rtBorder);
					}
					else
					{
						lock.Unlock();
						m_pDxSurface->Render(m_pRacoateFrame);
						m_pDxSurface->Present(m_hRenderWnd);
					}
				}
				else
				{
					if (m_pBorderRect)
					{
						CopyRect(&rtBorder, m_pBorderRect.get());
						lock.Unlock();
						m_pDxSurface->Render(pAvFrame);
						m_pDxSurface->Present(m_hRenderWnd, &rtBorder);
					}
					else
					{
						lock.Unlock();
						m_pDxSurface->Render(pAvFrame,m_nRocateAngle);
						m_pDxSurface->Present(m_hRenderWnd);
					}
				}

			}
			else if (m_pDDraw)
			{
				if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
				{
					byte *pY = NULL , *pUV = NULL;
					int nPitch;
					LockDxvaFrame(pAvFrame, &pY, &pUV, nPitch);
					m_pYUVImage->dwLineSize[0] = nPitch;
					m_pYUVImage->pBuffer[0] = (PBYTE)pY;
					m_pYUVImage->pBuffer[1] = (PBYTE)pUV;
					UnlockDxvaFrame(pAvFrame);
					RECT rtBorder;
					Autolock(&m_csBorderRect);
					if (m_pBorderRect)
					{
						CopyRect(&rtBorder, m_pBorderRect.get());
						lock.Unlock();
						m_pDDraw->Draw(*m_pYUVImage, &rtBorder, nullptr, true);
						Autolock1(&m_csListRenderUnit);
						for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end() && !m_bStopFlag; it++)
							(*it)->RenderImage(*m_pYUVImage, &rtBorder, nullptr, true);
					}
					else
					{
						lock.Unlock();
						m_pDDraw->Draw(*m_pYUVImage, nullptr, nullptr, true);	
						Autolock1(&m_csListRenderUnit);
						for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end() && !m_bStopFlag; it++)
							(*it)->RenderImage(*m_pYUVImage, nullptr, nullptr, true);
					}				
				}
				else
				{
					m_pYUVImage->pBuffer[0] = (PBYTE)pAvFrame->data[0];
					m_pYUVImage->pBuffer[1] = (PBYTE)pAvFrame->data[1];
					m_pYUVImage->pBuffer[2] = (PBYTE)pAvFrame->data[2];
					m_pYUVImage->dwLineSize[0] = pAvFrame->linesize[0];
					m_pYUVImage->dwLineSize[1] = pAvFrame->linesize[1];
					m_pYUVImage->dwLineSize[2] = pAvFrame->linesize[2];
					RECT rtBorder;
					Autolock(&m_csBorderRect);
					if (m_pBorderRect)
					{
						CopyRect(&rtBorder, m_pBorderRect.get());
						lock.Unlock();
						m_pDDraw->Draw(*m_pYUVImage, &rtBorder, nullptr, true);
						Autolock1(&m_csListRenderUnit);
						for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end() && !m_bStopFlag; it++)
							(*it)->RenderImage(*m_pYUVImage, &rtBorder, nullptr, true);
					}
					else
					{
						lock.Unlock();
						m_pDDraw->Draw(*m_pYUVImage, nullptr, nullptr, true);
						Autolock1(&m_csListRenderUnit);
						for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end() && !m_bStopFlag; it++)
							(*it)->RenderImage(*m_pYUVImage, nullptr, nullptr, true);
					}
				}
			}
		}
		else
		{
			RECT rtRender;
			GetWindowRect(m_hRenderWnd, &rtRender);
			int nWndWidth = rtRender.right - rtRender.left;
			int nWndHeight = rtRender.bottom - rtRender.top;
			float fScaleWnd = (float)nWndHeight / nWndWidth;
			float fScaleVideo = (float)pAvFrame->height / pAvFrame->width;
			if (fScaleVideo < fScaleWnd)			// 窗口高度超出比例,需要去掉一部分高度,视频需要上下居中
			{
				int nNewHeight = (int)nWndWidth * fScaleVideo;
				int nOverHeight = nWndHeight - nNewHeight;
				if ((float)nOverHeight / nWndHeight > 0.01f)	// 窗口高度超出1%,则调整高度，否则忽略该差异
				{
					rtRender.top += nOverHeight / 2;
					rtRender.bottom -= nOverHeight / 2;
				}
			}
			else if (fScaleVideo > fScaleWnd)		// 窗口宽度超出比例,需要去掉一部分宽度，视频需要左右居中
			{
				int nNewWidth = nWndHeight/fScaleVideo;
				int nOverWidth = nWndWidth - nNewWidth;
				if ((float)nOverWidth / nWndWidth > 0.01f)
				{
					rtRender.left += nOverWidth / 2;
					rtRender.right -= nOverWidth / 2;
				}
			}
			if (m_pDxSurface)
			{
				ScreenToClient(m_hRenderWnd, (LPPOINT)&rtRender);
				ScreenToClient(m_hRenderWnd, ((LPPOINT)&rtRender) + 1);
				RECT rtBorder;
				Autolock(&m_csBorderRect);
// 				if (m_pRacoateFrame)
// 				{
// 					if (m_pBorderRect)
// 					{
// 						CopyRect(&rtBorder, m_pBorderRect.get());
// 						lock.Unlock();
// 						m_pDxSurface->Render(m_pRacoateFrame);
// 						m_pDxSurface->Present(m_hRenderWnd, &rtBorder, &rtRender);
// 					}
// 					else
// 					{
// 						lock.Unlock();
// 						m_pDxSurface->Render(m_pRacoateFrame);
// 						m_pDxSurface->Present(m_hRenderWnd, nullptr, &rtRender);
// 					}
// 				}
// 				else
				{
					if (m_pBorderRect)
					{
						CopyRect(&rtBorder, m_pBorderRect.get());
						lock.Unlock();
						m_pDxSurface->Render(pAvFrame,m_nRocateAngle);
						m_pDxSurface->Present(m_hRenderWnd, &rtBorder, &rtRender);
					}
					else
					{
						lock.Unlock();
						m_pDxSurface->Render(pAvFrame, m_nRocateAngle);
						m_pDxSurface->Present(m_hRenderWnd, nullptr, &rtRender);
					}
				}
				
			}
			else if (m_pDDraw)
			{
				m_pYUVImage->pBuffer[0] = (PBYTE)pAvFrame->data[0];
				m_pYUVImage->pBuffer[1] = (PBYTE)pAvFrame->data[1];
				m_pYUVImage->pBuffer[2] = (PBYTE)pAvFrame->data[2];
				m_pYUVImage->dwLineSize[0] = pAvFrame->linesize[0];
				m_pYUVImage->dwLineSize[1] = pAvFrame->linesize[1];
				m_pYUVImage->dwLineSize[2] = pAvFrame->linesize[2];
				RECT rtBorder;
				Autolock(&m_csBorderRect);
				if (m_pBorderRect)
				{
					CopyRect(&rtBorder, m_pBorderRect.get());
					lock.Unlock();
					m_pDDraw->Draw(*m_pYUVImage, &rtBorder, &rtRender, true);
				}
				else
				{
					lock.Unlock();
					m_pDDraw->Draw(*m_pYUVImage,nullptr, &rtRender, true);
				}
					
			}
		}
	}
	// 二分查找
	int BinarySearch(time_t tTime)
	{
		int low = 0;
		int high = m_nTotalFrames - 1;
		int middle = 0;
		while (low <= high)
		{
			middle = (low + high) / 2;
			if (tTime >= m_pFrameOffsetTable[middle].tTimeStamp &&
				tTime <= m_pFrameOffsetTable[middle + 1].tTimeStamp)
				return middle;
			//在左半边
			else if (tTime < m_pFrameOffsetTable[middle].tTimeStamp)
				high = middle - 1;
			//在右半边
			else if (tTime > m_pFrameOffsetTable[middle + 1].tTimeStamp)
				low = middle + 1;
		}
		//没找到
		return -1;
	}
public:
	/// @brief  启用日志
	/// @param	szLogFile		日志文件名,若该参数为null，则禁用日志
	void EnableRunlog(const char *szLogFile)
	{
		char szFileLog[MAX_PATH] = { 0 };
		if (szLogFile && strlen(szLogFile) != 0)
		{
			strcpy(szFileLog, szLogFile);
			m_pRunlog = make_shared<CRunlog>(szFileLog);
		}
		else
			m_pRunlog = nullptr;
	}

	/// @brief 设置音频播放参数
	/// @param nPlayFPS		音频码流的帧率
	/// @param nSampleFreq	采样频率
	/// @param nSampleBit	采样位置
	/// @remark 在播放音频之前，应先设置音频播放参数,SDK内部默认参数nPlayFPS = 50，nSampleFreq = 8000，nSampleBit = 16
	///         若音频播放参数与SDK内部默认参数一致，可以不用设置这些参数
	void SetAudioPlayParameters(DWORD nPlayFPS = 50, DWORD nSampleFreq = 8000, WORD nSampleBit = 16)
	{
		m_nAudioPlayFPS	 = nPlayFPS;
		m_nSampleFreq	 = nSampleFreq;
		m_nSampleBit	 = nSampleBit;
	}
	void ClearOnException()
	{
		if (m_pszFileName)
		{
			delete[]m_pszFileName;
			m_pszFileName = nullptr;
		}
		if (m_pRunlog)
			m_pRunlog = nullptr;
		DeleteCriticalSection(&m_csVideoCache);
		DeleteCriticalSection(&m_csAudioCache);
		DeleteCriticalSection(&m_csSeekOffset);
		DeleteCriticalSection(&m_csParser);
		DeleteCriticalSection(&m_csBorderRect);
		DeleteCriticalSection(&m_csListYUV);
		DeleteCriticalSection(&m_csListRenderUnit);
		if (m_hDvoFile)
			CloseHandle(m_hDvoFile);
	}
	CIPCPlayer(HWND hWnd, CHAR *szFileName = nullptr, char *szLogFile = nullptr) 
	{
		ZeroMemory(&m_csVideoCache, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_csVideoCache));
#ifdef _DEBUG
		m_pCSGlobalCount->Lock();
		m_nObjIndex = m_nGloabalCount++;
		m_pCSGlobalCount->Unlock();
		m_nLifeTime = timeGetTime();
		
// 		m_OuputTime.nDecode = m_nLifeTime;
// 		m_OuputTime.nInputStream = m_nLifeTime;
// 		m_OuputTime.nRender = m_nLifeTime;

		OutputMsg("%s \tObject:%d m_nLifeTime = %d.\n", __FUNCTION__, m_nObjIndex, m_nLifeTime);
#endif 
		m_nSDKVersion = IPC_IPC_SDK_VERSION_2015_12_16;
		if (szLogFile)
		{
			strcpy(m_szLogFileName, szLogFile);
			m_pRunlog = make_shared<CRunlogA>(szLogFile);
		}
		m_hEvnetYUVReady = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		m_hEventDecodeStart = CreateEvent(nullptr, TRUE, FALSE, nullptr);
 		m_hEventYUVRequire = CreateEvent(nullptr, FALSE, FALSE, nullptr);
 		m_hEventFrameCopied = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		InitializeCriticalSection(&m_csVideoCache);
		InitializeCriticalSection(&m_csAudioCache);
		InitializeCriticalSection(&m_csSeekOffset);
		InitializeCriticalSection(&m_csParser);
		InitializeCriticalSection(&m_csBorderRect);
		InitializeCriticalSection(&m_csListYUV);
		InitializeCriticalSection(&m_csListRenderUnit);
		m_csDsoundEnum->Lock();
		if (!m_pDsoundEnum)
			m_pDsoundEnum = make_shared<CDSoundEnum>();	///< 音频设备枚举器
		m_csDsoundEnum->Unlock();
		m_nAudioPlayFPS = 50;
		m_nSampleFreq = 8000;
		m_nSampleBit = 16;
		m_nProbeStreamTimeout = 10000;	// 毫秒
		m_nMaxYUVCache = 10;
#ifdef _DEBUG
		OutputMsg("%s Alloc a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
		nSize = sizeof(CIPCPlayer);
		m_nMaxFrameSize	 = 1024 * 256;
		m_nVideoFPS		 = 25;				// FPS的默认值为25
		m_fPlayRate		 = 1;
		m_fPlayInterval	 = 40.0f;
		//m_nVideoCodec	 = CODEC_H264;		// 视频默认使用H.264编码
		m_nVideoCodec = CODEC_UNKNOWN;
		m_nAudioCodec	 = CODEC_G711U;		// 音频默认使用G711U编码
//#ifdef _DEBUG
//		m_nMaxFrameCache = 200;				// 默认最大视频缓冲数量为200
// #else
 		m_nMaxFrameCache = 100;				// 默认最大视频缓冲数量为100
		m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
		m_hRenderWnd = hWnd;
// #endif
		if (szFileName)
		{
			m_pszFileName = _New char[MAX_PATH];
			strcpy(m_pszFileName, szFileName);
			// 打开文件
			m_hDvoFile = CreateFileA(m_pszFileName,
				GENERIC_READ,
				FILE_SHARE_READ,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_ARCHIVE,
				NULL);
			if (m_hDvoFile != INVALID_HANDLE_VALUE)
			{
				int nError = GetFileHeader();
				if (nError != IPC_Succeed)
				{
					OutputMsg("%s %d(%s):Not a IPC Media File.\n", __FILE__, __LINE__, __FUNCTION__);
					ClearOnException();
					throw std::exception("Not a IPC Media File.");
				}
				// GetLastFrameID取得的是最后一帧的ID，总帧数还要在此基础上+1
				if (m_pMediaHeader)
				{
					m_nSDKVersion = m_pMediaHeader->nSDKversion;
					switch (m_nSDKVersion)
					{
					case IPC_IPC_SDK_VERSION_2015_09_07:
					case IPC_IPC_SDK_VERSION_2015_10_20:
					case IPC_IPC_SDK_GSJ_HEADER:
					{
						m_nVideoFPS = 25;
						m_nVideoCodec = CODEC_UNKNOWN;
						m_nVideoWidth = 0;
						m_nVideoHeight = 0;
						// 取得第一帧和最后一帧的信息
						if (GetFrame(&m_FirstFrame, true) != IPC_Succeed || 
							GetFrame(&m_LastFrame, false) != IPC_Succeed)
						{
							OutputMsg("%s %d(%s):Can't get the First or Last.\n", __FILE__, __LINE__, __FUNCTION__);
							ClearOnException();
							throw std::exception("Can't get the First or Last.");
						}
						// 取得文件总时长(ms)
						__int64 nTotalTime = 0;
						__int64 nTotalTime2 = 0;
						if (m_pMediaHeader->nCameraType == 1)	// 安讯士相机
						{
							nTotalTime = (m_LastFrame.nFrameUTCTime - m_FirstFrame.nFrameUTCTime) * 100;
							nTotalTime2 = (m_LastFrame.nTimestamp - m_FirstFrame.nTimestamp) / 10000;
						}
						else
						{
							nTotalTime = (m_LastFrame.nFrameUTCTime - m_FirstFrame.nFrameUTCTime) * 1000;
							nTotalTime2 = (m_LastFrame.nTimestamp - m_FirstFrame.nTimestamp) / 1000;
						}
						if (nTotalTime < 0)
						{
							OutputMsg("%s %d(%s):The Frame timestamp is invalid.\n", __FILE__, __LINE__, __FUNCTION__);
							ClearOnException();
							throw std::exception("The Frame timestamp is invalid.");
						}
						if (nTotalTime2 > 0)
						{
							m_nTotalTime = nTotalTime2;
							// 根据总时间预测总帧数
							m_nTotalFrames = m_nTotalTime / 40;		// 老版文件使用固定帧率,每帧间隔为40ms
							m_nTotalFrames += 25;
						}
						else if (nTotalTime > 0)
						{
							m_nTotalTime = nTotalTime;
							// 根据总时间预测总帧数
							m_nTotalFrames = m_nTotalTime / 40;		// 老版文件使用固定帧率,每帧间隔为40ms
							m_nTotalFrames += 50;
						}
						else
						{
							OutputMsg("%s %d(%s):Frame timestamp error.\n", __FILE__, __LINE__, __FUNCTION__);
							ClearOnException();
							throw std::exception("Frame timestamp error.");
						}
						break;
					}
					
					case IPC_IPC_SDK_VERSION_2015_12_16:
					{
						int nDvoError = GetLastFrameID(m_nTotalFrames);
						if (nDvoError != IPC_Succeed)
						{
							OutputMsg("%s %d(%s):Can't get last FrameID .\n", __FILE__, __LINE__, __FUNCTION__);
							ClearOnException();
							throw std::exception("Can't get last FrameID.");
						}
						m_nTotalFrames++;
						m_nVideoCodec = m_pMediaHeader->nVideoCodec;
						m_nAudioCodec = m_pMediaHeader->nAudioCodec;
						if (m_nVideoCodec == CODEC_UNKNOWN)
						{
							m_nVideoWidth = 0;
							m_nVideoHeight = 0;
						}
						else
						{
							m_nVideoWidth = m_pMediaHeader->nVideoWidth;
							m_nVideoHeight = m_pMediaHeader->nVideoHeight;
							if (!m_nVideoWidth || !m_nVideoHeight)
							{
// 								OutputMsg("%s %d(%s):Invalid Mdeia File Header.\n", __FILE__, __LINE__, __FUNCTION__);
// 								ClearOnException();
// 								throw std::exception("Invalid Mdeia File Header.");
								m_nVideoCodec = CODEC_UNKNOWN;
							}
						}
						if (m_pMediaHeader->nFps == 0)
							m_nVideoFPS = 25;
						else
							m_nVideoFPS = m_pMediaHeader->nFps;
					}
					break;
					default:
					{
						OutputMsg("%s %d(%s):Invalid SDK Version.\n", __FILE__, __LINE__, __FUNCTION__);
						ClearOnException();
						throw std::exception("Invalid SDK Version.");
					}
					}
					m_nFileFrameInterval = 1000 / m_nVideoFPS;
				}
// 				m_hCacheFulled = CreateEvent(nullptr, true, false, nullptr);
// 				m_bThreadSummaryRun = true;
// 				m_hThreadGetFileSummary = (HANDLE)_beginthreadex(nullptr, 0, ThreadGetFileSummary, this, 0, 0);
// 				if (!m_hThreadGetFileSummary)
// 				{
// 					OutputMsg("%s %d(%s):_beginthreadex ThreadGetFileSummary Failed.\n", __FILE__, __LINE__, __FUNCTION__);
// 					ClearOnException();
// 					throw std::exception("_beginthreadex ThreadGetFileSummary Failed.");
// 				}
				m_nParserBufferSize	 = m_nMaxFrameSize * 4;
				m_pParserBuffer = (byte *) _aligned_malloc(m_nParserBufferSize,16);
			}
			else
			{
				OutputMsg("%s %d(%s):Open file failed.\n", __FILE__, __LINE__, __FUNCTION__);
				ClearOnException();
				throw std::exception("Open file failed.");
			}
		}
		
	}
	
	CIPCPlayer(HWND hWnd, int nBufferSize = 2048*1024, char *szLogFile = nullptr)
	{
		ZeroMemory(&m_csVideoCache, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_csVideoCache));
#ifdef _DEBUG
		m_pCSGlobalCount->Lock();
		m_nObjIndex = m_nGloabalCount++;
		m_pCSGlobalCount->Unlock();
		m_nLifeTime = timeGetTime();

		OutputMsg("%s \tObject:%d m_nLifeTime = %d.\n", __FUNCTION__, m_nObjIndex, m_nLifeTime);
#endif 
		m_nSDKVersion = IPC_IPC_SDK_VERSION_2015_12_16;
		if (szLogFile)
		{
			strcpy(m_szLogFileName, szLogFile);
			m_pRunlog = make_shared<CRunlogA>(szLogFile);
		}
		m_hEvnetYUVReady = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		m_hEventDecodeStart = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		m_hEventYUVRequire = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		m_hEventFrameCopied = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		InitializeCriticalSection(&m_csVideoCache);
		InitializeCriticalSection(&m_csAudioCache);
		InitializeCriticalSection(&m_csSeekOffset);
		InitializeCriticalSection(&m_csParser);
		InitializeCriticalSection(&m_csBorderRect);
		InitializeCriticalSection(&m_csListYUV);
		InitializeCriticalSection(&m_csListRenderUnit);
		m_csDsoundEnum->Lock();
		if (!m_pDsoundEnum)
			m_pDsoundEnum = make_shared<CDSoundEnum>();	///< 音频设备枚举器
		m_csDsoundEnum->Unlock();
		m_nAudioPlayFPS = 50;
		m_nSampleFreq = 8000;
		m_nSampleBit = 16;
		m_nProbeStreamTimeout = 10000;	// 毫秒
		m_nMaxYUVCache = 10;
#ifdef _DEBUG
		OutputMsg("%s Alloc a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
		nSize = sizeof(CIPCPlayer);
		m_nMaxFrameSize = 1024 * 256;
		m_nVideoFPS = 25;				// FPS的默认值为25
		m_fPlayRate = 1;
		m_fPlayInterval = 40.0f;
		//m_nVideoCodec	 = CODEC_H264;		// 视频默认使用H.264编码
		m_nVideoCodec = CODEC_UNKNOWN;
		m_nAudioCodec = CODEC_G711U;		// 音频默认使用G711U编码
		//#ifdef _DEBUG
		//		m_nMaxFrameCache = 200;				// 默认最大视频缓冲数量为200
		// #else
		m_nMaxFrameCache = 100;				// 默认最大视频缓冲数量为100
		m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
		m_hRenderWnd = hWnd;
		// #endif
		
	}
	~CIPCPlayer()
	{
#ifdef _DEBUG
		OutputMsg("%s \tReady to Free a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
		StopPlay(m_bEnableD3DCache);
		/*
		if (m_hWnd)
		{
			if (m_bRefreshWnd)
			{
				PAINTSTRUCT ps;
				HDC hdc;
				hdc = ::BeginPaint(m_hWnd, &ps);
				HBRUSH hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
				::SetBkColor(hdc, RGB(0, 0, 0));
				RECT rtWnd;
				GetWindowRect(m_hWnd, &rtWnd);
				::ScreenToClient(m_hWnd, (LPPOINT)&rtWnd);
				::ScreenToClient(m_hWnd, ((LPPOINT)&rtWnd) + 1);
				if (GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)
					swap(rtWnd.left, rtWnd.right);

				::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rtWnd, NULL, 0, NULL);
				::EndPaint(m_hWnd, &ps);
			}
		}
		*/
		if (m_pParserBuffer)
		{
			//delete[]m_pParserBuffer;
			_aligned_free(m_pParserBuffer);
#ifdef _DEBUG
			m_pParserBuffer = nullptr;
#endif
		}
		if (m_pDsBuffer)
		{
			m_pDsPlayer->DestroyDsoundBuffer(m_pDsBuffer);
#ifdef _DEBUG
			m_pDsBuffer = nullptr;
#endif
		}
		if (m_pDecodec)
			m_pDecodec.reset();
		if (m_pDxSurface)
		{
// 			if (m_pDxSurface->IsInited() && m_bEnableD3DCache)
// 			{
// 				PutDxCache(m_pDxSurface);
// 				OutputMsg("%s nVideoWidth = %d\tnVideoHeight = %d\n",__FUNCTION__, m_nVideoWidth, m_nVideoHeight);
// 			}
// 			else
// 			{
				delete m_pDxSurface;
				m_pDxSurface = nullptr;
			//}
		}
		if (m_pDDraw)
		{
			delete m_pDDraw;
		}
// 		if (m_hCacheFulled)
// 		{
// 			CloseHandle(m_hCacheFulled);
// #ifdef _DEBUG
// 			m_hCacheFulled = nullptr;
// #endif		
// 		}
		
		if (m_pRocateImage)
		{
			_aligned_free(m_pRocateImage);
			m_pRocateImage = nullptr;
		}
		if (m_pRacoateFrame)
		{
			av_free(m_pRacoateFrame);
			m_pRacoateFrame = nullptr;
		}
		m_listVideoCache.clear();
		if (m_pszFileName)
			delete[]m_pszFileName;
		if (m_hDvoFile)
			CloseHandle(m_hDvoFile);

		if (m_hEvnetYUVReady)
			CloseHandle(m_hEvnetYUVReady);
		if (m_hEventDecodeStart)
			CloseHandle(m_hEventDecodeStart);

 		if (m_hEventYUVRequire)
 			CloseHandle(m_hEventYUVRequire);
		if (m_hEventFrameCopied)
			CloseHandle(m_hEventFrameCopied);

// 		if (m_hThreadGetFileSummary)
// 		{
// 			m_bThreadSummaryRun = false;		// 令索引线程立即退出
// 			WaitForSingleObject(m_hThreadGetFileSummary, INFINITE);
// 			CloseHandle(m_hThreadGetFileSummary);
// 		}
		DeleteCriticalSection(&m_csVideoCache);
		DeleteCriticalSection(&m_csAudioCache);
		DeleteCriticalSection(&m_csSeekOffset);
		DeleteCriticalSection(&m_csParser);
		DeleteCriticalSection(&m_csBorderRect);
		DeleteCriticalSection(&m_csListYUV);
		DeleteCriticalSection(&m_csListRenderUnit);

#ifdef _DEBUG
		OutputMsg("%s \tFinish Free a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
		OutputMsg("%s \tObject:%d Exist Time = %u(ms).\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
	}
	/// @brief  是否为文件播放
	/// @retval			true	文件播放
	/// @retval			false	流播放
	bool IsFilePlayer()
	{
		if (m_hDvoFile || m_pszFileName)
			return true;
		else
			return false;
	}
	// 
	int AddRenderWnd(HWND hRenderWnd/*,RECT rtRender*/)
	{
// 		if (!hRenderWnd)
// 			return IPC_Error_InvalidParameters;
// 		Autolock(&m_csListRenderUnit);
// 		if (m_listRenderUnit.size() >= (_Max_RenderWnds -  1))
// 			return IPC_Error_InsufficentMemory;
// 		lock.Unlock();
// 		CDirectDrawPtr pDDrawPtr = make_shared<CDirectDraw>();
// 		RenderUnitPtr pRenderUnitPtr = make_shared<RenderUnit>(hRenderWnd, m_nVideoWidth, m_nVideoHeight, m_bEnableHaccel);
// 		lock.Lock();
// 		m_listRenderUnit.push_back(pRenderUnitPtr);
		return IPC_Succeed;
	}

	int RemoveRenderWnd(HWND hRenderWnd)
	{
// 		if (!hRenderWnd)
// 			return IPC_Error_InvalidParameters;
// 		Autolock(&m_csListRenderUnit);
// 		if (m_listRenderUnit.size() < 1)
// 			return IPC_Succeed;
// 		bool bFound = false;
// 		auto it = m_listRenderUnit.begin();
// 		for (; it != m_listRenderUnit.end(); it++)
// 			if ((*it)->hRenderWnd == hRenderWnd)
// 			{
// 				bFound = true;
// 				break;
// 			}
// 		if (bFound)
// 		{
// 			auto pRenderUnit = (*it);
// 			m_listRenderUnit.erase(it);
// 			lock.Unlock();
// 		}

		return IPC_Succeed;
	}
	void SetRefresh(bool bRefresh = true)
	{
		m_bRefreshWnd = bRefresh;
	}

	// 设置流播放头
	// 在流播放时，播放之前，必须先设置流头
	int SetStreamHeader(CHAR *szStreamHeader, int nHeaderSize)
	{
		if (nHeaderSize != sizeof(IPC_MEDIAINFO))
			return IPC_Error_InvalidParameters;
		IPC_MEDIAINFO *pMediaHeader = (IPC_MEDIAINFO *)szStreamHeader;
		if (pMediaHeader->nMediaTag != IPC_TAG)
			return IPC_Error_InvalidParameters;
		m_pMediaHeader = make_shared<IPC_MEDIAINFO>();
		if (m_pMediaHeader)
		{
			memcpy_s(m_pMediaHeader.get(), sizeof(IPC_MEDIAINFO), szStreamHeader, sizeof(IPC_MEDIAINFO));
			m_nSDKVersion = m_pMediaHeader->nSDKversion;
			switch (m_nSDKVersion)
			{
			case IPC_IPC_SDK_VERSION_2015_09_07:
			case IPC_IPC_SDK_VERSION_2015_10_20:
			case IPC_IPC_SDK_GSJ_HEADER:
			{
				m_nVideoFPS = 25;
				m_nVideoCodec = CODEC_UNKNOWN;
				m_nVideoWidth = 0;
				m_nVideoHeight = 0;
				break;
			}
			case IPC_IPC_SDK_VERSION_2015_12_16:
			{
				m_nVideoCodec = m_pMediaHeader->nVideoCodec;
				m_nAudioCodec = m_pMediaHeader->nAudioCodec;
				if (m_nVideoCodec == CODEC_UNKNOWN)
				{
					m_nVideoWidth = 0;
					m_nVideoHeight = 0;
				}
				else
				{
					m_nVideoWidth = m_pMediaHeader->nVideoWidth;
					m_nVideoHeight = m_pMediaHeader->nVideoHeight;
					if (!m_nVideoWidth || !m_nVideoHeight)
						//return IPC_Error_MediaFileHeaderError;
						m_nVideoCodec = CODEC_UNKNOWN;
				}
				if (m_pMediaHeader->nFps == 0)
					m_nVideoFPS = 25;
				else
					m_nVideoFPS = m_pMediaHeader->nFps;
				m_nPlayFrameInterval = 1000/m_nVideoFPS;
			}
				break;
			default:
				return IPC_Error_InvalidSDKVersion;
			}
			m_nFileFrameInterval = 1000 / m_nVideoFPS;
			return IPC_Succeed;
		}
		else
			return IPC_Error_InsufficentMemory;
	}
	inline bool GetD3DCache()
	{
		return m_bEnableD3DCache;
	}
	int SetMaxFrameSize(int nMaxFrameSize = 256*1024)
	{
		if (m_hThreadParser || m_hThreadDecode)
			return IPC_Error_PlayerHasStart;
		m_nMaxFrameSize = nMaxFrameSize;
		return IPC_Succeed;
	}

	inline int GetMaxFrameSize()
	{
		return m_nMaxFrameSize;
	}

	inline void SetMaxFrameCache(int nMaxFrameCache = 100)
	{
		m_nMaxFrameCache = nMaxFrameCache;
	}

	void ClearFrameCache()
	{
		_MyEnterCriticalSection(&m_csVideoCache);
		m_listVideoCache.clear();
		_MyLeaveCriticalSection(&m_csVideoCache);

		_MyEnterCriticalSection(&m_csAudioCache);
		m_listAudioCache.clear();
		_MyLeaveCriticalSection(&m_csAudioCache);
	}

	/// @brief 设置显示边界,边界外的图像将不予以显示
	/// @param rtBorder	边界参数 详见以下图表
	/// left	左边界距离
	/// top		上边界距离
	/// right	右边界距离
	/// bottom  下边界距离 
	///┌───────────────────────────────────┐
	///│                  │                   │
	///│                 top                  │
	///│                  │                   │─────── the source rect
	///│       ┌───────────────────┐        │
	///│       │                     │        │
	///│       │                     │        │
	///│─left─ │  the clipped rect   │─right─ │
	///│       │                     │        │
	///│       │                     │        │
	///│       └───────────────────┘        │
	///│                  │                   │
	///│                bottom                │
	///│                  │                   │
	///└───────────────────────────────────┘
	/// @remark 边界的上下左右位置不可颠倒
	void SetBorderRect(RECT rtBorder)
	{
		RECT rtVideo = {0};
		rtVideo.left = rtBorder.left;
		rtVideo.right = m_nVideoWidth - rtBorder.right;
		rtVideo.top += rtBorder.top;
		rtVideo.bottom = m_nVideoHeight - rtBorder.bottom;
		Autolock(&m_csBorderRect);
		if (!m_pBorderRect)
			m_pBorderRect = make_shared<RECT>();

		CopyRect(m_pBorderRect.get(), &rtVideo);
	}

	void RemoveBorderRect()
	{
		Autolock(&m_csBorderRect);
		if (m_pBorderRect)
			m_pBorderRect = nullptr;
	}

	int EnableAsyncRender(bool bAsyncRender = true)
	{
		if (m_hThreadDecode)
			return IPC_Error_PlayerHasStart;
		m_bEnableAsyncRender = bAsyncRender;
		return IPC_Succeed;
	}
		
	/// @brief			开始播放
	/// @param [in]		bEnaleAudio		是否开启音频播放
	/// #- true			开启音频播放
	/// #- false		关闭音频播放
	/// @param [in]		bEnableHaccel	是否开启硬解码功能
	/// #- true			启用硬解码
	/// #- false		禁用硬解码
	/// @param [in]		bFitWindow		视频是否适应窗口
	/// #- true			视频填满窗口,这样会把图像拉伸,可能会造成图像变形
	/// #- false		只按图像原始比例在窗口中显示,超出比例部分,则以黑色背景显示
	/// @retval			0	操作成功
	/// @retval			1	流缓冲区已满
	/// @retval			-1	输入参数无效
	/// @remark			播放流数据时，相应的帧数据其实并未立即播放，而是被放了播放队列中，应该根据ipcplay_PlayStream
	///					的返回值来判断，是否继续播放，若说明队列已满，则应该暂停播放
	int StartPlay(bool bEnaleAudio = false,bool bEnableHaccel = false,bool bFitWindow = true)
	{
#ifdef _DEBUG
		OutputMsg("%s \tObject:%d Time = %d.\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
		m_bPause = false;
		m_bFitWindow = bFitWindow;	
		if (GetOsMajorVersion() >= 6)
			m_bEnableHaccel = bEnableHaccel;
		
// 		if (!m_hWnd || !IsWindow(m_hWnd))
// 		{
// 			return IPC_Error_InvalidWindow;
// 		}
		if (m_pszFileName )
		{
			if (m_hDvoFile == INVALID_HANDLE_VALUE)
			{
				return GetLastError();
			}
			
			if (!m_pMediaHeader ||	// 文件头无效
				!m_nTotalFrames )	// 无法取得视频总帧数
				return IPC_Error_NotVideoFile;
			// 启动文件解析线程
			m_bThreadParserRun = true;
			m_hThreadParser = (HANDLE)_beginthreadex(nullptr, 0, ThreadParser, this, 0, 0);

		}
		else
		{
//  		if (!m_pMediaHeader)
//  			return IPC_Error_NotInputStreamHeader;		// 未设置流播放头
			m_listVideoCache.clear();
			m_listAudioCache.clear();
		}
		m_bStopFlag = false;
		// 启动流播放线程
		m_bThreadDecodeRun = true;
		m_bThreadRenderRun = true;		
		if (m_bEnableAsyncRender && m_hRenderWnd)
		{
			m_hThreadRender = (HANDLE)_beginthreadex(nullptr, 0, ThreadRender, this, CREATE_SUSPENDED, 0);
			if (!m_hThreadRender)
			{
#ifdef _DEBUG
				OutputMsg("%s \tObject:%d ThreadRender Start failed:%d.\n", __FUNCTION__, m_nObjIndex, GetLastError());
#endif
				return IPC_Error_VideoThreadStartFailed;
			}
		}
			
		m_hThreadDecode = (HANDLE)_beginthreadex(nullptr, 0, ThreadDecode, this, 0, 0);
		if (!m_hThreadDecode)
		{
#ifdef _DEBUG
			OutputMsg("%s \tObject:%d ThreadPlayVideo Start failed:%d.\n", __FUNCTION__, m_nObjIndex, GetLastError());
#endif
			return IPC_Error_VideoThreadStartFailed;
		}
		if (m_hRenderWnd)
			EnableAudio(bEnaleAudio);
		else
			EnableAudio(false);
		m_dwStartTime = timeGetTime();
		return IPC_Succeed;
	}

	/// @brief			判断播放器是否正在播放中	
	/// @retval			true	播放器正在播放中
	/// @retval			false	插放器已停止播放
	bool IsPlaying()
	{
		if (m_hThreadDecode)
			return true;
		else
			return false;
	}

	/// @brief 复位播放窗口
	bool Reset(HWND hWnd = nullptr, int nWidth = 0, int nHeight = 0)
	{
// 		CAutoLock lock(&m_csDxSurface, false, __FILE__, __FUNCTION__, __LINE__);
// 		if (m_pDxSurface)
// 		{
// 			m_bDxReset = true;
// 			m_hDxReset = hWnd;
// 			return true;
// 		}
// 		else
// 			return false;
		return true;
	}

	int SetPixelFormat(PIXELFMORMAT nPixelFmt = YV12)
	{
		switch (nPixelFmt)
		{
		case YV12:
			m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
			break;
		case NV12:
			m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
			break;
		case R8G8B8:
			m_nPixelFormat = D3DFMT_A8R8G8B8;
			break;
		default:
			assert(false);
			return IPC_Error_InvalidParameters;
			break;
		}
		return IPC_Succeed;
	}

	int GetFileHeader()
	{
		if (!m_hDvoFile)
			return IPC_Error_FileNotOpened;
		DWORD dwBytesRead = 0;
		m_pMediaHeader = make_shared<IPC_MEDIAINFO>();
		if (!m_pMediaHeader)
		{
			CloseHandle(m_hDvoFile);
			return -1;
		}

		if (SetFilePointer(m_hDvoFile, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			assert(false);
			return 0;
		}

		if (!ReadFile(m_hDvoFile, (void *)m_pMediaHeader.get(), sizeof(IPC_MEDIAINFO), &dwBytesRead, nullptr))
		{
			CloseHandle(m_hDvoFile);
			return GetLastError();
		}
		// 分析视频文件头
		if ((m_pMediaHeader->nMediaTag != IPC_TAG && 
			 m_pMediaHeader->nMediaTag != GSJ_TAG) ||
			dwBytesRead != sizeof(IPC_MEDIAINFO))
		{
			CloseHandle(m_hDvoFile);
			return IPC_Error_NotVideoFile;
		}
		m_nSDKVersion = m_pMediaHeader->nSDKversion;
		switch (m_nSDKVersion)
		{

		case IPC_IPC_SDK_VERSION_2015_09_07:
		case IPC_IPC_SDK_VERSION_2015_10_20:
		case IPC_IPC_SDK_GSJ_HEADER:
		{
			m_nVideoFPS = 25;
			m_nVideoCodec = CODEC_UNKNOWN;
			m_nVideoWidth = 0;
			m_nVideoHeight = 0;
		}
			break;
		case IPC_IPC_SDK_VERSION_2015_12_16:
		{
			m_nVideoCodec = m_pMediaHeader->nVideoCodec;
			m_nAudioCodec = m_pMediaHeader->nAudioCodec;
			if (m_nVideoCodec == CODEC_UNKNOWN)
			{
				m_nVideoWidth = 0;
				m_nVideoHeight = 0;
			}
			else
			{
				m_nVideoWidth = m_pMediaHeader->nVideoWidth;
				m_nVideoHeight = m_pMediaHeader->nVideoHeight;
				if (!m_nVideoWidth || !m_nVideoHeight)
					//return IPC_Error_MediaFileHeaderError;
					m_nVideoCodec = CODEC_UNKNOWN;
			}
			if (m_pMediaHeader->nFps == 0)
				m_nVideoFPS = 25;
			else
				m_nVideoFPS = m_pMediaHeader->nFps;
		}
		break;
		default:
		
			return IPC_Error_InvalidSDKVersion;
		}
		m_nFileFrameInterval = 1000 / m_nVideoFPS;
		
		return IPC_Succeed;
	}
	
	void FitWindow(bool bFitWindow)
	{
		m_bFitWindow = bFitWindow;
	}

	/// @brief			输入IPC私格式的码流数据
	/// @retval			0	操作成功
	/// @retval			1	流缓冲区已满
	/// @retval			-1	输入参数无效
	/// @remark			播放流数据时，相应的帧数据其实并未立即播放，而是被放了播放队列中，应该根据ipcplay_PlayStream
	///					的返回值来判断，是否继续播放，若说明队列已满，则应该暂停输入
	int InputStream(unsigned char *szFrameData, int nFrameSize, UINT nCacheSize = 0, bool bThreadInside = false/*是否内部线程调用标志*/)
	{
		if (!szFrameData || nFrameSize < sizeof(IPCFrameHeader))
			return IPC_Error_InvalidFrame;
		m_bIpcStream = false;
		int nMaxCacheSize = m_nMaxFrameCache;
		if (nCacheSize != 0)
			nMaxCacheSize = nCacheSize;
		if (m_bStopFlag)
			return IPC_Error_PlayerHasStop;
		if (!bThreadInside)
		{// 若不是内部线程，则需要检查视频和音频解码是否已经运行
			if (!m_bThreadDecodeRun || !m_hThreadDecode)
			{
#ifdef _DEBUG
//				OutputMsg("%s \tObject:%d Video decode thread not run.\n", __FUNCTION__, m_nObjIndex);
#endif
				return IPC_Error_VideoThreadNotRun;
			}
		}

		m_bIpcStream = false;		// 非IPC码流
		IPCFrameHeader *pHeaderEx = (IPCFrameHeader *)szFrameData;
		if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
		{
			switch (pHeaderEx->nType)
			{
				case FRAME_P:
				case FRAME_B:
				case 0:
				case FRAME_I:
				case FRAME_IDR:
				{
	// 				if (!m_hThreadPlayVideo)
	// 					return IPC_Error_VideoThreadNotRun;
					CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
					if (m_listVideoCache.size() >= nMaxCacheSize)
						return IPC_Error_FrameCacheIsFulled;
					StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize,m_nFileFrameInterval,m_nSDKVersion);
					if (!pStream)
						return IPC_Error_InsufficentMemory;
					m_listVideoCache.push_back(pStream);
					break;
				}
				case FRAME_G711A:      // G711 A律编码帧
				case FRAME_G711U:      // G711 U律编码帧
				case FRAME_G726:       // G726编码帧
				case FRAME_AAC:        // AAC编码帧。
				{
	// 				if (!m_hThreadPlayVideo)
	// 					return IPC_Error_AudioThreadNotRun;
					
					if (m_fPlayRate != 1.0f)
						break;
					CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
					if (m_listAudioCache.size() >= nMaxCacheSize * 2)
					{
						if (m_bEnableAudio)
							return IPC_Error_FrameCacheIsFulled;
						else
							m_listAudioCache.pop_front();
					}
					StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval/2);
					if (!pStream)
						return IPC_Error_InsufficentMemory;
					m_listAudioCache.push_back(pStream);
					m_nAudioFrames++;
					break;
				}
				default:
				{
					assert(false);
					return IPC_Error_InvalidFrameType;
					break;
				}
			}
		}
		else
		{
			switch (pHeaderEx->nType)
			{
			case 0:				// 视频BP帧
			case 1:				// 视频I帧
			{
				CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (m_listVideoCache.size() >= nMaxCacheSize)
					return IPC_Error_FrameCacheIsFulled;
				StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval, m_nSDKVersion);
				if (!pStream)
					return IPC_Error_InsufficentMemory;
				m_listVideoCache.push_back(pStream);
				break;
			}
			
			case 2:				// 音频帧
			case FRAME_G711U:
			//case FRAME_G726:    // G726编码帧
			{
				if (!m_bEnableAudio)
					break;
				if (m_fPlayRate != 1.0f)
					break;
				CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (m_listAudioCache.size() >= nMaxCacheSize*2)
					return IPC_Error_FrameCacheIsFulled;
				Frame(szFrameData)->nType = CODEC_G711U;			// 旧版SDK只支持G711U解码，所以这里强制转换为G711U，以正确解码
				StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval / 2);
				if (!pStream)
					return IPC_Error_InsufficentMemory;
				m_listAudioCache.push_back(pStream);
				m_nAudioFrames++;
				break;
			}
			default:
			{
				//assert(false);
				return IPC_Error_InvalidFrameType;
				break;
			}
			}
		}

		return IPC_Succeed;
	}

	/// @brief			输入IPC IPC码流数据
	/// @retval			0	操作成功
	/// @retval			1	流缓冲区已满
	/// @retval			-1	输入参数无效
	/// @remark			播放流数据时，相应的帧数据其实并未立即播放，而是被放了播放队列中，应该根据ipcplay_PlayStream
	///					的返回值来判断，是否继续播放，若说明队列已满，则应该暂停播放
// 	UINT m_nAudioFrames1 = 0;
// 	UINT m_nVideoFraems = 0;
// 	DWORD m_dwInputStream = timeGetTime();
	int InputStream(IN byte *pFrameData, IN int nFrameType, IN int nFrameLength, int nFrameNum, time_t nFrameTime)
	{
//#ifdef _DEBUG
		if (m_OuputTime.nInputStream == 0 ||
			(timeGetTime() - m_OuputTime.nInputStream) >= 10000)
		{
			OutputMsg("%s \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
			m_OuputTime.nInputStream = timeGetTime();
		}
//#endif
		if (m_bStopFlag)
			return IPC_Error_PlayerHasStop;

		if (!m_bThreadDecodeRun ||!m_hThreadDecode)
		{
			CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (m_listVideoCache.size() >= 25)
			{
				OutputMsg("%s \tObject:%d Video decode thread not run.\n", __FUNCTION__, m_nObjIndex);
				return IPC_Error_VideoThreadNotRun;
			}		
		}
		DWORD dwThreadCode = 0;
		GetExitCodeThread(m_hThreadDecode, &dwThreadCode);
		if (dwThreadCode != STILL_ACTIVE)		// 线程已退出
		{
			TraceMsgA("%s ThreadDecode has exit Abnormally.\n", __FUNCTION__);
			return IPC_Error_VideoThreadAbnormalExit;
		}
		m_bIpcStream = true;
		switch (nFrameType)
		{
			case 0:									// 海思I帧号为0，这是固件的一个BUG，有待修正
			
			case IPC_IDR_FRAME: 	// IDR帧。
			case IPC_I_FRAME:		// I帧。		
			case IPC_P_FRAME:       // P帧。
			case IPC_B_FRAME:       // B帧。
			case IPC_GOV_FRAME: 	// GOV帧。
			{
				//m_nVideoFraems++;
				StreamFramePtr pStream = make_shared<StreamFrame>(pFrameData, nFrameType, nFrameLength, nFrameNum, nFrameTime);
				CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (m_listVideoCache.size() >= m_nMaxFrameCache)
				{
// #ifdef _DEBUG
// 					OutputMsg("%s \tObject:%d Video Frame cache is Fulled.\n", __FUNCTION__, m_nObjIndex);
// #endif
					return IPC_Error_FrameCacheIsFulled;
				}	
				if (!pStream)
				{
// #ifdef _DEBUG
// 					OutputMsg("%s \tObject:%d InsufficentMemory when alloc memory for video frame.\n", __FUNCTION__, m_nObjIndex);
// #endif
					return IPC_Error_InsufficentMemory;
				}
				m_listVideoCache.push_back(pStream);
			}
				break;
			case IPC_711_ALAW:      // 711 A律编码帧
			case IPC_711_ULAW:      // 711 U律编码帧
			case IPC_726:           // 726编码帧
			case IPC_AAC:           // AAC编码帧。
			{
				m_nAudioCodec = (IPC_CODEC)nFrameType;
// 				if ((timeGetTime() - m_dwInputStream) >= 20000)
// 				{
// 					TraceMsgA("%s VideoFrames = %d\tAudioFrames = %d.\n", __FUNCTION__, m_nVideoFraems, m_nAudioFrames1);
// 					m_dwInputStream = timeGetTime();
// 				}
				if (!m_bEnableAudio)
					break;
				StreamFramePtr pStream = make_shared<StreamFrame>(pFrameData, nFrameType, nFrameLength, nFrameNum, nFrameTime);
				if (!pStream)
					return IPC_Error_InsufficentMemory;
				CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
				m_nAudioFrames++;
				m_listAudioCache.push_back(pStream);
			}
				break;
			default:
			{
				assert(false);
				break;
			}
		}
		return 0;
	}

// 	void SuspendDecode()
// 	{
// 		if (m_hThreadDecode)
// 			SuspendThread(m_hThreadDecode);
// 	}
// 	void ResumeDecode()
// 	{
// 		if (m_hThreadDecode)
// 			ResumeThread(m_hThreadDecode);
// 	}
	
	bool StopPlay(bool bAsync = false,DWORD nTimeout = 200)
	{
#ifdef _DEBUG
		TraceFunction();
		OutputMsg("%s \tObject:%d Time = %d.\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
// 		if (!m_bIpcStream)			// 对于文件码流，不使用异步关闭的方式
// 			nTimeout = INFINITE;
		m_bStopFlag = true;
		m_bThreadParserRun = false;
		m_bThreadDecodeRun = false;
		m_bThreadPlayAudioRun = false;
		m_bThreadRenderRun = false;
		//m_bThreadSummaryRun = false;
		HANDLE hArray[16] = { 0 };
		int nHandles = 0;
		m_bEnableD3DCache = bAsync;

		m_bPause = false;
		DWORD dwThreadExitCode = 0;
		if (m_hThreadParser)
		{
			GetExitCodeThread(m_hThreadParser, &dwThreadExitCode);
			if (dwThreadExitCode == STILL_ACTIVE)		// 线程仍在运行
				hArray[nHandles++] = m_hThreadParser;
		}
		if (m_hThreadDecode)
		{
			GetExitCodeThread(m_hThreadDecode, &dwThreadExitCode);
			if (dwThreadExitCode == STILL_ACTIVE)		// 线程仍在运行
				hArray[nHandles++] = m_hThreadDecode;
		}
		if (m_hThreadRender)
		{
			ResumeThread(m_hThreadRender);				// 异步渲染线程可能仍未唤醒
			GetExitCodeThread(m_hThreadRender, &dwThreadExitCode);
			if (dwThreadExitCode == STILL_ACTIVE)		// 线程仍在运行
				hArray[nHandles++] = m_hThreadRender;
		}
		if (m_hThreadPlayAudio)
		{
			ResumeThread(m_hThreadPlayAudio);
			GetExitCodeThread(m_hThreadPlayAudio, &dwThreadExitCode);
			if (dwThreadExitCode == STILL_ACTIVE)		// 线程仍在运行
				hArray[nHandles++] = m_hThreadPlayAudio;
			
		}
// 		if (m_hThreadGetFileSummary)
// 			hArray[nHandles++] = m_hThreadGetFileSummary;
		EnterCriticalSection(&m_csAudioCache);
		m_listAudioCache.clear();
		LeaveCriticalSection(&m_csAudioCache);

		EnterCriticalSection(&m_csVideoCache);
		m_listVideoCache.clear();
		LeaveCriticalSection(&m_csVideoCache);

		if (nHandles > 0)
		{
			
			if (WaitForMultipleObjects(nHandles, hArray, true, nTimeout) == WAIT_TIMEOUT)
			{
				OutputMsg("%s Wait for thread exit timeout.\n", __FUNCTION__);
				return false;
			}
		}
		if (m_hThreadParser)
		{
			CloseHandle(m_hThreadParser);
			m_hThreadParser = nullptr;
			OutputMsg("%s ThreadParser has exit.\n", __FUNCTION__);
		}
		if (m_hThreadDecode)
		{
			CloseHandle(m_hThreadDecode);
			m_hThreadDecode = nullptr;
#ifdef _DEBUG
			OutputMsg("%s ThreadPlayVideo Object:%d has exit.\n", __FUNCTION__,m_nObjIndex);
#endif
		}
		if (m_hThreadRender)
		{
			CloseHandle(m_hThreadRender);
			m_hThreadRender = nullptr;
		}
		if (m_hThreadPlayAudio)
		{
			CloseHandle(m_hThreadPlayAudio);
			m_hThreadPlayAudio = nullptr;
			OutputMsg("%s ThreadPlayAudio has exit.\n", __FUNCTION__);
		}
// 		if (m_hRenderFrameReady)
// 		{
// 			CloseHandle(m_hRenderFrameReady);
// 			m_hRenderFrameReady = nullptr;
// 		}
// 		if (m_hRenderFrameCopied)
// 		{
// 			CloseHandle(m_hRenderFrameCopied);
// 			m_hRenderFrameCopied = nullptr;
// 		}
		EnableAudio(false);
		if (m_pFrameOffsetTable)
		{
			delete[]m_pFrameOffsetTable;
			m_pFrameOffsetTable = nullptr;
		}

#ifdef _DEBUG
		m_hThreadDecode = nullptr;		
		m_hThreadParser = nullptr;
//		m_hThreadGetFileSummary = nullptr;
		m_hThreadPlayAudio = nullptr;
		m_pFrameOffsetTable = nullptr;
#endif
		return true;
	}

	void Pause()
	{
		m_bPause = !m_bPause;
	}

	/// @brief			设置探测码流类型时，等待码流的超时值
	/// @param [in]		dwTimeout 等待码流的赶时值，单位毫秒
	/// @remark			该函数必须要在Start之前调用，才能生效
	void SetProbeStreamTimeout(DWORD dwTimeout = 3000)
	{
		m_nProbeStreamTimeout = dwTimeout;
	}

	/// @brief			开启硬解码功能
	/// @param [in]		bEnableHaccel	是否开启硬解码功能
	/// #- true			开启硬解码功能
	/// #- false		关闭硬解码功能
	/// @remark			开启和关闭硬解码功能必须要Start之前调用才能生效
	int  EnableHaccel(IN bool bEnableHaccel = false)
	{
		if (m_hThreadDecode)
			return IPC_Error_PlayerHasStart;
		else
		{
			if (bEnableHaccel)
			{
				if (GetOsMajorVersion() >= 6)
				{
					m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
					m_bEnableHaccel = bEnableHaccel;
				}
				else
					return IPC_Error_UnsupportHaccel;
			}
			else
				m_bEnableHaccel = bEnableHaccel;
			return IPC_Succeed;
		}
	}

	/// @brief			获取硬解码状态
	/// @retval			true	已开启硬解码功能
	/// @retval			flase	未开启硬解码功能
	inline bool  GetHaccelStatus()
	{
		return m_bEnableHaccel;
	}

	/// @brief			检查是否支持特定视频编码的硬解码
	/// @param [in]		nCodec		视频编码格式,@see IPC_CODEC
	/// @retval			true		支持指定视频编码的硬解码
	/// @retval			false		不支持指定视频编码的硬解码
	static bool  IsSupportHaccel(IN IPC_CODEC nCodec)
	{	
		enum AVCodecID nAvCodec = AV_CODEC_ID_NONE;
		switch (nCodec)
		{		
		case CODEC_H264:
			nAvCodec = AV_CODEC_ID_H264;
			break;
		case CODEC_H265:
			nAvCodec = AV_CODEC_ID_H264;
			break;		
		default:
			return false;
		}
		shared_ptr<CVideoDecoder>pDecoder = make_shared<CVideoDecoder>();
		UINT nAdapter = D3DADAPTER_DEFAULT;
		if (!pDecoder->InitD3D(nAdapter))
			return false;
		if (pDecoder->CodecIsSupported(nAvCodec) == S_OK)		
			return true;
		else
			return false;
	}

	/// @brief			取得当前播放视频的即时信息
	int GetPlayerInfo(PlayerInfo *pPlayInfo)
	{
		if (!pPlayInfo)
			return IPC_Error_InvalidParameters;
		if (m_hThreadDecode|| m_hDvoFile)
		{
			ZeroMemory(pPlayInfo, sizeof(PlayerInfo));
			pPlayInfo->nVideoCodec	 = m_nVideoCodec;
			pPlayInfo->nVideoWidth	 = m_nVideoWidth;
			pPlayInfo->nVideoHeight	 = m_nVideoHeight;
			pPlayInfo->nAudioCodec	 = m_nAudioCodec;
			pPlayInfo->bAudioEnabled = m_bEnableAudio;
			pPlayInfo->tTimeEplased	 = (time_t)(1000 * (GetExactTime() - m_dfTimesStart));
			pPlayInfo->nFPS			 = m_nVideoFPS;
			pPlayInfo->nPlayFPS		 = m_nPlayFPS;
			pPlayInfo->nCacheSize	 = m_nVideoCache;
			pPlayInfo->nCacheSize2	 = m_nAudioCache;
			if (!m_bIpcStream)
			{
				pPlayInfo->bFilePlayFinished = m_bFilePlayFinished;
				pPlayInfo->nCurFrameID = m_nCurVideoFrame;
				pPlayInfo->nTotalFrames = m_nTotalFrames;
				if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
				{
					pPlayInfo->tTotalTime = m_nTotalFrames*1000/m_nVideoFPS;
					pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_nFirstFrameTime)/1000;
				}
				else
				{
					pPlayInfo->tTotalTime = m_nTotalTime;
					if (m_pMediaHeader->nCameraType == 1)	// 安讯士相机
						pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_FirstFrame.nTimestamp) / (1000 * 1000);
					else
					{
						pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_FirstFrame.nTimestamp) / 1000;
						pPlayInfo->nCurFrameID = pPlayInfo->tCurFrameTime*m_nVideoFPS/1000;
					}
				}
			}
			else
				pPlayInfo->tTotalTime = 0;
			
			return IPC_Succeed;
		}
		else
			return IPC_Error_PlayerNotStart;
	}
// 	static void CloseShareMemory(HANDLE &hMemHandle, void *pMemory)
// 	{
// 		if (!pMemory || !hMemHandle)
// 			return;
// 		if (pMemory)
// 		{
// 			UnmapViewOfFile(pMemory);
// 			pMemory = NULL;
// 		}
// 		if (hMemHandle)
// 		{
// 			CloseHandle(hMemHandle);
// 			hMemHandle = NULL;
// 		}
// 	}
// 
// 	static void *OpenShareMemory(HANDLE &hMemHandle, TCHAR *szUniqueName)
// 	{
// 		void *pShareSection = NULL;
// 		bool bResult = false;
// 		__try
// 		{
// 			__try
// 			{
// 				if (!szUniqueName || _tcslen(szUniqueName) < 1)
// 					__leave;
// 				hMemHandle = OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, szUniqueName);
// 				if (hMemHandle == INVALID_HANDLE_VALUE ||
// 					hMemHandle == NULL)
// 				{
// 					_TraceMsg(_T("%s %d MapViewOfFile Failed,ErrorCode = %d.\r\n"), __FILEW__, __LINE__, GetLastError());
// 					__leave;
// 				}
// 				pShareSection = MapViewOfFile(hMemHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
// 				if (pShareSection == NULL)
// 				{
// 					_TraceMsg(_T("%s %d MapViewOfFile Failed,ErrorCode = %d.\r\n"), __FILEW__, __LINE__, GetLastError());
// 					__leave;
// 				}
// 				bResult = true;
// 				_TraceMsg(_T("Open Share memory map Succeed."));
// 			}
// 			__finally
// 			{
// 				if (!bResult)
// 				{
// 					CloseShareMemory(hMemHandle, pShareSection);
// 					pShareSection = NULL;
// 				}
// 			}
// 		}
// 		__except (1)
// 		{
// 			TraceMsg(_T("Exception in OpenShareMemory.\r\n"));
// 		}
// 		return pShareSection;
// 	}
	/// @brief			检查截图窗口是否存在
	/// @param [inout]	hSnapWnd		返回截图进程的窗口
	/// @retval			0	操作成功，否则操作失败
// 	static int CheckSnapshotWnd(HWND &hSnapWnd)
// 	{
// 		// 查找截图窗口
// 		if (hSnapWnd && IsWindow(hSnapWnd))
// 			return IPC_Succeed;
// 		hSnapWnd = FindWindow(nullptr, _T("IPC SnapShot"));
// 		if (!hSnapWnd)
// 		{// 准备创建截图进程
// 			TCHAR szAppPath[1024] = { 0 };
// 			GetAppPath(szAppPath, 1024);
// 			_tcscat(szAppPath, _T("\\IPCSnapshot.exe"));
// 			if (!PathFileExists(szAppPath))
// 			{// 截图程序文件不存在
// 				return IPC_Error_SnapShotProcessFileMissed;
// 			}
// 			//////////////////////////////////////////////////////////////////////////
// 			///必须把命令行通过_stprintf方式将命令行_SnapShotCmdLine复制到szCmdLine中才能生效
// 			///原因不明,但必须这么做，否则目标进程无法收到命令行参数
// 			//////////////////////////////////////////////////////////////////////////
// 			TCHAR szCmdLine[128] = { 0 };
// 			_stprintf(szCmdLine, _T(" %s"), _SnapShotCmdLine);
// 			STARTUPINFO si;
// 			PROCESS_INFORMATION pi;
// 			ZeroMemory(&si, sizeof(si));
// 			si.cb = sizeof(si);
// 			ZeroMemory(&pi, sizeof(pi));
// 			if (!CreateProcess(szAppPath, //module name 
// 				szCmdLine,
// 				NULL,             // Process handle not inheritable. 
// 				NULL,             // Thread handle not inheritable. 
// 				FALSE,            // Set handle inheritance to FALSE. 
// 				0,                // No creation flags. 
// 				NULL,             // Use parent's environment block. 
// 				NULL,             // Use parent's starting directory. 
// 				&si,              // Pointer to STARTUPINFO structure.
// 				&pi))             // Pointer to PROCESS_INFORMATION structure.
// 			{
// 				return IPC_Error_SnapShotProcessStartFailed;
// 			}
// 			CloseHandle(pi.hProcess);
// 			CloseHandle(pi.hThread);
// 		}
// 		int nRetry = 0;
// 		while (!hSnapWnd && nRetry < 5)
// 		{
// 			hSnapWnd = FindWindow(nullptr, _T("IPC SnapShot"));
// 			nRetry++;
// 			Sleep(100);
// 		}
// 		if (!hSnapWnd || !IsWindow(hSnapWnd))
// 			return IPC_Error_SnapShotProcessNotRun;
// 		return IPC_Succeed;
// 	}

	/// @brief			截取正放播放的视频图像
	/// @param [in]		szFileName		要保存的文件名
	/// @param [in]		nFileFormat		保存文件的编码格式,@see SNAPSHOT_FORMAT定义
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
// 	inline int  SnapShot2(IN WCHAR *szFileName, IN SNAPSHOT_FORMAT nFileFormat = XIFF_JPG)
// 	{
// 		if (!szFileName || !wcslen(szFileName))
// 			return -1;
// 		if (m_hThreadPlayVideo)
// 		{
// 			if (WaitForSingleObject(m_hEvnetYUVReady, 5000) == WAIT_TIMEOUT)
// 				return IPC_Error_PlayerNotStart;
// 			if (m_pDxSurface)
// 			{
// 				int nResult = CheckSnapshotWnd(g_hSnapShotWnd);
// 				if (nResult != IPC_Succeed)
// 					return nResult;
// 				wcscpy(m_szSnapShotFile, szFileName);
// 				TraceMsgA("%s Input FileName:%s\tOuput FileName:%s\n", __FUNCTION__, szFileName, m_szSnapShotFile);
// 				SetEvent(m_hEventYUVRequire);		// 请求YUV数据
// 				m_nD3DImageFormat = nFileFormat;
// 				// 等待截图完成
// 				if (WaitForSingleObject(m_hEventSnapShot, 5000) == WAIT_TIMEOUT)
// 					return IPC_Error_SnapShotFailed;
// 				return IPC_Succeed;
// 			}
// 			else
// 				return IPC_Error_WindowNotAssigned;
// 		}
// 		else
// 			return IPC_Error_PlayerNotStart;
// 		return IPC_Succeed;
// 	}

	/// @brief			截取正放播放的视频图像
	/// @param [in]		szFileName		要保存的文件名
	/// @param [in]		nFileFormat		保存文件的编码格式,@see SNAPSHOT_FORMAT定义
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	inline int  SnapShot(IN CHAR *szFileName, IN SNAPSHOT_FORMAT nFileFormat = XIFF_JPG)
	{
		if (!szFileName || !strlen(szFileName))
			return -1;
		if (m_hThreadDecode)
		{
			if (WaitForSingleObject(m_hEvnetYUVReady, 5000) == WAIT_TIMEOUT)
				return IPC_Error_PlayerNotStart;
			char szAvError[1024] = { 0 };
// 			if (m_pSnapshot && m_pSnapshot->GetPixelFormat() != m_nDecodePirFmt)
// 				m_pSnapshot.reset();

			if (!m_pSnapshot)
			{
				if (m_nDecodePixelFmt == AV_PIX_FMT_DXVA2_VLD)
					m_pSnapshot = make_shared<CSnapshot>(AV_PIX_FMT_YUV420P, m_nVideoWidth, m_nVideoHeight);
				else 
					m_pSnapshot = make_shared<CSnapshot>(m_nDecodePixelFmt, m_nVideoWidth, m_nVideoHeight);
				
				if (!m_pSnapshot)
					return IPC_Error_InsufficentMemory;
				if (m_pSnapshot->SetCodecID(m_nVideoCodec) != IPC_Succeed)
					return IPC_Error_UnsupportedCodec;
			}
			
			SetEvent(m_hEventYUVRequire);
			if (WaitForSingleObject(m_hEventFrameCopied, 2000) == WAIT_TIMEOUT)
				return IPC_Error_SnapShotFailed;
			int nResult = IPC_Succeed;
			switch (nFileFormat)
			{
			case XIFF_BMP:
				if (!m_pSnapshot->SaveBmp(szFileName))
					nResult = IPC_Error_SnapShotFailed;
				break;
			case XIFF_JPG:
				if (!m_pSnapshot->SaveJpeg(szFileName))
					nResult = IPC_Error_SnapShotFailed;
				break;
			default:
				nResult = IPC_Error_UnsupportedFormat;
				break;
			}
			//m_pSnapshot.reset();
			return nResult;
		}
		else
			return IPC_Error_PlayerNotStart;
		return IPC_Succeed;
	}
	/// @brief			处理截图请求
	/// remark			处理完成后，将置信m_hEventSnapShot事件
	void ProcessSnapshotRequire(AVFrame *pAvFrame)
	{
		if (!pAvFrame)
			return;
		if (WaitForSingleObject(m_hEventYUVRequire, 0) == WAIT_TIMEOUT)
			return;
		
		if (pAvFrame->format == AV_PIX_FMT_YUV420P ||
			pAvFrame->format == AV_PIX_FMT_YUVJ420P)
		{// 暂不支持dxva 硬解码帧
			m_pSnapshot->CopyFrame(pAvFrame);
			SetEvent(m_hEventFrameCopied);
		}
		else if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
		{
			m_pSnapshot->CopyDxvaFrame(pAvFrame);
			SetEvent(m_hEventFrameCopied);			
		}
		else
		{
			return;
		}
	}

	/// @brief			设置播放的音量
	/// @param [in]		nVolume			要设置的音量值，取值范围0~100，为0时，则为静音
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	void  SetVolume(IN int nVolume)
	{
		if (m_bEnableAudio && m_pDsBuffer)
		{
			//int nDsVolume = nVolume * 100 - 10000;
			m_pDsBuffer->SetVolume(nVolume);
		}
	}

	/// @brief			取得当前播放的音量
	int  GetVolume()
	{
		if (m_bEnableAudio && m_pDsBuffer)
			return (m_pDsBuffer->GetVolume() + 10000) / 100;
		else
			return 0;
	}

	/// @brief			设置当前播放的速度的倍率
	/// @param [in]		fPlayRate		当前的播放的倍率,大于1时为加速播放,小于1时为减速播放，不能为0或小于0
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	inline int  SetRate(IN float fPlayRate)
	{
#ifdef _DEBUG
		m_nRenderFPS = 0;
		dfTRender = 0.0f;
		m_nRenderFrames = 0;
#endif
		if (m_bIpcStream)
		{
			return IPC_Error_NotFilePlayer;
		}
		if (fPlayRate > (float)m_nVideoFPS)
			fPlayRate = m_nVideoFPS;
		// 取得当前显示器的刷新率，显示器的刷新率决定了显示图像的最高帧数
		// 通过统计每显示一帧图像(含解码和显示)耗费的时间
		
		DEVMODE   dm;
		dm.dmSize = sizeof(DEVMODE);
		::EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
		m_fPlayInterval = (int)(1000 / (m_nVideoFPS*fPlayRate));
/// marked by xionggao.lee @2016.03.23
// 		float nMinPlayInterval = ((float)1000) / dm.dmDisplayFrequency;
// 		if (m_fPlayInterval < nMinPlayInterval)
// 			m_fPlayInterval = nMinPlayInterval;
		m_nPlayFPS = 1000 / m_fPlayInterval;
		m_fPlayRate = fPlayRate;

		return IPC_Succeed;
	}

	/// @brief			跳跃到指视频帧进行播放
	/// @param [in]		nFrameID	要播放帧的起始ID
	/// @param [in]		bUpdate		是否更新画面,bUpdate为true则予以更新画面,画面则不更新
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	/// @remark			1.若所指定时间点对应帧为非关键帧，帧自动移动到就近的关键帧进行播放
	///					2.若所指定帧为非关键帧，帧自动移动到就近的关键帧进行播放
	///					3.只有在播放暂时,bUpdate参数才有效
	///					4.用于单帧播放时只能向前移动
	int  SeekFrame(IN int nFrameID,bool bUpdate = false)
	{
		if (!m_bSummaryIsReady)
			return IPC_Error_SummaryNotReady;

		if (!m_hDvoFile || !m_pFrameOffsetTable)
			return IPC_Error_NotFilePlayer;

		if (nFrameID < 0 || nFrameID > m_nTotalFrames)
			return IPC_Error_InvalidFrame;	
		EnterCriticalSection(&m_csVideoCache);
		m_listVideoCache.clear();
		LeaveCriticalSection(&m_csVideoCache);

		EnterCriticalSection(&m_csAudioCache);
		m_listAudioCache.clear();
		LeaveCriticalSection(&m_csAudioCache);
		
		// 从文件摘要中，取得文件偏移信息
		// 查找最近的I帧
		int nForward = nFrameID, nBackWord = nFrameID;
		while (nForward < m_nTotalFrames)
		{
			if (m_pFrameOffsetTable[nForward].bIFrame)
				break;
			nForward++;
		}
		if (nForward >= m_nTotalFrames)
			nForward--;
		
		while (nBackWord > 0 && nBackWord < m_nTotalFrames)
		{
			if (m_pFrameOffsetTable[nBackWord].bIFrame)
				break;
			nBackWord --;
		}
	
		if ((nForward - nFrameID) <= (nFrameID - nBackWord))
			m_nFrametoRead = nForward;
		else
			m_nFrametoRead = nBackWord;
		m_nCurVideoFrame = m_nFrametoRead;
		//TraceMsgA("%s Seek to Frame %d\tFrameTime = %I64d\n", __FUNCTION__, m_nFrametoRead, m_pFrameOffsetTable[m_nFrametoRead].tTimeStamp/1000);
		if (m_hThreadParser)
			SetSeekOffset(m_pFrameOffsetTable[m_nFrametoRead].nOffset);
		else
		{// 只用于单纯的解析文件时移动文件指针
			CAutoLock lock(&m_csParser, false, __FILE__, __FUNCTION__, __LINE__);
			m_nParserOffset = 0;
			m_nParserDataLength = 0;
			LONGLONG nOffset = m_pFrameOffsetTable[m_nFrametoRead].nOffset;
			if (LargerFileSeek(m_hDvoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			{
				OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}
		}
		if (bUpdate && 
			m_hThreadDecode &&	// 必须启动播放线程
			m_bPause &&				// 必须是暂停模式			
			m_pDecodec)				// 解码器必须已启动
		{
			// 读取一帧,并予以解码,显示
			DWORD nBufferSize = m_pFrameOffsetTable[m_nFrametoRead].nFrameSize;
			LONGLONG nOffset = m_pFrameOffsetTable[m_nFrametoRead].nOffset;

			byte *pBuffer = _New byte[nBufferSize + 1];
			if (!pBuffer)
				return IPC_Error_InsufficentMemory;

			unique_ptr<byte>BufferPtr(pBuffer);			
			DWORD nBytesRead = 0;
			if (LargerFileSeek(m_hDvoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			{
				OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}

			if (!ReadFile(m_hDvoFile, pBuffer, nBufferSize, &nBytesRead, nullptr))
			{
				OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}
			AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
			shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
			AVFrame *pAvFrame = av_frame_alloc();
			shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);
			av_init_packet(pAvPacket);
			m_nCurVideoFrame = Frame(pBuffer)->nFrameID;
			pAvPacket->size = Frame(pBuffer)->nLength;
			if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
				pAvPacket->data = pBuffer + sizeof(IPCFrameHeaderEx);
			else
				pAvPacket->data = pBuffer + sizeof(IPCFrameHeader);
			int nGotPicture = 0;
			char szAvError[1024] = { 0 };
			int nAvError = m_pDecodec->Decode(pAvFrame, nGotPicture, pAvPacket);
			if (nAvError < 0)
			{
				av_strerror(nAvError, szAvError, 1024);
				OutputMsg("%s Decode error:%s.\n", __FUNCTION__, szAvError);
				return IPC_Error_DecodeFailed;
			}
			av_packet_unref(pAvPacket);
			if (nGotPicture)
			{
				RenderFrame(pAvFrame);
			}
			av_frame_unref(pAvFrame);
			return IPC_Succeed;
		}

		return IPC_Succeed;
	}
	
	/// @brief			跳跃到指定时间偏移进行播放
	/// @param [in]		tTimeOffset		要播放的起始时间,单位秒,如FPS=25,则0.04秒为第2帧，1.00秒，为第25帧
	/// @param [in]		bUpdate		是否更新画面,bUpdate为true则予以更新画面,画面则不更新
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	/// @remark			1.若所指定时间点对应帧为非关键帧，帧自动移动到就近的关键帧进行播放
	///					2.若所指定帧为非关键帧，帧自动移动到就近的关键帧进行播放
	///					3.只有在播放暂时,bUpdate参数才有效
	int  SeekTime(IN time_t tTimeOffset, bool bUpdate)
	{
		if (!m_hDvoFile)
			return IPC_Error_NotFilePlayer;
		if (!m_bSummaryIsReady)
			return IPC_Error_SummaryNotReady;

		int nFrameID = 0;
		if (m_nVideoFPS == 0 || m_nFileFrameInterval == 0)
		{// 使用二分法查找
			nFrameID = BinarySearch(tTimeOffset);
			if (nFrameID == -1)
				return IPC_Error_InvalidTimeOffset;
		}
		else
		{
			int nTimeDiff = tTimeOffset;// -m_pFrameOffsetTable[0].tTimeStamp;
			if (nTimeDiff < 0)
				return IPC_Error_InvalidTimeOffset;
			nFrameID = nTimeDiff / m_nFileFrameInterval;
		}
		return SeekFrame(nFrameID, bUpdate);
	}
	/// @brief 从文件中读取一帧，读取的起点默认值为0,SeekFrame或SeekTime可设定其起点位置
	/// @param [in,out]		pBuffer		帧数据缓冲区,可设置为null
	/// @param [in,out]		nBufferSize 帧缓冲区的大小
	int GetFrame(INOUT byte **pBuffer, OUT UINT &nBufferSize)
	{
		if (!m_hDvoFile)
			return IPC_Error_NotFilePlayer;
		if (m_hThreadDecode || m_hThreadParser)
			return IPC_Error_PlayerHasStart;
		if (!m_pFrameOffsetTable || !m_nTotalFrames)
			return IPC_Error_SummaryNotReady;

		DWORD nBytesRead = 0;
		FrameParser Parser;
		CAutoLock lock(&m_csParser, false, __FILE__, __FUNCTION__, __LINE__);
		byte *pFrameBuffer = &m_pParserBuffer[m_nParserOffset];
		if (!ParserFrame(&pFrameBuffer, m_nParserDataLength, &Parser))
		{
			// 残留数据长为nDataLength
			memmove(m_pParserBuffer, pFrameBuffer, m_nParserDataLength);
			if (!ReadFile(m_hDvoFile, &m_pParserBuffer[m_nParserDataLength], (m_nParserBufferSize - m_nParserDataLength), &nBytesRead, nullptr))
			{
				OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}
			m_nParserOffset = 0;
			m_nParserDataLength += nBytesRead;
			pFrameBuffer = m_pParserBuffer;
			if (!ParserFrame(&pFrameBuffer, m_nParserDataLength, &Parser))
			{
				return IPC_Error_NotVideoFile;
			}
		}
		m_nParserOffset += Parser.nFrameSize;
		*pBuffer = (byte *)Parser.pHeaderEx;
		nBufferSize = Parser.nFrameSize;
		return IPC_Succeed;
	}
	
	/// @brief			播放下一帧
	/// @retval			0	操作成功
	/// @retval			-24	播放器未暂停
	/// @remark			该函数仅适用于单帧播放
	int  SeekNextFrame()
	{
		if (m_hThreadDecode &&	// 必须启动播放线程
			m_bPause &&				// 必须是暂停模式			
			m_pDecodec)				// 解码器必须已启动
		{
			if (!m_hDvoFile || !m_pFrameOffsetTable)
				return IPC_Error_NotFilePlayer;

			_MyEnterCriticalSection(&m_csVideoCache);
			m_listVideoCache.clear();			
			//m_nFrameOffset = 0;
			_MyLeaveCriticalSection(&m_csVideoCache);

			_MyEnterCriticalSection(&m_csAudioCache);
			m_listAudioCache.clear();
			_MyLeaveCriticalSection(&m_csAudioCache);

			// 读取一帧,并予以解码,显示
			DWORD nBufferSize = m_pFrameOffsetTable[m_nCurVideoFrame].nFrameSize;
			LONGLONG nOffset = m_pFrameOffsetTable[m_nCurVideoFrame].nOffset;

			byte *pBuffer = _New byte[nBufferSize + 1];
			if (!pBuffer)
				return IPC_Error_InsufficentMemory;

			unique_ptr<byte>BufferPtr(pBuffer);
			DWORD nBytesRead = 0;
			if (LargerFileSeek(m_hDvoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			{
				OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}

			if (!ReadFile(m_hDvoFile, pBuffer, nBufferSize, &nBytesRead, nullptr))
			{
				OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return GetLastError();
			}
			AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
			shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
			AVFrame *pAvFrame = av_frame_alloc();
			shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);
			av_init_packet(pAvPacket);
			pAvPacket->size = Frame(pBuffer)->nLength;
			if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
				pAvPacket->data = pBuffer + sizeof(IPCFrameHeaderEx);
			else
				pAvPacket->data = pBuffer + sizeof(IPCFrameHeader);
			int nGotPicture = 0;
			char szAvError[1024] = { 0 };
			int nAvError = m_pDecodec->Decode(pAvFrame, nGotPicture, pAvPacket);
			if (nAvError < 0)
			{
				av_strerror(nAvError, szAvError, 1024);
				OutputMsg("%s Decode error:%s.\n", __FUNCTION__, szAvError);
				return IPC_Error_DecodeFailed;
			}
			av_packet_unref(pAvPacket);
			if (nGotPicture)
			{
				RenderFrame(pAvFrame);
				ProcessYUVCapture(pAvFrame, (LONGLONG)GetExactTime() * 1000);

				m_tCurFrameTimeStamp = m_pFrameOffsetTable[m_nCurVideoFrame].tTimeStamp;
				m_nCurVideoFrame++;
				if (m_pFilePlayCallBack)
					m_pFilePlayCallBack(this, m_pUserFilePlayer);
			}
			av_frame_unref(pAvFrame);
			return IPC_Succeed;
		}
		else
			return IPC_Error_PlayerIsNotPaused;
	}

	/// @brief			开/关音频播放
	/// @param [in]		bEnable			是否播放音频
	/// -#	true		开启音频播放
	/// -#	false		禁用音频播放
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	/// @retval			IPC_Error_AudioFailed	音频播放设备未就绪
	int  EnableAudio(bool bEnable = true)
	{
		TraceFunction();
// 		if (m_fPlayRate != 1.0f)
// 			return IPC_Error_AudioFailed;
		if (m_pDsoundEnum->GetAudioPlayDevices() <= 0)
			return IPC_Error_AudioFailed;
		if (bEnable)
		{
			if (!m_pDsPlayer)
				m_pDsPlayer = make_shared<CDSound>(m_hRenderWnd);
			if (!m_pDsPlayer->IsInitialized())
			{
				if (!m_pDsPlayer->Initialize(m_hRenderWnd, m_nAudioPlayFPS,1,m_nSampleFreq,m_nSampleBit))
				{
					m_pDsPlayer = nullptr;
					m_bEnableAudio = false;
					return IPC_Error_AudioFailed;
				}
			}
			m_pDsBuffer = m_pDsPlayer->CreateDsoundBuffer();
			if (!m_pDsBuffer)			
			{
				m_bEnableAudio = false;
				assert(false);
				return IPC_Error_AudioFailed;
			}
			m_bThreadPlayAudioRun = true;
			m_hAudioFrameEvent[0] = CreateEvent(nullptr, false, true, nullptr);
			m_hAudioFrameEvent[1] = CreateEvent(nullptr, false, true, nullptr);
			
			m_hThreadPlayAudio = (HANDLE)_beginthreadex(nullptr, 0, m_nAudioPlayFPS ==8?ThreadPlayAudioGSJ:ThreadPlayAudioIPC, this, 0, 0);
			m_pDsBuffer->StartPlay();
			m_pDsBuffer->SetVolume(50);
			m_dfLastTimeAudioPlay = 0.0f;
			m_dfLastTimeAudioSample = 0.0f;
			m_bEnableAudio = true;
		}
		else
		{
			if (m_hThreadPlayAudio)
			{
				m_bEnableAudio = false;			
				if (m_bThreadPlayAudioRun)		// 尚未执行停止音频解码线程的操作
				{
					m_bThreadPlayAudioRun = false;
					ResumeThread(m_hThreadPlayAudio);
					WaitForSingleObject(m_hThreadPlayAudio, INFINITE);
					CloseHandle(m_hThreadPlayAudio);
					m_hThreadPlayAudio = nullptr;
					OutputMsg("%s ThreadPlayAudio has exit.\n", __FUNCTION__);
					EnterCriticalSection(&m_csAudioCache);
					m_listAudioCache.clear();
					LeaveCriticalSection(&m_csAudioCache);
				}
				CloseHandle(m_hAudioFrameEvent[0]);
				CloseHandle(m_hAudioFrameEvent[1]);
			}

			if (m_pDsBuffer)
			{
				m_pDsPlayer->DestroyDsoundBuffer(m_pDsBuffer);
				m_pDsBuffer = nullptr;
			}
			if (m_pDsPlayer)
				m_pDsPlayer = nullptr;

			
		}
		return IPC_Succeed;
	}

	/// @brief			刷新播放窗口
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	/// @remark			该功能一般用于播放结束后，刷新窗口，把画面置为黑色
	inline void  Refresh()
	{
		if (m_hRenderWnd)
			::InvalidateRect(m_hRenderWnd, nullptr, true);
	}

	int SetCallBack(IPC_CALLBACK nCallBackType, IN void *pUserCallBack, IN void *pUserPtr)
	{
		if (!pUserCallBack)
			return IPC_Error_InvalidParameters;
		switch (nCallBackType)
		{
		case ExternDcDraw:
		{
			if (m_pDxSurface)
			{
				m_pDxSurface->SetExternDraw(pUserCallBack, pUserPtr);
				return IPC_Succeed;
			}
			else
				return IPC_Error_DxError;
		}
			break;
		case ExternDcDrawEx:
			break;
		case YUVCapture:
		{
			m_pfnCaptureYUV = (CaptureYUV)pUserCallBack;
			m_pUserCaptureYUV = pUserPtr;
		}
			break;
		case YUVCaptureEx:
		{
			m_pfnCaptureYUVEx = (CaptureYUVEx)pUserCallBack;
			m_pUserCaptureYUVEx = pUserPtr;
		}
			break;
		case YUVFilter:
		{
			m_pfnYUVFilter = (CaptureYUVEx)pUserCallBack;
			m_pUserYUVFilter = pUserPtr;
		}
			break;
		case FramePaser:
		{
// 			m_pfnCaptureFrame = (CaptureFrame)pUserCallBack;
// 			m_pUserCaptureFrame = pUserPtr;
		}
			break;
		case FilePlayer:
		{
			m_pFilePlayCallBack = (FilePlayProc)pUserCallBack;
			m_pUserFilePlayer = pUserPtr;
		}
			break;
		default:
			return IPC_Error_InvalidParameters;
			break;
		}
		return IPC_Succeed;
	}

	/// @brief			取得文件的索引的信息,如文件总帧数,文件偏移表
	/// 以读取大块文件内容的形式，获取帧信息，执行效率上比GetFileSummary0要高
	/// 为提高效率，防止文件解析线程和索引线程竞争磁盘读取权，在得取索引信息的同时，
	/// 向缓冲中投入16秒(400帧)的视频数据，相当于解析线程可以延迟8-10秒才开始工作，
	/// 减少了竞争，提升了速度;与此同时，用户可以立即看到画面，提升了用户体验；
	int GetFileSummary(volatile bool &bWorking)
	{
//#ifdef _DEBUG
		double dfTimeStart = GetExactTime();
//#endif
		DWORD nBufferSize = 1024 * 1024*16;
		
		// 不再分析文件，因为StartPlay已经作过分析的确认
		DWORD nOffset = sizeof(IPC_MEDIAINFO);
		if (SetFilePointer(m_hDvoFile, nOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			assert(false);
			return -1;
		}
		if (!m_pFrameOffsetTable)
		{
			m_pFrameOffsetTable = _New FileFrameInfo[m_nTotalFrames];
			ZeroMemory(m_pFrameOffsetTable, sizeof(FileFrameInfo)*m_nTotalFrames);
		}
		
		byte *pBuffer = _New byte[nBufferSize];
		while (!pBuffer)
		{
			if (nBufferSize <= 1024 * 512)
			{// 连512K的内存都无法申请的话，则退出
				OutputMsg("%s Can't alloc enough memory.\n", __FUNCTION__);
				assert(false);
				return IPC_Error_InsufficentMemory;
			}
			nBufferSize /= 2;
			pBuffer = _New byte[nBufferSize];
		}
		shared_ptr<byte>BufferPtr(pBuffer);
		byte *pFrame = nullptr;
		int nFrameSize = 0;
		int nVideoFrames = 0;
		int nAudioFrames = 0;
		LONG nSeekOffset = 0;
		DWORD nBytesRead = 0;
		DWORD nDataLength = 0;
		byte *pFrameBuffer = nullptr;
		FrameParser Parser;
		int nFrameOffset = sizeof(IPC_MEDIAINFO);
		bool bIFrame = false;
		bool bStreamProbed = false;		// 是否已经探测过码流
		const UINT nMaxCache = 100;
		bool bFirstBlockIsFilled = true;
		int nAllFrames = 0;
		//m_bEnableAudio = true;			// 先开启音频标记，以输入音频数据,若后期关闭音频，则缓存数据会自动删除
		
		while (true && bWorking)
		{
			double dfT1 = GetExactTime();
			if (!ReadFile(m_hDvoFile, &pBuffer[nDataLength], (nBufferSize - nDataLength), &nBytesRead, nullptr))
			{
				OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return IPC_Error_ReadFileFailed;
			}
			dfT1 = GetExactTime();
			if (nBytesRead == 0)		// 未读取任何内容，已经达到文件结尾
				break;
			pFrameBuffer = pBuffer;
			nDataLength += nBytesRead;
			int nLength1 = 0;
			while (true && bWorking)
			{
				if (!ParserFrame(&pFrameBuffer, nDataLength, &Parser))
					break;
				nAllFrames++;
				nLength1 = 16 * 1024 * 1024 - (pFrameBuffer - pBuffer);
				if (nLength1 != nDataLength)
				{
					int nBreak = 3;
				}
				if (bFirstBlockIsFilled)
				{
					if (InputStream((byte *)Parser.pHeader, Parser.nFrameSize, (UINT)nMaxCache,true) == IPC_Error_FrameCacheIsFulled &&
						bWorking)
					{
						m_nSummaryOffset = nFrameOffset;
						EnterCriticalSection(&m_csVideoCache);
						int nVideoSize = m_listVideoCache.size();
						LeaveCriticalSection(&m_csVideoCache);
						EnterCriticalSection(&m_csAudioCache);
						int nAudioSize = m_listAudioCache.size();
						LeaveCriticalSection(&m_csAudioCache);
						m_nHeaderFrameID = m_listVideoCache.front()->FrameHeader()->nFrameID;
						TraceMsgA("HeadFrame ID = %d.\n", m_nHeaderFrameID);
						bFirstBlockIsFilled = false;
					}
				}

				if (IsIPCVideoFrame(Parser.pHeader, bIFrame, m_nSDKVersion))	// 只记录视频帧的文件偏移
				{
// 					if (m_nVideoCodec == CODEC_UNKNOWN &&
// 						bIFrame &&
// 						!bStreamProbed)
// 					{// 尝试探测码流
// 						bStreamProbed = ProbeStream((byte *)Parser.pRawFrame, Parser.nRawFrameSize);
// 					}
					if (nVideoFrames < m_nTotalFrames)
					{
						if (m_pFrameOffsetTable)
						{
							m_pFrameOffsetTable[nVideoFrames].nOffset = nFrameOffset;
							m_pFrameOffsetTable[nVideoFrames].nFrameSize = Parser.nFrameSize;
							m_pFrameOffsetTable[nVideoFrames].bIFrame = bIFrame;
							// 根据帧ID和文件播放间隔来精确调整每一帧的播放时间
							if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
								m_pFrameOffsetTable[nVideoFrames].tTimeStamp = nVideoFrames*m_nFileFrameInterval * 1000;
							else
								m_pFrameOffsetTable[nVideoFrames].tTimeStamp = Parser.pHeader->nTimestamp;
						}
					}
					else
						OutputMsg("%s %d(%s) Frame (%d) overflow.\n", __FILE__, __LINE__, __FUNCTION__, nVideoFrames);
					nVideoFrames++;
				}
				else
				{
					m_nAudioCodec = (IPC_CODEC)Parser.pHeaderEx->nType;
					nAudioFrames++;
				}

				nFrameOffset += Parser.nFrameSize;
			}
			nOffset += nBytesRead;
// 			if (bFirstBlockIsFilled && m_bThreadSummaryRun)
// 			{
// 				SetEvent(m_hCacheFulled);
// 				m_nSummaryOffset = nFrameOffset;
// 				CAutoLock lock(&m_csVideoCache);
// 				m_nHeaderFrameID = m_listVideoCache.front()->FrameHeader()->nFrameID;
// 				TraceMsgA("VideoCache = %d\tAudioCache = %d.\n", m_listVideoCache.size(), m_listAudioCache.size());
// 				bFirstBlockIsFilled = false;
// 			}
			// 残留数据长为nDataLength
			memcpy(pBuffer, pFrameBuffer, nDataLength);
			ZeroMemory(&pBuffer[nDataLength] ,nBufferSize - nDataLength);
		}
		m_nTotalFrames = nVideoFrames;
//#ifdef _DEBUG
		OutputMsg("%s TimeSpan = %.3f\tnVideoFrames = %d\tnAudioFrames = %d.\n", __FUNCTION__, TimeSpanEx(dfTimeStart), nVideoFrames,nAudioFrames);
//#endif		
		m_bSummaryIsReady = true;
		return IPC_Succeed;
	}
	/// @brief			解析帧数据
	/// @param [in,out]	pBuffer			来自IPC私有录像文件中的数据流
	/// @param [in,out]	nDataSize		pBuffer中有效数据的长度
	/// @param [out]	pFrameParser	IPC私有录像的帧数据	
	/// @retval			true	操作成功
	/// @retval			false	失败，pBuffer已经没有有效的帧数据
	bool ParserFrame(INOUT byte **ppBuffer,
					 INOUT DWORD &nDataSize, 
					 FrameParser* pFrameParser)
	{
		int nOffset = 0;
		if (nDataSize < sizeof(IPCFrameHeaderEx))
			return false;
		if (Frame(*ppBuffer)->nFrameTag != IPC_TAG &&
			Frame(*ppBuffer)->nFrameTag != GSJ_TAG)
		{
			static char *szKey1 = "MOVD";
			static char *szKey2 = "IMWH";
			nOffset = KMP_StrFind(*ppBuffer, nDataSize, (byte *)szKey1, 4);
			if (nOffset < 0)
				nOffset = KMP_StrFind(*ppBuffer, nDataSize, (byte *)szKey2, 4);
			nOffset -= offsetof(IPCFrameHeader, nFrameTag);
		}

		if (nOffset < 0)
			return false;

		byte *pFrameBuff = *ppBuffer;
		if (m_nSDKVersion < IPC_IPC_SDK_VERSION_2015_12_16 || m_nSDKVersion == IPC_IPC_SDK_GSJ_HEADER)
		{// 旧版文件
			// 帧头信息不完整
			if ((nOffset + sizeof(IPCFrameHeader)) >= nDataSize)
				return false;
			pFrameBuff += nOffset;
			// 帧数据不完整
			if (nOffset + FrameSize2(pFrameBuff) >= nDataSize)
				return false;
			if (pFrameParser)
			{
				pFrameParser->pHeader = (IPCFrameHeader *)(pFrameBuff);
				bool bIFrame = false;
// 				if (IsIPCVideoFrame(pFrameParser->pHeaderEx, bIFrame,m_nSDKVersion))
// 					OutputMsg("Frame ID:%d\tType = Video:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
// 				else
// 					OutputMsg("Frame ID:%d\tType = Audio:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
				pFrameParser->nFrameSize = FrameSize2(pFrameBuff);
				pFrameParser->pRawFrame = *ppBuffer + sizeof(IPCFrameHeader);
				pFrameParser->nRawFrameSize = Frame2(pFrameBuff)->nLength;
			}
			nDataSize -= (FrameSize2(pFrameBuff) + nOffset);
			pFrameBuff += FrameSize2(pFrameBuff);
		}
		else
		{// 新版文件
			// 帧头信息不完整
			if ((nOffset + sizeof(IPCFrameHeaderEx)) >= nDataSize)
				return false;

			pFrameBuff += nOffset;
		
			// 帧数据不完整
			if (nOffset + FrameSize(pFrameBuff) >= nDataSize)
				return false;
		
			if (pFrameParser)
			{
				pFrameParser->pHeaderEx = (IPCFrameHeaderEx *)pFrameBuff;
				bool bIFrame = false;
// 				if (IsIPCVideoFrame(pFrameParser->pHeaderEx, bIFrame,m_nSDKVersion))
// 					OutputMsg("Frame ID:%d\tType = Video:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
// 				else
// 					OutputMsg("Frame ID:%d\tType = Audio:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
				pFrameParser->nFrameSize = FrameSize(pFrameBuff);
				pFrameParser->pRawFrame = pFrameBuff + sizeof(IPCFrameHeaderEx);
				pFrameParser->nRawFrameSize = Frame(pFrameBuff)->nLength;
			}
			nDataSize -= (FrameSize(pFrameBuff) + nOffset);
			pFrameBuff += FrameSize(pFrameBuff);
		}
		*ppBuffer = pFrameBuff;
		return true;
	}

	///< @brief 视频文件解析线程
	static UINT __stdcall ThreadParser(void *p)
	{// 若指定了有效的窗口句柄，则把解析后的文件数据放入播放队列，否则不放入播放队列
		CIPCPlayer* pThis = (CIPCPlayer *)p;
		LONGLONG nSeekOffset = 0;
		DWORD nBufferSize = pThis->m_nMaxFrameSize * 4;
		DWORD nBytesRead = 0;
		DWORD nDataLength = 0;

		LARGE_INTEGER liFileSize;
		if (!GetFileSizeEx(pThis->m_hDvoFile, &liFileSize))
			return 0;
			
		if (pThis->GetFileSummary(pThis->m_bThreadParserRun) != IPC_Succeed)
		{
			assert(false);
			return 0;
		}
		
		byte *pBuffer = _New byte[nBufferSize];		
		shared_ptr<byte>BufferPtr(pBuffer);
		FrameParser Parser;
		pThis->m_tLastFrameTime = 0;	
		if (SetFilePointer(pThis->m_hDvoFile, (LONG)sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			assert(false);
			return 0;
		}

#ifdef _DEBUG
		double dfT1 = GetExactTime();
		bool bOuputTime = false;
#endif
		IPCFrameHeaderEx HeaderEx;
		int nInputResult = 0;
		bool bFileEnd = false;
		while (pThis->m_bThreadParserRun)
		{
			if (pThis->m_bPause)
			{
				Sleep(20);
				continue;
			}
			if (pThis->m_nSummaryOffset)
			{
				CAutoLock lock(&pThis->m_csVideoCache);
				if (pThis->m_listVideoCache.size() < pThis->m_nMaxFrameCache)
				{
					if (SetFilePointer(pThis->m_hDvoFile, (LONG)pThis->m_nSummaryOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
						pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
					pThis->m_nSummaryOffset = 0;
					lock.Unlock();
					Sleep(20);
				}
				else
				{
					lock.Unlock();
					Sleep(20);
					continue;
				}
			}
			else if (nSeekOffset = pThis->GetSeekOffset())	// 是否需要移动文件指针,若nSeekOffset不为0，则需要移动文件指针
			{
				pThis->OutputMsg("Detect SeekFrame Operation.\n");

				EnterCriticalSection(&pThis->m_csVideoCache);
				pThis->m_listVideoCache.clear();
				LeaveCriticalSection(&pThis->m_csVideoCache);

				EnterCriticalSection(&pThis->m_csAudioCache);
				pThis->m_listAudioCache.clear();
				LeaveCriticalSection(&pThis->m_csAudioCache);

				pThis->SetSeekOffset(0);
				bFileEnd = false;
				pThis->m_bFilePlayFinished = false;
				nDataLength = 0;
#ifdef _DEBUG
				pThis->m_bSeekSetDetected = true;
#endif
				if (SetFilePointer(pThis->m_hDvoFile, (LONG)nSeekOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)				
					pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			}
			if (bFileEnd)
			{// 文件读取结事，且播放队列为空，则认为播放结束
				EnterCriticalSection(&pThis->m_csVideoCache);
				int nVideoCacheSize = pThis->m_listVideoCache.size();
				LeaveCriticalSection(&pThis->m_csVideoCache);
				if (nVideoCacheSize == 0)
				{
					pThis->m_bFilePlayFinished = true;
				}
				Sleep(20);
				continue;
			}
			if (!ReadFile(pThis->m_hDvoFile, &pBuffer[nDataLength], (nBufferSize - nDataLength), &nBytesRead, nullptr))
			{
				pThis->OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				return 0;
			}
			
			if (nBytesRead == 0)
			{// 到达文件结尾
				pThis->OutputMsg("%s Reaching File end nBytesRead = %d.\n", __FUNCTION__, nBytesRead);
				LONGLONG nOffset = 0;
				if (!GetFilePosition(pThis->m_hDvoFile, nOffset))
				{
					pThis->OutputMsg("%s GetFilePosition Failed,Error =%d.\n", __FUNCTION__, GetLastError());
					return 0;
				}
				if (nOffset == liFileSize.QuadPart)
				{
					bFileEnd = true;
					pThis->OutputMsg("%s Reaching File end.\n", __FUNCTION__);
				}
			}
			else
				pThis->m_bFilePlayFinished = false;
			nDataLength += nBytesRead;
			byte *pFrameBuffer = pBuffer;

			bool bIFrame = false;
			bool bFrameInput = true;
			while (pThis->m_bThreadParserRun)
			{
				if (pThis->m_bPause)		// 通过pause 函数，暂停数据读取
				{
					Sleep(20);
					continue;
				}
				if (bFrameInput)
				{
					bFrameInput = false;
					if (!pThis->ParserFrame(&pFrameBuffer, nDataLength, &Parser))
						break;	
					nInputResult = pThis->InputStream((byte *)Parser.pHeader, Parser.nFrameSize);
					switch (nInputResult)
					{
					case IPC_Succeed:
					case IPC_Error_InvalidFrameType:
					default:
						bFrameInput = true;
						break;
					case IPC_Error_FrameCacheIsFulled:	// 缓冲区已满
						bFrameInput = false;
						Sleep(10);
						break;
					}
				}
				else
				{
					nInputResult = pThis->InputStream((byte *)Parser.pHeader, Parser.nFrameSize);
					switch (nInputResult)
					{
					case IPC_Succeed:
					case IPC_Error_InvalidFrameType:
					default:
						bFrameInput = true;
						break;
					case IPC_Error_FrameCacheIsFulled:	// 缓冲区已满
						bFrameInput = false;					
						Sleep(10);
						break;
					}
				}
			}
			// 残留数据长为nDataLength
			memmove(pBuffer, pFrameBuffer, nDataLength);
#ifdef _DEBUG
			ZeroMemory(&pBuffer[nDataLength],nBufferSize - nDataLength);
#endif
			// 若是单纯解析数据线程，则需要暂缓读取数据
// 			if (!pThis->m_hWnd )
// 			{
// 				Sleep(10);
// 			}
		}
		return 0;
	}

	/// @brief			获取码流类型	
	/// @param [in]		pVideoCodec		由ipcplay_OpenFile或ipcplay_OpenStream返回的播放句柄
	/// @param [out]	pAudioCodec	返回当前hPlayHandle是否已开启硬解码功能
	/// @remark 码流类型定义请参考:@see IPC_CODEC
	/// @retval			0	操作成功
	/// @retval			-1	输入参数无效
	/// @retval			IPC_Error_PlayerNotStart	播放器尚未启动,无法取得播放过程的信息或属性
	int GetCodec(IPC_CODEC *pVideoCodec, IPC_CODEC *pAudioCodec)
	{
		if (!m_hThreadDecode)
			return IPC_Error_PlayerNotStart;
		if (pVideoCodec)
			*pVideoCodec = m_nVideoCodec;
		if (pAudioCodec)
			*pAudioCodec = m_nAudioCodec;
		return IPC_Succeed;
	}

	// 探测视频码流类型,要求必须输入I帧
	bool ProbeStream(byte *szFrameBuffer,int nBufferLength)
	{	
		shared_ptr<CVideoDecoder>pDecodec = make_shared<CVideoDecoder>();
		if (!m_pStreamProbe)
			m_pStreamProbe = make_shared<StreamProbe>();
		
		if (!m_pStreamProbe)
			return false;
		m_pStreamProbe->nProbeCount++;
		m_pStreamProbe->GetProbeStream(szFrameBuffer, nBufferLength);
		if (m_pStreamProbe->nProbeDataLength <= 64)
			return false;
		if (pDecodec->ProbeStream(this, ReadAvData, m_nMaxFrameSize) != 0)
		{
			OutputMsg("%s Failed in ProbeStream,you may need to input more stream.\n", __FUNCTION__);
			assert(false);
			return false;
		}
		pDecodec->CancelProbe();	
		if (pDecodec->m_nCodecId == AV_CODEC_ID_NONE)
		{
			OutputMsg("%s Unknown Video Codec or not found any codec in the stream.\n", __FUNCTION__);
			assert(false);
			return false;
		}

		if (!pDecodec->m_pAVCtx->width || !pDecodec->m_pAVCtx->height)
		{
			assert(false);
			return false;
		}
		if (pDecodec->m_nCodecId == AV_CODEC_ID_H264)
		{
			m_nVideoCodec = CODEC_H264;
			OutputMsg("%s Video Codec:%H.264 Width = %d\tHeight = %d.\n", __FUNCTION__, pDecodec->m_pAVCtx->width, pDecodec->m_pAVCtx->height);
		}
		else if (pDecodec->m_nCodecId == AV_CODEC_ID_HEVC)
		{
			m_nVideoCodec = CODEC_H265;
			OutputMsg("%s Video Codec:%H.265 Width = %d\tHeight = %d.\n", __FUNCTION__, pDecodec->m_pAVCtx->width, pDecodec->m_pAVCtx->height);
		}
		else
		{
			m_nVideoCodec = CODEC_UNKNOWN;
			OutputMsg("%s Unsupported Video Codec.\n", __FUNCTION__);
			assert(false);
			return false;
		}
		m_pStreamProbe->nProbeAvCodecID = pDecodec->m_nCodecId;
		m_pStreamProbe->nProbeWidth = pDecodec->m_pAVCtx->width;
		m_pStreamProbe->nProbeHeight = pDecodec->m_pAVCtx->height;
		m_nVideoHeight = pDecodec->m_pAVCtx->height;
		m_nVideoWidth = pDecodec->m_pAVCtx->width;
		return true;
	}
			
	/// @brief 把NV12图像转换为YUV420P图像
	void CopyNV12ToYUV420P(byte *pYV12, byte *pNV12[2], int src_pitch[2], unsigned width, unsigned height)
	{
		byte* dstV = pYV12 + width*height;
		byte* dstU = pYV12 + width*height / 4;
		UINT heithtUV = height / 2;
		UINT widthUV = width / 2;
		byte *pSrcUV = pNV12[1];
		byte *pSrcY = pNV12[0];
		int &nYpitch = src_pitch[0];
		int &nUVpitch = src_pitch[1];

		// 复制Y分量
		for (int i = 0; i < height; i++)
			memcpy(pYV12 + i*width, pSrcY + i*nYpitch, width);

		// 复制VU分量
		for (int i = 0; i < heithtUV; i++)
		{
			for (int j = 0; j < width; j++)
			{
				dstU[i*widthUV + j] = pSrcUV[i*nUVpitch + 2 * j];
				dstV[i*widthUV + j] = pSrcUV[i*nUVpitch + 2 * j + 1];
			}
		}
	}

	/// @brief 把Dxva硬解码帧转换成YUV420图像
	void CopyDxvaFrame(byte *pYuv420, AVFrame *pAvFrameDXVA)
	{
		if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
			return;

		IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
		D3DLOCKED_RECT lRect;
		D3DSURFACE_DESC SurfaceDesc;
		pSurface->GetDesc(&SurfaceDesc);
		HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
		if (FAILED(hr))
		{
			OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
			return;
		}

		// Y分量图像
		byte *pSrcY = (byte *)lRect.pBits;
		// UV分量图像
		//byte *pSrcUV = (byte *)lRect.pBits + lRect.Pitch * SurfaceDesc.Height;
		byte *pSrcUV = (byte *)lRect.pBits + lRect.Pitch * pAvFrameDXVA->height;

		byte* dstY = pYuv420;
		byte* dstV = pYuv420 + pAvFrameDXVA->width*pAvFrameDXVA->height;
		byte* dstU = pYuv420 + pAvFrameDXVA->width*pAvFrameDXVA->height/ 4;

		UINT heithtUV = pAvFrameDXVA->height / 2;
		UINT widthUV = pAvFrameDXVA->width / 2;

		// 复制Y分量
		for (int i = 0; i < pAvFrameDXVA->height; i++)
			memcpy(&dstY[i*pAvFrameDXVA->width], &pSrcY[i*lRect.Pitch], pAvFrameDXVA->width);

		// 复制VU分量
		for (int i = 0; i < heithtUV; i++)
		{
			for (int j = 0; j < widthUV; j++)
			{
				dstU[i*widthUV + j] = pSrcUV[i*lRect.Pitch + 2 * j];
				dstV[i*widthUV + j] = pSrcUV[i*lRect.Pitch + 2 * j + 1];
			}
		}

		pSurface->UnlockRect();
	}
	
	void CopyDxvaFrameNV12(byte *pYuv420, AVFrame *pAvFrameDXVA)
	{
		if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
			return;

		IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
		D3DLOCKED_RECT lRect;
		D3DSURFACE_DESC SurfaceDesc;
		pSurface->GetDesc(&SurfaceDesc);
		HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
		if (FAILED(hr))
		{
			OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
			return;
		}

		// Y分量图像
		byte *pSrcY = (byte *)lRect.pBits;
		// UV分量图像
		byte *pSrcUV = (byte *)lRect.pBits + lRect.Pitch * pAvFrameDXVA->height;

		byte* dstY = pYuv420;
		byte* dstUV = pYuv420 + pAvFrameDXVA->width*pAvFrameDXVA->height;
		
		UINT heithtUV = pAvFrameDXVA->height / 2;
		UINT widthUV = pAvFrameDXVA->width / 2;

		// 复制Y分量
		for (int i = 0; i < pAvFrameDXVA->height; i++)
			memcpy(&dstY[i*pAvFrameDXVA->width], &pSrcY[i*lRect.Pitch], pAvFrameDXVA->width);

		// 复制VU分量
		for (int i = 0; i < heithtUV; i++)
			memcpy(&dstUV[i*pAvFrameDXVA->width], &pSrcUV[i*lRect.Pitch], pAvFrameDXVA->width);

		pSurface->UnlockRect();
	}


		
	bool LockDxvaFrame(AVFrame *pAvFrameDXVA,byte **ppSrcY,byte **ppSrcUV,int &nPitch)
	{
		if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
			return false;

		IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
		D3DLOCKED_RECT lRect;
		D3DSURFACE_DESC SurfaceDesc;
		pSurface->GetDesc(&SurfaceDesc);
		HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
		if (FAILED(hr))
		{
			OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
			return false;
		}
		// Y分量图像
		*ppSrcY = (byte *)lRect.pBits;
		// UV分量图像
		//(PBYTE)SrcRect.pBits + SrcRect.Pitch * m_pDDraw->m_dwHeight;
		*ppSrcUV = (byte *)lRect.pBits + lRect.Pitch * pAvFrameDXVA->height;
		nPitch = lRect.Pitch;
		return true;
	}

	void UnlockDxvaFrame(AVFrame *pAvFrameDXVA)
	{
		if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
			return;

		IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
		pSurface->UnlockRect();
	}
	// 把YUVC420P帧复制到YV12缓存中
	void CopyFrameYUV420(byte *pYUV420, int nYUV420Size, AVFrame *pFrame420P)
	{
		byte *pDest = pYUV420;
		int nStride = pFrame420P->width;
		int nSize = nStride * nStride;
		int nHalfSize = (nSize) >> 1;
		byte *pDestY = pDest;										// Y分量起始地址

		byte *pDestU = pDest + nSize;								// U分量起始地址
		int nSizeofU = nHalfSize >> 1;

		byte *pDestV = pDestU + (size_t)(nHalfSize >> 1);			// V分量起始地址
		int nSizeofV = nHalfSize >> 1;

		// YUV420P的U和V分量对调，便成为YV12格式
		// 复制Y分量
		for (int i = 0; i < pFrame420P->height; i++)
			memcpy_s(pDestY + i * nStride, nSize * 3 / 2 - i*nStride, pFrame420P->data[0] + i * pFrame420P->linesize[0], pFrame420P->width);

		// 复制YUV420P的U分量到目村的YV12的U分量
		for (int i = 0; i < pFrame420P->height / 2; i++)
			memcpy_s(pDestU + i * nStride / 2, nSizeofU - i*nStride / 2, pFrame420P->data[1] + i * pFrame420P->linesize[1], pFrame420P->width / 2);

		// 复制YUV420P的V分量到目村的YV12的V分量
		for (int i = 0; i < pFrame420P->height / 2; i++)
			memcpy_s(pDestV + i * nStride / 2, nSizeofV - i*nStride / 2, pFrame420P->data[2] + i * pFrame420P->linesize[2], pFrame420P->width / 2);
	}
	void ProcessYUVFilter(AVFrame *pAvFrame, LONGLONG nTimestamp)
	{
		if (m_pfnYUVFilter)
		{// 在m_pfnYUVFileter中，用户需要把YUV数据处理分，再分成YUV数据
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// dxva 硬解码帧
				CopyDxvaFrame(m_pYUV, pAvFrame);
				byte* pU = m_pYUV + pAvFrame->width*pAvFrame->height;
				byte* pV = m_pYUV + pAvFrame->width*pAvFrame->height / 4;
				m_pfnYUVFilter(this,
								m_pYUV,
								pU,
								pV,
								pAvFrame->width,
								pAvFrame->width/2,
								pAvFrame->width,
								pAvFrame->height,
								nTimestamp,
								m_pUserYUVFilter);
			}
			else
				m_pfnYUVFilter(this,
								pAvFrame->data[0],
								pAvFrame->data[1],
								pAvFrame->data[2],
								pAvFrame->linesize[0],
								pAvFrame->linesize[1],
								pAvFrame->width,
								pAvFrame->height,
								nTimestamp,
								m_pUserYUVFilter);
		}
	}
	
	void ProcessYUVCapture(AVFrame *pAvFrame,LONGLONG nTimestamp)
	{
		if (m_pfnCaptureYUV)
		{
			if (!m_pYUV)
			{
				m_nYUVSize = pAvFrame->width * pAvFrame->height * 3 / 2;
				m_pYUV = (byte *)av_malloc(m_nYUVSize);
				m_pYUVPtr = shared_ptr<byte>(m_pYUV, av_free);
			}
			// 合并YUV数据
			int nPicSize = pAvFrame->linesize[0] * pAvFrame->height;
						
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// 暂不支持dxva 硬解码帧
				
				CopyDxvaFrame(m_pYUV, pAvFrame);	
			}
			else
			{
				if (!m_pYUV)
				{
					m_nYUVSize = pAvFrame->width * pAvFrame->height * 3 / 2;
					m_pYUV = (byte *)av_malloc(m_nYUVSize);
					m_pYUVPtr = shared_ptr<byte>(m_pYUV, av_free);
				}
				memcpy_s(m_pYUV, m_nYUVSize, pAvFrame->data[0], nPicSize);
				memcpy_s(&m_pYUV[nPicSize], m_nYUVSize - nPicSize, pAvFrame->data[1], nPicSize / 4);
				memcpy_s(&m_pYUV[nPicSize + nPicSize / 4], m_nYUVSize - nPicSize - nPicSize / 4, pAvFrame->data[2], nPicSize / 4);
			}
			TraceMsgA("%s m_pfnCaptureYUV = %p", __FUNCTION__, m_pfnCaptureYUV);
			m_pfnCaptureYUV(this, m_pYUV, m_nYUVSize, pAvFrame->width, pAvFrame->height, nTimestamp, m_pUserCaptureYUV);		
		}
		if (m_pfnCaptureYUVEx)
		{
			if (!m_pYUV)
			{
				m_nYUVSize = pAvFrame->width * pAvFrame->height * 3 / 2;
				m_pYUV = (byte *)av_malloc(m_nYUVSize);
				m_pYUVPtr = shared_ptr<byte>(m_pYUV, av_free);
			}
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// dxva 硬解码帧
				//CopyDxvaFrameNV12(m_pYUV, pAvFrame);
				byte *pY = NULL;
				byte *pUV = NULL;
				int nPitch = 0;
				LockDxvaFrame(pAvFrame,&pY,&pUV,nPitch);
				byte* pU = m_pYUV + pAvFrame->width*pAvFrame->height;
				byte* pV = m_pYUV + pAvFrame->width*pAvFrame->height/4;
				
				m_pfnCaptureYUVEx(this,
								  pY,
								  pUV,
								  NULL,
								  nPitch,
								  nPitch/2,
								  pAvFrame->width,
								  pAvFrame->height,
								  nTimestamp,
								  m_pUserCaptureYUVEx);
				UnlockDxvaFrame(pAvFrame);
			}
			else
			{
				m_pfnCaptureYUVEx(this,
								 pAvFrame->data[0],
								 pAvFrame->data[1],
								 pAvFrame->data[2],
								 pAvFrame->linesize[0],
								 pAvFrame->linesize[1],
								 pAvFrame->width,
								 pAvFrame->height,
								 nTimestamp,
								 m_pUserCaptureYUVEx);
			}
		}
	}
	
	/// @brief			码流探测读取数据包回调函数
	/// @param [in]		opaque		用户输入的回调函数参数指针
	/// @param [in]		buf			读取数据的缓存
	/// @param [in]		buf_size	缓存的长度
	/// @return			实际读取数据的长度
	static int ReadAvData(void *opaque, uint8_t *buf, int buf_size)
	{
		AvQueue *pAvQueue = (AvQueue *)opaque;
		CIPCPlayer *pThis = (CIPCPlayer *)pAvQueue->pUserData;
		
		int nReturnVal = buf_size;
		int nRemainedLength = 0;
		pAvQueue->pAvBuffer = buf;
		if (!pThis->m_pStreamProbe)
			return 0;
		int &nDataLength = pThis->m_pStreamProbe->nProbeDataRemained;
		byte *pProbeBuff = pThis->m_pStreamProbe->pProbeBuff;
		int &nProbeOffset = pThis->m_pStreamProbe->nProbeOffset;
		if (nDataLength > 0)
		{
			nRemainedLength = nDataLength - nProbeOffset;
			if (nRemainedLength > buf_size)
			{
				memcpy_s(buf,buf_size, &pProbeBuff[nProbeOffset], buf_size);
				nProbeOffset += buf_size;
				nDataLength -= buf_size;
			}
			else
			{
				memcpy_s(buf, buf_size, &pProbeBuff[nProbeOffset], nRemainedLength);
				nDataLength -= nRemainedLength;
				nProbeOffset = 0;
				nReturnVal = nRemainedLength;
			}
			return nReturnVal;
		}
		else
			return 0;
	}

	/// @brief			实现指时时长的延时
	/// @param [in]		dwDelay		延时时长，单位为ms
	/// @param [in]		bStopFlag	处理停止的标志,为false时，则停止延时
	/// @param [in]		nSleepGranularity	延时时Sleep的粒度,粒度越小，中止延时越迅速，同时也更耗CPU，反之亦然
	static void Delay(DWORD dwDelay, volatile bool &bStopFlag, WORD nSleepGranularity = 20)
	{
		DWORD dwTotal = 0;
		while (bStopFlag && dwTotal < dwDelay)
		{
			Sleep(nSleepGranularity);
			dwTotal += nSleepGranularity;
		}
	}
	struct DecodeFrameStat
	{
		int nTotalFrame;			// 解码总帧数
		double dfTotalDecodeTime;	// 解码总时间
		double dfAvgDecodeTime;		// 平均解码时间
		DecodeFrameStat()
		{
			ZeroMemory(this, sizeof(DecodeFrameStat));
		}
		void Stat(double dfTimespan)
		{
			nTotalFrame++;
			dfTotalDecodeTime += dfTimespan;
			dfAvgDecodeTime = dfTotalDecodeTime / nTotalFrame;
		}
		void Reset()
		{
			ZeroMemory(this, sizeof(DecodeFrameStat));
		}
	};
// 	HANDLE	m_hRenderFrameReady = nullptr;
// 	HANDLE	m_hRenderFrameCopied = nullptr;
	volatile bool m_bThreadRenderRun = false;
	HANDLE m_hThreadRender = nullptr;
	AVFrame *m_pRenderFrame = nullptr;
	static UINT __stdcall ThreadRender(void *p)
	{
		CIPCPlayer* pThis = (CIPCPlayer *)p;
		int nRetry = 0;

		// 若有指定了窗口句柄，则需要初始显示对象
		if (pThis->m_hRenderWnd)
		{
			bool bCacheDxSurface = false;		// 是否为缓存中取得的Surface对象
			if (GetOsMajorVersion() < Win7MajorVersion)
			{
				if (!pThis->m_pDDraw)
					pThis->m_pDDraw = _New CDirectDraw();
				pThis->InitizlizeDisplay();
			}
// 			else
// 			{
// 				if (!pThis->m_pDxSurface)
// 				{
// 					D3DFORMAT nPixFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
// 					if (pThis->m_bEnableHaccel)
// 						nPixFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
// // 					pThis->m_pDxSurface = GetDxInCache(pThis->m_nVideoWidth, pThis->m_nVideoHeight, nPixFormat);
// // 					if (pThis->m_pDxSurface)
// // 						bCacheDxSurface = true;
// // 					else
// 					pThis->m_pDxSurface = _New CDxSurfaceEx();
// 				}
// 				if (!bCacheDxSurface)
// 				{
// 					nRetry = 0;
// 					while (pThis->m_bThreadDecodeRun)
// 					{
// 						if (!pThis->InitizlizeDisplay())
// 						{
// 							nRetry++;
// 							Delay(2500, pThis->m_bThreadDecodeRun);
// 							if (nRetry >= 3)
// 							{
// 								if (pThis->m_hRenderWnd)
// 									::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_INITD3DFAILED, 0);
// 								return 0;
// 							}
// 						}
// 						else
// 							break;
// 					}
// 				}
// 			}

			RECT rtWindow;
			GetWindowRect(pThis->m_hRenderWnd, &rtWindow);
			pThis->OutputMsg("%s Window Width = %d\tHeight = %d.\n", __FUNCTION__, (rtWindow.right - rtWindow.left), (rtWindow.bottom - rtWindow.top));
#ifdef _DEBUG
			pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
		}
		while (pThis->m_bThreadRenderRun)
		{
			do 
			{
				Autolock(&pThis->m_csListYUV);
				if (pThis->m_listNV12.size() > 0)
				{
					FrameNV12Ptr FramePtr = pThis->m_listNV12.front();
					pThis->m_listNV12.pop_front();
					lock.Unlock();
					byte *pY = NULL, *pUV = NULL;
					pThis->m_pYUVImage->dwLineSize[0] = FramePtr->nStrideY;
					pThis->m_pYUVImage->dwLineSize[1] = FramePtr->nStrideUV;
					pThis->m_pYUVImage->pBuffer[0] = (PBYTE)FramePtr->pNV12Surface;
					pThis->m_pYUVImage->pBuffer[1] = (PBYTE)FramePtr->pNV12Surface + FramePtr->nStrideY*FramePtr->nSurfaceHeight;					
					pThis->m_pDDraw->Draw(*pThis->m_pYUVImage, nullptr, nullptr, true);
// 					Autolock1(&pThis->m_csListRenderUnit);
// 					for (auto it = pThis->m_listRenderUnit.begin(); it != pThis->m_listRenderUnit.end() && !pThis->m_bStopFlag; it++)
// 						(*it)->RenderImage(*(pThis->m_pYUVImage), nullptr, nullptr, true);
					
				}
				else if (pThis->m_listYV12.size() > 0)
				{
					FrameYV12Ptr FramePtr = pThis->m_listYV12.front();
					pThis->m_listYV12.pop_front();
					lock.Unlock();
					pThis->m_pYUVImage->pBuffer[0] = (PBYTE)FramePtr->pY;
					pThis->m_pYUVImage->pBuffer[1] = (PBYTE)FramePtr->pU;
					pThis->m_pYUVImage->pBuffer[2] = (PBYTE)FramePtr->pV;
					pThis->m_pYUVImage->dwLineSize[0] = FramePtr->nStrideY;
					pThis->m_pYUVImage->dwLineSize[1] = FramePtr->nStrideUV;
					pThis->m_pYUVImage->dwLineSize[2] = FramePtr->nStrideUV;
					pThis->m_pDDraw->Draw(*(pThis->m_pYUVImage), nullptr, nullptr, true);
// 					Autolock1(&pThis->m_csListRenderUnit);
// 					for (auto it = pThis->m_listRenderUnit.begin(); it != pThis->m_listRenderUnit.end() && !pThis->m_bStopFlag; it++)
// 						(*it)->RenderImage(*(pThis->m_pYUVImage), nullptr, nullptr, true);
				}

			} while (false);

			Sleep(10);
		}
		return 0;
	}

	bool m_bD3dShared = false;
	void SetD3dShared(bool bD3dShared = true)
	{
		m_bD3dShared = bD3dShared;
	}
	// 把解码后的YUV帧放入YUV缓存
	// 返回false时，表示缓存已经超过允许的最大数量,该帧被丢弃
	// 返回true时，则操作成功
	bool PushFrame(AVFrame *pFrame)
	{
		if (!pFrame)
			return false;
		if (pFrame->format == AV_PIX_FMT_DXVA2_VLD)
		{
			Autolock(&m_csListYUV);
			if (m_listNV12.size() < m_nMaxYUVCache)
			{
				FrameNV12Ptr FramePtr = make_shared<FrameNV12>(pFrame, (INT64)(GetExactTime() * 1000));
				m_listNV12.push_back(FramePtr);
			}
			else
			{
				OutputMsg("%s YUV list size = %d.\n", __FUNCTION__, m_listNV12.size());
				return false;
			}
		}
		else
		{
			Autolock(&m_csListYUV);
			if (m_listYV12.size() < m_nMaxYUVCache)
			{
				FrameYV12Ptr FramePtr = make_shared<FrameYV12>(pFrame, (INT64)(GetExactTime() * 1000));
				m_listYV12.push_back(FramePtr);
			}
			else
			{
				OutputMsg("%s YUV list size = %d.\n", __FUNCTION__, m_listNV12.size());
				return false;
			}
		}
		return true;
	}
	

	static UINT __stdcall ThreadDecode(void *p)
	{
		CIPCPlayer* pThis = (CIPCPlayer *)p;
		
#ifdef _DEBUG
		pThis->OutputMsg("%s \tObject:%d Enter ThreadPlayVideo m_nLifeTime = %d.\n", __FUNCTION__, pThis->m_nObjIndex, timeGetTime() - pThis->m_nLifeTime);
#endif
		int nAvError = 0;
		char szAvError[1024] = { 0 };
#ifdef _DEBUG
		sprintf_s(szAvError, 1024, "Object:%d.\n", pThis->m_nObjIndex);
		TraceFunction1(szAvError);
#endif
		if (!pThis->m_hRenderWnd)
		{
			pThis->OutputMsg("%s Warning!!!A Windows handle is Needed otherwith the video Will not showed..\n", __FUNCTION__);
			//assert(false);
			//return 0;
		}
		// 创建多媒体事件
		//TimerEvent PlayEvent(1000 / pThis->m_nVideoFPS);
		
		// 等待有效的视频帧数据
		long tFirst = timeGetTime();
		while (pThis->m_bThreadDecodeRun)
		{
			Autolock(&pThis->m_csVideoCache);
			if ((timeGetTime() - tFirst) > 15000)
			{// 等待超时
				assert(false);
				return 0;
			}
			if (pThis->m_listVideoCache.size() < 1)
			{
				lock.Unlock();
				Sleep(5);
				continue;
			}
			else
				break;
		}
		if (!pThis->m_bThreadDecodeRun)
			return 0;
		
		// 等待I帧
		tFirst = timeGetTime();
//		DWORD dfTimeout = 3000;
// 		if (!pThis->m_bIpcStream)	// 只有IPC码流才需要长时间等待
// 			dfTimeout = 1000;
		AVCodecID nCodecID = AV_CODEC_ID_NONE;
		int nDiscardFrames = 0;
		bool bProbeSucced = false;
		if (pThis->m_nVideoCodec == CODEC_UNKNOWN ||		/// 码流未知则尝试探测码
			!pThis->m_nVideoWidth||
			!pThis->m_nVideoHeight)
		{
			bool bGovInput = false;
			while (pThis->m_bThreadDecodeRun)
			{
				if ((timeGetTime() - tFirst) >= pThis->m_nProbeStreamTimeout)
					break;
				CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (pThis->m_listVideoCache.size() > 1)
					break;
				Sleep(25);
			}
			if (!pThis->m_bThreadDecodeRun)
				return 0;
			auto itPos = pThis->m_listVideoCache.begin();
			while (!bProbeSucced && pThis->m_bThreadDecodeRun)
			{
#ifndef _DEBUG
				if ((timeGetTime() - tFirst) < pThis->m_nProbeStreamTimeout)
#else
				if ((timeGetTime() - tFirst) < INFINITE)
#endif
				{
					Sleep(5);
					CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
					auto it = find_if(itPos, pThis->m_listVideoCache.end(), StreamFrame::IsIFrame);
					if (it != pThis->m_listVideoCache.end() )
					{// 探测码流类型
						itPos = it;
						itPos++;
						TraceMsgA("%s Probestream FrameType = %d\tFrameLength = %d.\n",__FUNCTION__, (*it)->FrameHeader()->nType,(*it)->FrameHeader()->nLength);
						if ((*it)->FrameHeader()->nType == FRAME_GOV )
						{
							if (bGovInput)
								continue;
							bGovInput = true;
							if (bProbeSucced = pThis->ProbeStream((byte *)(*it)->Framedata(pThis->m_nSDKVersion), (*it)->FrameHeader()->nLength))
								break;
						}
						else
							if (bProbeSucced = pThis->ProbeStream((byte *)(*it)->Framedata(pThis->m_nSDKVersion), (*it)->FrameHeader()->nLength))
								break;
					}
				}
				else
				{
#ifdef _DEBUG
					pThis->OutputMsg("%s Warning!!!\nThere is No an I frame in %d second.m_listVideoCache.size() = %d.\n", __FUNCTION__, (int)pThis->m_nProbeStreamTimeout / 1000, pThis->m_listVideoCache.size());
					pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
					if (pThis->m_hRenderWnd)
						::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_NOTRECVIFRAME, 0);
					return 0;
				}
			}
			if (!pThis->m_bThreadDecodeRun)
				return 0;
			
			if (!bProbeSucced)		// 探测失败
			{
				pThis->OutputMsg("%s Failed in ProbeStream,you may input a unknown stream.\n", __FUNCTION__);
#ifdef _DEBUG
				pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
				if (pThis->m_hRenderWnd)
					::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNKNOWNSTREAM, 0);
				return 0;
			}
			// 把ffmpeg的码流ID转为IPC的码流ID,并且只支持H264和HEVC
			nCodecID = pThis->m_pStreamProbe->nProbeAvCodecID;
			if (nCodecID != AV_CODEC_ID_H264 && 
				nCodecID != AV_CODEC_ID_HEVC)
			{
				pThis->m_nVideoCodec = CODEC_UNKNOWN;
				pThis->OutputMsg("%s Probed a unknown stream,Decode thread exit.\n", __FUNCTION__);
				if (pThis->m_hRenderWnd)
					::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNKNOWNSTREAM, 0);
				return 0;
			}
		}

		switch (pThis->m_nVideoCodec)
		{
		case CODEC_H264:
			nCodecID = AV_CODEC_ID_H264;
			break;
		case CODEC_H265:
			nCodecID = AV_CODEC_ID_H265;
			break;
		default:
		{
			pThis->OutputMsg("%s You Input a unknown stream,Decode thread exit.\n", __FUNCTION__);
			if (pThis->m_hRenderWnd)	// 在线程中尽量避免使用SendMessage，因为可能会导致阻塞
				::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNSURPPORTEDSTREAM, 0);
			return 0;
			break;
		}
		}
		
		int nRetry = 0;

		shared_ptr<CVideoDecoder>pDecodec = make_shared<CVideoDecoder>();
		if (!pDecodec)
		{
			pThis->OutputMsg("%s Failed in allocing memory for Decoder.\n", __FUNCTION__);
			//assert(false);
			return 0;
		}

		if (!pThis->InitizlizeDisplay())
		{
			assert(false);
			return 0;
		}
		if (pThis->m_bD3dShared)
		{
			pDecodec->SetD3DShared(pThis->m_pDxSurface->GetD3D9(), pThis->m_pDxSurface->GetD3DDevice());
			pThis->m_pDxSurface->SetD3DShared(true);
		}

		// 使用单线程解码,多线程解码在某此比较慢的CPU上可能会提高效果，但现在I5 2GHZ以上的CPU上的多线程解码效果并不明显反而会占用更多的内存
		pDecodec->SetDecodeThreads(1);		
		// 初始化解码器
		while (pThis->m_bThreadDecodeRun )
		{// 某此时候可能会因为内存或资源不够导致初始化解码操作性,因此可以延迟一段时间后再次初始化，若多次初始化仍不成功，则需退出线程
			DeclareRunTime(5);
			SaveRunTime();
			if (!pDecodec->InitDecoder(nCodecID, pThis->m_nVideoWidth, pThis->m_nVideoHeight, pThis->m_bEnableHaccel))
			{
				pThis->OutputMsg("%s Failed in Initializing Decoder.\n", __FUNCTION__);
#ifdef _DEBUG
				pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
				nRetry++;
				if (nRetry >= 3)
				{
					if (pThis->m_hRenderWnd)// 在线程中尽量避免使用SendMessage，因为可能会导致阻塞
						::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_INITDECODERFAILED, 0);
					return 0;
				}
				Delay(2500,pThis->m_bThreadDecodeRun);
			}
			else
				break;
			SaveRunTime();
		}
		if (!pThis->m_bThreadDecodeRun)
			return 0;

		if (pThis->m_hThreadRender)
			ResumeThread(pThis->m_hThreadRender);
// 		// 若有指定了窗口句柄，则需要初始显示对象
// 		if (pThis->m_hRenderWnd &&
// 			!pThis->m_hThreadRender)	// 若为异步渲染，则在渲染线中初始化显示组件
// 		{
// 			DeclareRunTime(5);
// 			SaveRunTime();
// 			bool bCacheDxSurface = false;		// 是否为缓存中取得的Surface对象
// 			if (GetOsMajorVersion() < Win7MajorVersion)
// 			{
// 				if (!pThis->m_pDDraw)
// 					pThis->m_pDDraw = _New CDirectDraw();
// 				pThis->InitizlizeDisplay();
// 			}
// 			else
// 			{
// 				if (!pThis->m_pDxSurface)
// 				{
// 					D3DFORMAT nPixFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
// 					if (pThis->m_bEnableHaccel)
// 						nPixFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
// // 					pThis->m_pDxSurface = GetDxInCache(pThis->m_nVideoWidth, pThis->m_nVideoHeight, nPixFormat);
// // 					if (pThis->m_pDxSurface)
// // 						bCacheDxSurface = true;
// // 					else
// 						pThis->m_pDxSurface = _New CDxSurfaceEx();
// 				}
// 				if (!bCacheDxSurface)
// 				{
// 					nRetry = 0;
// 					while (pThis->m_bThreadDecodeRun)
// 					{
// 						if (!pThis->InitizlizeDisplay())
// 						{
// 							nRetry++;
// 							Delay(2500, pThis->m_bThreadDecodeRun);
// 							if (nRetry >= 3)
// 							{
// 								if (pThis->m_hRenderWnd)
// 									::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_INITD3DFAILED, 0);
// 								return 0;
// 							}
// 						}
// 						else
// 							break;
// 					}
// 				}
// 			}
// 			SaveRunTime();
// 			RECT rtWindow;
// 			GetWindowRect(pThis->m_hRenderWnd, &rtWindow);
// 			pThis->OutputMsg("%s Window Width = %d\tHeight = %d.\n", __FUNCTION__, (rtWindow.right - rtWindow.left), (rtWindow.bottom - rtWindow.top));
// #ifdef _DEBUG
// 			pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
// #endif
// 		}
		if (pThis->m_pStreamProbe)
			pThis->m_pStreamProbe = nullptr;
		pThis->m_pDecodec = pDecodec;
	
		AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
		shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);	
		av_init_packet(pAvPacket);	
		AVFrame *pAvFrame = av_frame_alloc();
		shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);
		
		StreamFramePtr FramePtr;
		int nGot_picture = 0;
		DWORD nResult = 0;
		float fTimeSpan = 0;
		int nFrameInterval = pThis->m_nFileFrameInterval;
		pThis->m_dfTimesStart = GetExactTime();
		
//		 取得当前显示器的刷新率,在垂直同步模式下,显示器的刷新率决定了，显示图像的最高帧数
//		 通过统计每显示一帧图像(含解码和显示)耗费的时间
// 		DEVMODE   dm;
// 		dm.dmSize = sizeof(DEVMODE);
// 		::EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);		
// 		int nRefreshInvertal = 1000 / dm.dmDisplayFrequency;	// 显示器刷新间隔

		double dfDecodeStartTime = GetExactTime();
		double dfRenderTime = GetExactTime() - pThis->m_fPlayInterval;	// 图像被显示的时间
		double dfRenderStartTime = 0.0f;
		double dfRenderTimeSpan = 0.000f;
		double dfTimeSpan = 0.0f;
		
#ifdef _DEBUG
		EnterCriticalSection(&pThis->m_csVideoCache);
		TraceMsgA("%s Video cache Size = %d .\n", __FUNCTION__, pThis->m_listVideoCache.size());
		LeaveCriticalSection(&pThis->m_csVideoCache);
		pThis->OutputMsg("%s \tObject:%d Start Decoding.\n", __FUNCTION__,pThis->m_nObjIndex);
#endif
//	    以下代码用以测试解码和显示占用时间，建议不要删除		
// 		TimeTrace DecodeTimeTrace("DecodeTime", __FUNCTION__);
// 		TimeTrace RenderTimeTrace("RenderTime", __FUNCTION__);

#ifdef _DEBUG
		int nFrames = 0;
		int nFirstID = 0;
		time_t dfDelayArray[100] = { 0 };
#endif
		int nIFrameTime = 0;
		DecodeFrameStat FrameStat;		// 解码统计
		DecodeFrameStat IFrameStat;		// I帧解码统计

		int nFramesAfterIFrame = 0;		// 相对I帧的编号,I帧后的第一帧为1，第二帧为2依此类推
		int nSkipFrames = 0;
		bool bDecodeSucceed = false;
		double dfDecodeTimespan = 0.0f;	// 解码所耗费时间
		double dfDecodeITimespan = 0.0f; // I帧解码和显示所耗费时间
		double dfTimeDecodeStart = 0.0f;
		pThis->m_nFirstFrameTime = 0;
		float fLastPlayRate = pThis->m_fPlayRate;		// 记录上一次的播放速率，当播放速率发生变化时，需要重置帧统计数据
		
		if (pThis->m_dwStartTime)
		{
			TraceMsgA("%s %d Render Timespan = %d.\n", __FUNCTION__, __LINE__, timeGetTime() - pThis->m_dwStartTime);
			pThis->m_dwStartTime = 0;
		}
			
		int nFramesPlayed = 0;			// 播放总帆数
		double dfTimeStartPlay = GetExactTime();// 播放起始时间
		int nTimePlayFrame = 0;		// 播放一帧所耗费时间（MS）
		int nPlayCount = 0;
		int TPlayArray[100] = {0};
		double dfT1 = GetExactTime();
		int nVideoCacheSize = 0;
		dfDecodeStartTime = GetExactTime() - pThis->m_nPlayFrameInterval / 1000.0f;
		while (pThis->m_bThreadDecodeRun)
		{
			if (pThis->m_bPause)
			{
				Sleep(40);
				continue;
			}

			EnterCriticalSection(&pThis->m_csVideoCache);
			nVideoCacheSize = pThis->m_listVideoCache.size();
			LeaveCriticalSection(&pThis->m_csVideoCache);
			int nTimeSpan1 = (int)(TimeSpanEx(dfDecodeStartTime) * 1000);
			if (nVideoCacheSize <2 &&
				 (pThis->m_nPlayFrameInterval - nTimeSpan1) > 5 )
			{
				Sleep(5);
				continue;
			}

// 			do 
// 			{
			// 此为最帧率测试代码,建议不要删除
			// xionggao.lee @2016.01.15 
#ifdef _DEBUG
				int nFPS = 25;
				int nTimespan2 = (int)(TimeSpanEx(pThis->m_dfTimesStart) * 1000);
				if (nTimespan2)
					nFPS = nFrames * 1000 / nTimespan2;

				int nTimeSpan = (int)(TimeSpanEx(dfDecodeStartTime) * 1000);
				TPlayArray[nPlayCount++] = nTimeSpan;
				TPlayArray[0] = nFPS;
				if (nPlayCount >= 50)
				{
					pThis->OutputMsg("%sPlay Interval([0] is FPS):\n", __FUNCTION__);
					for (int i = 0; i < nPlayCount; i++)
					{
						pThis->OutputMsg("%02d\t", TPlayArray[i]);
						if ((i + 1) % 10 == 0)
							pThis->OutputMsg("\n");
					}
					pThis->OutputMsg(".\n");
					nPlayCount = 0;
					CAutoLock lock(&pThis->m_csVideoCache);
					TraceMsgA("%s Videocache size = %d.\n", __FUNCTION__, pThis->m_listVideoCache.size());
				}
				dfT1 = GetExactTime();
				
#endif
				dfDecodeStartTime = GetExactTime();
				if (!pThis->m_bIpcStream)
				{// 文件或流媒体播放，可调节播放速度
					bool bPopFrame = false;
					// 查找时间上最匹配的帧,并删除不匹配的非I帧
					int nSkipFrames = 0;
					CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
					if (!pThis->m_nFirstFrameTime && 
						pThis->m_listVideoCache.size() > 0)
						pThis->m_nFirstFrameTime = pThis->m_listVideoCache.front()->FrameHeader()->nTimestamp;
					for (auto it = pThis->m_listVideoCache.begin(); it != pThis->m_listVideoCache.end();)
					{
						time_t tFrameSpan = ((*it)->FrameHeader()->nTimestamp - pThis->m_tLastFrameTime) / 1000;
						if (StreamFrame::IsIFrame(*it))
						{
							bPopFrame = true;
							break;
						}
						if (pThis->m_fPlayRate < 16.0 && // 16倍速以下，才考虑按时跳帧
							tFrameSpan / pThis->m_fPlayRate >= max(pThis->m_fPlayInterval, FrameStat.dfAvgDecodeTime * 1000))
						{
							bPopFrame = true;
							break;
						}
						else
						{
							it = pThis->m_listVideoCache.erase(it);
							nSkipFrames++;
						}
					}
					if (nSkipFrames)
						pThis->OutputMsg("%s Skip Frames = %d bPopFrame = %s.\n", __FUNCTION__, nSkipFrames, bPopFrame ? "true" : "false");
					if (bPopFrame)
					{
						FramePtr = pThis->m_listVideoCache.front();
						pThis->m_listVideoCache.pop_front();
						//TraceMsgA("%s Pop a Frame ,FrameID = %d\tFrameTimestamp = %d.\n", __FUNCTION__, FramePtr->FrameHeader()->nFrameID, FramePtr->FrameHeader()->nTimestamp);
					}
					pThis->m_nVideoCache = pThis->m_listVideoCache.size();
					if (!bPopFrame)
					{
						lock.Unlock();	// 须提前解锁，不然Sleep后才会解锁，导致其它地方被锁住
						Sleep(10);
						continue;
					}
					lock.Unlock();
					pAvPacket->data = (uint8_t *)FramePtr->Framedata(pThis->m_nSDKVersion);
					pAvPacket->size = FramePtr->FrameHeader()->nLength;
					pThis->m_tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
					av_frame_unref(pAvFrame);
				
					nAvError = pDecodec->Decode(pAvFrame, nGot_picture, pAvPacket);
					if (nAvError < 0)
					{
						av_strerror(nAvError, szAvError, 1024);
						//dfDecodeStartTime = GetExactTime();
						continue;
					}
					//avcodec_flush_buffers()
					av_packet_unref(pAvPacket);
					dfDecodeTimespan = TimeSpanEx(dfDecodeStartTime);
					if (StreamFrame::IsIFrame(FramePtr))			// 统计I帧解码时间
						IFrameStat.Stat(dfDecodeTimespan);
					FrameStat.Stat(dfDecodeTimespan);	// 统计所有帧解码时间
					if (fLastPlayRate != pThis->m_fPlayRate)
					{// 播放速率发生变化，重置统计数据
						IFrameStat.Reset();
						FrameStat.Reset();
					}
					fLastPlayRate = pThis->m_fPlayRate;
					fTimeSpan = (TimeSpanEx(dfRenderTime) + dfRenderTimeSpan )* 1000;
					int nSleepTime = 0;
					if (fTimeSpan  < pThis->m_fPlayInterval)
					{
						nSleepTime =(int) (pThis->m_fPlayInterval - fTimeSpan);
						Sleep(nSleepTime);
					}
				}
				else
				{// IPC 码流，则直接播放
							
					bool bPopFrame = false;
					Autolock(&pThis->m_csVideoCache);
				
					if (pThis->m_listVideoCache.size() > 0)
					{
						FramePtr = pThis->m_listVideoCache.front();
						pThis->m_listVideoCache.pop_front();
						bPopFrame = true;
						nVideoCacheSize = pThis->m_listVideoCache.size();
					}
					lock.Unlock();
					if (!bPopFrame)
					{
						Sleep(10);
						continue;
					}

					pAvPacket->data = (uint8_t *)FramePtr->Framedata(pThis->m_nSDKVersion);
					pAvPacket->size = FramePtr->FrameHeader()->nLength;
					pThis->m_tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
					av_frame_unref(pAvFrame);
					nAvError = pDecodec->Decode(pAvFrame, nGot_picture, pAvPacket);
					if (nAvError < 0)
					{
						av_strerror(nAvError, szAvError, 1024);
						//dfDecodeTimespan = TimeSpanEx(dfDecodeStartTime);
						//dfDecodeStartTime = GetExactTime();
						continue;
					}
					av_packet_unref(pAvPacket);
					dfDecodeTimespan = TimeSpanEx(dfDecodeStartTime);
				}

	#ifdef _DEBUG
				if (pThis->m_bSeekSetDetected)
				{
					int nFrameID = FramePtr->FrameHeader()->nFrameID;
					int nTimeStamp = FramePtr->FrameHeader()->nTimestamp/1000;
					pThis->OutputMsg("%s First Frame after SeekSet:ID = %d\tTimeStamp = %d.\n", __FUNCTION__, nFrameID, nTimeStamp);
					pThis->m_bSeekSetDetected = false;
				}
				nFrames++;
	#endif
				
				dfRenderStartTime = GetExactTime();
 				if (nGot_picture)
 				{
					pThis->m_nDecodePixelFmt = (AVPixelFormat)pAvFrame->format;
					SetEvent(pThis->m_hEvnetYUVReady);
					SetEvent(pThis->m_hEventDecodeStart);
 					pThis->m_nCurVideoFrame = FramePtr->FrameHeader()->nFrameID;
 					pThis->m_tCurFrameTimeStamp = FramePtr->FrameHeader()->nTimestamp;
					pThis->ProcessYUVFilter(pAvFrame, (LONGLONG)pThis->m_nCurVideoFrame);

					if (!pThis->m_bIpcStream &&
						1.0f == pThis->m_fPlayRate  &&
						pThis->m_bEnableAudio &&
						pThis->m_hAudioFrameEvent[0] &&
						pThis->m_hAudioFrameEvent[1])
						WaitForMultipleObjects(2, pThis->m_hAudioFrameEvent, TRUE, 40);
					if (!pThis->m_hThreadRender)
						pThis->RenderFrame(pAvFrame);
					else
						pThis->PushFrame(pAvFrame);
					if (!bDecodeSucceed)
					{
						bDecodeSucceed = true;
	#ifdef _DEBUG
						pThis->OutputMsg("%s \tObject:%d  SetEvent Snapshot  m_nLifeTime = %d.\n", __FUNCTION__, pThis->m_nObjIndex, timeGetTime() - pThis->m_nLifeTime);
	#endif
					}
					pThis->ProcessSnapshotRequire(pAvFrame);
					pThis->ProcessYUVCapture(pAvFrame, (LONGLONG)pThis->m_nCurVideoFrame);
 					if (pThis->m_pFilePlayCallBack)
 						pThis->m_pFilePlayCallBack(pThis, pThis->m_pUserFilePlayer);
 				}
				else
				{
					TraceMsgA("%s \tObject:%d Decode Succeed but Not get a picture ,FrameType = %d\tFrameLength %d.\n", __FUNCTION__, pThis->m_nObjIndex, FramePtr->FrameHeader()->nType, FramePtr->FrameHeader()->nLength);
				}
			
				dfRenderTimeSpan = TimeSpanEx(dfRenderStartTime);
				nTimePlayFrame = (int)(TimeSpanEx(dfDecodeStartTime)*1000);
				dfRenderTime = GetExactTime();
//			}
//			while (false);
		}
		av_frame_unref(pAvFrame);
		return 0;
	}
	static UINT __stdcall ThreadPlayAudioGSJ(void *p)
	{
		CIPCPlayer *pThis = (CIPCPlayer *)p;
		int nAudioFrameInterval = pThis->m_fPlayInterval / 2;

		DWORD nResult = 0;
		int nTimeSpan = 0;
		StreamFramePtr FramePtr;
		int nAudioError = 0;
		byte *pPCM = nullptr;
		shared_ptr<CAudioDecoder> pAudioDecoder = make_shared<CAudioDecoder>();
		int nPCMSize = 0;
		int nDecodeSize = 0;
		__int64 nFrameEvent = 0;
 		if (pThis->m_nAudioPlayFPS == 8)
 			Sleep(250);
		// 预读第一帧，以初始化音频解码器
		while (pThis->m_bThreadPlayAudioRun)
		{
			if (!FramePtr)
			{
				CAutoLock lock(&pThis->m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (pThis->m_listAudioCache.size() > 0)
				{
					FramePtr = pThis->m_listAudioCache.front();
					break;
				}
			}
			Sleep(10);
		}
		if (!FramePtr)
			return 0;
		if (pAudioDecoder->GetCodecType() == CODEC_UNKNOWN)
		{
			const IPCFrameHeaderEx *pHeader = FramePtr->FrameHeader();
			nDecodeSize = pHeader->nLength * 2;		//G711 压缩率为2倍
			switch (pHeader->nType)
			{
			case FRAME_G711A:			//711 A律编码帧
			{
				pAudioDecoder->SetACodecType(CODEC_G711A, SampleBit16);
				pThis->m_nAudioCodec = CODEC_G711A;
				pThis->OutputMsg("%s Audio Codec:G711A.\n", __FUNCTION__);
				break;
			}
			case FRAME_G711U:			//711 U律编码帧
			{
				pAudioDecoder->SetACodecType(CODEC_G711U, SampleBit16);
				pThis->m_nAudioCodec = CODEC_G711U;
				pThis->OutputMsg("%s Audio Codec:G711U.\n", __FUNCTION__);
				break;
			}

			case FRAME_G726:			//726编码帧
			{
				// 因为目前IPC相机的G726编码,虽然采用的是16位采样，但使用32位压缩编码，因此解压得使用SampleBit32
				pAudioDecoder->SetACodecType(CODEC_G726, SampleBit32);
				pThis->m_nAudioCodec = CODEC_G726;
				nDecodeSize = FramePtr->FrameHeader()->nLength * 8;		//G726最大压缩率可达8倍
				pThis->OutputMsg("%s Audio Codec:G726.\n", __FUNCTION__);
				break;
			}
			case FRAME_AAC:				//AAC编码帧。
			{
				pAudioDecoder->SetACodecType(CODEC_AAC, SampleBit16);
				pThis->m_nAudioCodec = CODEC_AAC;
				nDecodeSize = FramePtr->FrameHeader()->nLength * 24;
				pThis->OutputMsg("%s Audio Codec:AAC.\n", __FUNCTION__);
				break;
			}
			default:
			{
				assert(false);
				pThis->OutputMsg("%s Unspported audio codec.\n", __FUNCTION__);
				return 0;
				break;
			}
			}
		}
		if (nPCMSize < nDecodeSize)
		{
			pPCM = new byte[nDecodeSize];
			nPCMSize = nDecodeSize;
		}
#ifdef _DEBUG
		TimeTrace TimeAudio("AudioTime", __FUNCTION__);
#endif
		double dfLastPlayTime = GetExactTime();
		double dfPlayTimeSpan = 0.0f;
		UINT nFramesPlayed = 0;
		WaitForSingleObject(pThis->m_hEventDecodeStart,1000);
		::EnterCriticalSection(&pThis->m_csAudioCache);
		pThis->m_nAudioCache = pThis->m_listAudioCache.size();
		::LeaveCriticalSection(&pThis->m_csAudioCache);
		TraceMsgA("%s Audio Cache Size = %d.\n", __FUNCTION__, pThis->m_nAudioCache);
		time_t tLastFrameTime = 0;
		double dfDecodeStart = GetExactTime();
		DWORD dwOsMajorVersion = GetOsMajorVersion();
#ifdef _DEBUG
		int nSleepCount = 0;
		TimeTrace TraceSleepCount("SleepCount", __FUNCTION__,25);
#endif
		while (pThis->m_bThreadPlayAudioRun)
		{
			if (pThis->m_bPause)
			{
				if (pThis->m_pDsBuffer->IsPlaying())
					pThis->m_pDsBuffer->StopPlay();
				Sleep(100);
				continue;
			}

			nTimeSpan = (int)((GetExactTime() - dfLastPlayTime) * 1000);
			if (pThis->m_fPlayRate != 1.0f)
			{// 只有正常倍率才播放声音
				if (pThis->m_pDsBuffer->IsPlaying())
					pThis->m_pDsBuffer->StopPlay();
				::EnterCriticalSection(&pThis->m_csAudioCache);
				if (pThis->m_listAudioCache.size() > 0)	
					pThis->m_listAudioCache.pop_front();
				::LeaveCriticalSection(&pThis->m_csAudioCache);
				Sleep(5);
				continue;
			}
			
			if (nTimeSpan > 1000*3/pThis->m_nAudioPlayFPS)			// 连续3*音频码流间隔没有音频数据，则视为音频暂停
				pThis->m_pDsBuffer->StopPlay();
			else if(!pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StartPlay();
			bool bPopFrame = false;
			if (pThis->m_bIpcStream && pThis->m_nAudioPlayFPS == 8)
			{
				if (pThis->m_pDsBuffer->IsPlaying())
					pThis->m_pDsBuffer->WaitForPosNotify();
			}
 			
			::EnterCriticalSection(&pThis->m_csAudioCache);
			if (pThis->m_listAudioCache.size() > 0)
			{
				FramePtr = pThis->m_listAudioCache.front();
				pThis->m_listAudioCache.pop_front();
				bPopFrame = true;
			}
			pThis->m_nAudioCache = pThis->m_listAudioCache.size();
			::LeaveCriticalSection(&pThis->m_csAudioCache);
			
			if (!bPopFrame)
			{
				if (!pThis->m_bIpcStream)
					Sleep(10);
				continue;
			}

			if (nFramesPlayed < 50 && dwOsMajorVersion < 6)
			{// 修正在XP系统中，前50帧会被瞬间丢掉的问题
				if (((TimeSpanEx(dfLastPlayTime) + dfPlayTimeSpan)*1000) < nAudioFrameInterval)
					Sleep(nAudioFrameInterval - (TimeSpanEx(dfLastPlayTime)*1000));
			}
			
			if (pThis->m_pDsBuffer->IsPlaying() //||
				/*pThis->m_pDsBuffer->WaitForPosNotify()*/)
			{
				if (pAudioDecoder->Decode(pPCM, nPCMSize, (byte *)FramePtr->Framedata(pThis->m_nSDKVersion), FramePtr->FrameHeader()->nLength) != 0)
				{
					if (!pThis->m_pDsBuffer->WritePCM(pPCM, nPCMSize,!pThis->m_bIpcStream))
						pThis->OutputMsg("%s Write PCM Failed.\n", __FUNCTION__);
					//SetEvent(pThis->m_hAudioFrameEvent[nFrameEvent++ % 2]);
				}
				else
					TraceMsgA("%s Audio Decode Failed Is.\n", __FUNCTION__);
			}
			nFramesPlayed++;
 			if (pThis->m_nAudioPlayFPS == 8 && nFramesPlayed <= 8)
 				Sleep(120);
			dfPlayTimeSpan = TimeSpanEx(dfLastPlayTime);
			dfLastPlayTime = GetExactTime();
			tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
		}
		if (pPCM)
			delete[]pPCM;
		return 0;
	}

	static UINT __stdcall ThreadPlayAudioIPC(void *p)
	{
		CIPCPlayer *pThis = (CIPCPlayer *)p;
		int nAudioFrameInterval = pThis->m_fPlayInterval / 2;

		DWORD nResult = 0;
		int nTimeSpan = 0;
		StreamFramePtr FramePtr;
		int nAudioError = 0;
		byte *pPCM = nullptr;
		shared_ptr<CAudioDecoder> pAudioDecoder = make_shared<CAudioDecoder>();
		int nPCMSize = 0;
		int nDecodeSize = 0;
		__int64 nFrameEvent = 0;

		// 预读第一帧，以初始化音频解码器
		while (pThis->m_bThreadPlayAudioRun)
		{
			if (!FramePtr)
			{
				CAutoLock lock(&pThis->m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
				if (pThis->m_listAudioCache.size() > 0)
				{
					FramePtr = pThis->m_listAudioCache.front();
					break;
				}
			}
			Sleep(10);
		}
		if (!FramePtr)
			return 0;
		if (pAudioDecoder->GetCodecType() == CODEC_UNKNOWN)
		{
			const IPCFrameHeaderEx *pHeader = FramePtr->FrameHeader();
			nDecodeSize = pHeader->nLength * 2;		//G711 压缩率为2倍
			switch (pHeader->nType)
			{
			case FRAME_G711A:			//711 A律编码帧
			{
				pAudioDecoder->SetACodecType(CODEC_G711A, SampleBit16);
				pThis->m_nAudioCodec = CODEC_G711A;
				pThis->OutputMsg("%s Audio Codec:G711A.\n", __FUNCTION__);
				break;
			}
			case FRAME_G711U:			//711 U律编码帧
			{
				pAudioDecoder->SetACodecType(CODEC_G711U, SampleBit16);
				pThis->m_nAudioCodec = CODEC_G711U;
				pThis->OutputMsg("%s Audio Codec:G711U.\n", __FUNCTION__);
				break;
			}

			case FRAME_G726:			//726编码帧
			{
				// 因为目前IPC相机的G726编码,虽然采用的是16位采样，但使用32位压缩编码，因此解压得使用SampleBit32
				pAudioDecoder->SetACodecType(CODEC_G726, SampleBit32);
				pThis->m_nAudioCodec = CODEC_G726;
				nDecodeSize = FramePtr->FrameHeader()->nLength * 8;		//G726最大压缩率可达8倍
				pThis->OutputMsg("%s Audio Codec:G726.\n", __FUNCTION__);
				break;
			}
			case FRAME_AAC:				//AAC编码帧。
			{
				pAudioDecoder->SetACodecType(CODEC_AAC, SampleBit16);
				pThis->m_nAudioCodec = CODEC_AAC;
				nDecodeSize = FramePtr->FrameHeader()->nLength * 24;
				pThis->OutputMsg("%s Audio Codec:AAC.\n", __FUNCTION__);
				break;
			}
			default:
			{
				assert(false);
				pThis->OutputMsg("%s Unspported audio codec.\n", __FUNCTION__);
				return 0;
				break;
			}
			}
		}
		if (nPCMSize < nDecodeSize)
		{
			pPCM = new byte[nDecodeSize];
			nPCMSize = nDecodeSize;
		}
		double dfLastPlayTime = 0.0f;
		double dfPlayTimeSpan = 0.0f;
		UINT nFramesPlayed = 0;
		WaitForSingleObject(pThis->m_hEventDecodeStart, 1000);
		::EnterCriticalSection(&pThis->m_csAudioCache);
		pThis->m_nAudioCache = pThis->m_listAudioCache.size();
		::LeaveCriticalSection(&pThis->m_csAudioCache);
		TraceMsgA("%s Audio Cache Size = %d.\n", __FUNCTION__, pThis->m_nAudioCache);
		time_t tLastFrameTime = 0;
		double dfDecodeStart = GetExactTime();
		DWORD dwOsMajorVersion = GetOsMajorVersion();
		while (pThis->m_bThreadPlayAudioRun)
		{
			if (pThis->m_bPause)
			{
				if (pThis->m_pDsBuffer->IsPlaying())
					pThis->m_pDsBuffer->StopPlay();
				Sleep(20);
				continue;
			}

			nTimeSpan = (int)((GetExactTime() - dfLastPlayTime) * 1000);
			if (pThis->m_fPlayRate != 1.0f)
			{// 只有正常倍率才播放声音
				if (pThis->m_pDsBuffer->IsPlaying())
					pThis->m_pDsBuffer->StopPlay();
				::EnterCriticalSection(&pThis->m_csAudioCache);
				if (pThis->m_listAudioCache.size() > 0)
					pThis->m_listAudioCache.pop_front();
				::LeaveCriticalSection(&pThis->m_csAudioCache);
				Sleep(5);
				continue;
			}

			if (nTimeSpan > 100)			// 连续100ms没有音频数据，则视为音频暂停
				pThis->m_pDsBuffer->StopPlay();
			else if (!pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StartPlay();
			bool bPopFrame = false;

// 			if (!pThis->m_pAudioPlayEvent->GetNotify(1))
// 			{
// 				continue;
// 			}
			::EnterCriticalSection(&pThis->m_csAudioCache);
			if (pThis->m_listAudioCache.size() > 0)
			{
				FramePtr = pThis->m_listAudioCache.front();
				pThis->m_listAudioCache.pop_front();
				bPopFrame = true;
			}
			pThis->m_nAudioCache = pThis->m_listAudioCache.size();
			::LeaveCriticalSection(&pThis->m_csAudioCache);

			if (!bPopFrame)
			{
				Sleep(10);
				continue;
			}
			nFramesPlayed++;
			
			if (nFramesPlayed < 50 && dwOsMajorVersion < 6)
			{// 修正在XP系统中，前50帧会被瞬间丢掉的问题
				if (((TimeSpanEx(dfLastPlayTime) + dfPlayTimeSpan) * 1000) < nAudioFrameInterval)
					Sleep(nAudioFrameInterval - (TimeSpanEx(dfLastPlayTime) * 1000));
			}

			dfPlayTimeSpan = GetExactTime();
			if (pThis->m_pDsBuffer->IsPlaying())
			{
				if (pAudioDecoder->Decode(pPCM, nPCMSize, (byte *)FramePtr->Framedata(pThis->m_nSDKVersion), FramePtr->FrameHeader()->nLength) != 0)
				{
					if (!pThis->m_pDsBuffer->WritePCM(pPCM, nPCMSize))
						pThis->OutputMsg("%s Write PCM Failed.\n", __FUNCTION__);
					SetEvent(pThis->m_hAudioFrameEvent[nFrameEvent++ % 2]);
				}
				else
					TraceMsgA("%s Audio Decode Failed Is.\n", __FUNCTION__);
			}
			dfPlayTimeSpan = TimeSpanEx(dfPlayTimeSpan);
			dfLastPlayTime = GetExactTime();
			tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
		}
		if (pPCM)
			delete[]pPCM;
		return 0;
	}

	RocateAngle m_nRocateAngle;
	AVFrame		*m_pRacoateFrame = nullptr;
	byte		*m_pRocateImage = nullptr;
	int SetRocate(RocateAngle nRocate = Rocate90)
	{
		if (m_bEnableHaccel)
			return IPC_Error_AudioFailed;
		m_nRocateAngle = nRocate;
		return IPC_Succeed;
	}
	struct TimerEvent
	{
		HANDLE	 hEvent;
		MMRESULT nTimerID;				///< 多媒体定时器ID，需要使用多媒体定时器		
		double   dfLastFire;
		int		 TestArray[100];
		int		 nFireCount;
		TimerEvent(int nPeriod = 40)
		{
			ZeroMemory(this, sizeof(TimerEvent));
			timeBeginPeriod(1);
			nTimerID = timeSetEvent(nPeriod, 1, TimerCallBack, (DWORD_PTR)this, TIME_PERIODIC);
			hEvent = CreateEvent(nullptr, false, false, nullptr);
			dfLastFire = GetExactTime();
		}
		void SetNotify()
		{
			SetEvent(hEvent);
		}
		
		bool GetNotify(DWORD dwTimeout = 0)
		{
			return (WaitForSingleObject(hEvent, dwTimeout) == WAIT_OBJECT_0);
		}
		
		~TimerEvent()
		{
			CloseHandle(hEvent);
			hEvent = nullptr;
			timeKillEvent(nTimerID);
			timeEndPeriod(1);
		}
		void TestTimer()
		{
			int nTimeSpan = (int)(TimeSpanEx(dfLastFire) * 1000);
			TestArray[nFireCount++] = nTimeSpan;
			if (nFireCount >= 50)
			{
				TraceMsgA("%s  TimerCallBack Interval:\n", __FUNCTION__);
				for (int i = 0; i < nFireCount; i++)
				{
					TraceMsgA("%02d\t", TestArray[i]);
					if ((i + 1) % 10 == 0)
						TraceMsgA("\n");
				}
				TraceMsgA(".\n");
				nFireCount = 0;
			}
			dfLastFire = GetExactTime();
		}
		static void __stdcall TimerCallBack(UINT nTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
		{
			((TimerEvent *)dwUser)->SetNotify();
			//((TimerEvent *)dwUser)->TestTimer();
		}
	};
	


	static void *_AllocAvFrame()
	{
		return av_frame_alloc();
	}
	static void _AvFree(void *p)
	{
		av_free(p);
	}

};